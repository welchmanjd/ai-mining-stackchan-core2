// Module implementation.
#include "core/tts_coordinator.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "ai/ai_talk_controller.h"
#include "ai/azure_tts.h"
#include "ai/mining_task.h"
#include "behavior/stackchan_behavior.h"
#include "config/runtime_features.h"
#include "core/app_types.h"
#include "core/orchestrator.h"
#include "ui/ui_mining_core2.h"
#include "utils/logging.h"

static TtsCoordinatorContext g_ctx;

static uint32_t g_ttsInflightId = 0;
static uint32_t g_ttsInflightRid = 0;
static String   g_ttsInflightSpeechText;
static uint32_t g_ttsInflightSpeechId = 0;
static bool     g_prevAudioPlaying = false;
static bool     g_pausedByTts = false;

static void applyMiningPolicyForTts_(bool ttsBusy, bool aiBusy) {
  (void)ttsBusy;
  const bool speaking = M5.Speaker.isPlaying();
  const bool wantPause = speaking || aiBusy;
  if (wantPause != g_pausedByTts) {
    MC_EVT("TTS", "mining pause: %d -> %d (speaking=%d aiBusy=%d)",
           (int)g_pausedByTts, (int)wantPause, (int)speaking, (int)aiBusy);
    setMiningPaused(wantPause);
    g_pausedByTts = wantPause;
  }
}

void ttsCoordinatorInit(const TtsCoordinatorContext& ctx) {
  g_ctx = ctx;
}

bool ttsCoordinatorIsBusy() {
  if (!g_ctx.tts_) return false;
  return g_ctx.tts_->isBusy();
}

static void clearInflight_() {
  g_ttsInflightId = 0;
  g_ttsInflightRid = 0;
  g_ttsInflightSpeechText = "";
  g_ttsInflightSpeechId = 0;
}

static bool contextReady_() {
  if (!g_ctx.tts_ || !g_ctx.orch_) {
    MC_LOGW("TTS", "tts coordinator context not ready");
    return false;
  }
  return true;
}

void ttsCoordinatorClearInflight() {
  clearInflight_();
  UIMining::instance().setStackchanSpeech("");
}

static int modeValue_() {
  if (!g_ctx.mode_) return -1;
  return *g_ctx.mode_;
}

static int attentionValue_() {
  if (!g_ctx.attentionActive_) return 0;
  return *g_ctx.attentionActive_ ? 1 : 0;
}


static void onAbortTts_() {
  if (!g_ctx.ai_ || !g_ctx.orch_ || !g_ctx.tts_) return;
  uint32_t abortId = 0;
  const char* reason = nullptr;
  if (!g_ctx.ai_->consumeAbortTts(&abortId, &reason)) return;
  const char* r = (reason && reason[0]) ? reason : "abort";
  mc_logf("[MAIN] abort tts id=%lu reason=%s -> cancel+clear inflight+clear orch",
          (unsigned long)abortId, r);
  g_ctx.tts_->cancel(abortId, r);
  clearInflight_();
  UIMining::instance().setStackchanSpeech("");
  g_ctx.orch_->cancelSpeak(abortId, r, Orchestrator::CancelSource::Main);
}

static void handleTtsDone_(uint32_t now, bool ttsBusyNow) {
  (void)now;
  if (!g_ctx.tts_ || !g_ctx.orch_) return;
  uint32_t gotId = 0;
  bool ttsOk = false;
  char ttsReason[24] = {0};
  if (!g_ctx.tts_->consumeDone(&gotId, &ttsOk, ttsReason, sizeof(ttsReason))) return;
  const char* r = (ttsReason[0] ? ttsReason : "-");
  LOG_EVT_INFO("EVT_TTS_DONE_RX_MAIN",
               "got=%lu inflight=%lu inflight_rid=%lu tts_ok=%d reason=%s",
               (unsigned long)gotId,
               (unsigned long)g_ttsInflightId,
               (unsigned long)g_ttsInflightRid,
               ttsOk ? 1 : 0,
               r);
  bool desync = false;
  uint32_t doneRid = 0;
  Orchestrator::OrchKind doneKind = Orchestrator::OrchKind::None;
  const bool orchOk = g_ctx.orch_->onTtsDone(gotId, &doneRid, &doneKind, &desync);
  const uint32_t ridForLog = (g_ttsInflightId == gotId) ? g_ttsInflightRid : 0;
  LOG_EVT_INFO("EVT_TTS_DONE",
               "rid=%lu tts_id=%lu tts_ok=%d reason=%s orch_ok=%d",
               (unsigned long)ridForLog,
               (unsigned long)gotId,
               ttsOk ? 1 : 0,
               r,
               orchOk ? 1 : 0);
  if (orchOk) {
    if (doneKind == Orchestrator::OrchKind::AiSpeak && doneRid != 0 && g_ctx.ai_) {
      g_ctx.ai_->onSpeakDone(doneRid, now);
    }
    UIMining::instance().setStackchanSpeech("");
    LOG_EVT_INFO("EVT_PRESENT_SPEECH_CLEAR", "tts_id=%lu", (unsigned long)gotId);
    clearInflight_();
  } else {
    LOG_EVT_INFO("EVT_TTS_DONE_IGNORED",
                 "got_tts_id=%lu expected=%lu",
                 (unsigned long)gotId,
                 (unsigned long)g_ttsInflightId);
    if (desync) {
      LOG_EVT_INFO("EVT_ORCH_SPEAK_DESYNC",
                   "got=%lu expect=%lu",
                   (unsigned long)gotId,
                   (unsigned long)g_ttsInflightId);
      g_ctx.tts_->requestSessionReset();
      UIMining::instance().setStackchanSpeech("");
      clearInflight_();
    }
  }
}

static void handlePendingSpeak_(bool ttsBusyNow) {
  if (!g_ctx.tts_ || !g_ctx.orch_) return;
  if (ttsBusyNow || g_ttsInflightId != 0) return;
  if (!g_ctx.orch_->hasPendingSpeak()) return;
  auto pending = g_ctx.orch_->popNextPending();
  if (!pending.valid_) return;
  const bool ok = g_ctx.tts_->speakAsync(pending.text_, pending.ttsId_);
  if (ok) {
    g_ttsInflightId  = pending.ttsId_;
    g_ttsInflightRid = pending.rid_;
    g_ttsInflightSpeechText = pending.text_;
    g_ttsInflightSpeechId = pending.ttsId_;
    g_ctx.orch_->setExpectedSpeak(pending.ttsId_, pending.rid_, pending.kind_);
    LOG_EVT_INFO("EVT_PRESENT_TTS_START",
                 "rid=%lu tts_id=%lu type=pending prio=%d busy=%d mode=%d attn=%d",
                 (unsigned long)pending.rid_, (unsigned long)pending.ttsId_,
                 (int)pending.prio_, 0, modeValue_(), attentionValue_());
  } else {
    LOG_EVT_INFO("EVT_PRESENT_TTS_PENDING_FAIL",
                 "rid=%lu tts_id=%lu prio=%d mode=%d attn=%d",
                 (unsigned long)pending.rid_, (unsigned long)pending.ttsId_,
                 (int)pending.prio_, modeValue_(), attentionValue_());
  }
}

static void updateAudioStart_() {
  if (!g_ctx.tts_ || !g_ctx.orch_) return;
  const bool audioPlayingNow = M5.Speaker.isPlaying();
  if (!g_prevAudioPlaying && audioPlayingNow && g_ttsInflightId != 0) {
    g_ctx.orch_->onAudioStart(g_ttsInflightId);
    if (g_ctx.bubbleClearFn_) {
      g_ctx.bubbleClearFn_("tts_start", false);
    }
    if (g_ttsInflightSpeechId != 0 &&
        g_ttsInflightSpeechId == g_ttsInflightId &&
        g_ttsInflightSpeechText.length() > 0) {
      UIMining::instance().setStackchanSpeech(g_ttsInflightSpeechText);
      LOG_EVT_INFO("EVT_PRESENT_SPEECH_SYNC",
                   "tts_id=%lu len=%u",
                   (unsigned long)g_ttsInflightId,
                   (unsigned)g_ttsInflightSpeechText.length());
    }
  }
  g_prevAudioPlaying = audioPlayingNow;
}

static void updateMiningYield_(bool ttsBusyNow) {
  static bool s_ttsYieldApplied = false;
  static MiningYieldProfile s_ttsSavedYield = MiningYieldNormal();
  static bool s_ttsSavedYieldValid = false;
  const bool attention = g_ctx.attentionActive_ && *g_ctx.attentionActive_;
  if (ttsBusyNow) {
    if (!s_ttsYieldApplied && !attention) {
      s_ttsSavedYield = getMiningYieldProfile();
      s_ttsSavedYieldValid = true;
      setMiningYieldProfile(MiningYieldStrong());
      s_ttsYieldApplied = true;
      MC_EVT("TTS", "mining yield: Strong");
    }
  } else {
    if (s_ttsYieldApplied && !attention) {
      if (s_ttsSavedYieldValid) setMiningYieldProfile(s_ttsSavedYield);
      else setMiningYieldProfile(MiningYieldNormal());
      s_ttsYieldApplied = false;
      MC_EVT("TTS", "mining yield: restore");
    }
  }
}

void ttsCoordinatorTick(uint32_t now) {
  if (!contextReady_()) return;
  onAbortTts_();
  g_ctx.tts_->poll();
  updateAudioStart_();
  const bool ttsBusyNow = g_ctx.tts_->isBusy();
  handleTtsDone_(now, ttsBusyNow);
  if (g_ctx.behavior_) {
    g_ctx.behavior_->setTtsSpeaking(ttsBusyNow);
  }
  if (g_ctx.ai_) {
    applyMiningPolicyForTts_(ttsBusyNow, g_ctx.ai_->isBusy());
  } else {
    applyMiningPolicyForTts_(ttsBusyNow, false);
  }
  handlePendingSpeak_(ttsBusyNow);
  updateMiningYield_(ttsBusyNow);
}

void ttsCoordinatorMaybeSpeak(const OrchestratorApi::SpeakStartCmd& cmd, int evType) {
  if (!contextReady_()) return;
  const RuntimeFeatures features = getRuntimeFeatures();
  if (!features.ttsEnabled_) return;
  const bool ttsBusyNow = g_ctx.tts_->isBusy();
  const bool canSpeakNow = (!ttsBusyNow) && (g_ttsInflightId == 0);
  if (canSpeakNow) {
    const bool speakOk = g_ctx.tts_->speakAsync(cmd.text_, cmd.ttsId_);
    if (speakOk) {
      g_ttsInflightId  = cmd.ttsId_;
      g_ttsInflightRid = cmd.rid_;
      g_ttsInflightSpeechText = cmd.text_;
      g_ttsInflightSpeechId = cmd.ttsId_;
      g_ctx.orch_->setExpectedSpeak(cmd.ttsId_, cmd.rid_, cmd.kind_);
      LOG_EVT_INFO("EVT_PRESENT_TTS_START",
                   "rid=%lu tts_id=%lu type=%d prio=%d busy=%d mode=%d attn=%d",
                   (unsigned long)cmd.rid_, (unsigned long)cmd.ttsId_,
                   evType, (int)cmd.prio_,
                   ttsBusyNow ? 1 : 0, modeValue_(), attentionValue_());
    }
  } else {
    g_ctx.orch_->enqueueSpeakPending(cmd);
    LOG_EVT_INFO("EVT_PRESENT_TTS_DEFER_BUSY",
                 "rid=%lu tts_id=%lu prio=%d busy=%d mode=%d attn=%d",
                 (unsigned long)cmd.rid_, (unsigned long)cmd.ttsId_,
                 (int)cmd.prio_, ttsBusyNow ? 1 : 0,
                 modeValue_(), attentionValue_());
  }
}
