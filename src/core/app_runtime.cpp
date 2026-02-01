// Module implementation.
#include "core/app_runtime.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp32-hal-cpu.h>

#include "ai/ai_talk_controller.h"
#include "ai/azure_tts.h"
#include "ai/mining_task.h"
#include "behavior/stackchan_behavior.h"
#include "config/config.h"
#include "config/mc_config_store.h"
#include "config/runtime_features.h"
#include "core/orchestrator.h"
#include "core/tts_coordinator.h"
#include "ui/app_presenter.h"
#include "ui/ui_mining_core2.h"
#include "utils/logging.h"

static AppRuntimeContext g_ctx;

static unsigned long g_lastUiMs = 0;
static AppMode g_mode = Dash;
static bool     g_prevAiBusyForBehavior = false;
static uint32_t g_aiBusyStartMs = 0;
static uint32_t g_aiBusyDebugLastMs = 0;
static uint32_t g_aiTapConsumedCount = 0;
static int      g_aiTapFirstX = 0, g_aiTapFirstY = 0;
static int      g_aiTapLastX  = 0, g_aiTapLastY  = 0;
static uint32_t g_aiTapFirstMs = 0;
static AiTalkController::AiState g_aiTapLastState = AiTalkController::AiState::Idle;
static bool     g_attentionActive = false;
static uint32_t g_attentionUntilMs = 0;
static MiningYieldProfile g_savedYield = MiningYieldNormal();
static bool     g_savedYieldValid = false;
static uint32_t g_displaySleepTimeoutMs = (uint32_t)MC_DISPLAY_SLEEP_SECONDS * 1000UL;
static unsigned long g_lastInputMs = 0;
static bool g_displaySleeping = false;
static bool g_suppressTouchBeepOnce = false;
static bool g_timeNtpDone = false;
static const uint8_t  kDisplayActiveBrightness = 128;
static const uint32_t kDisplaySleepMessageMs  = 5000UL;
static bool     g_bubbleOnlyActive = false;
static uint32_t g_bubbleOnlyUntilMs = 0;
static uint32_t g_bubbleOnlyRid = 0;
static int      g_bubbleOnlyEvType = 0;
enum class BubbleSource : uint8_t { None = 0, Ai = 1, Behavior = 2, Info = 3, System = 4 };
static BubbleSource g_bubbleOnlySource = BubbleSource::None;
static bool     g_lastPopEmptyBusy = false;
static AppMode  g_lastPopEmptyMode = Dash;
static bool     g_lastPopEmptyAttn = false;

static const char* aiStateName_(AiTalkController::AiState s) {
  switch (s) {
    case AiTalkController::AiState::Idle:           return "IDLE";
    case AiTalkController::AiState::Listening:      return "LISTENING";
    case AiTalkController::AiState::Thinking:       return "THINKING";
    case AiTalkController::AiState::Speaking:       return "SPEAKING";
    case AiTalkController::AiState::PostSpeakBlank: return "POST";
    case AiTalkController::AiState::Cooldown:       return "COOLDOWN";
    default:                                        return "?";
  }
}

static long getDisplaySleepSecondsFromStore_(long fallbackSec) {
  // Pull display timeout from stored JSON config (best-effort).
  String j = mcConfigGetMaskedJson(); // contains display_sleep_s, attention_text, etc.
  StaticJsonDocument<1024> doc;
  DeserializationError e = deserializeJson(doc, j);
  if (e) return fallbackSec;
  JsonVariant v = doc["display_sleep_s"];
  if (v.is<long>()) {
    long sec = v.as<long>();
    if (sec > 0) return sec;
  } else if (v.is<int>()) {
    long sec = (long)v.as<int>();
    if (sec > 0) return sec;
  }
  return fallbackSec;
}

static uint32_t bubbleShow_Ms(const String& text) {
  const size_t len = text.length();
  uint32_t ms = 1500 + (uint32_t)(len * 120);
  const uint32_t maxMs = 8000;
  if (ms > maxMs) ms = maxMs;
  return ms;
}

static void bubbleClear_(const char* reason, bool forceUiClear) {
  if (!g_bubbleOnlyActive) return;
  const uint32_t oldRid = g_bubbleOnlyRid;
  const int oldType = g_bubbleOnlyEvType;
  g_bubbleOnlyActive = false;
  g_bubbleOnlyUntilMs = 0;
  if (g_mode == Stackchan && (forceUiClear || !g_attentionActive)) {
    UIMining::instance().setStackchanSpeech("");
  }
  LOG_EVT_INFO("EVT_PRESENT_BUBBLE_ONLY_CLEAR",
               "rid=%lu type=%d mode=%d attn=%d reason=%s",
               (unsigned long)oldRid, oldType,
               (int)g_mode, g_attentionActive ? 1 : 0,
               reason ? reason : "-");
  g_bubbleOnlyRid = 0;
  g_bubbleOnlyEvType = 0;
  g_bubbleOnlySource = BubbleSource::None;
}

static void bubbleShow_(const String& text,
                       uint32_t now,
                       uint32_t rid,
                       int evType,
                       int prio,
                       BubbleSource source) {
  if (!text.length()) return;
  if (g_attentionActive) return;
  UIMining::instance().setStackchanSpeech(text);
  g_bubbleOnlyActive = true;
  const uint32_t showMs = bubbleShow_Ms(text);
  g_bubbleOnlyUntilMs = now + showMs;
  g_bubbleOnlyRid = rid;
  g_bubbleOnlyEvType = evType;
  g_bubbleOnlySource = source;
  LOG_EVT_INFO("EVT_PRESENT_BUBBLE_ONLY_SHOW",
               "rid=%lu type=%d prio=%d len=%u mode=%d attn=%d show_ms=%lu text=%s",
               (unsigned long)rid,
               evType,
               prio,
               (unsigned)text.length(),
               (int)g_mode, g_attentionActive ? 1 : 0,
               (unsigned long)showMs,
               text.c_str());
}

static OrchPrio toOrchPrio_(ReactionPriority p) {
  switch (p) {
    case ReactionPriority::Low:    return OrchPrio::Low;
    case ReactionPriority::High:   return OrchPrio::High;
    case ReactionPriority::Normal:
    default:                       return OrchPrio::Normal;
  }
}

static bool wifiConnect_() {
  const auto& cfg = appConfig();
  enum WifiState {
    NotStarted,
    Connecting,
    Done
  };
  static WifiState   s_state   = NotStarted;
  static uint32_t    s_startMs = 0;
  static const uint32_t wifiConnectTimeoutMs = 20000UL;
  switch (s_state) {
    case NotStarted: {
      WiFi.mode(WIFI_STA);
      WiFi.begin(cfg.wifiSsid_, cfg.wifiPass_);
      s_startMs = millis();
      MC_LOGI("WIFI", "begin connect (ssid=%s)", cfg.wifiSsid_);
      s_state = Connecting;
      return false;
    }
    case Connecting: {
      wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
        MC_EVT("WIFI", "connected: %s", WiFi.localIP().toString().c_str());
        s_state = Done;
        return true;
      }
      if (millis() - s_startMs > wifiConnectTimeoutMs) {
        MC_LOGW("WIFI", "connect timeout (status=%d)", (int)st);
        s_state = Done;
        return true;
      }
      return false;
    }
    case Done:
    default:
      return true;
  }
}

static void setupTimeNtp_() {
  setenv("TZ", "JST-9", 1);
  tzset();
  configTime(9 * 3600, 0,
             "ntp.nict.jp",
             "time.google.com",
             "pool.ntp.org");
}

void appRuntimeInit(const AppRuntimeContext& ctx) {
  g_ctx = ctx;
  long sec = getDisplaySleepSecondsFromStore_((long)MC_DISPLAY_SLEEP_SECONDS);
  g_displaySleepTimeoutMs = (uint32_t)sec * 1000UL;
  mc_logf("[MAIN] display_sleep_s=%ld => timeout=%lu ms",
          sec, (unsigned long)g_displaySleepTimeoutMs);
  g_lastUiMs        = 0;
  g_lastInputMs     = millis();
  g_displaySleeping = false;
}

void appRuntimeTick(uint32_t now) {
  if (!g_ctx.ai_ || !g_ctx.orch_ || !g_ctx.behavior_) return;

  g_ctx.ai_->tick(now);
  {
    String aiBubbleText;
    if (g_ctx.ai_->consumeBubbleUpdate(&aiBubbleText)) {
      bubbleShow_(aiBubbleText, now, 0, -1, 0, BubbleSource::Ai);
    }
  }
  static uint32_t s_lastOverlayPushMs = 0;
  static uint8_t  s_lastAiState = 255;
  const uint8_t st = (uint8_t)g_ctx.ai_->state();
  if ((st != s_lastAiState) || (now - s_lastOverlayPushMs >= 200)) {
    UIMining::instance().setAiOverlay(g_ctx.ai_->getOverlay());
    s_lastOverlayPushMs = now;
    s_lastAiState = st;
  }

  // Orchestrator tick (timeout recovery)
  if (g_ctx.orch_->tick(now)) {
    LOG_EVT_INFO("EVT_ORCH_TIMEOUT_MAIN", "recover=1");
    if (g_ctx.tts_) {
      g_ctx.tts_->requestSessionReset();
    }
    ttsCoordinatorClearInflight();
  }

  ttsCoordinatorTick(now);

  static wl_status_t s_prevWifi = WL_IDLE_STATUS;
  const wl_status_t wifiNow = WiFi.status();
  if (s_prevWifi == WL_CONNECTED && wifiNow != WL_CONNECTED) {
    mc_logf("[WIFI] disconnected (status=%d) -> reset TTS session", (int)wifiNow);
    if (g_ctx.tts_) {
      g_ctx.tts_->requestSessionReset();
    }
  }
  s_prevWifi = wifiNow;

  bool anyInput = false;
  const bool btnA = M5.BtnA.wasPressed();
  const bool btnB = M5.BtnB.wasPressed();
  const bool btnC = M5.BtnC.wasPressed();
  if (btnA || btnB || btnC) {
    anyInput = true;
    g_suppressTouchBeepOnce = true;
  }
  static bool s_prevTouchPressed = false;
  bool touchPressed = false;
  bool touchDown    = false;
  int touchX = 0;
  int touchY = 0;
  auto& tp = M5.Touch;
  static uint32_t s_lastTouchPollMs = 0;
  static int  s_touchX = 0;
  static int  s_touchY = 0;
  static bool s_touchPressed = false;
  if (tp.isEnabled()) {
    if ((uint32_t)(now - s_lastTouchPollMs) >= 25) {
      s_lastTouchPollMs = now;
      auto det = tp.getDetail();
      s_touchPressed = det.isPressed();
      if (s_touchPressed) {
        s_touchX = det.x;
        s_touchY = det.y;
      }
    }
    touchPressed = s_touchPressed;
    touchX = s_touchX;
    touchY = s_touchY;
    touchDown = touchPressed && !s_prevTouchPressed;
    s_prevTouchPressed = touchPressed;
    if (touchPressed) anyInput = true;
  }
  // Cache touch state for UI
  {
    UIMining::TouchSnapshot ts;
    ts.enabled_ = tp.isEnabled();
    ts.pressed_ = touchPressed;
    ts.down_    = touchDown;
    ts.x_       = touchX;
    ts.y_       = touchY;
    UIMining::instance().setTouchSnapshot(ts);
  }

  if (g_displaySleeping) {
    if (anyInput) {
      MC_EVT("MAIN", "display wake (sleep off)");
      M5.Display.setBrightness(kDisplayActiveBrightness);
      g_displaySleeping = false;
      g_lastInputMs     = now;
    }
    return;
  }

  UIMining& ui = UIMining::instance();
  if (btnB) {
    const char* text = appConfig().helloText_;
    const RuntimeFeatures features = getRuntimeFeatures();
    if (features.ttsEnabled_ && g_ctx.tts_) {
      static uint32_t s_ttsFailLastLogMs = 0;
      static uint32_t s_ttsFailSuppressed = 0;
      if (!g_ctx.tts_->speakAsync(text, (uint32_t)0, nullptr)) {
        s_ttsFailSuppressed++;
        const uint32_t kFailLogIntervalMs = 3000;
        if (s_ttsFailLastLogMs == 0 || (now - s_ttsFailLastLogMs) >= kFailLogIntervalMs) {
          if (s_ttsFailSuppressed > 1) {
            mc_logf("[TTS] speakAsync failed (busy / wifi / config?) (suppressed x%lu)",
                    (unsigned long)(s_ttsFailSuppressed - 1));
          } else {
            mc_logf("[TTS] speakAsync failed (busy / wifi / config?)");
          }
          s_ttsFailSuppressed = 0;
          s_ttsFailLastLogMs = now;
        }
      } else {
        // success: reset suppression window
        s_ttsFailSuppressed = 0;
      }
    } else {
      bubbleShow_(String(text), now, 0, 0, 0, BubbleSource::System);
    }
  }

  if (anyInput) g_lastInputMs = now;
  if (btnA) {
    M5.Speaker.tone(1500, 50);
    if (g_mode == Dash) {
      g_mode = Stackchan;
      ui.onEnterStackchanMode();
    } else {
      g_mode = Dash;
      ui.onLeaveStackchanMode();
      if (g_attentionActive) {
        g_attentionActive = false;
        if (g_savedYieldValid) setMiningYieldProfile(g_savedYield);
        ui.triggerAttention(0);
      }
    }
    MC_EVT("MAIN", "BtnA pressed, mode=%d", (int)g_mode);
  }

  bool aiConsumedTap = false;
  if (g_mode == Stackchan && touchDown) {
    const AiTalkController::AiState stateBeforeTap = g_ctx.ai_->state();
    const int screenH = M5.Display.height();
    aiConsumedTap = g_ctx.ai_->onTap(touchX, touchY, screenH);
    if (aiConsumedTap) {
      if (g_aiTapConsumedCount == 0) {
        g_aiTapFirstX = touchX;
        g_aiTapFirstY = touchY;
        g_aiTapFirstMs = now;
      }
      g_aiTapConsumedCount++;
      g_aiTapLastX = touchX;
      g_aiTapLastY = touchY;
      const AiTalkController::AiState sNow = g_ctx.ai_->state();
      if (sNow != AiTalkController::AiState::Idle) {
        g_aiTapLastState = sNow;
      } else if (stateBeforeTap != AiTalkController::AiState::Idle) {
        g_aiTapLastState = stateBeforeTap;
      }
      MC_LOGT("AI", "tap consumed by AI (%d,%d)", touchX, touchY);
    }
  }
  if (g_mode == Stackchan && g_ctx.ai_->isBusy() && g_attentionActive) {
    MC_EVT("ATTN", "force exit (aiBusy=1)");
    g_attentionActive = false;
    g_attentionUntilMs = 0;
    if (g_savedYieldValid) setMiningYieldProfile(g_savedYield);
    else setMiningYieldProfile(MiningYieldNormal());
    ui.triggerAttention(0);
  }
  // --- Attention mode: tap in Stackchan screen ---
  if (!aiConsumedTap && (g_mode == Stackchan) && touchDown) {
    if (g_attentionActive) {
    } else if (g_ctx.ai_->isBusy()) {
      MC_LOGT("ATTN", "suppressed (aiBusy=1)");
    } else {
      const uint32_t dur = 3000;
      MC_EVT("ATTN", "enter dur=%ums", (unsigned)dur);
      g_savedYield = getMiningYieldProfile();
      g_savedYieldValid = true;
      g_attentionActive = true;
      g_attentionUntilMs = now + dur;
      ui.triggerAttention(dur, nullptr);
      M5.Speaker.tone(1800, 30);
      if (g_bubbleOnlyActive) {
        bubbleClear_("attention_start", true);
      }
    }
  }
  // Attention timeout
  if (g_attentionActive && (int32_t)(g_attentionUntilMs - now) <= 0) {
    g_attentionActive = false;
    MC_EVT("ATTN", "exit");
    if (g_savedYieldValid) setMiningYieldProfile(g_savedYield);
    else setMiningYieldProfile(MiningYieldNormal());
    ui.triggerAttention(0);
  }

  const bool wifiDone = wifiConnect_();
  if (wifiDone && !g_timeNtpDone && WiFi.status() == WL_CONNECTED) {
    setupTimeNtp_();
    g_timeNtpDone = true;
  }

  const bool ttsBusyNow = ttsCoordinatorIsBusy();
  if ((uint32_t)(now - g_lastUiMs) >= 100) {
    g_lastUiMs = now;
    MiningSummary summary;
    updateMiningSummary(summary);
    if (g_bubbleOnlyActive && (int32_t)(g_bubbleOnlyUntilMs - now) <= 0) {
      bubbleClear_("timeout", false);
    }
    UIMining::PanelData data;
    buildPanelData(summary, ui, data);
    g_ctx.behavior_->update(data, now);
    StackchanReaction reaction;
    bool gotReaction = false;
    const bool suppressBehaviorNow = (g_mode == Stackchan) && g_ctx.ai_->isBusy();
    if (suppressBehaviorNow && !g_prevAiBusyForBehavior) {
      g_aiBusyStartMs = now;
      MC_EVT("AI", "busy enter state=%s reason=ai_busy", aiStateName_(g_ctx.ai_->state()));
    } else if (!suppressBehaviorNow && g_prevAiBusyForBehavior) {
      const float durS = (now - g_aiBusyStartMs) / 1000.0f;
      MC_EVT("AI", "busy exit state=%s dur=%.1fs reason=ai_idle",
             aiStateName_(g_ctx.ai_->state()), durS);
      if (g_aiTapConsumedCount > 0) {
        const float spanS = (now - g_aiTapFirstMs) / 1000.0f;
        MC_LOGD("AI", "tap consumed x%lu last=(%d,%d) first=(%d,%d) span=%.1fs during=%s",
                (unsigned long)g_aiTapConsumedCount,
                g_aiTapLastX, g_aiTapLastY,
                g_aiTapFirstX, g_aiTapFirstY,
                spanS, aiStateName_(g_aiTapLastState));
        g_aiTapConsumedCount = 0;
      }
    }
    g_prevAiBusyForBehavior = suppressBehaviorNow;
    if (suppressBehaviorNow) {
      gotReaction = false;
      if ((now - g_aiBusyDebugLastMs) >= 1000) {
        MC_LOGT("AI", "suppress Behavior while busy (state=%s)", aiStateName_(g_ctx.ai_->state()));
        g_aiBusyDebugLastMs = now;
      }
    } else {
      gotReaction = g_ctx.behavior_->popReaction(&reaction);
    }
    if (gotReaction) {
      LOG_EVT_INFO("EVT_PRESENT_POP",
                   "rid=%lu type=%d prio=%d speak=%d busy=%d mode=%d attn=%d",
                   (unsigned long)reaction.rid_, (int)reaction.evType_,
                   (int)reaction.priority_, reaction.speak_ ? 1 : 0,
                   ttsBusyNow ? 1 : 0, (int)g_mode, g_attentionActive ? 1 : 0);
      const bool suppressedByAttention = (g_mode == Stackchan) && g_attentionActive;
      const bool isIdleTick = (reaction.evType_ == StackchanEventType::IdleTick);
      if (g_mode == Stackchan && !isIdleTick) {
        const bool isBubbleInfo =
          (reaction.evType_ == StackchanEventType::InfoPool) ||
          (reaction.evType_ == StackchanEventType::InfoPing) ||
          (reaction.evType_ == StackchanEventType::InfoHashrate) ||
          (reaction.evType_ == StackchanEventType::InfoShares);
        if (!reaction.speak_ && !isBubbleInfo) {
          static bool s_hasLastExp = false;
          static m5avatar::Expression s_lastExp = m5avatar::Expression::Neutral;
          if (!s_hasLastExp || reaction.expression_ != s_lastExp) {
            ui.setStackchanExpression(reaction.expression_);
            s_lastExp = reaction.expression_;
            s_hasLastExp = true;
          }
        }
      }
      // ---- bubble-only present (speak=0) ----
      if (g_mode == Stackchan) {
        if (reaction.speak_ && g_bubbleOnlyActive) {
          bubbleClear_("tts_event", false);
        }
        if (!reaction.speak_ &&
            !isIdleTick &&
            reaction.speechText_.length() &&
            !suppressedByAttention) {
          const bool isBubbleInfo =
            (reaction.evType_ == StackchanEventType::InfoPool) ||
            (reaction.evType_ == StackchanEventType::InfoPing) ||
            (reaction.evType_ == StackchanEventType::InfoHashrate) ||
            (reaction.evType_ == StackchanEventType::InfoShares) ||
            (reaction.evType_ == StackchanEventType::InfoMiningOff);
          const BubbleSource bubbleSource = isBubbleInfo ? BubbleSource::Info : BubbleSource::Behavior;
          bubbleShow_(reaction.speechText_,
                     now,
                     reaction.rid_,
                     (int)reaction.evType_,
                     (int)reaction.priority_,
                     bubbleSource);
        }
      }
      // TTS
      const RuntimeFeatures features = getRuntimeFeatures();
      if (reaction.speak_ && reaction.speechText_.length() && features.ttsEnabled_) {
        auto cmd = g_ctx.orch_->makeSpeakStartCmd(reaction.rid_, reaction.speechText_,
                                                  toOrchPrio_(reaction.priority_),
                                                  Orchestrator::OrchKind::BehaviorSpeak);
        if (cmd.valid_) {
          ttsCoordinatorMaybeSpeak(cmd, (int)reaction.evType_);
        }
      }
    } else {
      // low-rate heartbeat only
      static uint32_t s_lastHbMs = 0;
      static uint32_t s_emptyStreak = 0;
      s_emptyStreak++;
      const uint32_t PRESENTER_HEARTBEAT_MS = 10000;
      const bool stateChanged =
        (ttsBusyNow != g_lastPopEmptyBusy) ||
        (g_mode != g_lastPopEmptyMode) ||
        (g_attentionActive != g_lastPopEmptyAttn);
      if (stateChanged || (now - s_lastHbMs) >= PRESENTER_HEARTBEAT_MS) {
        LOG_EVT_HEARTBEAT("EVT_PRESENT_HEARTBEAT",
                      "busy=%d mode=%d attn=%d empty_streak=%lu",
                      ttsBusyNow ? 1 : 0, (int)g_mode, g_attentionActive ? 1 : 0,
                      (unsigned long)s_emptyStreak);
        s_lastHbMs = now;
        s_emptyStreak = 0;
        g_lastPopEmptyBusy = ttsBusyNow;
        g_lastPopEmptyMode = g_mode;
        g_lastPopEmptyAttn = g_attentionActive;
      }
    }
    String ticker = buildTicker(summary);
    if (g_mode == Stackchan) {
      ui.drawStackchanScreen(data);
    } else {
      ui.drawAll(data, ticker);
    }
    g_suppressTouchBeepOnce = false;
  }

  if (!g_displaySleeping && (uint32_t)(now - g_lastInputMs) >= g_displaySleepTimeoutMs) {
    MC_EVT("MAIN", "display sleep (screen off)");
    UIMining::instance().drawSleepMessage();
    delay(kDisplaySleepMessageMs);
    M5.Display.setBrightness(0);
    g_displaySleeping = true;
  }
}

uint32_t* appRuntimeDisplaySleepTimeoutMsPtr() {
  return &g_displaySleepTimeoutMs;
}

bool* appRuntimeAttentionActivePtr() {
  return &g_attentionActive;
}

AppMode* appRuntimeModePtr() {
  return &g_mode;
}

BubbleClearFn appRuntimeBubbleClearFn() {
  return bubbleClear_;
}
