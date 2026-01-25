#include "ai_talk_controller.h"
#include "logging.h"
#include "orchestrator.h"
#include "openai_llm.h"
#include "mc_text_utils.h"
#include <M5Unified.h>
#include "config.h"
#include <string.h>

// ------------------------------------------------------------
// Step2 fix: keep build working even if config / overlay differs
// ------------------------------------------------------------

// config.h に無い環境でもビルドが通るようにフォールバック
#ifndef MC_AI_LISTENING_HINT_TEXT
  #define MC_AI_LISTENING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_THINKING_HINT_TEXT
  #define MC_AI_THINKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_SPEAKING_HINT_TEXT
  #define MC_AI_SPEAKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif


// AiTalkController::AiState -> (ai_interface.h 側の) ::AiState へ安全に変換
// UI側に無い状態（PostSpeakBlank）は Speaking に丸める。
static inline ::AiState mcToUiAiState_(AiTalkController::AiState s) {
  switch (s) {
    case AiTalkController::AiState::Idle:          return ::AiState::Idle;
    case AiTalkController::AiState::Listening:     return ::AiState::Listening;
    case AiTalkController::AiState::Thinking:      return ::AiState::Thinking;
    case AiTalkController::AiState::Speaking:      return ::AiState::Speaking;
    case AiTalkController::AiState::PostSpeakBlank:return ::AiState::Speaking; // UIに無いので丸める
    case AiTalkController::AiState::Cooldown:      return ::AiState::Cooldown;
    default:                                       return ::AiState::Idle;
  }
}


// ---- timings ----
static constexpr uint32_t kListeningTimeoutMs      = (uint32_t)MC_AI_LISTEN_MAX_SECONDS * 1000UL;
static constexpr uint32_t kListeningCancelWindowMs = (uint32_t)MC_AI_LISTEN_CANCEL_WINDOW_SEC * 1000UL;
static constexpr uint32_t kThinkingMockMs          = 200;  // 最低限の「考え中」表示（ブロッキング後でも0にしない）
static constexpr uint32_t kPostSpeakBlankMs        = 500;
static constexpr uint32_t kCooldownMs              = (uint32_t)MC_AI_COOLDOWN_MS;
static constexpr uint32_t kErrorExtraMs            = (uint32_t)MC_AI_COOLDOWN_ERROR_EXTRA_MS;

// STT+LLM 全体上限（ms）
static constexpr uint32_t kTotalThinkBudgetMs      = 20000;
static constexpr uint32_t kBudgetMarginMs          = 250;



// TTS done が来ないときの安全タイムアウト（ネット遅延 + 音声再生を含む）
// 固定20sだと、TTS取得が遅いときに tts_timeout になって状態だけ先に進んでしまう。
// 返答テキストのバイト数に応じて動的に延長する。
static uint32_t calcTtsHardTimeoutMs_(size_t textBytes) {
  uint32_t t = (uint32_t)MC_AI_TTS_HARD_TIMEOUT_BASE_MS +
               (uint32_t)(textBytes * (size_t)MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS);
  const uint32_t tMin = (uint32_t)MC_AI_TTS_HARD_TIMEOUT_MIN_MS;
  const uint32_t tMax = (uint32_t)MC_AI_TTS_HARD_TIMEOUT_MAX_MS;
  if (t < tMin) t = tMin;
  if (t > tMax) t = tMax;
  return t;
}

// orch が無い（sandbox等）ときの擬似発話時間
static constexpr uint32_t kSimulatedSpeakMs        = 2000;



// Step2: UTF-8 clamp / one-line sanitize are centralized in mc_text_utils.*



void AiTalkController::begin(Orchestrator* orch) {
  orch_ = orch;

  // 録音機能初期化（失敗してもAI全体は死なない。LISTEN開始時にエラー扱いへ）
  const bool recOk = recorder_.begin();
  mc_logf("[REC] begin ok=%d", recOk ? 1 : 0);

  enterIdle_(millis(), "begin");
  abortTtsId_ = 0;
  abortTtsReason_[0] = 0;

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
      // 「保険cancel」で cancel done が出ないように、必ず isRecording ガード
      if (recorder_.isRecording()) {
        recorder_.cancel();
      }
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

  inputText_ = mcUtf8ClampBytes(text, 200);
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

bool AiTalkController::consumeAbortTts(uint32_t* outId, const char** outReason) {
  if (abortTtsId_ == 0) return false;
  if (outId) *outId = abortTtsId_;
  if (outReason) *outReason = (abortTtsReason_[0] ? abortTtsReason_ : nullptr);
  abortTtsId_ = 0;
  abortTtsReason_[0] = 0;
  return true;
}



void AiTalkController::tick(uint32_t nowMs) {
  switch (state_) {
    case AiState::Idle:
      overlay_.active = false;
      return;

    case AiState::Listening: {
      const uint32_t elapsed = nowMs - listenStartMs_;
      if (elapsed >= kListeningTimeoutMs) {
        // 10秒で確定停止 → THINKING
        lastRecOk_ = recorder_.stop(nowMs);

        // stopがTIMEOUTでも、サンプルが取れているなら続行させる（救済）
        const size_t samples = recorder_.samples();
        if (!lastRecOk_ && samples >= (size_t)(MC_AI_REC_SAMPLE_RATE * 0.2f)) { // 0.2秒以上
          mc_logf("[REC] stop not ok but samples=%u, continue as ok", (unsigned)samples);
          lastRecOk_ = true;
        }

        // stop ok ログは recorder 側に寄せる（ここでは出さない）

        enterThinking_(nowMs);
      } else {
        updateOverlay_(nowMs);
      }
      return;
    }

    case AiState::Thinking: {
      const uint32_t elapsed = nowMs - thinkStartMs_;
      if (replyReady_ && elapsed >= kThinkingMockMs) {
        // 念のため（保険）
        replyText_ = mcUtf8ClampBytes(replyText_, MC_AI_TTS_MAX_CHARS);

        // bubble show要求（必ず main 側が setStackchanSpeech()）
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

        // enterSpeaking_で計算済みだが、万一0ならここでも計算しておく
        if (speakHardTimeoutMs_ == 0) {
          speakHardTimeoutMs_ = calcTtsHardTimeoutMs_(replyText_.length());
          mc_logf("[ai] tts hard timeout(late calc)=%lums (len=%u)",
                  (unsigned long)speakHardTimeoutMs_,
                  (unsigned)replyText_.length());
        }

        if (elapsed >= speakHardTimeoutMs_) {
          // doneが来ない（/遅すぎる） → エラー扱いでcooldown延長
          mc_logf("[ai] tts timeout elapsed=%lums limit=%lums (expect=%lu)",
                  (unsigned long)elapsed,
                  (unsigned long)speakHardTimeoutMs_,
                  (unsigned long)expectTtsId_);

          // Phase6-1a: abort reason を統一して貫通させる
          static constexpr const char* kReason = "ai_tts_timeout";

          // Orchestrator側にもキャンセルを通知（pending/expectを確実に掃除）
          if (orch_ && expectTtsId_ != 0) {
            orch_->cancelSpeak(expectTtsId_, kReason, Orchestrator::CancelSource::AI);
          }

          // abort通知（mainで cancel+clear する）
          abortTtsId_ = expectTtsId_;
          strncpy(abortTtsReason_, kReason, sizeof(abortTtsReason_) - 1);
          abortTtsReason_[sizeof(abortTtsReason_) - 1] = 0;

          expectTtsId_ = 0;        // 遅延doneは無視する（次に引きずらない）
          enterCooldown_(nowMs, true, kReason);
        } else {
          updateOverlay_(nowMs);
        }
      }
      return;
    }

    case AiState::PostSpeakBlank: {
      const uint32_t elapsed = nowMs - blankStartMs_;
      if (elapsed >= kPostSpeakBlankMs) {
        enterCooldown_(nowMs, errorFlag_, "post_blank_done");
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



void AiTalkController::enterThinking_(uint32_t nowMs) {
  state_ = AiState::Thinking;

  // STT+LLM 全体タイムアウト基準
  overallStartMs_ = nowMs;

  overlay_.active = true;
  overlay_.hint = MC_AI_IDLE_HINT_TEXT;

  // ---- STT（8秒枠）----
  lastUserText_ = "";
  lastSttOk_ = false;
  lastSttStatus_ = 0;
  errorFlag_ = false;

  // ---- LLM ----
  replyReady_ = false;
  lastLlmOk_ = false;
  lastLlmHttp_ = 0;
  lastLlmTookMs_ = 0;
  lastLlmErr_ = "";
  lastLlmTextHead_ = "";

  if (!lastRecOk_ || recorder_.samples() == 0) {
    lastSttOk_ = false;
    lastSttStatus_ = 0;
    lastUserText_ = MC_AI_ERR_MIC_TOO_QUIET;
    errorFlag_ = true;
    mc_logf("[STT] skip (rec not ok)");
  } else {
    const uint32_t sttT0 = millis();

    // 20s枠から残りを見つつ STT の上限を決める（通常は 8s のまま）
    uint32_t sttTimeout = (uint32_t)MC_AI_STT_TIMEOUT_MS;
    const uint32_t elapsed0 = millis() - overallStartMs_;
    if (elapsed0 + kBudgetMarginMs < kTotalThinkBudgetMs) {
      const uint32_t remain = kTotalThinkBudgetMs - elapsed0 - kBudgetMarginMs;
      if (remain < sttTimeout) sttTimeout = remain;
    }

    auto stt = AzureStt::transcribePcm16Mono(
        recorder_.data(),
        recorder_.samples(),
        MC_AI_REC_SAMPLE_RATE,
        sttTimeout
    );
    const uint32_t sttMs = millis() - sttT0;

    lastSttOk_ = stt.ok;
    lastSttStatus_ = stt.status;

    if (stt.ok) {
      lastUserText_ = mcUtf8ClampBytes(stt.text, MC_AI_MAX_INPUT_CHARS);
    } else {
      lastUserText_ = stt.err.length() ? stt.err : String(MC_AI_ERR_TEMP_FAIL_TRY_AGAIN);
      errorFlag_ = true;
    }

    // 秘密/全文ログ禁止：先頭だけ
    String head = mcLogHead(lastUserText_, 30);
    mc_logf("[STT] done ok=%d http=%d took=%lums text_len=%u head=\"%s\"",
            lastSttOk_ ? 1 : 0,
            lastSttStatus_,
            (unsigned long)sttMs,
            (unsigned)lastUserText_.length(),
            head.c_str());
  }

  // ---- LLM（10秒枠、ただし全体20秒を超えない）----
  if (lastSttOk_) {
    const uint32_t elapsed = millis() - overallStartMs_;
    uint32_t llmTimeout = 0;
    if (elapsed + kBudgetMarginMs < kTotalThinkBudgetMs) {
      llmTimeout = kTotalThinkBudgetMs - elapsed - kBudgetMarginMs;
      if (llmTimeout > (uint32_t)MC_AI_LLM_TIMEOUT_MS) llmTimeout = (uint32_t)MC_AI_LLM_TIMEOUT_MS;
    }

    if (llmTimeout < 200) {
      // もう予算が無い
      lastLlmOk_ = false;
      lastLlmErr_ = "LLM timeout";
      errorFlag_ = true;
      replyText_ = String(MC_AI_TEXT_FALLBACK);
      bubbleText_ = replyText_;
      mc_logf("[AI] LLM skipped (budget) elapsed=%lums", (unsigned long)elapsed);
    } else {
      const auto llm = OpenAiLlm::generateReply(lastUserText_, llmTimeout);
      lastLlmOk_ = llm.ok;
      lastLlmHttp_ = llm.http;
      lastLlmTookMs_ = llm.tookMs;

      if (llm.ok) {
        replyText_ = llm.text;
        replyText_ = mcUtf8ClampBytes(replyText_, MC_AI_TTS_MAX_CHARS);
        bubbleText_ = replyText_;

        // 先頭だけ（overlay用）
        lastLlmTextHead_ = mcUtf8ClampBytes(replyText_, 40);
        if (replyText_.length() > lastLlmTextHead_.length()) lastLlmTextHead_ += "…";
      } else {
        // 失敗時フォールバック + cooldown +1秒
        errorFlag_ = true;
        lastLlmErr_ = mcSanitizeOneLine(llm.err);
        {
          String h = mcUtf8ClampBytes(lastLlmErr_, 40);
          if (lastLlmErr_.length() > h.length()) h += "…";
          lastLlmErr_ = h;
        }
        replyText_ = String(MC_AI_TEXT_FALLBACK);
        bubbleText_ = replyText_;
      }

      mc_logf("[AI] LLM http=%d ok=%d took=%lums outLen=%u",
              lastLlmHttp_,
              lastLlmOk_ ? 1 : 0,
              (unsigned long)lastLlmTookMs_,
              (unsigned)replyText_.length());
    }
  } else {
    // STT失敗：LLMは呼ばない
    errorFlag_ = true;
    lastLlmErr_ = "STT failed";
    replyText_ = mcUtf8ClampBytes(lastUserText_, MC_AI_TTS_MAX_CHARS);
    bubbleText_ = replyText_;
  }

  replyReady_ = true;

  // THINK タイマー開始（tick上の“短い考え中”表示用）
  thinkStartMs_ = millis();

  LOG_EVT_INFO("EVT_AI_STATE", "state=THINKING");
  updateOverlay_(thinkStartMs_);
}



void AiTalkController::enterListening_(uint32_t nowMs) {
  // 録音開始に失敗したら、busy扱い（LISTENING）へ入らない。
  // 例：TTS再生中などで I2S owner=Speaker のとき。
  lastRecOk_ = recorder_.start(nowMs);

  // start成功ログは recorder 側に寄せる（ここでは出さない）
  if (!lastRecOk_) {
    // tap は消費してよいが、state は Idle のままにする。
    mc_logf("[ai] listen start failed -> stay IDLE");
    return;
  }

  state_ = AiState::Listening;
  listenStartMs_ = nowMs;

  overallStartMs_ = 0;

  inputText_ = "";
  lastUserText_ = "";
  lastSttOk_ = false;
  lastSttStatus_ = 0;
  errorFlag_ = false;

  // LLM state reset
  replyReady_ = false;
  lastLlmOk_ = false;
  lastLlmHttp_ = 0;
  lastLlmTookMs_ = 0;
  lastLlmErr_ = "";
  lastLlmTextHead_ = "";

  replyText_ = "";
  expectTtsId_ = 0;

  // 開始時に吹き出し消す（Behaviorの残りを消す）
  bubbleText_  = "";
  bubbleDirty_ = true;

  overlay_ = AiUiOverlay();
  overlay_.active = true;
  overlay_.hint = MC_AI_IDLE_HINT_TEXT;

  LOG_EVT_INFO("EVT_AI_STATE", "state=LISTENING");
  updateOverlay_(nowMs);
}


void AiTalkController::enterIdle_(uint32_t nowMs, const char* reason) {
  // 録音中の可能性があるので保険でキャンセル
  // 「保険cancel」で cancel done が出ないように、必ず isRecording ガード
  if (recorder_.isRecording()) {
    recorder_.cancel();
  }

  state_ = AiState::Idle;

  inputText_ = "";
  replyText_ = "";

  // TTS待ちの解除
  expectTtsId_ = 0;
  speakHardTimeoutMs_ = 0;

  // overlay消す
  overlay_ = AiUiOverlay();
  overlay_.active = false;

  // cooldown延長フラグもリセット（前の挙動へ）
  errorFlag_ = false;

  LOG_EVT_INFO("EVT_AI_STATE",
               "state=IDLE reason=%s",
               reason ? reason : "-");

  (void)nowMs; // 現状は未使用（将来の拡張用）
}


void AiTalkController::enterSpeaking_(uint32_t nowMs) {
  state_ = AiState::Speaking;
  speakStartMs_ = nowMs;

  // TTSありの場合のみ、done待ち上限を動的に計算（前の挙動へ）
  speakHardTimeoutMs_ = 0;
  if (expectTtsId_ != 0) {
    speakHardTimeoutMs_ = calcTtsHardTimeoutMs_(replyText_.length());
    mc_logf("[ai] tts hard timeout=%lums (len=%u)",
            (unsigned long)speakHardTimeoutMs_,
            (unsigned)replyText_.length());
  }

  LOG_EVT_INFO("EVT_AI_STATE", "state=SPEAKING");
  updateOverlay_(nowMs);
}




void AiTalkController::enterPostSpeakBlank_(uint32_t nowMs) {
  state_ = AiState::PostSpeakBlank;
  blankStartMs_ = nowMs;

  // 吹き出しは空にする（一定時間）
  bubbleText_ = "";
  bubbleDirty_ = true;

  LOG_EVT_INFO("EVT_AI_STATE", "state=POST_SPEAK_BLANK");
  updateOverlay_(nowMs);
}

void AiTalkController::enterCooldown_(uint32_t nowMs, bool error, const char* reason) {
  state_ = AiState::Cooldown;
  cooldownStartMs_ = nowMs;

  // 基本 cooldown + エラー時は延長
  cooldownDurMs_ = kCooldownMs;
  if (error) cooldownDurMs_ += kErrorExtraMs;

  // TTS待ちはここで解除（遅延doneは無視する）
  expectTtsId_ = 0;
  speakHardTimeoutMs_ = 0;

  (void)reason;

  LOG_EVT_INFO("EVT_AI_STATE", "state=COOLDOWN");
  updateOverlay_(nowMs);
}

void AiTalkController::updateOverlay_(uint32_t nowMs) {
  if (state_ == AiState::Idle) {
    overlay_.active = false;
    overlay_.state = ::AiState::Idle;
    overlay_.line1 = "";
    overlay_.line2 = "";
    // hint は残してもいいが、ここでは空にしておく
    // overlay_.hint = "";
    return;
  }

  overlay_.active = true;

  // 型ズレ吸収（AiTalkController::AiState -> ::AiState）
  overlay_.state = mcToUiAiState_(state_);

  // hint は config が無い環境でも通るようフォールバック済み（上で #ifndef 済み）
  // “AI” 固定にしたいならここを変えてOK。現状は既存の定数を優先。
  if (!overlay_.hint.length()) overlay_.hint = MC_AI_IDLE_HINT_TEXT;

  auto ceilSec = [](uint32_t remainMs) -> int {
    return (int)((remainMs + 999) / 1000);
  };

  // デフォルト
  overlay_.line1 = "";
  overlay_.line2 = "";

  switch (state_) {
    case AiState::Listening: {
      overlay_.hint = MC_AI_LISTENING_HINT_TEXT;

      const uint32_t elapsed = nowMs - listenStartMs_;
      const uint32_t remain  = (elapsed >= kListeningTimeoutMs) ? 0 : (kListeningTimeoutMs - elapsed);
      overlay_.line1 = "LISTEN " + String(ceilSec(remain)) + "s";
      overlay_.line2 = "";
      return;
    }

    case AiState::Thinking: {
      overlay_.hint = MC_AI_THINKING_HINT_TEXT;

      // THINKING 中の表示：
      // - STT失敗なら STT: ERR + 先頭
      // - STT成功なら LLM OK/ERR + 先頭
      if (!lastSttOk_) {
        overlay_.line1 = "STT: ERR";
        String head = mcLogHead(lastUserText_, 40);
        if (!head.length()) head = "...";
        overlay_.line2 = head;
        return;
      }

      overlay_.line1 = lastLlmOk_ ? "LLM: OK" : "LLM: ERR";
      String head = lastLlmOk_ ? lastLlmTextHead_ : lastLlmErr_;
      head = mcLogHead(head, 40);
      if (!head.length()) head = "...";
      overlay_.line2 = head;
      return;
    }

    case AiState::Speaking: {
      overlay_.hint = MC_AI_SPEAKING_HINT_TEXT;
      overlay_.line1 = "SPEAK";
      overlay_.line2 = "";
      return;
    }

    case AiState::PostSpeakBlank: {
      overlay_.hint = MC_AI_SPEAKING_HINT_TEXT;

      const uint32_t elapsed = nowMs - blankStartMs_;
      const uint32_t remain  = (elapsed >= kPostSpeakBlankMs) ? 0 : (kPostSpeakBlankMs - elapsed);
      overlay_.line1 = "BLANK " + String(ceilSec(remain)) + "s";
      overlay_.line2 = "";
      return;
    }

    case AiState::Cooldown: {
      overlay_.hint = MC_AI_IDLE_HINT_TEXT;

      const uint32_t elapsed = nowMs - cooldownStartMs_;
      const uint32_t remain  = (elapsed >= cooldownDurMs_) ? 0 : (cooldownDurMs_ - elapsed);
      overlay_.line1 = "COOL " + String(ceilSec(remain)) + "s";
      overlay_.line2 = "";
      return;
    }

    default:
      overlay_.hint = MC_AI_IDLE_HINT_TEXT;
      overlay_.line1 = "AI";
      overlay_.line2 = "";
      return;
  }
}
