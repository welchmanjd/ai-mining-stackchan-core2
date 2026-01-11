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
static bool makeWav_(const int16_t* pcm, size_t samples, uint32_t sampleRate, String& out) {
  if (!pcm || samples == 0) return false;

  const uint32_t dataBytes = (uint32_t)(samples * sizeof(int16_t));
  const uint32_t riffSize  = 36 + dataBytes;

  out = "";
  out.reserve(44 + dataBytes);

  auto pushU32 = [&](uint32_t v) {
    out += (char)(v & 0xFF);
    out += (char)((v >> 8) & 0xFF);
    out += (char)((v >> 16) & 0xFF);
    out += (char)((v >> 24) & 0xFF);
  };
  auto pushU16 = [&](uint16_t v) {
    out += (char)(v & 0xFF);
    out += (char)((v >> 8) & 0xFF);
  };

  out += "RIFF"; pushU32(riffSize);
  out += "WAVE";
  out += "fmt "; pushU32(16);      // PCM fmt chunk size
  pushU16(1);                      // audio format PCM
  pushU16(1);                      // channels mono
  pushU32(sampleRate);             // sample rate
  pushU32(sampleRate * 2);         // byte rate (16bit mono)
  pushU16(2);                      // block align
  pushU16(16);                     // bits per sample
  out += "data"; pushU32(dataBytes);

  // PCM little-endian（Stringは write() を持たないので concat(ptr,len) を使う）
  // これなら 0x00 を含むバイナリも長さ指定で安全に追加できる
  out.concat((const char*)pcm, dataBytes);
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
    mc_logf("[%s] wifi not connected", kTag);
    return r;
  }

  const String region = mcCfgAzRegion();
  const String key    = mcCfgAzKey();
  const String lang =
    #ifdef MC_AZ_STT_LANGUAGE
        String(MC_AZ_STT_LANGUAGE);
    #else
        String("ja-JP");
    #endif

  String host         = normalizeSpeechHost_(mcCfgAzEndpoint());

  if (region.length() == 0 || key.length() == 0) {
    r.ok = false;
    r.err = "Azure設定がないよ";
    r.status = -11;
    mc_logf("[%s] missing region/key", kTag);
    return r;
  }

  // host が空なら region の標準STTホストへ
  if (host.length() == 0) {
    host = region + ".stt.speech.microsoft.com";
  }

  // endpoint URL
  String url = "https://" + host
    + "/speech/recognition/conversation/cognitiveservices/v1?language=" + (lang.length() ? lang : "ja-JP");

  // WAV化
  String wav;
  if (!makeWav_(pcm, samples, (uint32_t)sampleRate, wav)) {
    r.ok = false;
    r.err = "音声が空だよ";
    r.status = -12;
    mc_logf("[%s] makeWav failed samples=%u", kTag, (unsigned)samples);
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

  mc_logf("[%s] POST %s (bytes=%u, timeout=%lums)",
          kTag, host.c_str(), (unsigned)wav.length(), (unsigned long)timeoutMs);

  if (!https.begin(client, url)) {
    r.ok = false;
    r.err = "STT接続に失敗";
    r.status = -20;
    mc_logf("[%s] https.begin failed", kTag);
    return r;
  }

  // headers
  https.addHeader("Ocp-Apim-Subscription-Key", key);

  String ct = "audio/wav; codecs=audio/pcm; samplerate=" + String(sampleRate);
  https.addHeader("Content-Type", ct);

  const int httpCode = https.POST((uint8_t*)wav.c_str(), wav.length());
  r.status = httpCode;

  if (httpCode <= 0) {
    r.ok = false;
    r.err = "STT通信エラー";
    mc_logf("[%s] http fail code=%d err=%s", kTag, httpCode, https.errorToString(httpCode).c_str());
    https.end();
    return r;
  }

  String body = https.getString();
  https.end();

  // 成功は 200。認識できない場合も 200 で空文字が来ることがある
  if (httpCode != 200) {
    r.ok = false;
    r.err = "STT失敗";
    // レスポンス本文は長いことがあるので、ログは短く
    String head = body;
    if (head.length() > 120) head = head.substring(0, 120);
    mc_logf("[%s] http=%d body_head=%s", kTag, httpCode, head.c_str());
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
    mc_logf("[%s] json parse fail: %s body_head=%s", kTag, e.c_str(), head.c_str());
    return r;
  }

  const char* displayText = doc["DisplayText"] | "";
  if (!displayText || !displayText[0]) {
    r.ok = false;
    r.err = "うまく聞き取れなかったよ";
    mc_logf("[%s] empty DisplayText", kTag);
    return r;
  }

  r.ok = true;
  r.text = String(displayText);

  // ログは先頭だけ（全文禁止）
  String head = r.text;
  if (head.length() > 60) head = head.substring(0, 60);
  mc_logf("[%s] ok text_len=%u head=\"%s\"", kTag, (unsigned)r.text.length(), head.c_str());

  return r;
}

} // namespace AzureStt
