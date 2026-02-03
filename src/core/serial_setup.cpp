// Module implementation.
#include "core/public/serial_setup.h"

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

#include "ai/azure_tts.h"
#include "config/config.h"
#include "config/mc_config_store.h"
#include "config/runtime_features.h"
#include "ui/ui_mining_core2.h"
#include "utils/logging.h"

static SerialSetupContext g_ctx;

// ===== Web setup serial commands (simple line protocol) =====
static char   g_setupLine[512];
static size_t g_setupLineLen = 0;

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
    if (!g_ctx.tts_) {
      Serial.println("@AZTEST NG tts_unavailable");
      return;
    }
    // Reload runtime Azure config so tests after SET/SAVE work without reboot.
    g_ctx.tts_->begin(mcCfgSpkVolume());
    bool ok = g_ctx.tts_->testCredentials();
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
        if (g_ctx.displaySleepTimeoutMs_) {
          if (sec > 0) {
            *g_ctx.displaySleepTimeoutMs_ = (uint32_t)sec * 1000UL;
          } else {
            *g_ctx.displaySleepTimeoutMs_ =
              (uint32_t)MC_DISPLAY_SLEEP_SECONDS * 1000UL;
          }
        }
        MC_LOGI("MAIN", "display_sleep_s set: %ld sec", sec);
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

void serialSetupInit(const SerialSetupContext& ctx) {
  g_ctx = ctx;
}

void pollSetupSerial() {
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
