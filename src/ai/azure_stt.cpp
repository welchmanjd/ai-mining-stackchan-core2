// Module implementation.
#include "ai/azure_stt.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config/mc_config_store.h"
#include "utils/logging.h"
namespace azure_stt {
static const char* kTag = "STT";
static String normalizeSpeechHost_(String host) {
  // Normalize host: strip scheme/path and reject obvious TTS endpoints.
  host.trim();
  if (host.length() == 0) return "";
  if (host.startsWith("https://")) host = host.substring(strlen("https://"));
  if (host.startsWith("http://"))  host = host.substring(strlen("http://"));
  int slash = host.indexOf('/');
  if (slash >= 0) host = host.substring(0, slash);
  while (host.endsWith(".")) host.remove(host.length() - 1);
  String lower = host;
  lower.toLowerCase();
  if (lower.indexOf("tts") >= 0) {
    return "";
  }
  return host;
}
// PCM16 mono -> WAV bytes (in memory)
//
// NOTE:
struct WavBuf {
  uint8_t* data_ = nullptr;
  size_t len_ = 0;
};
static void putLE16_(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void putLE32_(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void freeWav_(WavBuf& b) {
  if (b.data_) {
    free(b.data_);
    b.data_ = nullptr;
  }
  b.len_ = 0;
}
static bool makeWav_(const int16_t* pcm, size_t samples, uint32_t sampleRate, WavBuf& out) {
  // Build a minimal WAV buffer in memory for STT upload.
  freeWav_(out);
  if (!pcm || samples == 0) return false;
  const uint32_t dataBytes = (uint32_t)(samples * sizeof(int16_t));
  out.len_ = 44 + (size_t)dataBytes;
  out.data_ = (uint8_t*)malloc(out.len_);
  if (!out.data_) {
    out.len_ = 0;
    return false;
  }
  memset(out.data_, 0, out.len_);
  uint8_t* h = out.data_;
  // RIFF header
  memcpy(h + 0, "RIFF", 4);
  putLE32_(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVE", 4);
  // fmt chunk
  memcpy(h + 12, "fmt ", 4);
  putLE32_(h + 16, 16);        // fmt chunk size
  putLE16_(h + 20, 1);         // PCM
  putLE16_(h + 22, 1);         // mono
  putLE32_(h + 24, sampleRate);
  putLE32_(h + 28, sampleRate * 2); // byte rate (16bit mono)
  putLE16_(h + 32, 2);         // block align
  putLE16_(h + 34, 16);        // bits per sample
  // data chunk
  memcpy(h + 36, "data", 4);
  putLE32_(h + 40, dataBytes);
  // PCM payload (little-endian)
  memcpy(h + 44, (const uint8_t*)pcm, dataBytes);
  return true;
}
SttResult transcribePcm16Mono(
    const int16_t* pcm,
    size_t samples,
    int sampleRate,
    uint32_t timeoutMs) {
  SttResult r;
  if (!WiFi.isConnected()) {
    // Fast-fail if network is down.
    r.ok_ = false;
    r.err_ = "Wi-Fiがつながってないよ";
    r.status_ = -10;
    MC_EVT("STT", "fail stage=wifi");
    MC_LOGW("STT", "wifi not connected");
    return r;
  }
  const String region = mcCfgAzRegion();
  const String key    = mcCfgAzKey();
#ifdef MC_AZ_STT_LANGUAGE
  const String lang = String(MC_AZ_STT_LANGUAGE);
#else
  const String lang = String("ja-JP");
#endif
  String hostFromCfg = normalizeSpeechHost_(mcCfgAzEndpoint());
  bool useCustomHost = (hostFromCfg.length() > 0);
  if (region.length() == 0 || key.length() == 0) {
    r.ok_ = false;
    r.err_ = "Azure設定がないよ";
    r.status_ = -11;
    MC_EVT("STT", "fail stage=config");
    MC_LOGE("STT", "missing region/key");
    return r;
  }
  String host = hostFromCfg;
  if (host.length() == 0) {
    host = region + ".stt.speech.microsoft.com";
  }
  String url = "https://" + host
    + "/speech/recognition/conversation/cognitiveservices/v1?language=" + (lang.length() ? lang : "ja-JP");
  WavBuf wav;
  if (!makeWav_(pcm, samples, (uint32_t)sampleRate, wav)) {
    r.ok_ = false;
    r.err_ = "音声が空だよ";
    r.status_ = -12;
    MC_EVT("STT", "fail stage=wav samples=%u", (unsigned)samples);
    MC_LOGE("STT", "makeWav failed samples=%u", (unsigned)samples);
    return r;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout((int)timeoutMs);
#if defined(HTTPCLIENT_DEFAULT_TCP_TIMEOUT)
  https.setConnectTimeout((int)timeoutMs);
#endif
  https.setReuse(false);
  MC_EVT_D("STT", "start custom=%d bytes=%u timeout=%lums",
           useCustomHost ? 1 : 0, (unsigned)wav.len_, (unsigned long)timeoutMs);
  if (!https.begin(client, url)) {
    freeWav_(wav);
    r.ok_ = false;
    r.err_ = "STT接続に失敗";
    r.status_ = -20;
    MC_EVT("STT", "fail stage=begin");
    MC_LOGE("STT", "https.begin failed");
    return r;
  }
  https.addHeader("Ocp-Apim-Subscription-Key", key);
  String ct = "audio/wav; codecs=audio/pcm; samplerate=" + String(sampleRate);
  https.addHeader("Content-Type", ct);
  const uint32_t t0 = millis();
  const int httpCode = https.POST(wav.data_, wav.len_);
  const uint32_t took = millis() - t0;
  freeWav_(wav);
  r.status_ = httpCode;
  if (httpCode <= 0) {
    r.ok_ = false;
    r.err_ = "STT通信エラー";
    MC_EVT("STT", "fail stage=http_post code=%d took=%lums",
           httpCode, (unsigned long)took);
    MC_LOGD("STT", "http fail code=%d err=%s",
            httpCode, https.errorToString(httpCode).c_str());
    https.end();
    return r;
  }
  String body = https.getString();
  const uint32_t bodyLen = (uint32_t)body.length();
  https.end();
  if (httpCode != 200) {
    r.ok_ = false;
    r.err_ = "STT失敗";
    MC_EVT("STT", "fail stage=http status=%d took=%lums body_len=%lu",
           httpCode, (unsigned long)took, (unsigned long)bodyLen);
    MC_LOGD("STT", "http=%d took=%lums body_len=%lu",
            httpCode, (unsigned long)took, (unsigned long)bodyLen);
    return r;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    r.ok_ = false;
    r.err_ = "STT解析失敗";
    MC_EVT("STT", "fail stage=json_parse took=%lums body_len=%lu",
           (unsigned long)took, (unsigned long)bodyLen);
    MC_LOGD("STT", "json parse fail: %s body_len=%lu",
            e.c_str(), (unsigned long)bodyLen);
    return r;
  }
  const char* recStatus   = doc["RecognitionStatus"] | "";
  const char* displayText = doc["DisplayText"] | "";
  if (!displayText || !displayText[0]) {
    r.ok_ = false;
    r.err_ = "うまく聞き取れなかったよ";
    MC_EVT("STT", "fail stage=no_text status=%s took=%lums",
           (recStatus && recStatus[0]) ? recStatus : "-",
           (unsigned long)took);
    MC_LOGD("STT", "no text (status=%s) http=%d took=%lums",
            (recStatus && recStatus[0]) ? recStatus : "?",
            httpCode,
            (unsigned long)took);
    return r;
  }
  r.ok_ = true;
  r.text_ = String(displayText);
  MC_EVT_D("STT", "done http=%d took=%lums text_len=%u",
           httpCode, (unsigned long)took, (unsigned)r.text_.length());
  return r;
}
} // namespace azure_stt
