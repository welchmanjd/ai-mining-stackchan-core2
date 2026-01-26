#include "azure_stt.h"
#include "logging.h"
#include "mc_config_store.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace AzureStt {

static const char* kTag = "STT";

static String normalizeSpeechHost_(String host) {
  host.trim();
  if (host.length() == 0) return "";

  // "https://foo" みたいなのが来ても剥がす
  if (host.startsWith("https://")) host = host.substring(strlen("https://"));
  if (host.startsWith("http://"))  host = host.substring(strlen("http://"));

  // path が入ってたら落とす
  int slash = host.indexOf('/');
  if (slash >= 0) host = host.substring(0, slash);

  // 末尾ドット
  while (host.endsWith(".")) host.remove(host.length() - 1);

  // 明らかにTTS用っぽいのは避ける（custom subdomain名に "tts" が入ってるケース）
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
// Arduino String に1バイトずつ append すると 0x00 が落ちる/壊れる経路があり得る。
// WAVヘッダは 0x00 を多用するため、バイナリは malloc したバッファで作る。
struct WavBuf {
  uint8_t* data = nullptr;
  size_t len = 0;
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
  if (b.data) {
    free(b.data);
    b.data = nullptr;
  }
  b.len = 0;
}

static bool makeWav_(const int16_t* pcm, size_t samples, uint32_t sampleRate, WavBuf& out) {
  freeWav_(out);
  if (!pcm || samples == 0) return false;

  const uint32_t dataBytes = (uint32_t)(samples * sizeof(int16_t));
  out.len = 44 + (size_t)dataBytes;
  out.data = (uint8_t*)malloc(out.len);
  if (!out.data) {
    out.len = 0;
    return false;
  }
  memset(out.data, 0, out.len);

  uint8_t* h = out.data;

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
    r.ok = false;
    r.err = "Wi-Fiがつながってないよ";
    r.status = -10;
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

  String host = normalizeSpeechHost_(mcCfgAzEndpoint());

  if (region.length() == 0 || key.length() == 0) {
    r.ok = false;
    r.err = "Azure設定がないよ";
    r.status = -11;
    MC_LOGE("STT", "missing region/key");
    return r;
  }

  // host が空なら region の標準STTホストへ
  if (host.length() == 0) {
    host = region + ".stt.speech.microsoft.com";
  }

  // endpoint URL
  String url = "https://" + host
    + "/speech/recognition/conversation/cognitiveservices/v1?language=" + (lang.length() ? lang : "ja-JP");

  // WAV化（バイナリバッファ）
  WavBuf wav;
  if (!makeWav_(pcm, samples, (uint32_t)sampleRate, wav)) {
    r.ok = false;
    r.err = "音声が空だよ";
    r.status = -12;
    MC_LOGE("STT", "makeWav failed samples=%u", (unsigned)samples);
    return r;
  }

  WiFiClientSecure client;
  client.setInsecure(); // CA固定は後回し

  HTTPClient https;
  https.setTimeout((int)timeoutMs);
#if defined(HTTPCLIENT_DEFAULT_TCP_TIMEOUT)
  https.setConnectTimeout((int)timeoutMs);
#endif
  https.setReuse(false);

  // STT は 1回/対話 なので L1 で流れが追えるようにする
  MC_LOGI("STT", "POST %s (bytes=%u, timeout=%lums)",
          host.c_str(), (unsigned)wav.len, (unsigned long)timeoutMs);

  if (!https.begin(client, url)) {
    freeWav_(wav);
    r.ok = false;
    r.err = "STT接続に失敗";
    r.status = -20;
    MC_LOGE("STT", "https.begin failed");
    return r;
  }

  // headers
  https.addHeader("Ocp-Apim-Subscription-Key", key);

  String ct = "audio/wav; codecs=audio/pcm; samplerate=" + String(sampleRate);
  https.addHeader("Content-Type", ct);

  const uint32_t t0 = millis();
  const int httpCode = https.POST(wav.data, wav.len);
  const uint32_t took = millis() - t0;

  freeWav_(wav);

  r.status = httpCode;

  if (httpCode <= 0) {
    r.ok = false;
    r.err = "STT通信エラー";
    MC_LOGE("STT", "http fail code=%d err=%s", httpCode, https.errorToString(httpCode).c_str());
    https.end();
    return r;
  }

  String body = https.getString();
  https.end();

  MC_LOGI("STT", "done http=%d took=%lums body_len=%u",
          httpCode, (unsigned long)took, (unsigned)body.length());

  // 成功は 200。認識できない場合も 200 で空文字が来ることがある
  if (httpCode != 200) {
    r.ok = false;
    r.err = "STT失敗";
    // レスポンス本文は長いことがあるので、ログは短く
    String head = body;
    if (head.length() > 120) head = head.substring(0, 120);
    MC_LOGD("STT", "http=%d body_head=%s", httpCode, head.c_str());
    return r;
  }

  // JSON parse（ArduinoJson v7: StaticJsonDocument は非推奨）
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);

  if (e) {
    r.ok = false;
    r.err = "STT解析失敗";
    String head = body;
    if (head.length() > 120) head = head.substring(0, 120);
    MC_LOGE("STT", "json parse fail: %s body_head=%s", e.c_str(), head.c_str());
    return r;
  }

  const char* recStatus   = doc["RecognitionStatus"] | "";
  const char* displayText = doc["DisplayText"] | "";

  if (!displayText || !displayText[0]) {
    r.ok = false;
    r.err = "うまく聞き取れなかったよ";

    // 本文は長いので先頭だけ（秘密は含まれない想定）
    String head = body;
    if (head.length() > 160) head = head.substring(0, 160);

    MC_LOGD("STT", "no text (status=%s) http=%d body_head=%s",
            (recStatus && recStatus[0]) ? recStatus : "?",
            httpCode,
            head.c_str());
    return r;
  }

  r.ok = true;
  r.text = String(displayText);

  // ログは先頭だけ（全文禁止）
  String head = r.text;
  if (head.length() > 60) head = head.substring(0, 60);
  MC_LOGI("STT", "ok text_len=%u head=\"%s\"", (unsigned)r.text.length(), head.c_str());

  return r;
}


} // namespace AzureStt
