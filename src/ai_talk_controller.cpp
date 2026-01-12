#include "ai_talk_controller.h"
#include "logging.h"
#include "orchestrator.h"
#include "openai_llm.h"
#include <M5Unified.h>
#include "config.h"

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


static String utf8SafeClamp_(const String& s, size_t maxBytes) {
  const size_t L = s.length();
  if (L <= maxBytes) return s;

  const char* p = s.c_str();
  size_t cut = maxBytes;

  // cut がUTF-8の継続バイト(10xxxxxx)に刺さっていたら手前へ戻す
  while (cut > 0 && (((uint8_t)p[cut] & 0xC0) == 0x80)) {
    cut--;
  }
  return s.substring(0, (unsigned)cut);
}

// UTF-8を壊さずに maxBytes 以内へ丸める（バイト上限は維持）
static size_t utf8SeqLen_(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1; // 不正先頭は1扱い
}

String AiTalkController::clampBytes_(const String& s, size_t maxBytes) {
  const size_t n = s.length(); // bytes
  if (n <= maxBytes) return s;
  if (maxBytes == 0) return "";

  size_t i = 0;
  while (i < n && i < maxBytes) {
    const uint8_t c = (uint8_t)s[i];
    const size_t L = utf8SeqLen_(c);
    if (i + L > maxBytes) break;

    // continuation bytes validate (tolerant)
    bool ok = true;
    for (size_t k = 1; k < L; k++) {
      if (i + k >= n) { ok = false; break; }
      const uint8_t cc = (uint8_t)s[i + k];
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
    }
    if (!ok) break;

    i += L;
  }
  return s.substring(0, (unsigned)i);
}



void AiTalkController::begin(Orchestrator* orch) {
  orch_ = orch;

  // 録音機能初期化（失敗してもAI全体は死なない。LISTEN開始時にエラー扱いへ）
  const bool recOk = recorder_.begin();
  mc_logf("[REC] begin ok=%d", recOk ? 1 : 0);

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
      recorder_.cancel();
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
        // 10秒で確定停止 → THINKING
        lastRecOk_ = recorder_.stop(nowMs);

        // ★重要：Mic(I2S)が掴みっぱなしだと、直後のTTSで Speaker のI2S初期化が失敗して音が壊れることがある。
        // stop()が「録音停止」だけで Mic.end() していない実装でもここで確実に解放する。
        if (M5.Mic.isEnabled()) {
          mc_logf("[REC] mic still enabled after stop -> end (release I2S)");
          M5.Mic.end();
          delay(10);
        }

        // stopがTIMEOUTでも、サンプルが取れているなら続行させる（救済）
        const uint32_t durMs = recorder_.durationMs();
        const size_t samples = recorder_.samples();
        if (!lastRecOk_ && samples >= (size_t)(MC_AI_REC_SAMPLE_RATE * 0.2f)) { // 0.2秒以上
          mc_logf("[REC] stop not ok but samples=%u, continue as ok", (unsigned)samples);
          lastRecOk_ = true;
        }

        mc_logf("[REC] stop ok=%d dur=%lums samples=%u",
                lastRecOk_ ? 1 : 0,
                (unsigned long)durMs,
                (unsigned)samples);

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
        replyText_ = clampBytes_(replyText_, MC_AI_TTS_MAX_CHARS);

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
          expectTtsId_ = 0;        // 遅延doneは無視する（次に引きずらない）
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
      lastUserText_ = clampBytes_(stt.text, MC_AI_MAX_INPUT_CHARS);
    } else {
      lastUserText_ = stt.err.length() ? stt.err : String(MC_AI_ERR_TEMP_FAIL_TRY_AGAIN);
      errorFlag_ = true;
    }

    // 秘密/全文ログ禁止：先頭だけ
    String head = lastUserText_;
    if (head.length() > 30) head = head.substring(0, 30);
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
        replyText_ = clampBytes_(replyText_, MC_AI_TTS_MAX_CHARS);
        bubbleText_ = replyText_;

        // 先頭だけ（overlay用）
        lastLlmTextHead_ = clampBytes_(replyText_, 40);
        if (replyText_.length() > lastLlmTextHead_.length()) lastLlmTextHead_ += "…";
      } else {
        // 失敗時フォールバック + cooldown +1秒
        errorFlag_ = true;
        lastLlmErr_ = llm.err;
        if (lastLlmErr_.length() > 40) lastLlmErr_ = lastLlmErr_.substring(0, 40) + "…";
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
    replyText_ = clampBytes_(lastUserText_, MC_AI_TTS_MAX_CHARS);
    bubbleText_ = replyText_;
  }

  replyReady_ = true;

  // THINK タイマー開始（tick上の“短い考え中”表示用）
  thinkStartMs_ = millis();

  LOG_EVT_INFO("EVT_AI_STATE", "state=THINKING");
  updateOverlay_(thinkStartMs_);
}

void AiTalkController::enterListening_(uint32_t nowMs) {
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

  // 録音スタート
  lastRecOk_ = recorder_.start(nowMs);
  mc_logf("[REC] start ok=%d", lastRecOk_ ? 1 : 0);

  LOG_EVT_INFO("EVT_AI_STATE", "state=LISTENING");
  updateOverlay_(nowMs);
}




void AiTalkController::enterIdle_(uint32_t nowMs, const char* reason) {
  // 録音中の可能性があるので保険でキャンセル
  recorder_.cancel();

  state_ = AiState::Idle;

  inputText_ = "";
  replyText_ = "";

  // TTS待ちの解除
  expectTtsId_ = 0;
  speakHardTimeoutMs_ = 0;

  // overlay消す
  overlay_ = AiUiOverlay();
  overlay_.active = false;

  // cooldown延長フラグもリセット
  errorFlag_ = false;

  LOG_EVT_INFO("EVT_AI_STATE",
               "state=IDLE reason=%s",
               reason ? reason : "-");

  (void)nowMs; // 現状は未使用（将来の拡張用）
}



void AiTalkController::enterSpeaking_(uint32_t nowMs) {
  state_ = AiState::Speaking;
  speakStartMs_ = nowMs;

  // TTSありの場合のみ、done待ち上限を動的に計算
  speakHardTimeoutMs_ = 0;
  if (expectTtsId_ != 0) {
    speakHardTimeoutMs_ = calcTtsHardTimeoutMs_(replyText_.length());
    mc_logf("[ai] tts hard timeout=%lums (len=%u)",
            (unsigned long)speakHardTimeoutMs_,
            (unsigned)replyText_.length());
  }

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
      // THINKING 中の表示：
      // - STT失敗なら STTERR を表示
      // - STT成功なら LLM結果（成功/失敗）を表示
      if (!lastSttOk_) {
        overlay_.line1 = "STT: ERR";
        String head = lastUserText_;
        if (head.length() > 40) head = head.substring(0, 40) + "…";
        overlay_.line2 = head;
        return;
      }

      overlay_.line1 = lastLlmOk_ ? "LLM: OK" : "LLM: ERR";

      String head = lastLlmOk_ ? lastLlmTextHead_ : lastLlmErr_;
      if (!head.length()) head = "...";
      if (head.length() > 40) head = head.substring(0, 40) + "…";
      overlay_.line2 = head;
      return;
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
