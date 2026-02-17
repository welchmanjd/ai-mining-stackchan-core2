// Module implementation.
#include "core/public/app_runtime.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <M5Unified.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <WiFi.h>
#include <esp32-hal-cpu.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ai/ai_talk_controller.h"
#include "ai/azure_tts.h"
#include "ai/mining_task.h"
#include "ai/openai_llm.h"
#include "behavior/servo_driver.h"
#include "behavior/stackchan_behavior.h"
#include "config/config.h"
#include "config/mc_config_store.h"
#include "config/runtime_features.h"
#include "core/orchestrator.h"
#include "core/public/tts_coordinator.h"
#include "ui/app_presenter.h"
#include "ui/ui_mining_core2.h"
#include "utils/app_types.h"
#include "utils/logging.h"

static AppRuntimeContext g_ctx;

static unsigned long g_lastUiMs = 0;
static AppMode g_mode = Dash; // Changed: Start in Dashboard (Splash) mode
static bool g_prevAiBusyForBehavior = false;
static uint32_t g_aiBusyStartMs = 0;
static uint32_t g_aiBusyDebugLastMs = 0;
static uint32_t g_aiTapConsumedCount = 0;
static int g_aiTapFirstX = 0, g_aiTapFirstY = 0;
static int g_aiTapLastX = 0, g_aiTapLastY = 0;
static uint32_t g_aiTapFirstMs = 0;
static AiState g_aiTapLastState = AiState::Idle;
static bool g_attentionActive = false;
static uint32_t g_attentionUntilMs = 0;
static MiningYieldProfile g_savedYield = MiningYieldNormal();
static bool g_savedYieldValid = false;
static uint32_t g_displaySleepTimeoutMs =
    (uint32_t)MC_DISPLAY_SLEEP_SECONDS * 1000UL;
static unsigned long g_lastInputMs = 0;
static bool g_displaySleeping = false;
static bool g_suppressTouchBeepOnce = false;
static bool g_timeNtpDone = false;
static const uint8_t kDisplayActiveBrightness = MC_UI_DEFAULT_BRIGHTNESS;
static bool g_bubbleOnlyActive = false;
static uint32_t g_bubbleOnlyUntilMs = 0;
static uint32_t g_bubbleOnlyRid = 0;
static int g_bubbleOnlyEvType = 0;
enum class BubbleSource : uint8_t {
  None = 0,
  Ai = 1,
  Behavior = 2,
  Info = 3,
  System = 4
};
static BubbleSource g_bubbleOnlySource = BubbleSource::None;
static bool g_lastPopEmptyBusy = false;
static AppMode g_lastPopEmptyMode = Stackchan;
static bool g_lastPopEmptyAttn = false;
struct RuntimeInputState {
  bool anyInput_ = false;
  bool btnA_ = false;
  bool btnB_ = false;
  bool btnC_ = false;
  bool touchPressed_ = false;
  bool touchDown_ = false;
  int touchX_ = 0;
  int touchY_ = 0;
};

// ===== Splash boot checks (relay) =====
enum class BootCheckState : uint8_t {
  Waiting = 0,
  Connecting = 1,
  Ok = 2,
  Fail = 3,
  Skip = 4
};
enum class BootRelayStage : uint8_t {
  Wifi = 0,
  Mining,
  OpenAi,
  Azure,
  Done
};
static BootRelayStage g_bootStage = BootRelayStage::Wifi;
static BootCheckState g_bootWifiState = BootCheckState::Waiting;
static BootCheckState g_bootMiningState = BootCheckState::Waiting;
static BootCheckState g_bootOpenAiState = BootCheckState::Waiting;
static BootCheckState g_bootAzureState = BootCheckState::Waiting;
static String g_bootWifiDiag = "";
static String g_bootMiningDiag = "";
static String g_bootOpenAiDiag = "";
static String g_bootAzureDiag = "";
static String g_bootActiveDiag = "";
static uint32_t g_bootWifiConnectedSinceMs = 0;
static uint32_t g_bootMiningStartMs = 0;
static uint32_t g_bootOpenAiNextCheckMs = 0;
static int g_bootOpenAiRetryCount = 0;
static uint32_t g_bootAzureNextCheckMs = 0;
static int g_bootAzureRetryCount = 0;
static bool g_bootMiningHold = true;
struct BootProbeResult {
  bool running_ = false;
  bool done_ = false;
  bool ok_ = false;
  int http_ = 0;
  uint32_t tookMs_ = 0;
  char err_[96] = {0};
};
static portMUX_TYPE g_bootProbeMux = portMUX_INITIALIZER_UNLOCKED;
static BootProbeResult g_bootOpenAiProbe;
static BootProbeResult g_bootAzureProbe;
static TaskHandle_t g_bootOpenAiProbeTask = nullptr;
static TaskHandle_t g_bootAzureProbeTask = nullptr;

static const char *aiStateName_(AiState s) {
  switch (s) {
  case AiState::Idle:
    return "IDLE";
  case AiState::Listening:
    return "LISTENING";
  case AiState::Thinking:
    return "THINKING";
  case AiState::Speaking:
    return "SPEAKING";
  case AiState::PostSpeakBlank:
    return "POST";
  case AiState::Cooldown:
    return "COOLDOWN";
  default:
    return "?";
  }
}

static long getDisplaySleepSecondsFromStore_(long fallbackSec) {
  // Pull display timeout from stored JSON config (best-effort).
  String j =
      mcConfigGetMaskedJson(); // contains display_sleep_s, attention_text, etc.
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, j);
  if (e)
    return fallbackSec;
  JsonVariant v = doc["display_sleep_s"];
  if (v.is<long>()) {
    long sec = v.as<long>();
    if (sec > 0)
      return sec;
  } else if (v.is<int>()) {
    long sec = (long)v.as<int>();
    if (sec > 0)
      return sec;
  }
  return fallbackSec;
}

static uint32_t bubbleShow_Ms(const String &text) {
  const size_t len = text.length();
  uint32_t ms = MC_BUBBLE_MIN_MS + (uint32_t)(len * MC_BUBBLE_PER_CHAR_MS);
  const uint32_t maxMs = 8000;
  if (ms > maxMs)
    ms = maxMs;
  return ms;
}

static void bubbleClear_(const char *reason, bool forceUiClear) {
  if (!g_bubbleOnlyActive)
    return;
  const uint32_t oldRid = g_bubbleOnlyRid;
  const int oldType = g_bubbleOnlyEvType;
  g_bubbleOnlyActive = false;
  g_bubbleOnlyUntilMs = 0;
  if (g_mode == Stackchan && (forceUiClear || !g_attentionActive)) {
    UIMining::instance().setStackchanSpeech("");
  }
  LOG_EVT_INFO("EVT_PRESENT_BUBBLE_ONLY_CLEAR",
               "rid=%lu type=%d mode=%d attn=%d reason=%s",
               (unsigned long)oldRid, oldType, (int)g_mode,
               g_attentionActive ? 1 : 0, reason ? reason : "-");
  g_bubbleOnlyRid = 0;
  g_bubbleOnlyEvType = 0;
  g_bubbleOnlySource = BubbleSource::None;
}

static void bubbleShow_(const String &text, uint32_t now, uint32_t rid,
                        int evType, int prio, BubbleSource source) {
  if (!text.length())
    return;
  if (g_attentionActive)
    return;
  UIMining::instance().setStackchanSpeech(text);
  g_bubbleOnlyActive = true;
  const uint32_t showMs = bubbleShow_Ms(text);
  g_bubbleOnlyUntilMs = now + showMs;
  g_bubbleOnlyRid = rid;
  g_bubbleOnlyEvType = evType;
  g_bubbleOnlySource = source;
  LOG_EVT_INFO(
      "EVT_PRESENT_BUBBLE_ONLY_SHOW",
      "rid=%lu type=%d prio=%d len=%u mode=%d attn=%d show_ms=%lu text=%s",
      (unsigned long)rid, evType, prio, (unsigned)text.length(), (int)g_mode,
      g_attentionActive ? 1 : 0, (unsigned long)showMs, text.c_str());
}

static OrchPrio toOrchPrio_(ReactionPriority p) {
  switch (p) {
  case ReactionPriority::Low:
    return OrchPrio::Low;
  case ReactionPriority::High:
    return OrchPrio::High;
  case ReactionPriority::Normal:
  default:
    return OrchPrio::Normal;
  }
}

static bool wifiConnect_() {
  const RuntimeFeatures features = getRuntimeFeatures();
  const auto &cfg = appConfig();
  enum WifiState { NotStarted, Connecting, RetryWait, Done };
  static WifiState s_state = NotStarted;
  static bool s_forcedOff = false;
  static uint32_t s_startMs = 0;
  static uint32_t s_retryAtMs = 0;
  static uint8_t s_attempt = 0;
  static const uint32_t wifiConnectTimeoutMs = MC_WIFI_CONNECT_TIMEOUT_MS;
  static const uint32_t wifiRetryDelayMs = 1000UL;
  static const uint8_t wifiMaxAttempts = 2;
  if (!features.wifiEnabled_) {
    if (!s_forcedOff) {
      MC_LOGI("WIFI", "disabled by runtime config");
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      s_forcedOff = true;
      s_state = NotStarted;
    }
    g_bootWifiState = BootCheckState::Skip;
    g_bootWifiDiag = "WiFi is disabled in settings.";
    return true;
  }
  if (!features.wifiConfigured_) {
    g_bootWifiState = BootCheckState::Fail;
    g_bootWifiDiag = "WiFi SSID is empty.";
    return true;
  }
  if (s_forcedOff) {
    s_forcedOff = false;
    s_state = NotStarted;
    s_attempt = 0;
  }
  switch (s_state) {
  case NotStarted: {
    s_attempt = 1;
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid_, cfg.wifiPass_);
    s_startMs = millis();
    MC_LOGI("WIFI", "begin connect (ssid=%s, attempt=%u)", cfg.wifiSsid_,
            (unsigned)s_attempt);
    s_state = Connecting;
    g_bootWifiState = BootCheckState::Connecting;
    g_bootWifiDiag = "Connecting to WiFi...";
    return false;
  }
  case Connecting: {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      MC_EVT("WIFI", "connected: %s", WiFi.localIP().toString().c_str());
      s_state = Done;
      g_bootWifiState = BootCheckState::Ok;
      g_bootWifiDiag = "WiFi connection is OK.";
      return true;
    }
    if (millis() - s_startMs > wifiConnectTimeoutMs) {
      MC_LOGW("WIFI", "connect timeout (status=%d attempt=%u/%u)", (int)st,
              (unsigned)s_attempt, (unsigned)wifiMaxAttempts);
      if (s_attempt < wifiMaxAttempts) {
        s_attempt++;
        WiFi.disconnect(true, false);
        s_retryAtMs = millis() + wifiRetryDelayMs;
        s_state = RetryWait;
        g_bootWifiState = BootCheckState::Connecting;
        g_bootWifiDiag = "WiFi timeout. Retrying...";
        return false;
      }
      s_state = Done;
      g_bootWifiState = BootCheckState::Fail;
      switch (st) {
      case WL_NO_SSID_AVAIL:
        g_bootWifiDiag = "SSID not found. Check AP name.";
        break;
      case WL_CONNECT_FAILED:
        g_bootWifiDiag = "WiFi password/auth failed.";
        break;
      default:
        g_bootWifiDiag = "WiFi connect timeout.";
        break;
      }
      return true;
    }
    g_bootWifiState = BootCheckState::Connecting;
    g_bootWifiDiag = "Connecting to WiFi...";
    return false;
  }
  case RetryWait:
    if ((int32_t)(millis() - s_retryAtMs) >= 0) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(cfg.wifiSsid_, cfg.wifiPass_);
      s_startMs = millis();
      s_state = Connecting;
      MC_LOGI("WIFI", "retry connect (attempt=%u/%u)", (unsigned)s_attempt,
              (unsigned)wifiMaxAttempts);
      g_bootWifiState = BootCheckState::Connecting;
      g_bootWifiDiag = "Connecting to WiFi...";
    }
    return false;
  case Done:
  default:
    if (WiFi.status() == WL_CONNECTED) {
      g_bootWifiState = BootCheckState::Ok;
      g_bootWifiDiag = "WiFi connection is OK.";
    } else if (g_bootWifiState != BootCheckState::Fail) {
      g_bootWifiState = BootCheckState::Connecting;
      g_bootWifiDiag = "Connecting to WiFi...";
    }
    return true;
  }
}

static bool bootStateDone_(BootCheckState s) {
  return s == BootCheckState::Ok || s == BootCheckState::Fail ||
         s == BootCheckState::Skip;
}

static void bootStoreProbeResult_(BootProbeResult &dst, bool ok, int http,
                                  uint32_t tookMs, const char *err) {
  portENTER_CRITICAL(&g_bootProbeMux);
  dst.running_ = false;
  dst.done_ = true;
  dst.ok_ = ok;
  dst.http_ = http;
  dst.tookMs_ = tookMs;
  strncpy(dst.err_, err ? err : "", sizeof(dst.err_) - 1);
  dst.err_[sizeof(dst.err_) - 1] = '\0';
  portEXIT_CRITICAL(&g_bootProbeMux);
}

static bool bootConsumeProbeResult_(BootProbeResult &src, bool *ok, int *http,
                                    uint32_t *tookMs, char *err,
                                    size_t errLen) {
  bool has = false;
  bool okLocal = false;
  int httpLocal = 0;
  uint32_t tookLocal = 0;
  char errLocal[96] = {0};
  portENTER_CRITICAL(&g_bootProbeMux);
  if (src.done_) {
    has = true;
    okLocal = src.ok_;
    httpLocal = src.http_;
    tookLocal = src.tookMs_;
    strncpy(errLocal, src.err_, sizeof(errLocal) - 1);
    errLocal[sizeof(errLocal) - 1] = '\0';
    src.done_ = false;
  }
  portEXIT_CRITICAL(&g_bootProbeMux);
  if (!has) {
    return false;
  }
  if (ok) {
    *ok = okLocal;
  }
  if (http) {
    *http = httpLocal;
  }
  if (tookMs) {
    *tookMs = tookLocal;
  }
  if (err && errLen > 0) {
    strncpy(err, errLocal, errLen - 1);
    err[errLen - 1] = '\0';
  }
  return true;
}

static void bootOpenAiProbeTask_(void *pv) {
  (void)pv;
  const auto probe = openai_llm::probeConnection(MC_OPENAI_PROBE_TIMEOUT_MS);
  bootStoreProbeResult_(g_bootOpenAiProbe, probe.ok_, probe.http_,
                        probe.tookMs_, probe.err_.c_str());
  g_bootOpenAiProbeTask = nullptr;
  vTaskDelete(nullptr);
}

static void bootAzureProbeTask_(void *pv) {
  (void)pv;
  if (!g_ctx.tts_) {
    bootStoreProbeResult_(g_bootAzureProbe, false, 0, 0,
                          "TTS service is unavailable.");
    g_bootAzureProbeTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  const uint32_t t0 = millis();
  const bool ok = g_ctx.tts_->testCredentials();
  const uint32_t tookMs = millis() - t0;
  const char *err = ok ? "" : "Azure credential check failed.";
  bootStoreProbeResult_(g_bootAzureProbe, ok, ok ? 200 : 0, tookMs, err);
  g_bootAzureProbeTask = nullptr;
  vTaskDelete(nullptr);
}

static bool bootStartOpenAiProbe_() {
  portENTER_CRITICAL(&g_bootProbeMux);
  if (g_bootOpenAiProbe.running_) {
    portEXIT_CRITICAL(&g_bootProbeMux);
    return true;
  }
  g_bootOpenAiProbe.running_ = true;
  g_bootOpenAiProbe.done_ = false;
  g_bootOpenAiProbe.ok_ = false;
  g_bootOpenAiProbe.http_ = 0;
  g_bootOpenAiProbe.tookMs_ = 0;
  g_bootOpenAiProbe.err_[0] = '\0';
  portEXIT_CRITICAL(&g_bootProbeMux);

  if (xTaskCreatePinnedToCore(bootOpenAiProbeTask_, "BootOpenAiProbe",
                              MC_BOOT_PROBE_TASK_STACK, nullptr,
                              MC_BOOT_PROBE_TASK_PRIO, &g_bootOpenAiProbeTask,
                              MC_BOOT_PROBE_TASK_CORE) !=
      pdPASS) {
    bootStoreProbeResult_(g_bootOpenAiProbe, false, 0, 0,
                          "openai_probe_task_create_failed");
    return false;
  }
  return true;
}

static bool bootStartAzureProbe_() {
  portENTER_CRITICAL(&g_bootProbeMux);
  if (g_bootAzureProbe.running_) {
    portEXIT_CRITICAL(&g_bootProbeMux);
    return true;
  }
  g_bootAzureProbe.running_ = true;
  g_bootAzureProbe.done_ = false;
  g_bootAzureProbe.ok_ = false;
  g_bootAzureProbe.http_ = 0;
  g_bootAzureProbe.tookMs_ = 0;
  g_bootAzureProbe.err_[0] = '\0';
  portEXIT_CRITICAL(&g_bootProbeMux);

  if (xTaskCreatePinnedToCore(bootAzureProbeTask_, "BootAzureProbe",
                              MC_BOOT_PROBE_TASK_STACK, nullptr,
                              MC_BOOT_PROBE_TASK_PRIO, &g_bootAzureProbeTask,
                              MC_BOOT_PROBE_TASK_CORE) !=
      pdPASS) {
    bootStoreProbeResult_(g_bootAzureProbe, false, 0, 0,
                          "azure_probe_task_create_failed");
    return false;
  }
  return true;
}

static void bootRunOpenAiCheck_(uint32_t now) {
  if (g_bootOpenAiState == BootCheckState::Ok ||
      g_bootOpenAiState == BootCheckState::Fail) {
    return;
  }
  if (now < g_bootOpenAiNextCheckMs) {
    g_bootOpenAiState = BootCheckState::Connecting;
    g_bootOpenAiDiag = "Connecting to OpenAI...";
    return;
  }
  bool ok = false;
  int http = 0;
  uint32_t tookMs = 0;
  char err[96] = {0};
  if (!bootConsumeProbeResult_(g_bootOpenAiProbe, &ok, &http, &tookMs, err,
                               sizeof(err))) {
    g_bootOpenAiState = BootCheckState::Connecting;
    g_bootOpenAiDiag = "Connecting to OpenAI...";
    (void)bootStartOpenAiProbe_();
    return;
  }

  if (ok) {
    g_bootOpenAiState = BootCheckState::Ok;
    g_bootOpenAiDiag = "OpenAI connection is OK.";
    g_bootOpenAiRetryCount = 0;
    g_bootOpenAiNextCheckMs = 0;
    MC_LOGI("BOOT", "OpenAI probe OK (http=%d took=%lums)", http,
            (unsigned long)tookMs);
    return;
  }
  g_bootOpenAiState = BootCheckState::Fail;
  g_bootOpenAiDiag = err[0] ? String(err) : String("OpenAI probe failed");
  static const uint32_t kBackoffMs[] = MC_BOOT_PROBE_BACKOFF_MS;
  const int idx = (g_bootOpenAiRetryCount < 2) ? g_bootOpenAiRetryCount : 2;
  g_bootOpenAiNextCheckMs = now + kBackoffMs[idx];
  g_bootOpenAiRetryCount++;
  MC_LOGW("BOOT", "OpenAI probe NG (http=%d took=%lums): %s", http,
          (unsigned long)tookMs, g_bootOpenAiDiag.c_str());
}

static void bootRunAzureCheck_(uint32_t now) {
  if (g_bootAzureState == BootCheckState::Ok ||
      g_bootAzureState == BootCheckState::Fail) {
    return;
  }
  if (!g_ctx.tts_) {
    g_bootAzureState = BootCheckState::Fail;
    g_bootAzureDiag = "TTS service is unavailable.";
    return;
  }
  if (now < g_bootAzureNextCheckMs) {
    g_bootAzureState = BootCheckState::Connecting;
    g_bootAzureDiag = "Connecting to Azure...";
    return;
  }
  bool ok = false;
  int http = 0;
  uint32_t tookMs = 0;
  char err[96] = {0};
  if (!bootConsumeProbeResult_(g_bootAzureProbe, &ok, &http, &tookMs, err,
                               sizeof(err))) {
    g_bootAzureState = BootCheckState::Connecting;
    g_bootAzureDiag = "Connecting to Azure...";
    (void)bootStartAzureProbe_();
    return;
  }

  if (ok) {
    g_bootAzureState = BootCheckState::Ok;
    g_bootAzureDiag = "Azure connection is OK.";
    g_bootAzureRetryCount = 0;
    g_bootAzureNextCheckMs = 0;
    MC_LOGI("BOOT", "Azure probe OK (http=%d took=%lums)", http,
            (unsigned long)tookMs);
    return;
  }
  g_bootAzureState = BootCheckState::Fail;
  g_bootAzureDiag = err[0] ? String(err) : String("Azure credential check failed.");
  static const uint32_t kBackoffMs[] = MC_BOOT_PROBE_BACKOFF_MS;
  const int idx = (g_bootAzureRetryCount < 2) ? g_bootAzureRetryCount : 2;
  g_bootAzureNextCheckMs = now + kBackoffMs[idx];
  g_bootAzureRetryCount++;
  MC_LOGW("BOOT", "Azure probe NG (http=%d took=%lums): %s", http,
          (unsigned long)tookMs, g_bootAzureDiag.c_str());
}

static void updateBootChecks_(uint32_t now, const RuntimeFeatures &features,
                              const MiningSummary &summary) {
  if (g_bootWifiState == BootCheckState::Ok) {
    if (g_bootWifiConnectedSinceMs == 0) {
      g_bootWifiConnectedSinceMs = now;
    }
  } else {
    g_bootWifiConnectedSinceMs = 0;
  }

  if (!features.miningEnabled_) {
    g_bootMiningState = BootCheckState::Skip;
    g_bootMiningDiag = "Mining is disabled.";
  } else if (g_bootWifiState != BootCheckState::Ok) {
    g_bootMiningState = BootCheckState::Waiting;
    g_bootMiningDiag = "Waiting for WiFi...";
    g_bootMiningStartMs = 0;
  } else if (g_bootMiningHold) {
    if (g_bootMiningStartMs == 0) {
      g_bootMiningStartMs = now;
    }
    const uint32_t holdConnectMs = 900UL;
    if ((now - g_bootMiningStartMs) < holdConnectMs) {
      g_bootMiningState = BootCheckState::Connecting;
      g_bootMiningDiag = "Connecting to mining pool...";
    } else {
      g_bootMiningState = BootCheckState::Ok;
      g_bootMiningDiag = "Mining check is OK.";
    }
  } else if (summary.anyConnected_) {
    g_bootMiningState = BootCheckState::Ok;
    g_bootMiningDiag = "Mining pool connection is OK.";
  } else {
    if (g_bootMiningStartMs == 0) {
      g_bootMiningStartMs = now;
    }
    const uint32_t miningTimeoutMs = MC_MINING_BOOT_TIMEOUT_MS;
    if ((now - g_bootMiningStartMs) > miningTimeoutMs) {
      g_bootMiningState = BootCheckState::Fail;
      g_bootMiningDiag = summary.poolDiag_.length()
                             ? summary.poolDiag_
                             : String("Mining pool connection failed.");
    } else {
      g_bootMiningState = BootCheckState::Connecting;
      g_bootMiningDiag = "Connecting to mining pool...";
    }
  }

  if (!features.aiEnabled_) {
    g_bootOpenAiState = BootCheckState::Skip;
    g_bootOpenAiDiag = "OpenAI is disabled.";
  } else if (g_bootWifiState != BootCheckState::Ok) {
    g_bootOpenAiState = BootCheckState::Waiting;
    g_bootOpenAiDiag = "Waiting for WiFi...";
  } else if (features.miningEnabled_ && !bootStateDone_(g_bootMiningState)) {
    g_bootOpenAiState = BootCheckState::Waiting;
    g_bootOpenAiDiag = "Waiting for Mining check...";
  } else if (g_bootMiningState == BootCheckState::Fail) {
    g_bootOpenAiState = BootCheckState::Waiting;
    g_bootOpenAiDiag = "Waiting for Mining check...";
  } else if ((now - g_bootWifiConnectedSinceMs) < MC_BOOT_WIFI_STABILIZE_MS) {
    g_bootOpenAiState = BootCheckState::Connecting;
    g_bootOpenAiDiag = "Connecting to OpenAI...";
  } else {
    bootRunOpenAiCheck_(now);
  }

  if (!features.ttsEnabled_) {
    g_bootAzureState = BootCheckState::Skip;
    g_bootAzureDiag = "Azure TTS is disabled.";
  } else if (g_bootWifiState != BootCheckState::Ok) {
    g_bootAzureState = BootCheckState::Waiting;
    g_bootAzureDiag = "Waiting for WiFi...";
  } else if (features.aiEnabled_ && !bootStateDone_(g_bootOpenAiState)) {
    g_bootAzureState = BootCheckState::Waiting;
    g_bootAzureDiag = "Waiting for OpenAI check...";
  } else if (g_bootOpenAiState == BootCheckState::Fail) {
    g_bootAzureState = BootCheckState::Waiting;
    g_bootAzureDiag = "Waiting for OpenAI check...";
  } else if ((now - g_bootWifiConnectedSinceMs) < MC_BOOT_WIFI_STABILIZE_MS) {
    g_bootAzureState = BootCheckState::Connecting;
    g_bootAzureDiag = "Connecting to Azure...";
  } else {
    bootRunAzureCheck_(now);
  }

  if (g_bootWifiState == BootCheckState::Fail) {
    g_bootStage = BootRelayStage::Wifi;
    g_bootActiveDiag = g_bootWifiDiag;
    return;
  }
  if (g_bootMiningState == BootCheckState::Fail) {
    g_bootStage = BootRelayStage::Mining;
    g_bootActiveDiag = g_bootMiningDiag;
    return;
  }
  if (g_bootOpenAiState == BootCheckState::Fail) {
    g_bootStage = BootRelayStage::OpenAi;
    g_bootActiveDiag = g_bootOpenAiDiag;
    return;
  }
  if (g_bootAzureState == BootCheckState::Fail) {
    g_bootStage = BootRelayStage::Azure;
    g_bootActiveDiag = g_bootAzureDiag;
    return;
  }

  if (g_bootWifiState != BootCheckState::Ok) {
    g_bootStage = BootRelayStage::Wifi;
    g_bootActiveDiag = g_bootWifiDiag;
  } else if (!bootStateDone_(g_bootMiningState)) {
    g_bootStage = BootRelayStage::Mining;
    g_bootActiveDiag = g_bootMiningDiag;
  } else if (!bootStateDone_(g_bootOpenAiState)) {
    g_bootStage = BootRelayStage::OpenAi;
    g_bootActiveDiag = g_bootOpenAiDiag;
  } else if (!bootStateDone_(g_bootAzureState)) {
    g_bootStage = BootRelayStage::Azure;
    g_bootActiveDiag = g_bootAzureDiag;
  } else {
    g_bootStage = BootRelayStage::Done;
    g_bootActiveDiag = "All checks passed.";
  }
}

static void setupTimeNtp_() {
  setenv("TZ", "JST-9", 1);
  tzset();
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com", "pool.ntp.org");
}

void appRuntimeInit(const AppRuntimeContext &ctx) {
  g_ctx = ctx;
  long sec = getDisplaySleepSecondsFromStore_((long)MC_DISPLAY_SLEEP_SECONDS);
  g_displaySleepTimeoutMs = (uint32_t)sec * 1000UL;
  mc_logf("[MAIN] display_sleep_s=%ld => timeout=%lu ms", sec,
          (unsigned long)g_displaySleepTimeoutMs);
  g_lastUiMs = 0;
  g_lastInputMs = millis();
  g_displaySleeping = false;
  g_bootStage = BootRelayStage::Wifi;
  g_bootWifiState = BootCheckState::Waiting;
  g_bootMiningState = BootCheckState::Waiting;
  g_bootOpenAiState = BootCheckState::Waiting;
  g_bootAzureState = BootCheckState::Waiting;
  g_bootWifiDiag = "";
  g_bootMiningDiag = "";
  g_bootOpenAiDiag = "";
  g_bootAzureDiag = "";
  g_bootActiveDiag = "";
  g_bootWifiConnectedSinceMs = 0;
  g_bootMiningStartMs = 0;
  g_bootOpenAiNextCheckMs = 0;
  g_bootOpenAiRetryCount = 0;
  g_bootAzureNextCheckMs = 0;
  g_bootAzureRetryCount = 0;
  g_bootMiningHold = true;
  portENTER_CRITICAL(&g_bootProbeMux);
  g_bootOpenAiProbe = BootProbeResult();
  g_bootAzureProbe = BootProbeResult();
  portEXIT_CRITICAL(&g_bootProbeMux);
  g_bootOpenAiProbeTask = nullptr;
  g_bootAzureProbeTask = nullptr;
}

static void handleAiAndOrchestrator_(uint32_t now,
                                     const RuntimeFeatures &runtimeFeatures) {
  if (runtimeFeatures.aiEnabled_) {
    g_ctx.ai_->tick(now);
    String aiBubbleText;
    if (g_ctx.ai_->consumeBubbleUpdate(&aiBubbleText)) {
      if (aiBubbleText.length() == 0) {
        bubbleClear_("ai_update", true);
      } else if (!runtimeFeatures.ttsEnabled_ ||
                 g_ctx.ai_->state() == AiState::Listening ||
                 g_ctx.ai_->state() == AiState::Thinking) {
        bubbleShow_(aiBubbleText, now, 0, -1, 0, BubbleSource::Ai);
      }
    }
  } else if (g_ctx.ai_->isBusy()) {
    g_ctx.ai_->forceStop(now, "ai_disabled");
  }

  if (g_ctx.orch_->tick(now)) {
    LOG_EVT_INFO("EVT_ORCH_TIMEOUT_MAIN",
                 "recover=1 expect_tts_id=%lu expect_rid=%lu",
                 (unsigned long)g_ctx.orch_->expectSpeakId(),
                 (unsigned long)g_ctx.orch_->expectRid());
    if (g_ctx.tts_) {
      g_ctx.tts_->requestSessionReset();
    }
    ttsCoordinatorClearInflight();
  }
  ttsCoordinatorTick(now);

  static wl_status_t s_prevWifi = WL_IDLE_STATUS;
  const wl_status_t wifiNow = WiFi.status();
  if (s_prevWifi == WL_CONNECTED && wifiNow != WL_CONNECTED) {
    mc_logf("[WIFI] disconnected (status=%d) -> reset TTS session",
            (int)wifiNow);
    if (g_ctx.tts_) {
      g_ctx.tts_->requestSessionReset();
    }
  }
  s_prevWifi = wifiNow;
}

static RuntimeInputState pollRuntimeInputs_(uint32_t now) {
  RuntimeInputState in;
  in.btnA_ = M5.BtnA.wasPressed();
  in.btnB_ = M5.BtnB.wasPressed();
  in.btnC_ = M5.BtnC.wasPressed();
  if (in.btnA_ || in.btnB_ || in.btnC_) {
    in.anyInput_ = true;
    g_suppressTouchBeepOnce = true;
  }

  static bool s_prevTouchPressed = false;
  auto &tp = M5.Touch;
  static uint32_t s_lastTouchPollMs = 0;
  static int s_touchX = 0;
  static int s_touchY = 0;
  static bool s_touchPressed = false;
  if (tp.isEnabled()) {
    if ((uint32_t)(now - s_lastTouchPollMs) >= MC_TOUCH_POLL_INTERVAL_MS) {
      s_lastTouchPollMs = now;
      auto det = tp.getDetail();
      s_touchPressed = det.isPressed();
      if (s_touchPressed) {
        s_touchX = det.x;
        s_touchY = det.y;
      }
    }
    in.touchPressed_ = s_touchPressed;
    in.touchX_ = s_touchX;
    in.touchY_ = s_touchY;
    in.touchDown_ = in.touchPressed_ && !s_prevTouchPressed;
    s_prevTouchPressed = in.touchPressed_;
    if (in.touchPressed_) {
      in.anyInput_ = true;
    }
  }

  UIMining::TouchSnapshot ts;
  ts.enabled_ = tp.isEnabled();
  ts.pressed_ = in.touchPressed_;
  ts.down_ = in.touchDown_;
  ts.x_ = in.touchX_;
  ts.y_ = in.touchY_;
  UIMining::instance().setTouchSnapshot(ts);
  return in;
}

static bool handleSleepWake_(uint32_t now, const RuntimeInputState &in) {
  if (!g_displaySleeping) {
    return false;
  }
  if (in.anyInput_) {
    MC_EVT("MAIN", "display wake (sleep off)");
    M5.Display.setBrightness(kDisplayActiveBrightness);
    g_displaySleeping = false;
    g_lastInputMs = now;
  }
  return true;
}

static void handleButtonAndTouch_(uint32_t now,
                                  const RuntimeFeatures &runtimeFeatures,
                                  const RuntimeInputState &in, UIMining &ui) {
  if (in.btnB_) {
    const char *text = appConfig().helloText_;
    if (runtimeFeatures.ttsEnabled_) {
      static uint32_t s_ttsFailLastLogMs = 0;
      static uint32_t s_ttsFailSuppressed = 0;
      if (!ttsCoordinatorTrySpeakNow(String(text))) {
        s_ttsFailSuppressed++;
        const uint32_t kFailLogIntervalMs = 3000;
        if (s_ttsFailLastLogMs == 0 ||
            (now - s_ttsFailLastLogMs) >= kFailLogIntervalMs) {
          if (s_ttsFailSuppressed > 1) {
            mc_logf("[TTS] BtnB speak rejected (busy / wifi / config?) "
                    "(suppressed x%lu)",
                    (unsigned long)(s_ttsFailSuppressed - 1));
          } else {
            mc_logf("[TTS] BtnB speak rejected (busy / wifi / config?)");
          }
          s_ttsFailSuppressed = 0;
          s_ttsFailLastLogMs = now;
        }
      } else {
        s_ttsFailSuppressed = 0;
      }
    }
  }

  if (in.anyInput_) {
    g_lastInputMs = now;
  }
  if (in.btnA_) {
    M5.Speaker.tone(MC_APP_BTN_A_BEEP_FREQ, MC_APP_BTN_A_BEEP_MS);
    if (g_mode == Dash) {
      g_mode = Stackchan;
      ui.onEnterStackchanMode();
    } else {
      g_mode = Dash;
      ui.onLeaveStackchanMode();
      if (g_attentionActive) {
        g_attentionActive = false;
        if (g_savedYieldValid) {
          setMiningYieldProfile(g_savedYield);
        }
        ui.triggerAttention(0);
      }
    }
    MC_EVT("MAIN", "BtnA pressed, mode=%d", (int)g_mode);
  }

  bool aiConsumedTap = false;
  if (runtimeFeatures.aiEnabled_ && g_mode == Stackchan && in.touchDown_) {
    const AiState stateBeforeTap = g_ctx.ai_->state();
    const int screenH = M5.Display.height();
    aiConsumedTap = g_ctx.ai_->onTap(in.touchX_, in.touchY_, screenH);
    if (aiConsumedTap) {
      if (g_aiTapConsumedCount == 0) {
        g_aiTapFirstX = in.touchX_;
        g_aiTapFirstY = in.touchY_;
        g_aiTapFirstMs = now;
      }
      g_aiTapConsumedCount++;
      g_aiTapLastX = in.touchX_;
      g_aiTapLastY = in.touchY_;
      const AiState sNow = g_ctx.ai_->state();
      if (sNow != AiState::Idle) {
        g_aiTapLastState = sNow;
      } else if (stateBeforeTap != AiState::Idle) {
        g_aiTapLastState = stateBeforeTap;
      }
      MC_LOGT("AI", "tap consumed by AI (%d,%d)", in.touchX_, in.touchY_);
    }
  }

  if (g_mode == Stackchan && g_ctx.ai_->isBusy() && g_attentionActive) {
    MC_EVT("ATTN", "force exit (aiBusy=1)");
    g_attentionActive = false;
    g_attentionUntilMs = 0;
    if (g_savedYieldValid) {
      setMiningYieldProfile(g_savedYield);
    } else {
      setMiningYieldProfile(MiningYieldNormal());
    }
    ui.triggerAttention(0);
  }

  if (!aiConsumedTap && g_mode == Stackchan && in.touchDown_) {
    if (in.btnB_) {
      MC_LOGT("ATTN", "suppressed (btnB=1)");
    } else if (g_attentionActive) {
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
      M5.Speaker.tone(MC_APP_ATTENTION_BEEP_FREQ, MC_APP_ATTENTION_BEEP_MS);
      if (g_bubbleOnlyActive) {
        bubbleClear_("attention_start", true);
      }
    }
  }

  if (g_attentionActive && (int32_t)(g_attentionUntilMs - now) <= 0) {
    g_attentionActive = false;
    MC_EVT("ATTN", "exit");
    if (g_savedYieldValid) {
      setMiningYieldProfile(g_savedYield);
    } else {
      setMiningYieldProfile(MiningYieldNormal());
    }
    ui.triggerAttention(0);
  }
}

static void handleUiAndBehaviorFrame_(uint32_t now,
                                      const RuntimeFeatures &runtimeFeatures,
                                      bool ttsBusyNow, UIMining &ui) {
  MiningSummary summary;
  updateMiningSummary(summary);
  if (g_bubbleOnlyActive && (int32_t)(g_bubbleOnlyUntilMs - now) <= 0) {
    const AiState aiStateNow = g_ctx.ai_->state();
    const bool holdAiStatusBubble =
        (g_bubbleOnlySource == BubbleSource::Ai) &&
        (aiStateNow == AiState::Listening || aiStateNow == AiState::Thinking);
    if (holdAiStatusBubble) {
      g_bubbleOnlyUntilMs = now + 1000;
    } else {
      bubbleClear_("timeout", false);
    }
  }

  UIMining::PanelData data;
  NetworkStatus ns = NetworkStatus::Unknown;
  switch (WiFi.status()) {
  case WL_CONNECTED:
    ns = NetworkStatus::Connected;
    break;
  case WL_NO_SSID_AVAIL:
    ns = NetworkStatus::NoSsid;
    break;
  case WL_CONNECT_FAILED:
    ns = NetworkStatus::ConnectFailed;
    break;
  case WL_DISCONNECTED:
    ns = NetworkStatus::Disconnected;
    break;
  default:
    ns = NetworkStatus::Unknown;
    break;
  }

  if (g_mode == Dash && ui.isSplashActive()) {
    updateBootChecks_(now, runtimeFeatures, summary);
  }
  int legacyBootStatus = 0;
  if (g_bootOpenAiState == BootCheckState::Ok) {
    legacyBootStatus = 3;
  } else if (g_bootOpenAiState == BootCheckState::Fail) {
    legacyBootStatus = 4;
  } else if (g_bootOpenAiState == BootCheckState::Connecting) {
    legacyBootStatus = 2;
  } else if (g_bootWifiState != BootCheckState::Ok) {
    legacyBootStatus = 1;
  }
  buildPanelData(summary, ui, data, ns, runtimeFeatures.aiEnabled_,
                 g_bootOpenAiState == BootCheckState::Ok, g_bootOpenAiDiag,
                 legacyBootStatus, (uint8_t)g_bootWifiState,
                 (uint8_t)g_bootMiningState, (uint8_t)g_bootOpenAiState,
                 (uint8_t)g_bootAzureState, g_bootWifiDiag, g_bootMiningDiag,
                 g_bootOpenAiDiag, g_bootAzureDiag, g_bootActiveDiag);
  g_ctx.behavior_->update(data, now);

  StackchanReaction reaction;
  bool gotReaction = false;
  const bool suppressBehaviorNow = (g_mode == Stackchan) && g_ctx.ai_->isBusy();
  if (suppressBehaviorNow && !g_prevAiBusyForBehavior) {
    g_aiBusyStartMs = now;
    MC_EVT("AI", "busy enter state=%s reason=ai_busy",
           aiStateName_(g_ctx.ai_->state()));
  } else if (!suppressBehaviorNow && g_prevAiBusyForBehavior) {
    const float durS = (now - g_aiBusyStartMs) / 1000.0f;
    MC_EVT("AI", "busy exit state=%s dur=%.1fs reason=ai_idle",
           aiStateName_(g_ctx.ai_->state()), durS);
    if (g_aiTapConsumedCount > 0) {
      const float spanS = (now - g_aiTapFirstMs) / 1000.0f;
      MC_LOGD("AI",
              "tap consumed x%lu last=(%d,%d) first=(%d,%d) span=%.1fs during=%s",
              (unsigned long)g_aiTapConsumedCount, g_aiTapLastX, g_aiTapLastY,
              g_aiTapFirstX, g_aiTapFirstY, spanS, aiStateName_(g_aiTapLastState));
      g_aiTapConsumedCount = 0;
    }
  }
  g_prevAiBusyForBehavior = suppressBehaviorNow;
  if (suppressBehaviorNow) {
    gotReaction = false;
    if ((now - g_aiBusyDebugLastMs) >= 1000) {
      MC_LOGT("AI", "suppress Behavior while busy (state=%s)",
              aiStateName_(g_ctx.ai_->state()));
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

    if (g_mode == Stackchan) {
      if (reaction.speak_ && g_bubbleOnlyActive) {
        bubbleClear_("tts_event", false);
      }
      if (!reaction.speak_ && !isIdleTick && reaction.speechText_.length() &&
          !suppressedByAttention) {
        const bool isBubbleInfo =
            (reaction.evType_ == StackchanEventType::InfoPool) ||
            (reaction.evType_ == StackchanEventType::InfoPing) ||
            (reaction.evType_ == StackchanEventType::InfoHashrate) ||
            (reaction.evType_ == StackchanEventType::InfoShares) ||
            (reaction.evType_ == StackchanEventType::InfoMiningOff);
        const BubbleSource bubbleSource =
            isBubbleInfo ? BubbleSource::Info : BubbleSource::Behavior;
        bubbleShow_(reaction.speechText_, now, reaction.rid_,
                    (int)reaction.evType_, (int)reaction.priority_,
                    bubbleSource);
      }
    }

    if (reaction.speak_ && reaction.speechText_.length() &&
        runtimeFeatures.ttsEnabled_) {
      auto cmd = g_ctx.orch_->makeSpeakStartCmd(
          reaction.rid_, reaction.speechText_, toOrchPrio_(reaction.priority_),
          Orchestrator::OrchKind::BehaviorSpeak);
      if (cmd.valid_) {
        ttsCoordinatorMaybeSpeak(cmd, (int)reaction.evType_);
      }
    }
  } else {
    static uint32_t s_lastHbMs = 0;
    static uint32_t s_emptyStreak = 0;
    s_emptyStreak++;
    const uint32_t PRESENTER_HEARTBEAT_MS = MC_PRESENTER_HEARTBEAT_MS;
    const bool stateChanged = (ttsBusyNow != g_lastPopEmptyBusy) ||
                              (g_mode != g_lastPopEmptyMode) ||
                              (g_attentionActive != g_lastPopEmptyAttn);
    if (stateChanged || (now - s_lastHbMs) >= PRESENTER_HEARTBEAT_MS) {
      LOG_EVT_HEARTBEAT("EVT_PRESENT_HEARTBEAT",
                        "busy=%d mode=%d attn=%d empty_streak=%lu",
                        ttsBusyNow ? 1 : 0, (int)g_mode,
                        g_attentionActive ? 1 : 0, (unsigned long)s_emptyStreak);
      s_lastHbMs = now;
      s_emptyStreak = 0;
      g_lastPopEmptyBusy = ttsBusyNow;
      g_lastPopEmptyMode = g_mode;
      g_lastPopEmptyAttn = g_attentionActive;
    }
  }

  String ticker = buildTicker(summary);
  if (g_mode == Dash && !ui.isSplashActive()) {
    static bool s_bootSwitchDone = false;
    if (!s_bootSwitchDone) {
      s_bootSwitchDone = true;
      g_mode = Stackchan;
      ui.onEnterStackchanMode();
      MC_LOGI("MAIN", "Boot splash done -> auto-switch to Stackchan");
    }
  }
  if (g_mode == Stackchan) {
    ui.drawStackchanScreen(data);
  } else {
    ui.drawAll(data, ticker);
  }
  g_suppressTouchBeepOnce = false;
}

static void handleNetworkUiAndSleep_(uint32_t now,
                                     const RuntimeFeatures &runtimeFeatures,
                                     UIMining &ui) {
  const bool wantMiningHold =
      runtimeFeatures.miningEnabled_ && g_mode == Dash && ui.isSplashActive();
  if (wantMiningHold != g_bootMiningHold) {
    g_bootMiningHold = wantMiningHold;
    setMiningBootHold(g_bootMiningHold);
    MC_EVT("BOOT", "mining hold: %d", g_bootMiningHold ? 1 : 0);
  }

  const bool wifiDone = wifiConnect_();
  if (wifiDone && !g_timeNtpDone && WiFi.status() == WL_CONNECTED) {
    setupTimeNtp_();
    g_timeNtpDone = true;
  }

  const bool ttsBusyNow = ttsCoordinatorIsBusy();
  if ((uint32_t)(now - g_lastUiMs) >= MC_APP_UI_FRAME_INTERVAL_MS) {
    g_lastUiMs = now;
    handleUiAndBehaviorFrame_(now, runtimeFeatures, ttsBusyNow, ui);
  }

  if (!g_displaySleeping &&
      (uint32_t)(now - g_lastInputMs) >= g_displaySleepTimeoutMs) {
    const uint32_t idleMs = (uint32_t)(now - g_lastInputMs);
    MC_EVT("MAIN", "display sleep (screen off) idle_ms=%lu timeout_ms=%lu",
           (unsigned long)idleMs, (unsigned long)g_displaySleepTimeoutMs);
    UIMining::instance().drawSleepMessage();
    M5.Display.setBrightness(0);
    g_displaySleeping = true;
  }
}

void appRuntimeTick(uint32_t now) {
  if (!g_ctx.ai_ || !g_ctx.orch_ || !g_ctx.behavior_) {
    return;
  }
  const RuntimeFeatures runtimeFeatures = getRuntimeFeatures();
  handleAiAndOrchestrator_(now, runtimeFeatures);

  const RuntimeInputState in = pollRuntimeInputs_(now);
  if (handleSleepWake_(now, in)) {
    return;
  }

  UIMining &ui = UIMining::instance();
  handleButtonAndTouch_(now, runtimeFeatures, in, ui);
  handleNetworkUiAndSleep_(now, runtimeFeatures, ui);
  float servoGazeX = 0.0f;
  float servoGazeY = 0.0f;
  bool servoActive = false;
  ui.getServoDriveTarget(&servoGazeX, &servoGazeY, &servoActive);
  servoDriverSetTarget(servoGazeX, servoGazeY, servoActive);
  servoDriverTick();
#if MC_SERVO_DIAG_POWER_LOG_ENABLE
  if (servoActive) {
    const int battMv = (int)M5.Power.getBatteryVoltage();
    const int battMa = (int)M5.Power.getBatteryCurrent();
    const int charging = (int)M5.Power.isCharging();
    MC_LOGI_RL("servo_power_diag", MC_SERVO_DIAG_POWER_LOG_INTERVAL_MS,
               "SERVO_PWR",
               "gaze=(%.3f,%.3f) batt=%dmV curr=%dmA chg=%d",
               (double)servoGazeX, (double)servoGazeY, battMv, battMa, charging);
  }
#endif
}

void appRuntimeNotifySerialActivity() {
  const uint32_t now = millis();
  g_lastInputMs = now;
  if (g_displaySleeping) {
    MC_EVT("MAIN", "display wake (serial activity)");
    M5.Display.setBrightness(kDisplayActiveBrightness);
    g_displaySleeping = false;
  }
}
uint32_t *appRuntimeDisplaySleepTimeoutMsPtr() {
  return &g_displaySleepTimeoutMs;
}

bool *appRuntimeAttentionActivePtr() { return &g_attentionActive; }

AppMode *appRuntimeModePtr() { return &g_mode; }

BubbleClearFn appRuntimeBubbleClearFn() { return bubbleClear_; }
