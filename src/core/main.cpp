// src/main.cpp
// Module implementation.
// ===== Mining-chan Core2 ? main entry (UI + orchestrator) =====
// Board   : M5Stack Core2
// Libs    : M5Unified, ArduinoJson, WiFi, WiFiClientSecure, HTTPClient, m5stack-avatar
#include <M5Unified.h>
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <stdarg.h>
#include <esp32-hal-cpu.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include "ui/ui_mining_core2.h"
#include "ui/app_presenter.h"
#include "config/config.h"
#include "ai/mining_task.h"
#include "core/logging.h"
#include "config/mc_config_store.h"
#include "ai/azure_tts.h"
#include "behavior/stackchan_behavior.h"
#include "core/orchestrator.h"
#include "ai/ai_talk_controller.h"
#include "core/runtime_features.h"
// Azure TTS
static AzureTts g_tts;
static unsigned long g_lastUiMs = 0;
enum AppMode : uint8_t {
  Dash = 0,
  Stackchan = 1,
};
static AppMode g_mode = Dash;
// Global mode switcher: dashboard vs. Stackchan avatar mode.
// ---- AI busy / tap log aggregation (Step2) ----
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
static bool     g_prevAiBusyForBehavior = false;
static uint32_t g_aiBusyStartMs = 0;
static uint32_t g_aiBusyDebugLastMs = 0;
static uint32_t g_aiTapConsumedCount = 0;
static int      g_aiTapFirstX = 0, g_aiTapFirstY = 0;
static int      g_aiTapLastX  = 0, g_aiTapLastY  = 0;
static uint32_t g_aiTapFirstMs = 0;
static AiTalkController::AiState g_aiTapLastState = AiTalkController::AiState::Idle;
// "Attention" ("WHAT?") mode: short-lived focus state triggered by tap in Stackchan screen.
static bool     g_attentionActive = false;
// Tap-triggered short focus state; resets automatically on timeout.
static uint32_t g_attentionUntilMs = 0;
static MiningYieldProfile g_savedYield = MiningYieldNormal();
static bool     g_savedYieldValid = false;
// ===== Web setup serial commands (simple line protocol) =====
static char   g_setupLine[512];
static size_t g_setupLineLen = 0;
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
// display sleep timeout [ms] (runtime configurable via SET display_sleep_s)
static uint32_t g_displaySleepTimeoutMs = (uint32_t)MC_DISPLAY_SLEEP_SECONDS * 1000UL;
// === src/main.cpp : replace whole function ===
static void handleSetupLine_(const char* line) {
  if (!line || !*line) return;
  String cmd(line);
  cmd.trim();
  // Simple serial command handler for web setup tooling.
  if (cmd.equalsIgnoreCase("HELLO")) {
    Serial.println("@OK HELLO");
    return;
  }
  if (cmd.equalsIgnoreCase("PING")) {
    Serial.println("@OK PONG");
    return;
  }
  if (cmd.equalsIgnoreCase("HELP")) {
    Serial.println("@OK CMDS=HELLO,PING,GET INFO,HELP");
    return;
  }
  if (cmd.equalsIgnoreCase("GET INFO")) {
    const auto& cfg = appConfig();
    char buf[200];
    snprintf(buf, sizeof(buf),
             "@INFO {\"app\":\"%s\",\"ver\":\"%s\",\"baud\":%d}",
             cfg.appName_, cfg.appVersion_, 115200);
    Serial.println(buf);
    return;
  }
  if (cmd.equalsIgnoreCase("GET CFG")) {
    String j = mcConfigGetMaskedJson();
    Serial.print("@CFG ");
    Serial.println(j);
    return;
  }
  if (cmd.equalsIgnoreCase("AZTEST")) {
    const RuntimeFeatures features = getRuntimeFeatures();
    if (!features.ttsEnabled_) {
      Serial.println("@AZTEST NG missing_required");
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("@AZTEST NG wifi_disconnected");
      return;
    }
    // Reload runtime Azure config so tests after SET/SAVE work without reboot.
    g_tts.begin(mcCfgSpkVolume());
    bool ok = g_tts.testCredentials();
    if (ok) Serial.println("@AZTEST OK");
    else    Serial.println("@AZTEST NG fetch_failed");
    return;
  }
  if (cmd.startsWith("SET ")) {
    // SET <KEY> <VALUE>
    String rest = cmd.substring(4);
    int sp = rest.indexOf(' ');
    if (sp < 0) {
      Serial.println("@ERR bad_set_format");
      return;
    }
    String key = rest.substring(0, sp);
    String val = rest.substring(sp + 1);
    key.trim(); val.trim();
    String err;
    if (mcConfigSetKV(key, val, err)) {
      // ---- apply runtime effects immediately (optional but nice) ----
      if (key.equalsIgnoreCase("display_sleep_s")) {
        long sec = val.toInt();
        if (sec > 0) {
          g_displaySleepTimeoutMs = (uint32_t)sec * 1000UL;
        } else {
          g_displaySleepTimeoutMs = (uint32_t)MC_DISPLAY_SLEEP_SECONDS * 1000UL;
        }
        MC_LOGI("MAIN", "display_sleep_s set: %ld sec => %lu ms",
                sec, (unsigned long)g_displaySleepTimeoutMs);
      }
      if (key.equalsIgnoreCase("attention_text")) {
        UIMining::instance().setAttentionDefaultText(val.c_str());
        MC_LOGI("MAIN", "attention_text set: %s", val.c_str());
      }
      if (key.equalsIgnoreCase("spk_volume")) {
        int v = val.toInt();
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        M5.Speaker.setVolume((uint8_t)v);
        MC_LOGI("MAIN", "spk_volume set: %d", v);
      }
      if (key.equalsIgnoreCase("cpu_mhz")) {
        int mhz = val.toInt();
        setCpuFrequencyMhz(mhz);
        MC_LOGI("MAIN", "cpu_mhz set: %d (now=%d)", mhz, getCpuFrequencyMhz());
      }
      Serial.print("@OK SET ");
      Serial.println(key);
    } else {
      Serial.print("@ERR SET ");
      Serial.print(key);
      Serial.print(" ");
      Serial.println(err);
    }
    return;
  }
  if (cmd.equalsIgnoreCase("SAVE")) {
    String err;
    if (mcConfigSave(err)) Serial.println("@OK SAVE");
    else { Serial.print("@ERR SAVE "); Serial.println(err); }
    return;
  }
  if (cmd.equalsIgnoreCase("REBOOT")) {
    Serial.println("@OK REBOOT");
    Serial.flush();
    delay(100);
    ESP.restart();
    return;
  }
  Serial.print("@ERR unknown_cmd: ");
  Serial.println(line);
}
static void pollSetupSerial_() {
  // Line-based parser with basic length guarding.
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      g_setupLine[g_setupLineLen] = '\0';
      handleSetupLine_(g_setupLine);
      g_setupLineLen = 0;
      continue;
    }
    if (g_setupLineLen + 1 >= sizeof(g_setupLine)) {
      g_setupLineLen = 0;
      Serial.println("@ERR line_too_long");
      continue;
    }
    g_setupLine[g_setupLineLen++] = c;
  }
}
static StackchanBehavior g_behavior;
static Orchestrator g_orch;
static AiTalkController g_ai;
static uint32_t g_ttsInflightId = 0;
static uint32_t g_ttsInflightRid = 0;
static String   g_ttsInflightSpeechText;
static uint32_t g_ttsInflightSpeechId = 0;
static bool     g_ttsPrevBusy = false;
static bool     g_prevAudioPlaying = false;
static uint32_t g_lastPopEmptyLogMs = 0;
static bool     g_lastPopEmptyBusy = false;
static AppMode  g_lastPopEmptyMode = Dash;
static bool     g_lastPopEmptyAttn = false;
// ---- Stackchan bubble-only (no TTS) ----
static bool     g_bubbleOnlyActive = false;
// Bubble-only mode shows text without triggering TTS playback.
static uint32_t g_bubbleOnlyUntilMs = 0;
static uint32_t g_bubbleOnlyRid = 0;
static int      g_bubbleOnlyEvType = 0;
enum class BubbleSource : uint8_t { None = 0, Ai = 1, Behavior = 2, Info = 3, System = 4 };
static BubbleSource g_bubbleOnlySource = BubbleSource::None;
static uint32_t bubbleShow_Ms(const String& text) {
  const size_t len = text.length();
  uint32_t ms = 1500 + (uint32_t)(len * 120);
  const uint32_t maxMs = 8000;
  if (ms > maxMs) ms = maxMs;
  return ms;
}
static void bubbleClear_(const char* reason, bool forceUiClear = false) {
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
static OrchPrio toOrchPrio_(ReactionPriority p);
static unsigned long g_lastInputMs = 0;
static bool g_displaySleeping = false;
static bool g_suppressTouchBeepOnce = false;
static bool g_timeNtpDone = false;
static const uint8_t  kDisplayActiveBrightness = 128;
static const uint32_t kDisplaySleepMessageMs  = 5000UL;
static int g_baseThreads = -1;
static int g_appliedThreads = -999;
static uint32_t g_zeroSince = 0;
static bool g_pausedByTts = false;
// === src/main.cpp : replace whole function ===
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
// ---------------- WiFi / Time ----------------
// === src/main.cpp : replace whole function ===
static bool wifiConnect_() {
  const auto& cfg = appConfig();
  enum WifiState {
    WIFI_NOT_STARTED,
    WIFI_CONNECTING,
    WIFI_DONE
  };
  static WifiState   state   = WIFI_NOT_STARTED;
  static uint32_t    t_start = 0;
  static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
  switch (state) {
    case WIFI_NOT_STARTED: {
      WiFi.mode(WIFI_STA);
      WiFi.begin(cfg.wifiSsid_, cfg.wifiPass_);
      t_start = millis();
      MC_LOGI("WIFI", "begin connect (ssid=%s)", cfg.wifiSsid_);
      state = WIFI_CONNECTING;
      return false;
    }
    case WIFI_CONNECTING: {
      wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
        MC_EVT("WIFI", "connected: %s", WiFi.localIP().toString().c_str());
        state = WIFI_DONE;
        return true;
      }
      if (millis() - t_start > WIFI_CONNECT_TIMEOUT_MS) {
        MC_LOGW("WIFI", "connect timeout (status=%d)", (int)st);
        state = WIFI_DONE;
        return true;
      }
      return false;
    }
    case WIFI_DONE:
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
void setup() {
  Serial.begin(115200);
  mcConfigBegin();
  // Step5: suppress "ssl_client UNKNOWN ERROR CODE" wallpaper logs.
  // That line is emitted as ESP_LOG_ERROR even when STT succeeds (http=200),
  // so keeping ERROR will not silence it.
  // Normal ops: mute it completely. Enable EVT_DEBUG_ENABLED when you want to see it.
#if EVT_DEBUG_ENABLED
  esp_log_level_set("ssl_client", ESP_LOG_ERROR);
#else
  esp_log_level_set("ssl_client", ESP_LOG_NONE);
#endif
  delay(50);
  mc_logf("[MAIN] setup() start");
  const uint32_t req_mhz = mcCfgCpuMhz();
  setCpuFrequencyMhz((int)req_mhz);
  mc_logf("[MAIN] cpu_mhz=%d (req=%lu)", getCpuFrequencyMhz(), (unsigned long)req_mhz);
  auto cfg_m5 = M5.config();
  cfg_m5.output_power  = true;
  cfg_m5.clear_display = true;
  cfg_m5.internal_imu = false;
  cfg_m5.internal_mic = true;
  cfg_m5.internal_spk = true;
  cfg_m5.internal_rtc = true;
  mc_logf("[MAIN] call M5.begin()");
  M5.begin(cfg_m5);
  mc_logf("[MAIN] M5.begin() done");
  M5.Speaker.setVolume(mcCfgSpkVolume());
  mc_logf("[MAIN] spk_volume=%u", (unsigned)mcCfgSpkVolume());
  const auto& cfg = appConfig();
  // Apply display sleep seconds from mc_config_store (via @CFG JSON)
  {
    long sec = getDisplaySleepSecondsFromStore_((long)MC_DISPLAY_SLEEP_SECONDS);
    g_displaySleepTimeoutMs = (uint32_t)sec * 1000UL;
    mc_logf("[MAIN] display_sleep_s=%ld => timeout=%lu ms",
            sec, (unsigned long)g_displaySleepTimeoutMs);
  }
  g_tts.begin();
  g_orch.init();
  g_ai.begin(&g_orch);
  M5.Display.setBrightness(kDisplayActiveBrightness);
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);
  UIMining::instance().begin(cfg.appName_, cfg.appVersion_);
  UIMining::instance().setAttentionDefaultText(mcCfgAttentionText());
  UIMining::instance().setStackchanSpeechTiming(
    2200, 1200,
    900,  1400
  );
  g_lastUiMs        = 0;
  g_lastInputMs     = millis();
  g_displaySleeping = false;
  mc_logf("%s %s booting...", cfg.appName_, cfg.appVersion_);
  startMiner();
}
void loop() {
  M5.update();
  // Web setup serial commands
  pollSetupSerial_();
  const uint32_t now = (uint32_t)millis();
  g_ai.tick(now);
  {
    String aiBubbleText;
    if (g_ai.consumeBubbleUpdate(&aiBubbleText)) {
      bubbleShow_(aiBubbleText, now, 0, -1, 0, BubbleSource::Ai);
    }
  }
  {
    uint32_t abortId = 0;
    const char* reason = nullptr;
    if (g_ai.consumeAbortTts(&abortId, &reason)) {
      const char* r = (reason && reason[0]) ? reason : "abort";
      mc_logf("[MAIN] abort tts id=%lu reason=%s -> cancel+clear inflight+clear orch",
              (unsigned long)abortId, r);
      g_tts.cancel(abortId, r);
      g_ttsInflightId = 0;
      g_ttsInflightRid = 0;
      g_ttsInflightSpeechId = 0;
      g_ttsInflightSpeechText = "";
      UIMining::instance().setStackchanSpeech("");
      g_orch.cancelSpeak(abortId, r, Orchestrator::CancelSource::Main);
    }
  }
  static uint32_t s_lastOverlayPushMs = 0;
  static uint8_t  s_lastAiState = 255;
  const uint8_t st = (uint8_t)g_ai.state();
  if ((st != s_lastAiState) || (now - s_lastOverlayPushMs >= 200)) {
    UIMining::instance().setAiOverlay(g_ai.getOverlay());
    s_lastOverlayPushMs = now;
    s_lastAiState = st;
  }
  const RuntimeFeatures features = getRuntimeFeatures();
  // Orchestrator tick (timeout recovery)
  if (g_orch.tick(now)) {
    LOG_EVT_INFO("EVT_ORCH_TIMEOUT_MAIN", "recover=1");
    g_tts.requestSessionReset();
    g_ttsInflightId = 0;
    g_ttsInflightRid = 0;
    g_ttsInflightSpeechId = 0;
    g_ttsInflightSpeechText = "";
    UIMining::instance().setStackchanSpeech("");
  }
  // TTS state update + completion
  g_tts.poll();
  const bool audioPlayingNow = M5.Speaker.isPlaying();
  if (!g_prevAudioPlaying && audioPlayingNow && g_ttsInflightId != 0) {
    g_orch.onAudioStart(g_ttsInflightId);
    if (g_bubbleOnlyActive) {
      bubbleClear_("tts_start");
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
  const bool ttsBusyNow = g_tts.isBusy();
  uint32_t gotId = 0;
  bool ttsOk = false;
  char ttsReason[24] = {0};
  if (g_tts.consumeDone(&gotId, &ttsOk, ttsReason, sizeof(ttsReason))) {
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
    const bool orchOk = g_orch.onTtsDone(gotId, &doneRid, &doneKind, &desync);
    const uint32_t ridForLog = (g_ttsInflightId == gotId) ? g_ttsInflightRid : 0;
    LOG_EVT_INFO("EVT_TTS_DONE",
                 "rid=%lu tts_id=%lu tts_ok=%d reason=%s orch_ok=%d",
                 (unsigned long)ridForLog,
                 (unsigned long)gotId,
                 ttsOk ? 1 : 0,
                 r,
                 orchOk ? 1 : 0);
    if (orchOk) {
      if (doneKind == Orchestrator::OrchKind::AiSpeak && doneRid != 0) {
        g_ai.onSpeakDone(doneRid, now);
      }
      UIMining::instance().setStackchanSpeech("");
      LOG_EVT_INFO("EVT_PRESENT_SPEECH_CLEAR", "tts_id=%lu", (unsigned long)gotId);
      g_ttsInflightSpeechText = "";
      g_ttsInflightSpeechId = 0;
      g_ttsInflightId = 0;
      g_ttsInflightRid = 0;
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
        g_tts.requestSessionReset();
        UIMining::instance().setStackchanSpeech("");
        g_ttsInflightSpeechText = "";
        g_ttsInflightSpeechId = 0;
        g_ttsInflightId = 0;
        g_ttsInflightRid = 0;
      }
    }
  }
  g_behavior.setTtsSpeaking(ttsBusyNow);
  applyMiningPolicyForTts_(ttsBusyNow, g_ai.isBusy());
  if (!g_tts.isBusy() && g_ttsInflightId == 0 && g_orch.hasPendingSpeak()) {
    auto pending = g_orch.popNextPending();
    if (pending.valid_) {
      const bool ok = g_tts.speakAsync(pending.text_, pending.ttsId_);
      if (ok) {
        g_ttsInflightId  = pending.ttsId_;
        g_ttsInflightRid = pending.rid_;
        g_ttsInflightSpeechText = pending.text_;
        g_ttsInflightSpeechId = pending.ttsId_;
        g_orch.setExpectedSpeak(pending.ttsId_, pending.rid_, pending.kind_);
        LOG_EVT_INFO("EVT_PRESENT_TTS_START",
                     "rid=%lu tts_id=%lu type=pending prio=%d busy=%d mode=%d attn=%d",
                     (unsigned long)pending.rid_, (unsigned long)pending.ttsId_,
                     (int)pending.prio_, 0, (int)g_mode, g_attentionActive ? 1 : 0);
      } else {
        LOG_EVT_INFO("EVT_PRESENT_TTS_PENDING_FAIL",
                     "rid=%lu tts_id=%lu prio=%d mode=%d attn=%d",
                     (unsigned long)pending.rid_, (unsigned long)pending.ttsId_,
                     (int)pending.prio_, (int)g_mode, g_attentionActive ? 1 : 0);
      }
    }
  }
  static wl_status_t s_prevWifi = WL_IDLE_STATUS;
  const wl_status_t wifiNow = WiFi.status();
  if (s_prevWifi == WL_CONNECTED && wifiNow != WL_CONNECTED) {
    mc_logf("[WIFI] disconnected (status=%d) -> reset TTS session", (int)wifiNow);
    g_tts.requestSessionReset();
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
  static bool prevTouchPressed = false;
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
    touchDown = touchPressed && !prevTouchPressed;
    prevTouchPressed = touchPressed;
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
  // === src/main.cpp : replace this block inside loop() ===
  if (g_displaySleeping) {
    if (anyInput) {
      MC_EVT("MAIN", "display wake (sleep off)");
      M5.Display.setBrightness(kDisplayActiveBrightness);
      g_displaySleeping = false;
      g_lastInputMs     = now;
    }
    delay(2);
    return;
  }
  UIMining& ui = UIMining::instance();
  if (btnB) {
    const char* text = appConfig().helloText_;
    if (features.ttsEnabled_) {
      static uint32_t s_ttsFailLastLogMs = 0;
      static uint32_t s_ttsFailSuppressed = 0;
      if (!g_tts.speakAsync(text, (uint32_t)0, nullptr)) {
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
    const AiTalkController::AiState stateBeforeTap = g_ai.state();
    const int screenH = M5.Display.height();
    aiConsumedTap = g_ai.onTap(touchX, touchY, screenH);
    if (aiConsumedTap) {
      if (g_aiTapConsumedCount == 0) {
        g_aiTapFirstX = touchX;
        g_aiTapFirstY = touchY;
        g_aiTapFirstMs = now;
      }
      g_aiTapConsumedCount++;
      g_aiTapLastX = touchX;
      g_aiTapLastY = touchY;
      const AiTalkController::AiState sNow = g_ai.state();
      if (sNow != AiTalkController::AiState::Idle) {
        g_aiTapLastState = sNow;
      } else if (stateBeforeTap != AiTalkController::AiState::Idle) {
        g_aiTapLastState = stateBeforeTap;
      }
      MC_LOGT("AI", "tap consumed by AI (%d,%d)", touchX, touchY);
    }
  }
  if (g_mode == Stackchan && g_ai.isBusy() && g_attentionActive) {
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
  } else if (g_ai.isBusy()) {
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
  if ((uint32_t)(now - g_lastUiMs) >= 100) {
    g_lastUiMs = now;
    MiningSummary summary;
    updateMiningSummary(summary);
    if (g_bubbleOnlyActive && (int32_t)(g_bubbleOnlyUntilMs - now) <= 0) {
      bubbleClear_("timeout");
    }
    UIMining::PanelData data;
    buildPanelData(summary, ui, data);
    g_behavior.update(data, now);
    StackchanReaction reaction;
    bool gotReaction = false;
    const bool suppressBehaviorNow = (g_mode == Stackchan) && g_ai.isBusy();
    if (suppressBehaviorNow && !g_prevAiBusyForBehavior) {
      g_aiBusyStartMs = now;
      MC_EVT("AI", "busy enter state=%s reason=ai_busy", aiStateName_(g_ai.state()));
    } else if (!suppressBehaviorNow && g_prevAiBusyForBehavior) {
      const float durS = (now - g_aiBusyStartMs) / 1000.0f;
      MC_EVT("AI", "busy exit state=%s dur=%.1fs reason=ai_idle", aiStateName_(g_ai.state()), durS);
      if (g_aiTapConsumedCount > 0) {
        const float spanS = (now - g_aiTapFirstMs) / 1000.0f;
        MC_LOGD("AI", "tap consumed x%lu last=(%d,%d) first=(%d,%d) span=%.1fs during=%s",
                (unsigned long)g_aiTapConsumedCount,
                g_aiTapLastX, g_aiTapLastY,
                g_aiTapFirstX, g_aiTapFirstY,
                spanS, aiStateName_(g_aiTapLastState));
        // reset
        g_aiTapConsumedCount = 0;
      }
    }
    g_prevAiBusyForBehavior = suppressBehaviorNow;
    if (suppressBehaviorNow) {
      gotReaction = false;
      if ((now - g_aiBusyDebugLastMs) >= 1000) {
        MC_LOGT("AI", "suppress Behavior while busy (state=%s)", aiStateName_(g_ai.state()));
        g_aiBusyDebugLastMs = now;
      }
    } else {
      gotReaction = g_behavior.popReaction(&reaction);
    }
    if (gotReaction) {
      LOG_EVT_INFO("EVT_PRESENT_POP",
                   "rid=%lu type=%d prio=%d speak=%d busy=%d mode=%d attn=%d",
                   (unsigned long)reaction.rid_, (int)reaction.evType_, (int)reaction.priority_,
                   reaction.speak_ ? 1 : 0, ttsBusyNow ? 1 : 0, (int)g_mode, g_attentionActive ? 1 : 0);
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
          bubbleClear_("tts_event");
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
      if (reaction.speak_ && reaction.speechText_.length() && features.ttsEnabled_) {
        auto cmd = g_orch.makeSpeakStartCmd(reaction.rid_, reaction.speechText_,
                                            toOrchPrio_(reaction.priority_),
                                            Orchestrator::OrchKind::BehaviorSpeak);
        if (cmd.valid_) {
          const bool canSpeakNow = (!ttsBusyNow) && (g_ttsInflightId == 0);
          if (canSpeakNow) {
            const bool speakOk = g_tts.speakAsync(cmd.text_, cmd.ttsId_);
            if (speakOk) {
              g_ttsInflightId  = cmd.ttsId_;
              g_ttsInflightRid = reaction.rid_;
              g_ttsInflightSpeechText = cmd.text_;
              g_ttsInflightSpeechId = cmd.ttsId_;
              g_orch.setExpectedSpeak(cmd.ttsId_, reaction.rid_, cmd.kind_);
              LOG_EVT_INFO("EVT_PRESENT_TTS_START",
                           "rid=%lu tts_id=%lu type=%d prio=%d busy=%d mode=%d attn=%d",
                           (unsigned long)reaction.rid_, (unsigned long)cmd.ttsId_,
                           (int)reaction.evType_, (int)reaction.priority_,
                           ttsBusyNow ? 1 : 0, (int)g_mode, g_attentionActive ? 1 : 0);
            }
          } else {
            g_orch.enqueueSpeakPending(cmd);
            LOG_EVT_INFO("EVT_PRESENT_TTS_DEFER_BUSY",
                         "rid=%lu tts_id=%lu prio=%d busy=%d mode=%d attn=%d",
                         (unsigned long)reaction.rid_, (unsigned long)cmd.ttsId_,
                         (int)reaction.priority_, ttsBusyNow ? 1 : 0,
                         (int)g_mode, g_attentionActive ? 1 : 0);
          }
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
  // === src/main.cpp : replace these blocks inside loop() ===
  if (!g_displaySleeping && (uint32_t)(now - g_lastInputMs) >= g_displaySleepTimeoutMs) {
    MC_EVT("MAIN", "display sleep (screen off)");
    UIMining::instance().drawSleepMessage();
    delay(kDisplaySleepMessageMs);
    M5.Display.setBrightness(0);
    g_displaySleeping = true;
  }
  static bool s_ttsYieldApplied = false;
  static MiningYieldProfile s_ttsSavedYield = MiningYieldNormal();
  static bool s_ttsSavedYieldValid = false;
  if (g_tts.isBusy()) {
    if (!s_ttsYieldApplied && !g_attentionActive) {
      s_ttsSavedYield = getMiningYieldProfile();
      s_ttsSavedYieldValid = true;
      setMiningYieldProfile(MiningYieldStrong());
      s_ttsYieldApplied = true;
      MC_EVT("TTS", "mining yield: Strong");
    }
  } else {
    if (s_ttsYieldApplied && !g_attentionActive) {
      if (s_ttsSavedYieldValid) setMiningYieldProfile(s_ttsSavedYield);
      else setMiningYieldProfile(MiningYieldNormal());
      s_ttsYieldApplied = false;
      MC_EVT("TTS", "mining yield: restore");
    }
  }
  delay(2);
}
static OrchPrio toOrchPrio_(ReactionPriority p) {
  switch (p) {
    case ReactionPriority::Low:    return OrchPrio::Low;
    case ReactionPriority::High:   return OrchPrio::High;
    case ReactionPriority::Normal:
    default:                       return OrchPrio::Normal;
  }
}
