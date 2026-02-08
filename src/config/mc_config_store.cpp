// src/mc_config_store.cpp
// Module implementation.
#include "mc_config_store.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

#include "config.h"
#include "utils/logging.h"
#ifndef MC_WIFI_SSID
  #define MC_WIFI_SSID ""
#endif
#ifndef MC_WIFI_PASS
  #define MC_WIFI_PASS ""
#endif
#ifndef MC_DUCO_USER
  #define MC_DUCO_USER ""
#endif
#ifndef MC_DUCO_MINER_KEY
  #define MC_DUCO_MINER_KEY "None"
#endif
#ifndef MC_AZ_SPEECH_REGION
  #define MC_AZ_SPEECH_REGION ""
#endif
#ifndef MC_AZ_SPEECH_KEY
  #define MC_AZ_SPEECH_KEY ""
#endif
#ifndef MC_AZ_CUSTOM_SUBDOMAIN
  #define MC_AZ_CUSTOM_SUBDOMAIN ""
#endif
namespace {
static const char* kCfgPath = "/mc_config.json";
struct RuntimeCfg {
  String wifiSsid_;
  String wifiPass_;
  String ducoUser_;
  String ducoKey_;
  String azRegion_;
  String azKey_;
  String azVoice_;
  String azEndpoint_;
  String openAiKey_;
  uint16_t cpuMhz_ = (uint16_t)MC_CPU_FREQ_MHZ; // 80/160/240
  uint32_t displaySleepS_ = MC_DISPLAY_SLEEP_SECONDS;
  String attentionText_;
  uint8_t spkVolume_ = (uint8_t)MC_SPK_VOLUME; // 0-255
  String speechShareAccepted_;
  String speechHello_;
};
static RuntimeCfg g_rt;
static bool g_loaded = false;
static bool g_dirty  = false;
static bool isAllQuestionMarks_(const String& s) {
  if (!s.length()) return false;
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c != '?' && c != ' ') return false;
  }
  return true;
}
static void applyDefaults_() {
  g_rt.wifiSsid_ = MC_WIFI_SSID;
  g_rt.wifiPass_ = MC_WIFI_PASS;
  g_rt.ducoUser_ = MC_DUCO_USER;
  g_rt.ducoKey_  = MC_DUCO_MINER_KEY;
  g_rt.azRegion_ = MC_AZ_SPEECH_REGION;
  g_rt.azKey_    = MC_AZ_SPEECH_KEY;
  g_rt.azVoice_  = MC_AZ_TTS_VOICE;
  g_rt.azEndpoint_ = MC_AZ_CUSTOM_SUBDOMAIN;
  g_rt.openAiKey_ = MC_OPENAI_API_KEY;
  g_rt.cpuMhz_     = (uint16_t)MC_CPU_FREQ_MHZ;
  g_rt.displaySleepS_ = (uint32_t)MC_DISPLAY_SLEEP_SECONDS;
  g_rt.attentionText_  = MC_ATTENTION_TEXT;
  g_rt.spkVolume_      = (uint8_t)MC_SPK_VOLUME;
  g_rt.speechShareAccepted_ = MC_SPEECH_SHARE_ACCEPTED;
  g_rt.speechHello_          = MC_SPEECH_HELLO;
}
static void loadOnce_() {
  if (g_loaded) return;
  g_loaded = true;
  applyDefaults_();
  if (!LittleFS.begin(true)) {
    MC_LOGE("CFG", "LittleFS.begin failed (format attempted)");
    return;
  }
  if (!LittleFS.exists(kCfgPath)) {
    MC_LOGI("CFG", "%s not found -> defaults", kCfgPath);
    return;
  }
  File f = LittleFS.open(kCfgPath, "r");
  if (!f) {
    MC_LOGE("CFG", "open failed: %s", kCfgPath);
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    MC_LOGE("CFG", "JSON parse failed: %s", err.c_str());
    return;
  }
  auto setStr = [&](const char* key, String& dst) {
    JsonVariant v = doc[key];
    if (!v.isNull()) dst = v.as<String>();
  };
  auto setU32 = [&](const char* key, uint32_t& dst) {
    JsonVariant v = doc[key];
    if (!v.isNull()) dst = v.as<uint32_t>();
  };
  auto setU8 = [&](const char* key, uint8_t& dst) {
    JsonVariant v = doc[key];
    if (v.isNull()) return;
    int n = v.as<int>();
    if (n < 0) n = 0;
    if (n > 255) n = 255;
    dst = (uint8_t)n;
  };
  auto setCpuMhz = [&](const char* key, uint16_t& dst) {
    JsonVariant v = doc[key];
    if (v.isNull()) return;
    int mhz = v.as<int>();
    if (!(mhz == 80 || mhz == 160 || mhz == 240)) return;
    dst = (uint16_t)mhz;
  };
  setStr("wifi_ssid", g_rt.wifiSsid_);
  setStr("wifi_pass", g_rt.wifiPass_);
  setStr("duco_user", g_rt.ducoUser_);
  if (!doc["duco_key"].isNull()) {
    setStr("duco_key", g_rt.ducoKey_);
  } else {
    setStr("duco_miner_key", g_rt.ducoKey_);
  }
  if (!doc["az_region"].isNull()) setStr("az_region", g_rt.azRegion_);
  else                            setStr("az_speech_region", g_rt.azRegion_);
  if (!doc["az_key"].isNull()) setStr("az_key", g_rt.azKey_);
  else                         setStr("az_speech_key", g_rt.azKey_);
  if (!doc["az_voice"].isNull()) setStr("az_voice", g_rt.azVoice_);
  else                           setStr("az_tts_voice", g_rt.azVoice_);
  if (!doc["az_endpoint"].isNull()) setStr("az_endpoint", g_rt.azEndpoint_);
  else                              setStr("az_custom_subdomain", g_rt.azEndpoint_);
  setStr("openai_key", g_rt.openAiKey_);
  if (!doc["cpu_mhz"].isNull()) {
    setCpuMhz("cpu_mhz", g_rt.cpuMhz_);
  } else {
    if (!doc["cpu_freq_mhz"].isNull()) {
      MC_LOGW("CFG", "deprecated key ignored: cpu_freq_mhz");
    }
  }
  setU32("display_sleep_s", g_rt.displaySleepS_);
  setStr("attention_text",  g_rt.attentionText_);
  setU8("spk_volume",       g_rt.spkVolume_);
  setStr("share_accepted_text", g_rt.speechShareAccepted_);
  setStr("hello_text",          g_rt.speechHello_);
  if (isAllQuestionMarks_(g_rt.speechShareAccepted_)) {
    g_rt.speechShareAccepted_ = MC_SPEECH_SHARE_ACCEPTED;
  }
  if (isAllQuestionMarks_(g_rt.speechHello_)) {
    g_rt.speechHello_ = MC_SPEECH_HELLO;
  }
  MC_LOGI("CFG", "loaded %s", kCfgPath);
}
} // namespace
void mcConfigBegin() {
  loadOnce_();
}
bool mcConfigSetKV(const String& key, const String& value, String& err) {
  loadOnce_();
  err = "";
  auto setDirty = [&] { g_dirty = true; };
  if (key == "wifi_ssid") { g_rt.wifiSsid_ = value; setDirty(); return true; }
  if (key == "wifi_pass") { g_rt.wifiPass_ = value; setDirty(); return true; }
  if (key == "duco_user") { g_rt.ducoUser_ = value; setDirty(); return true; }
  if (key == "duco_miner_key") { g_rt.ducoKey_ = value; setDirty(); return true; }
  if (key == "az_speech_region") { g_rt.azRegion_ = value; setDirty(); return true; }
  if (key == "az_speech_key")    { g_rt.azKey_    = value; setDirty(); return true; }
  if (key == "az_tts_voice")     { g_rt.azVoice_  = value; setDirty(); return true; }
  if (key == "az_custom_subdomain") {
    g_rt.azEndpoint_ = value;
    setDirty();
    return true;
  }
  if (key == "openai_key") { g_rt.openAiKey_ = value; setDirty(); return true; }
  if (key == "cpu_mhz") {
    char* endp = nullptr;
    long v = strtol(value.c_str(), &endp, 10);
    if (endp == value.c_str() || !(v == 80 || v == 160 || v == 240)) {
      err = "range(80|160|240)";
      return false;
    }
    g_rt.cpuMhz_ = (uint16_t)v;
    setDirty();
    return true;
  }
  if (key == "cpu_freq_mhz") {
    err = "deprecated_key";
    return false;
  }
  if (key == "display_sleep_s") {
    char* endp = nullptr;
    long v = strtol(value.c_str(), &endp, 10);
    if (endp == value.c_str() || v < 0) {
      err = "invalid_number";
      return false;
    }
    g_rt.displaySleepS_ = (uint32_t)v;
    setDirty();
    return true;
  }
  if (key == "attention_text") {
    g_rt.attentionText_ = value;
    setDirty();
    return true;
  }
  if (key == "spk_volume") {
    char* endp = nullptr;
    long v = strtol(value.c_str(), &endp, 10);
    if (endp == value.c_str() || v < 0 || v > 255) {
      err = "range(0-255)";
      return false;
    }
    g_rt.spkVolume_ = (uint8_t)v;
    setDirty();
    return true;
  }
  if (key == "share_accepted_text") {
    g_rt.speechShareAccepted_ = value;
    setDirty();
    return true;
  }
  if (key == "hello_text") {
    g_rt.speechHello_ = value;
    setDirty();
    return true;
  }
  err = "unknown_key";
  return false;
}
bool mcConfigSave(String& err) {
  loadOnce_();
  err = "";
  if (!LittleFS.begin(true)) {
    err = "fs_begin_failed";
    return false;
  }
  JsonDocument doc;
  doc["wifi_ssid"] = g_rt.wifiSsid_;
  doc["wifi_pass"] = g_rt.wifiPass_;
  doc["duco_user"] = g_rt.ducoUser_;
  doc["duco_key"]  = g_rt.ducoKey_;
  doc["az_region"] = g_rt.azRegion_;
  doc["az_key"]    = g_rt.azKey_;
  doc["az_voice"]  = g_rt.azVoice_;
  doc["az_endpoint"] = g_rt.azEndpoint_;
  doc["openai_key"] = g_rt.openAiKey_;
  doc["cpu_mhz"]      = g_rt.cpuMhz_;
  doc["display_sleep_s"] = g_rt.displaySleepS_;
  doc["attention_text"]  = g_rt.attentionText_;
  doc["spk_volume"]      = g_rt.spkVolume_;
  doc["share_accepted_text"] = g_rt.speechShareAccepted_;
  doc["hello_text"]          = g_rt.speechHello_;
  File f = LittleFS.open(kCfgPath, "w");
  if (!f) {
    err = "open_failed";
    return false;
  }
  if (serializeJson(doc, f) == 0) {
    f.close();
    err = "serialize_failed";
    return false;
  }
  f.close();
  g_dirty = false;
  MC_LOGI("CFG", "saved %s", kCfgPath);
  return true;
}
String mcConfigGetMaskedJson() {
  loadOnce_();
  JsonDocument doc;
  const bool wifiPassSet = g_rt.wifiPass_.length() > 0;
  const bool ducoKeySet  = (g_rt.ducoKey_.length() > 0) && (g_rt.ducoKey_ != "None");
  const bool azKeySet    = g_rt.azKey_.length() > 0;
  const bool openAiKeySet = g_rt.openAiKey_.length() > 0;
  doc["wifi_ssid"] = g_rt.wifiSsid_;
  doc["wifi_pass"] = "***";
  doc["wifi_pass_set"] = wifiPassSet;
  doc["duco_user"] = g_rt.ducoUser_;
  doc["duco_key"] = "***";
  doc["duco_key_set"] = ducoKeySet;
  doc["duco_miner_key"] = "***";
  doc["duco_miner_key_set"] = ducoKeySet;
  doc["az_region"] = g_rt.azRegion_;
  doc["az_key"]    = "***";
  doc["az_key_set"] = azKeySet;
  doc["az_voice"]  = g_rt.azVoice_;
  doc["az_endpoint"] = g_rt.azEndpoint_;
  doc["az_speech_region"] = g_rt.azRegion_;
  doc["az_speech_key"]    = "***";
  doc["az_speech_key_set"] = azKeySet;
  doc["az_tts_voice"]     = g_rt.azVoice_;
  doc["az_custom_subdomain"] = g_rt.azEndpoint_;
  doc["openai_key"] = "***";
  doc["openai_key_set"] = openAiKeySet;
  doc["cpu_mhz"] = g_rt.cpuMhz_;
  doc["display_sleep_s"] = g_rt.displaySleepS_;
  doc["attention_text"]  = g_rt.attentionText_;
  doc["spk_volume"]      = g_rt.spkVolume_;
  doc["share_accepted_text"] = g_rt.speechShareAccepted_;
  doc["hello_text"]          = g_rt.speechHello_;
  String out;
  serializeJson(doc, out);
  return out;
}
// ---- getters ----
const char* mcCfgWifiSsid() { loadOnce_(); return g_rt.wifiSsid_.c_str(); }
const char* mcCfgWifiPass() { loadOnce_(); return g_rt.wifiPass_.c_str(); }
const char* mcCfgDucoUser() { loadOnce_(); return g_rt.ducoUser_.c_str(); }
const char* mcCfgDucoKey()  { loadOnce_(); return g_rt.ducoKey_.c_str();  }
const char* mcCfgAzRegion() { loadOnce_(); return g_rt.azRegion_.c_str(); }
const char* mcCfgAzKey()    { loadOnce_(); return g_rt.azKey_.c_str();    }
const char* mcCfgAzVoice()  { loadOnce_(); return g_rt.azVoice_.c_str();  }
const char* mcCfgAzEndpoint() { loadOnce_(); return g_rt.azEndpoint_.c_str(); }
const char* mcCfgOpenAiKey() { loadOnce_(); return g_rt.openAiKey_.c_str(); }
const char* mcCfgAttentionText() { loadOnce_(); return g_rt.attentionText_.c_str(); }
uint8_t mcCfgSpkVolume()         { loadOnce_(); return g_rt.spkVolume_; }
const char* mcCfgShareAcceptedText() { loadOnce_(); return g_rt.speechShareAccepted_.c_str(); }
const char* mcCfgHelloText()         { loadOnce_(); return g_rt.speechHello_.c_str(); }
uint32_t mcCfgCpuMhz() { loadOnce_(); return (uint32_t)g_rt.cpuMhz_; }
