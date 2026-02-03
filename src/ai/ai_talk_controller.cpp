// Module implementation.
#include "ai/ai_talk_controller.h"

#include <string.h>

#include "ai/azure_stt.h"
#include "config/config.h"
#include "utils/logging.h"
#include "utils/mc_text_utils.h"

// Maps internal state to UI overlay status.
// ---- timings ----
// - LISTEN timeout / cancel window: MC_AI_LISTEN_TIMEOUT_MS /
// MC_AI_LISTEN_CANCEL_WINDOW_MS
// - thinking mock / post-speak blank / cooldown: MC_AI_THINKING_MOCK_MS /
// MC_AI_POST_SPEAK_BLANK_MS / MC_AI_COOLDOWN_MS(+MC_AI_COOLDOWN_ERROR_EXTRA_MS)
// - overall budget / margin: MC_AI_OVERALL_DEADLINE_MS /
// MC_AI_OVERALL_MARGIN_MS
// - simulated speak: MC_AI_SIMULATED_SPEAK_MS
static uint32_t calcTtsHardTimeoutMs_(size_t textBytes) {
  uint32_t t =
      (uint32_t)MC_AI_TTS_HARD_TIMEOUT_BASE_MS +
      (uint32_t)(textBytes * (size_t)MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS);
  const uint32_t tMin = (uint32_t)MC_AI_TTS_HARD_TIMEOUT_MIN_MS;
  const uint32_t tMax = (uint32_t)MC_AI_TTS_HARD_TIMEOUT_MAX_MS;
  if (t < tMin)
    t = tMin;
  if (t > tMax)
    t = tMax;
  return t;
}

void AiTalkController::llmTaskEntry_(void *arg) {
  auto *self = static_cast<AiTalkController *>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!self || !self->llmMutex_)
      continue;

    uint32_t reqId = 0;
    String input;
    uint32_t timeoutMs = 0;
    xSemaphoreTake(self->llmMutex_, portMAX_DELAY);
    reqId = self->llmReqId_;
    input = self->llmInput_;
    timeoutMs = self->llmTimeout_;
    xSemaphoreGive(self->llmMutex_);

    const auto res = openai_llm::generateReply(input, timeoutMs);

    xSemaphoreTake(self->llmMutex_, portMAX_DELAY);
    if (reqId == self->llmReqId_) {
      self->llmResult_ = res;
      self->llmDone_ = true;
      self->llmBusy_ = false;
    }
    xSemaphoreGive(self->llmMutex_);
  }
}

void AiTalkController::startLlmRequest_(const String &userText,
                                       uint32_t timeoutMs) {
  if (!llmTask_ || !llmMutex_) {
    lastLlmOk_ = false;
    lastLlmErr_ = "llm_task_not_ready";
    errorFlag_ = true;
    replyText_ = String(MC_AI_TEXT_FALLBACK);
    bubbleText_ = replyText_;
    replyReady_ = true;
    return;
  }
  if (timeoutMs < 200) {
    lastLlmOk_ = false;
    lastLlmErr_ = "LLM timeout";
    errorFlag_ = true;
    replyText_ = String(MC_AI_TEXT_FALLBACK);
    bubbleText_ = replyText_;
    replyReady_ = true;
    return;
  }
  xSemaphoreTake(llmMutex_, portMAX_DELAY);
  llmReqId_++;
  if (llmReqId_ == 0)
    llmReqId_ = 1;
  llmInput_ = userText;
  llmTimeout_ = timeoutMs;
  llmDone_ = false;
  llmBusy_ = true;
  xSemaphoreGive(llmMutex_);
  xTaskNotifyGive(llmTask_);
}

bool AiTalkController::tryConsumeLlmResult_() {
  if (!llmDone_ || !llmMutex_)
    return false;

  LlmResult res;
  xSemaphoreTake(llmMutex_, portMAX_DELAY);
  if (!llmDone_) {
    xSemaphoreGive(llmMutex_);
    return false;
  }
  res = llmResult_;
  llmDone_ = false;
  xSemaphoreGive(llmMutex_);

  lastLlmOk_ = res.ok_;
  lastLlmHttp_ = res.http_;
  lastLlmTookMs_ = res.tookMs_;
  if (res.ok_) {
    replyText_ = res.text_;
    replyText_ = mcUtf8ClampBytes(replyText_, MC_AI_TTS_MAX_CHARS);
    bubbleText_ = replyText_;
    lastLlmTextHead_ = mcUtf8ClampBytes(replyText_, 40);
    if (replyText_.length() > lastLlmTextHead_.length())
      lastLlmTextHead_ += "…";
  } else {
    errorFlag_ = true;
    lastLlmErr_ = mcSanitizeOneLine(res.err_);
    {
      String h = mcUtf8ClampBytes(lastLlmErr_, 40);
      if (lastLlmErr_.length() > h.length())
        h += "…";
      lastLlmErr_ = h;
    }
    replyText_ = String(MC_AI_TEXT_FALLBACK);
    bubbleText_ = replyText_;
  }
  replyReady_ = true;
  MC_EVT("LLM", "done ok=%d http=%d took=%lums outLen=%u",
         lastLlmOk_ ? 1 : 0, lastLlmHttp_, (unsigned long)lastLlmTookMs_,
         (unsigned)replyText_.length());
  MC_LOGD("LLM", "http=%d ok=%d took=%lums outLen=%u", lastLlmHttp_,
          lastLlmOk_ ? 1 : 0, (unsigned long)lastLlmTookMs_,
          (unsigned)replyText_.length());
  return true;
}

// Begin() does not allocate heavy resources; it only primes recorder + state.
void AiTalkController::begin(OrchestratorApi *orch) {
  orch_ = orch;
  const bool recOk = recorder_.begin();
  MC_LOGI("REC", "begin ok=%d", recOk ? 1 : 0);
  if (!llmMutex_) {
    llmMutex_ = xSemaphoreCreateMutex();
  }
  if (!llmTask_) {
    const BaseType_t ok = xTaskCreatePinnedToCore(
        llmTaskEntry_, "llmTask", (uint32_t)MC_AI_LLM_TASK_STACK, this,
        (UBaseType_t)MC_AI_LLM_TASK_PRIO, &llmTask_,
        (BaseType_t)MC_AI_LLM_TASK_CORE);
    MC_LOGI("LLM", "task create ok=%d", ok == pdPASS ? 1 : 0);
  }
  enterIdle_(millis(), "begin");
  abortTtsId_ = 0;
  abortTtsReason_[0] = 0;
}
bool AiTalkController::consumeBubbleUpdate(String *outText) {
  if (!outText)
    return false;
  if (!bubbleDirty_)
    return false;
  *outText = bubbleText_;
  bubbleDirty_ = false;
  return true;
}
bool AiTalkController::onTap(int /*x*/, int y, int screenH) {
  if (screenH > 0) {
    if (y >= (screenH / 3))
      return false;
  }
  return onTap();
}
bool AiTalkController::onTap() {
  const uint32_t now = millis();
  // Tap acts as a simple state toggle: idle->listen, listen->stop/think, others
  // ignore.
  if (state_ == AiState::Thinking || state_ == AiState::Speaking ||
      state_ == AiState::PostSpeakBlank || state_ == AiState::Cooldown) {
    return true;
  }
  if (state_ == AiState::Idle) {
    enterListening_(now);
    return true;
  }
  if (state_ == AiState::Listening) {
    const uint32_t elapsed = now - listenStartMs_;
    if (elapsed <= (uint32_t)MC_AI_LISTEN_CANCEL_WINDOW_MS) {
      if (recorder_.isRecording()) {
        recorder_.cancel();
      }
      enterIdle_(now, "tap_cancel");
      return true;
    }
    lastRecOk_ = recorder_.stop(now);
    enterThinking_(now);
    return true;
  }
  return false;
}
void AiTalkController::injectText(const String &text) {
  if (state_ != AiState::Listening)
    return;
  if (!text.length())
    return;
  inputText_ = mcUtf8ClampBytes(text, MC_AI_MAX_INPUT_CHARS);
  MC_LOGD("AI", "injectText len=%u", (unsigned)inputText_.length());
}
void AiTalkController::onSpeakDone(uint32_t rid, uint32_t nowMs) {
  if (state_ == AiState::Speaking && awaitingOrchSpeak_ && activeRid_ != 0 &&
      rid == activeRid_) {
    awaitingOrchSpeak_ = false;
    activeRid_ = 0;
    enterPostSpeakBlank_(nowMs);
  }
}
bool AiTalkController::consumeAbortTts(uint32_t *outId,
                                       const char **outReason) {
  if (abortTtsId_ == 0)
    return false;
  if (outId)
    *outId = abortTtsId_;
  if (outReason)
    *outReason = (abortTtsReason_[0] ? abortTtsReason_ : nullptr);
  abortTtsId_ = 0;
  abortTtsReason_[0] = 0;
  return true;
}
void AiTalkController::tick(uint32_t nowMs) {
  switch (state_) {
  case AiState::Idle:
    overlay_.active_ = false;
    return;
  case AiState::Listening: {
    const uint32_t elapsed = nowMs - listenStartMs_;
    // Auto-stop after timeout to avoid waiting forever.
    if (elapsed >= (uint32_t)MC_AI_LISTEN_TIMEOUT_MS) {
      lastRecOk_ = recorder_.stop(nowMs);
      const size_t samples = recorder_.samples();
      if (!lastRecOk_ && samples >= (size_t)(MC_AI_REC_SAMPLE_RATE * 0.2f)) {
        MC_LOGW("REC", "stop not ok but samples=%u, continue as ok",
                (unsigned)samples);
        lastRecOk_ = true;
      }
      enterThinking_(nowMs);
    } else {
      updateOverlay_(nowMs);
    }
    return;
  }
  case AiState::Thinking: {
    const uint32_t elapsed = nowMs - thinkStartMs_;
    if (!replyReady_) {
      tryConsumeLlmResult_();
    }
    // Wait for both the reply and the minimum "thinking" delay before speaking.
    if (replyReady_ && elapsed >= (uint32_t)MC_AI_THINKING_MOCK_MS) {
      replyText_ = mcUtf8ClampBytes(replyText_, MC_AI_TTS_MAX_CHARS);
      bubbleDirty_ = true;
      bool enqueued = false;
      awaitingOrchSpeak_ = false;
      activeRid_ = 0;
      if (orch_) {
        const uint32_t rid = (uint32_t)100000 + (nextRid_++);
        if (nextRid_ == 0)
          nextRid_ = 1;
        auto cmd = orch_->makeSpeakStartCmd(rid, replyText_,
                                            OrchestratorApi::OrchPrio::High,
                                            OrchestratorApi::OrchKind::AiSpeak);
        if (cmd.valid_) {
          {
            String head = mcLogHead(replyText_, MC_AI_LOG_HEAD_BYTES_TTS_LOG);
            if (replyText_.length() > head.length())
              head += "...";
            MC_LOGD("TTS", "text_head=\"%s\"", head.c_str());
          }
          orch_->enqueueSpeakPending(cmd);
          activeRid_ = rid;
          awaitingOrchSpeak_ = true;
          enqueued = true;
          LOG_EVT_INFO("EVT_AI_ENQUEUE_SPEAK", "rid=%lu tts_id=%lu len=%u",
                       (unsigned long)rid, (unsigned long)cmd.ttsId_,
                       (unsigned)replyText_.length());
        }
      }
      (void)enqueued;
      enterSpeaking_(nowMs);
    } else {
      updateOverlay_(nowMs);
    }
    return;
  }
  case AiState::Speaking: {
    // Step4:
    if (!awaitingOrchSpeak_) {
      const uint32_t elapsed = nowMs - speakStartMs_;
      if (elapsed >= (uint32_t)MC_AI_SIMULATED_SPEAK_MS) {
        enterPostSpeakBlank_(nowMs);
      } else {
        updateOverlay_(nowMs);
      }
      return;
    }
    const uint32_t elapsed = nowMs - speakStartMs_;
    if (speakHardTimeoutMs_ == 0) {
      speakHardTimeoutMs_ = calcTtsHardTimeoutMs_(replyText_.length());
      MC_LOGD("AI", "tts hard limit(late calc)=%lums (len=%u rid=%lu)",
              (unsigned long)speakHardTimeoutMs_, (unsigned)replyText_.length(),
              (unsigned long)activeRid_);
    }
    const uint32_t ttsIdNow =
        (orch_ && activeRid_ != 0) ? orch_->ttsIdForRid(activeRid_) : 0;
    // Hard timeout protects against stuck TTS playback.
    if (elapsed >= speakHardTimeoutMs_) {
      MC_LOGE(
          "AI",
          "TTS HARD TIMEOUT FIRE rid=%lu elapsed=%lums limit=%lums tts_id=%lu",
          (unsigned long)activeRid_, (unsigned long)elapsed,
          (unsigned long)speakHardTimeoutMs_, (unsigned long)ttsIdNow);
      static constexpr const char *reason = "ai_tts_timeout";
      uint32_t canceledId = 0;
      if (orch_ && activeRid_ != 0) {
        orch_->cancelSpeakByRid(activeRid_, reason,
                                OrchestratorApi::CancelSource::Ai, &canceledId);
      }
      if (canceledId != 0) {
        abortTtsId_ = canceledId;
        strncpy(abortTtsReason_, reason, sizeof(abortTtsReason_) - 1);
        abortTtsReason_[sizeof(abortTtsReason_) - 1] = 0;
      }
      awaitingOrchSpeak_ = false;
      activeRid_ = 0;
      enterCooldown_(nowMs, true, reason);
    } else {
      updateOverlay_(nowMs);
    }
    return;
  }
  case AiState::PostSpeakBlank: {
    const uint32_t elapsed = nowMs - blankStartMs_;
    if (elapsed >= (uint32_t)MC_AI_POST_SPEAK_BLANK_MS) {
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
  thinkStartMs_ = nowMs;
  overlay_.active_ = true;
  overlay_.state_ = state_;
  overlay_.hint_ = MC_AI_THINKING_HINT_TEXT;
  overlay_.line1_ = MC_AI_TEXT_THINKING;
  overlay_.line2_ = "";
  // ---- STT ----
  overallStartMs_ = millis();
  const uint32_t overallT0 = overallStartMs_;
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
    MC_EVT("STT", "skip reason=rec_not_ok samples=%u",
           (unsigned)recorder_.samples());
    MC_LOGW("STT", "skip (rec not ok) samples=%u",
            (unsigned)recorder_.samples());
  } else {
    uint32_t sttTimeout = (uint32_t)MC_AI_STT_TIMEOUT_MS;
    const uint32_t elapsed0 = millis() - overallT0;
    if (elapsed0 + (uint32_t)MC_AI_OVERALL_MARGIN_MS <
        (uint32_t)MC_AI_OVERALL_DEADLINE_MS) {
      const uint32_t remain = (uint32_t)MC_AI_OVERALL_DEADLINE_MS - elapsed0 -
                              (uint32_t)MC_AI_OVERALL_MARGIN_MS;
      if (remain < sttTimeout)
        sttTimeout = remain;
    }
    MC_EVT("STT", "start samples=%u sr=%d timeout=%lums",
           (unsigned)recorder_.samples(), (int)MC_AI_REC_SAMPLE_RATE,
           (unsigned long)sttTimeout);
    const uint32_t sttT0 = millis();
    auto stt =
        azure_stt::transcribePcm16Mono(recorder_.data(), recorder_.samples(),
                                       MC_AI_REC_SAMPLE_RATE, sttTimeout);
    const uint32_t sttMs = millis() - sttT0;
    lastSttOk_ = stt.ok_;
    lastSttStatus_ = stt.status_;
    if (stt.ok_) {
      lastUserText_ = mcUtf8ClampBytes(stt.text_, MC_AI_MAX_INPUT_CHARS);
      {
        String head = mcLogHead(lastUserText_, MC_AI_LOG_HEAD_BYTES_STT_LOG);
        if (lastUserText_.length() > head.length())
          head += "...";
        MC_LOGD("STT", "text_head=\"%s\"", head.c_str());
      }
    } else {
      lastUserText_ =
          stt.err_.length() ? stt.err_ : String(MC_AI_ERR_TEMP_FAIL_TRY_AGAIN);
      errorFlag_ = true;
    }
    MC_EVT("STT", "done ok=%d http=%d took=%lums text_len=%u",
           lastSttOk_ ? 1 : 0, lastSttStatus_, (unsigned long)sttMs,
           (unsigned)lastUserText_.length());
    MC_LOGD("STT", "done ok=%d http=%d took=%lums text_len=%u",
            lastSttOk_ ? 1 : 0, lastSttStatus_, (unsigned long)sttMs,
            (unsigned)lastUserText_.length());
  }
  if (lastSttOk_) {
    const uint32_t elapsed = millis() - overallT0;
    uint32_t llmTimeout = 0;
    if (elapsed + (uint32_t)MC_AI_OVERALL_MARGIN_MS <
        (uint32_t)MC_AI_OVERALL_DEADLINE_MS) {
      llmTimeout = (uint32_t)MC_AI_OVERALL_DEADLINE_MS - elapsed -
                   (uint32_t)MC_AI_OVERALL_MARGIN_MS;
      if (llmTimeout > (uint32_t)MC_AI_LLM_TIMEOUT_MS)
        llmTimeout = (uint32_t)MC_AI_LLM_TIMEOUT_MS;
    }
    if (llmTimeout < 200) {
      lastLlmOk_ = false;
      lastLlmErr_ = "LLM timeout";
      errorFlag_ = true;
      replyText_ = String(MC_AI_TEXT_FALLBACK);
      bubbleText_ = replyText_;
      MC_LOGW("LLM", "skipped (budget) elapsed=%lums", (unsigned long)elapsed);
      MC_EVT("LLM", "skip reason=budget elapsed=%lums", (unsigned long)elapsed);
      replyReady_ = true;
    } else {
      MC_EVT("LLM", "start timeout=%lums", (unsigned long)llmTimeout);
      startLlmRequest_(lastUserText_, llmTimeout);
    }
  } else {
    // STT NG
    replyText_ = String(MC_AI_TEXT_FALLBACK);
    bubbleText_ = replyText_;
    replyReady_ = true;
  }
}
void AiTalkController::enterListening_(uint32_t nowMs) {
  lastRecOk_ = recorder_.start(nowMs);
  if (!lastRecOk_) {
    MC_EVT("AI", "listen start failed -> stay IDLE");
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
  activeRid_ = 0;
  awaitingOrchSpeak_ = false;
  bubbleText_ = "";
  bubbleDirty_ = true;
  overlay_ = AiUiOverlay();
  overlay_.active_ = true;
  overlay_.hint_ = MC_AI_IDLE_HINT_TEXT;
  const uint32_t ttsId =
      (orch_ && activeRid_ != 0) ? orch_->ttsIdForRid(activeRid_) : 0;
  LOG_EVT_INFO("EVT_AI_STATE", "state=LISTENING rid=%lu tts_id=%lu",
               (unsigned long)activeRid_, (unsigned long)ttsId);
  updateOverlay_(nowMs);
}
void AiTalkController::enterIdle_(uint32_t nowMs, const char *reason) {
  if (recorder_.isRecording()) {
    recorder_.cancel();
  }
  state_ = AiState::Idle;
  inputText_ = "";
  replyText_ = "";
  const uint32_t oldRid = activeRid_;
  activeRid_ = 0;
  awaitingOrchSpeak_ = false;
  speakHardTimeoutMs_ = 0;
  overlay_ = AiUiOverlay();
  overlay_.active_ = false;
  errorFlag_ = false;
  const uint32_t ttsId =
      (orch_ && oldRid != 0) ? orch_->ttsIdForRid(oldRid) : 0;
  LOG_EVT_INFO("EVT_AI_STATE", "state=IDLE reason=%s rid=%lu tts_id=%lu",
               reason ? reason : "-", (unsigned long)oldRid,
               (unsigned long)ttsId);
  (void)nowMs;
}
void AiTalkController::enterSpeaking_(uint32_t nowMs) {
  state_ = AiState::Speaking;
  speakStartMs_ = nowMs;
  speakHardTimeoutMs_ = 0;
  if (awaitingOrchSpeak_) {
    speakHardTimeoutMs_ = calcTtsHardTimeoutMs_(replyText_.length());
    MC_LOGD("AI", "tts hard limit=%lums (len=%u rid=%lu)",
            (unsigned long)speakHardTimeoutMs_, (unsigned)replyText_.length(),
            (unsigned long)activeRid_);
  }
  const uint32_t ttsId =
      (orch_ && activeRid_ != 0) ? orch_->ttsIdForRid(activeRid_) : 0;
  LOG_EVT_INFO("EVT_AI_STATE", "state=SPEAKING rid=%lu tts_id=%lu",
               (unsigned long)activeRid_, (unsigned long)ttsId);
  updateOverlay_(nowMs);
}
void AiTalkController::enterPostSpeakBlank_(uint32_t nowMs) {
  state_ = AiState::PostSpeakBlank;
  blankStartMs_ = nowMs;
  bubbleText_ = "";
  bubbleDirty_ = true;
  const uint32_t ttsId =
      (orch_ && activeRid_ != 0) ? orch_->ttsIdForRid(activeRid_) : 0;
  LOG_EVT_INFO("EVT_AI_STATE", "state=POST_SPEAK_BLANK rid=%lu tts_id=%lu",
               (unsigned long)activeRid_, (unsigned long)ttsId);
  updateOverlay_(nowMs);
}
void AiTalkController::enterCooldown_(uint32_t nowMs, bool error,
                                      const char *reason) {
  state_ = AiState::Cooldown;
  cooldownStartMs_ = nowMs;
  cooldownDurMs_ = (uint32_t)MC_AI_COOLDOWN_MS;
  if (error)
    cooldownDurMs_ += (uint32_t)MC_AI_COOLDOWN_ERROR_EXTRA_MS;
  overlay_.active_ = true;
  overlay_.state_ = state_;
  overlay_.hint_ = MC_AI_IDLE_HINT_TEXT;
  overlay_.line1_ = MC_AI_TEXT_COOLDOWN;
  overlay_.line2_ = "";
  const uint32_t ttsId =
      (orch_ && activeRid_ != 0) ? orch_->ttsIdForRid(activeRid_) : 0;
  LOG_EVT_INFO("EVT_AI_STATE",
               "state=COOLDOWN reason=%s err=%d dur=%lums rid=%lu tts_id=%lu",
               reason ? reason : "-", error ? 1 : 0,
               (unsigned long)cooldownDurMs_, (unsigned long)activeRid_,
               (unsigned long)ttsId);
}
void AiTalkController::updateOverlay_(uint32_t nowMs) {
  overlay_.active_ = true;
  overlay_.state_ = state_;
  if (!overlay_.hint_.length())
    overlay_.hint_ = MC_AI_IDLE_HINT_TEXT;
  auto ceilSec = [](uint32_t remainMs) -> int {
    return (int)((remainMs + 999) / 1000);
  };
  overlay_.line1_ = "";
  overlay_.line2_ = "";
  switch (state_) {
  case AiState::Listening: {
    overlay_.hint_ = MC_AI_LISTENING_HINT_TEXT;
    const uint32_t elapsed = nowMs - listenStartMs_;
    const uint32_t remain = (elapsed >= (uint32_t)MC_AI_LISTEN_TIMEOUT_MS)
                                ? 0
                                : ((uint32_t)MC_AI_LISTEN_TIMEOUT_MS - elapsed);
    overlay_.line1_ = "LISTEN " + String(ceilSec(remain)) + "s";
    overlay_.line2_ = "";
    return;
  }
  case AiState::Thinking: {
    overlay_.hint_ = MC_AI_THINKING_HINT_TEXT;
    if (!lastSttOk_) {
      overlay_.line1_ = "STT: ERR";
      String head = mcLogHead(lastUserText_, MC_AI_LOG_HEAD_BYTES_OVERLAY);
      if (!head.length())
        head = "...";
      overlay_.line2_ = head;
      return;
    }
    overlay_.line1_ = lastLlmOk_ ? "LLM: OK" : "LLM: ERR";
    String head = lastLlmOk_ ? lastLlmTextHead_ : lastLlmErr_;
    head = mcLogHead(head, MC_AI_LOG_HEAD_BYTES_OVERLAY);
    if (!head.length())
      head = "...";
    overlay_.line2_ = head;
    return;
  }
  case AiState::Speaking: {
    overlay_.hint_ = MC_AI_SPEAKING_HINT_TEXT;
    overlay_.line1_ = "SPEAK";
    overlay_.line2_ = "";
    return;
  }
  case AiState::PostSpeakBlank: {
    overlay_.hint_ = MC_AI_SPEAKING_HINT_TEXT;
    const uint32_t elapsed = nowMs - blankStartMs_;
    const uint32_t remain =
        (elapsed >= (uint32_t)MC_AI_POST_SPEAK_BLANK_MS)
            ? 0
            : ((uint32_t)MC_AI_POST_SPEAK_BLANK_MS - elapsed);
    overlay_.line1_ = "BLANK " + String(ceilSec(remain)) + "s";
    overlay_.line2_ = "";
    return;
  }
  case AiState::Cooldown: {
    overlay_.hint_ = MC_AI_IDLE_HINT_TEXT;
    const uint32_t elapsed = nowMs - cooldownStartMs_;
    const uint32_t remain =
        (elapsed >= cooldownDurMs_) ? 0 : (cooldownDurMs_ - elapsed);
    overlay_.line1_ = "COOL " + String(ceilSec(remain)) + "s";
    overlay_.line2_ = "";
    return;
  }
  default:
    overlay_.hint_ = MC_AI_IDLE_HINT_TEXT;
    overlay_.line1_ = "AI";
    overlay_.line2_ = "";
    return;
  }
}
