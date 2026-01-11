#include "ai_talk_controller.h"
#include "logging.h"
#include "orchestrator.h"

// ---- timings ----
static constexpr uint32_t kListeningTimeoutMs      = 10000;
static constexpr uint32_t kListeningCancelWindowMs = 3000;
static constexpr uint32_t kThinkingMockMs          = 1000;
static constexpr uint32_t kPostSpeakBlankMs        = 500;
static constexpr uint32_t kCooldownMs              = 2000;
static constexpr uint32_t kErrorExtraMs            = 1000;

// TTS done が来ないときの安全タイムアウト
static constexpr uint32_t kTtsHardTimeoutMs        = 20000;

// orch が無い（sandbox等）ときの擬似発話時間
static constexpr uint32_t kSimulatedSpeakMs        = 2000;

String AiTalkController::clampBytes_(const String& s, size_t maxBytes) {
  if (s.length() <= maxBytes) return s;
  return s.substring(0, (unsigned)maxBytes);
}

void AiTalkController::begin(Orchestrator* orch) {
  orch_ = orch;
  enterIdle_(millis(), "begin");
}

bool AiTalkController::consumeBubbleUpdate(String* outText) {
  if (!outText) return false;
  if (!bubbleDirty_) return false;
  *outText = bubbleText_;
  bubbleDirty_ = false;
  return true;
}

bool AiTalkController::onTap(int /*x*/, int y, int screenH) {
  if (screenH > 0) {
    if (y >= (screenH / 3)) return false;  // 上1/3以外はAIが消費しない
  }
  return onTap();
}

bool AiTalkController::onTap() {
  const uint32_t now = millis();

  // Busy中は「無視」だが、Attentionなどへ流さないため tap を消費する
  if (state_ == AiState::Thinking ||
      state_ == AiState::Speaking ||
      state_ == AiState::PostSpeakBlank ||
      state_ == AiState::Cooldown) {
    return true;
  }

  if (state_ == AiState::Idle) {
    enterListening_(now);
    return true;
  }

  if (state_ == AiState::Listening) {
    const uint32_t elapsed = now - listenStartMs_;
    if (elapsed <= kListeningCancelWindowMs) {
      enterIdle_(now, "tap_cancel");
    }
    // 3秒以降の再タップは無視（ただし消費）
    return true;
  }

  return true;
}

void AiTalkController::injectText(const String& text) {
  // 本体統合フェーズ1では基本使わない（秘密保護のためフルログ禁止）
  if (state_ != AiState::Listening) return;
  if (!text.length()) return;

  inputText_ = clampBytes_(text, 200);
  // ログは長さのみ
  mc_logf("[ai] injectText len=%u", (unsigned)inputText_.length());
}

void AiTalkController::onTtsDone(uint32_t ttsId, uint32_t nowMs) {
  // SPEAKING中に自分のttsIdが完了したら、空吹き出しへ
  if (state_ == AiState::Speaking && expectTtsId_ != 0 && ttsId == expectTtsId_) {
    expectTtsId_ = 0;
    enterPostSpeakBlank_(nowMs);
  }
}

void AiTalkController::tick(uint32_t nowMs) {
  switch (state_) {
    case AiState::Idle:
      overlay_.active = false;
      return;

    case AiState::Listening: {
      const uint32_t elapsed = nowMs - listenStartMs_;
      if (elapsed >= kListeningTimeoutMs) {
        enterThinking_(nowMs);
      } else {
        updateOverlay_(nowMs);
      }
      return;
    }

    case AiState::Thinking: {
      const uint32_t elapsed = nowMs - thinkStartMs_;
      if (elapsed >= kThinkingMockMs) {
        // ---- dummy reply (<=120 bytes) ----
        replyText_ = "テストだよ。AI会話モードの配線確認中だよ。";
        replyText_ = clampBytes_(replyText_, 120);

        // bubble show要求（必ず main 側が setStackchanSpeech()）
        bubbleText_  = replyText_;
        bubbleDirty_ = true;

        // Orchestratorへ投入（speakAsync直叩き禁止）
        bool enqueued = false;
        if (orch_) {
          const uint32_t rid = (uint32_t)100000 + (nextRid_++);
          if (nextRid_ == 0) nextRid_ = 1;

          auto cmd = orch_->makeSpeakStartCmd(
              rid, replyText_,
              OrchPrio::High,
              Orchestrator::OrchKind::AiSpeak
          );

          if (cmd.valid) {
            orch_->enqueueSpeakPending(cmd);
            expectTtsId_ = cmd.ttsId;
            enqueued = true;

            LOG_EVT_INFO("EVT_AI_ENQUEUE_SPEAK",
                         "rid=%lu tts_id=%lu len=%u",
                         (unsigned long)rid,
                         (unsigned long)cmd.ttsId,
                         (unsigned)replyText_.length());
          }
        }

        // 失敗/tts無しでも状態機械は進める（speak時間は擬似）
        (void)enqueued;
        enterSpeaking_(nowMs);
      } else {
        updateOverlay_(nowMs);
      }
      return;
    }

    case AiState::Speaking: {
      // TTSあり：done待ち。TTS無し：擬似時間で進める
      if (expectTtsId_ == 0) {
        const uint32_t elapsed = nowMs - speakStartMs_;
        if (elapsed >= kSimulatedSpeakMs) {
          enterPostSpeakBlank_(nowMs);
        } else {
          updateOverlay_(nowMs);
        }
      } else {
        const uint32_t elapsed = nowMs - speakStartMs_;
        if (elapsed >= kTtsHardTimeoutMs) {
          // doneが来ない → エラー扱いでcooldown延長
          enterCooldown_(nowMs, true, "tts_timeout");
        } else {
          updateOverlay_(nowMs);
        }
      }
      return;
    }

    case AiState::PostSpeakBlank: {
      const uint32_t elapsed = nowMs - blankStartMs_;
      if (elapsed >= kPostSpeakBlankMs) {
        enterCooldown_(nowMs, false, "post_blank_done");
      } else {
        updateOverlay_(nowMs);
      }
      return;
    }

    case AiState::Cooldown: {
      const uint32_t elapsed = nowMs - cooldownStartMs_;
      if (elapsed >= cooldownDurMs_) {
        enterIdle_(nowMs, "cooldown_done");
      } else {
        updateOverlay_(nowMs);
      }
      return;
    }

    default:
      enterIdle_(nowMs, "unknown");
      return;
  }
}

void AiTalkController::enterIdle_(uint32_t nowMs, const char* reason) {
  state_ = AiState::Idle;

  overlay_ = AiUiOverlay();
  overlay_.active = false;

  // bubble clear（main側がsetStackchanSpeech("")）
  bubbleText_  = "";
  bubbleDirty_ = true;

  inputText_ = "";
  replyText_ = "";
  expectTtsId_ = 0;

  LOG_EVT_INFO("EVT_AI_STATE",
               "state=IDLE reason=%s", reason ? reason : "-");
  (void)nowMs;
}

void AiTalkController::enterListening_(uint32_t nowMs) {
  state_ = AiState::Listening;
  listenStartMs_ = nowMs;

  inputText_ = "";
  replyText_ = "";
  expectTtsId_ = 0;

  // 開始時に吹き出し消す（Behaviorの残りを消す）
  bubbleText_  = "";
  bubbleDirty_ = true;

  overlay_ = AiUiOverlay();
  overlay_.active = true;
  overlay_.hint = "AI";

  LOG_EVT_INFO("EVT_AI_STATE", "state=LISTENING");
  updateOverlay_(nowMs);
}

void AiTalkController::enterThinking_(uint32_t nowMs) {
  state_ = AiState::Thinking;
  thinkStartMs_ = nowMs;

  overlay_.active = true;
  overlay_.hint = "AI";

  LOG_EVT_INFO("EVT_AI_STATE", "state=THINKING");
  updateOverlay_(nowMs);
}

void AiTalkController::enterSpeaking_(uint32_t nowMs) {
  state_ = AiState::Speaking;
  speakStartMs_ = nowMs;

  overlay_.active = true;
  overlay_.hint = "AI";

  LOG_EVT_INFO("EVT_AI_STATE", "state=SPEAKING");
  updateOverlay_(nowMs);
}

void AiTalkController::enterPostSpeakBlank_(uint32_t nowMs) {
  state_ = AiState::PostSpeakBlank;
  blankStartMs_ = nowMs;

  // 0.5秒 空吹き出し（main側がsetStackchanSpeech("")）
  bubbleText_  = "";
  bubbleDirty_ = true;

  overlay_.active = true;
  overlay_.hint = "AI";

  LOG_EVT_INFO("EVT_AI_STATE", "state=POST_BLANK");
  updateOverlay_(nowMs);
}

void AiTalkController::enterCooldown_(uint32_t nowMs, bool error, const char* reason) {
  state_ = AiState::Cooldown;
  cooldownStartMs_ = nowMs;
  cooldownDurMs_ = kCooldownMs + (error ? kErrorExtraMs : 0);

  overlay_.active = true;
  overlay_.hint = "AI";

  LOG_EVT_INFO("EVT_AI_STATE",
               "state=COOLDOWN error=%d reason=%s",
               error ? 1 : 0, reason ? reason : "-");

  updateOverlay_(nowMs);
}

void AiTalkController::updateOverlay_(uint32_t nowMs) {
  if (state_ == AiState::Idle) {
    overlay_.active = false;
    return;
  }

  overlay_.active = true;
  if (!overlay_.hint.length()) overlay_.hint = "AI";

  auto ceilSec = [](uint32_t remainMs) -> int {
    return (int)((remainMs + 999) / 1000);
  };

  String s;
  switch (state_) {
    case AiState::Listening: {
      const uint32_t elapsed = nowMs - listenStartMs_;
      const uint32_t remain  = (elapsed >= kListeningTimeoutMs) ? 0 : (kListeningTimeoutMs - elapsed);
      s = "LISTEN " + String(ceilSec(remain)) + "s";
      break;
    }
    case AiState::Thinking: {
      const uint32_t elapsed = nowMs - thinkStartMs_;
      const uint32_t remain  = (elapsed >= kThinkingMockMs) ? 0 : (kThinkingMockMs - elapsed);
      s = "THINK " + String(ceilSec(remain)) + "s";
      break;
    }
    case AiState::Speaking:
      s = "SPEAK";
      break;
    case AiState::PostSpeakBlank: {
      const uint32_t elapsed = nowMs - blankStartMs_;
      const uint32_t remain  = (elapsed >= kPostSpeakBlankMs) ? 0 : (kPostSpeakBlankMs - elapsed);
      s = "BLANK " + String(ceilSec(remain)) + "s";
      break;
    }
    case AiState::Cooldown: {
      const uint32_t elapsed = nowMs - cooldownStartMs_;
      const uint32_t remain  = (elapsed >= cooldownDurMs_) ? 0 : (cooldownDurMs_ - elapsed);
      s = "COOL " + String(ceilSec(remain)) + "s";
      break;
    }
    default:
      s = "AI";
      break;
  }

  overlay_.line1 = s;
  overlay_.line2 = "";
}
