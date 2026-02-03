// src/main.cpp
// Module implementation.
// ===== Mining-chan Core2 ? main entry (UI + orchestrator) =====
// Board   : M5Stack Core2
// Libs    : M5Unified, ArduinoJson, WiFi, WiFiClientSecure, HTTPClient, m5stack-avatar
#include <time.h>

#include <Arduino.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <M5Unified.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <esp32-hal-cpu.h>
#include <esp_log.h>

#include "ai/ai_talk_controller.h"
#include "ai/azure_tts.h"
#include "ai/mining_task.h"
#include "behavior/stackchan_behavior.h"
#include "config/config.h"
#include "core/public/app_runtime.h"
#include "core/orchestrator.h"
#include "core/public/serial_setup.h"
#include "core/public/tts_coordinator.h"
#include "ui/ui_mining_core2.h"
#include "utils/app_types.h"
#include "utils/logging.h"
// Azure TTS
static AzureTts g_tts;
static StackchanBehavior g_behavior;
static Orchestrator g_orch;
static AiTalkController g_ai;
static const uint8_t  kDisplayActiveBrightness = 128;
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
  const uint32_t reqMhz = mcCfgCpuMhz();
  setCpuFrequencyMhz((int)reqMhz);
  mc_logf("[MAIN] cpu_mhz=%d (req=%lu)", getCpuFrequencyMhz(), (unsigned long)reqMhz);
  auto cfgM5 = M5.config();
  cfgM5.output_power  = true;
  cfgM5.clear_display = true;
  cfgM5.internal_imu = false;
  cfgM5.internal_mic = true;
  cfgM5.internal_spk = true;
  cfgM5.internal_rtc = true;
  mc_logf("[MAIN] call M5.begin()");
  M5.begin(cfgM5);
  mc_logf("[MAIN] M5.begin() done");
  M5.Speaker.setVolume(mcCfgSpkVolume());
  mc_logf("[MAIN] spk_volume=%u", (unsigned)mcCfgSpkVolume());
  const auto& cfg = appConfig();
  g_tts.begin();
  AppRuntimeContext runtimeCtx;
  runtimeCtx.ai_ = &g_ai;
  runtimeCtx.tts_ = &g_tts;
  runtimeCtx.orch_ = &g_orch;
  runtimeCtx.behavior_ = &g_behavior;
  appRuntimeInit(runtimeCtx);
  SerialSetupContext serialCtx;
  serialCtx.tts_ = &g_tts;
  serialCtx.displaySleepTimeoutMs_ = appRuntimeDisplaySleepTimeoutMsPtr();
  serialSetupInit(serialCtx);
  TtsCoordinatorContext ttsCtx;
  ttsCtx.tts_ = &g_tts;
  ttsCtx.orch_ = &g_orch;
  ttsCtx.ai_ = &g_ai;
  ttsCtx.behavior_ = &g_behavior;
  ttsCtx.attentionActive_ = appRuntimeAttentionActivePtr();
  ttsCtx.bubbleClearFn_ = appRuntimeBubbleClearFn();
  ttsCtx.mode_ = appRuntimeModePtr();
  ttsCoordinatorInit(ttsCtx);
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
  mc_logf("%s %s booting...", cfg.appName_, cfg.appVersion_);
  startMiner();
}
void loop() {
  M5.update();
  // Web setup serial commands
  pollSetupSerial();
  const uint32_t now = (uint32_t)millis();
  appRuntimeTick(now);
  delay(2);
}
