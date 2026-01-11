#include "ai_talk_controller.h"

void AiTalkController::begin() {
  state_ = AiState::Idle;
  stateStartMs_ = nowMs_();
  convoStartMs_ = 0;
  error_ = false;
  speakingBlankPhase_ = false;
  speakingPhaseStartMs_ = 0;
  inputText_ = "";
  replyText_ = "";
  overlay_ = AiUiOverlay{};
  tick(); // 初期overlay生成
}

bool AiTalkController::isBusy() const {
  return state_ != AiState::Idle;
}

AiUiOverlay AiTalkController::getOverlay() const {
  return overlay_;
}

void AiTalkController::onTap() {
  const uint32_t now = nowMs_();

  // THINKING/SPEAKING/COOLDOWN はタップ無視
  if (state_ == AiState::Thinking || state_ == AiState::Speaking || state_ == AiState::Cooldown) return;

  if (state_ == AiState::Idle) {
    // 会話開始
    convoStartMs_ = now;
    error_ = false;
    inputText_ = "";
    replyText_ = "";
    transitionTo_(AiState::Listening);
    return;
  }

  if (state_ == AiState::Listening) {
    const uint32_t e = elapsedStateMs_(now);

    // 開始3秒以内はキャンセル（破棄してIDLE）
    if (e <= kListeningCancelable) {
      transitionTo_(AiState::Idle);
      convoStartMs_ = 0;
      inputText_ = "";
      replyText_ = "";
      error_ = false;
      return;
    }

    // 3秒以降は「話し終えた」扱いで THINKING へ
    transitionTo_(AiState::Thinking);
    return;
  }
}

void AiTalkController::injectText(const String& text) {
  Serial.printf("[ai] injectText len=%u\n", (unsigned)text.length());

  // LISTENING/THINKINGを飛ばしてSPEAKING相当
  const uint32_t now = nowMs_();
  if (convoStartMs_ == 0) convoStartMs_ = now;
  error_ = false;

  // 入力200で切る → TTS 120で切る
  const String in200 = clampLen_(text, kInputLimitChars);
  replyText_ = clampLen_(in200, kTtsLimitChars);

  // 直ちにSPEAKINGへ
  transitionTo_(AiState::Speaking);
}

void AiTalkController::tick() {
  const uint32_t now = nowMs_();

  // 全体タイムアウト枠（20s）：超えたらエラー扱いでCOOLDOWNへ
  if (state_ != AiState::Idle && convoStartMs_ != 0) {
    if (elapsedConvoMs_(now) > kOverallTimeoutMs) {
      setErrorAndCooldown_();
    }
  }

  switch (state_) {
    case AiState::Idle: {
      // 何もしない
      break;
    }

    case AiState::Listening: {
      const uint32_t e = elapsedStateMs_(now);

      // 10秒で自動的に THINKING
      if (e >= kListeningMaxMs) {
        transitionTo_(AiState::Thinking);
      }
      break;
    }

    case AiState::Thinking: {
      const uint32_t e = elapsedStateMs_(now);

      // AIタイムアウト枠（10s）
      if (e > kAiTimeoutMs) {
        replyText_ = "（AI timeout）";
        setErrorAndCooldown_(); // そのままCOOLDOWNへ（SPEAKING省略でもOKだが、仕様上SPEAKINGを経由したいなら下の2行に変更）
        break;
      }

      // モック：1秒後に返答生成→SPEAKING
      if (e >= kThinkingMockMs) {
        // LISTENINGで音声が無いのでモック入力
        if (inputText_.isEmpty()) inputText_ = "（音声入力モック）";
        inputText_ = clampLen_(inputText_, kInputLimitChars);

        String r = "（モック返答）了解: " + inputText_;
        replyText_ = clampLen_(r, kTtsLimitChars);

        transitionTo_(AiState::Speaking);
      }
      break;
    }

    case AiState::Speaking: {
      // TTSはしない。UI上で「喋ってる扱い」→0.5秒空吹き出し→COOLDOWN
      if (!speakingBlankPhase_) {
        const uint32_t e = now - speakingPhaseStartMs_;
        if (e >= kSpeakingShowMs) {
          speakingBlankPhase_ = true;
          speakingPhaseStartMs_ = now;
        }
      } else {
        const uint32_t e = now - speakingPhaseStartMs_;
        if (e >= kSpeakingBlankMs) {
          transitionTo_(AiState::Cooldown);
        }
      }

      // TTSタイムアウト枠（10s）も一応監視（枠だけ）
      if (elapsedStateMs_(now) > kTtsTimeoutMs) {
        setErrorAndCooldown_();
      }
      break;
    }

    case AiState::Cooldown: {
      const uint32_t base = kCooldownMs + (error_ ? kCooldownErrExtraMs : 0);
      if (elapsedStateMs_(now) >= base) {
        transitionTo_(AiState::Idle);
        convoStartMs_ = 0;
        error_ = false;
      }
      break;
    }
  }

  // ---- overlay更新（毎tick）----
  overlay_.active = true;
  overlay_.state = state_;

  const int remainSec = remainingSecCeil_(now);
  overlay_.line1 = stateName_(state_) + " " + String(remainSec) + "s";

  // 2行目＆ヒント
  switch (state_) {
    case AiState::Idle:
      overlay_.line2 = "Tap top 1/3 to start";
      overlay_.hint  = ":say こんにちは";
      break;

    case AiState::Listening: {
      const uint32_t e = elapsedStateMs_(now);
      if (e <= kListeningCancelable) {
        overlay_.line2 = "Listening... (tap to cancel <=3s)";
      } else {
        overlay_.line2 = "Listening... (tap to end)";
      }
      overlay_.hint = "";
      break;
    }

    case AiState::Thinking:
      overlay_.line2 = "Thinking... (mock)";
      overlay_.hint = "";
      break;

    case AiState::Speaking:
      if (!speakingBlankPhase_) {
        overlay_.line2 = replyText_;
      } else {
        overlay_.line2 = ""; // 空吹き出し
      }
      overlay_.hint = "";
      break;

    case AiState::Cooldown:
      overlay_.line2 = error_ ? "Cooldown (error)" : "Cooldown";
      overlay_.hint = "";
      break;
  }
}

void AiTalkController::transitionTo_(AiState next) {
  // state transition log (no secrets)
  Serial.printf("[ai] %s -> %s\n", stateName_(state_).c_str(), stateName_(next).c_str());

  const uint32_t now = nowMs_();
  state_ = next;
  stateStartMs_ = now;

  if (next == AiState::Speaking) {
    speakingBlankPhase_ = false;
    speakingPhaseStartMs_ = now;

    // replyText_ が空なら最低限の文を用意
    if (replyText_.isEmpty()) replyText_ = "（喋りモック）";
    replyText_ = clampLen_(replyText_, kTtsLimitChars);
  }

  if (next == AiState::Thinking) {
    // LISTENINGから来た場合のモック入力（後で生成）
    if (inputText_.isEmpty()) inputText_ = "（音声入力モック）";
    inputText_ = clampLen_(inputText_, kInputLimitChars);
  }

  if (next == AiState::Idle) {
    // 表示用
    replyText_ = "";
    inputText_ = "";
    speakingBlankPhase_ = false;
    speakingPhaseStartMs_ = 0;
  }
}

void AiTalkController::setErrorAndCooldown_() {
  error_ = true;
  transitionTo_(AiState::Cooldown);
}

String AiTalkController::stateName_(AiState s) const {
  switch (s) {
    case AiState::Idle:      return "IDLE";
    case AiState::Listening: return "LISTENING";
    case AiState::Thinking:  return "THINKING";
    case AiState::Speaking:  return "SPEAKING";
    case AiState::Cooldown:  return "COOLDOWN";
  }
  return "UNKNOWN";
}

uint32_t AiTalkController::remainingMs_(uint32_t now) const {
  switch (state_) {
    case AiState::Listening: {
      const uint32_t e = elapsedStateMs_(now);
      return (e >= kListeningMaxMs) ? 0 : (kListeningMaxMs - e);
    }
    case AiState::Thinking: {
      const uint32_t e = elapsedStateMs_(now);
      // 表示はAI枠10s（実際は1sで完了する想定）
      return (e >= kAiTimeoutMs) ? 0 : (kAiTimeoutMs - e);
    }
    case AiState::Speaking: {
      // 表示は TTS枠10s でもいいが、ここは実モック長に合わせる
      // （見た目の残秒としては、今のフェーズ残りを返す）
      if (!speakingBlankPhase_) {
        const uint32_t e = now - speakingPhaseStartMs_;
        return (e >= kSpeakingShowMs) ? 0 : (kSpeakingShowMs - e);
      } else {
        const uint32_t e = now - speakingPhaseStartMs_;
        return (e >= kSpeakingBlankMs) ? 0 : (kSpeakingBlankMs - e);
      }
    }
    case AiState::Cooldown: {
      const uint32_t base = kCooldownMs + (error_ ? kCooldownErrExtraMs : 0);
      const uint32_t e = elapsedStateMs_(now);
      return (e >= base) ? 0 : (base - e);
    }
    case AiState::Idle:
    default:
      return 0;
  }
}

int AiTalkController::remainingSecCeil_(uint32_t now) const {
  const uint32_t ms = remainingMs_(now);
  // ceil(ms/1000)
  return (ms == 0) ? 0 : (int)((ms + 999) / 1000);
}

String AiTalkController::clampLen_(const String& s, size_t maxChars) const {
  if (s.length() <= maxChars) return s;
  return s.substring(0, (int)maxChars);
}
