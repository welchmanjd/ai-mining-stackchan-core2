// src/azure_tts.cpp
#include "ai/azure_tts.h"
#include "config/mc_config_store.h"
#include "core/logging.h"
#include <string.h>

#include <M5Unified.h>
#include <WiFi.h>
#include "audio/i2s_manager.h"

// TTS debug switch (optional): define -DTTS_DEBUG_ENABLED=1 to restore very chatty logs.
#ifndef TTS_DEBUG_ENABLED
#define TTS_DEBUG_ENABLED 0
#endif

// ---------- helpers ----------
static String trimCopy_(const String& s) {
  String t = s;
  t.trim();
  return t;
}

static bool readLineCRLF_(WiFiClient* s, String* out, uint32_t idleTimeoutMs) {
  out->remove(0);
  uint32_t t0 = millis();
  while (true) {
    while (s->available()) {
      char c = (char)s->read();
      if (c == '\r') continue;
      if (c == '\n') return true;
      out->concat(c);
      if (out->length() > 64) return false; // too long
    }
    if (!s->connected()) return false;
    if (millis() - t0 > idleTimeoutMs) return false;
    delay(1);
  }
}

static bool readExact_(WiFiClient* s, uint8_t* dst, size_t n, uint32_t idleTimeoutMs) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    int a = s->available();
    if (a <= 0) {
      if (!s->connected()) return false;
      if (millis() - t0 > idleTimeoutMs) return false;
      delay(1);
      continue;
    }
    t0 = millis();
    int r = s->readBytes(dst + got, (size_t)min<int>(a, (int)(n - got)));
    if (r <= 0) return false;
    got += (size_t)r;
  }
  return true;
}

static bool readChunkedBody_(WiFiClient* s, uint8_t** outBuf, size_t* outLen, uint32_t idleTimeoutMs) {
  *outBuf = nullptr;
  *outLen = 0;

  const size_t kCapMax = 512 * 1024;
  size_t cap = 8192;
  size_t used = 0;
  uint8_t* buf = (uint8_t*)malloc(cap);
  if (!buf) return false;

  while (true) {
    String line;
    if (!readLineCRLF_(s, &line, idleTimeoutMs)) { free(buf); return false; }
    line.trim();
    if (!line.length()) continue; // skip empty lines

    // chunk-size (hex) may have extensions: "1a;foo=bar"
    int semi = line.indexOf(';');
    if (semi >= 0) line = line.substring(0, semi);

    char* endp = nullptr;
    unsigned long chunk = strtoul(line.c_str(), &endp, 16);
    if (!endp || endp == line.c_str()) { free(buf); return false; }

    if (chunk == 0) {
      // consume trailing headers (optional) until empty line
      // (Azure usually ends soon; safe to just read one line if present)
      // We'll try to read one line; ignore failures.
      String tail;
      (void)readLineCRLF_(s, &tail, 50);
      break;
    }

    if (used + chunk > kCapMax) { free(buf); return false; }
    while (used + chunk > cap) {
      size_t ncap = cap * 2;
      if (ncap > kCapMax) { free(buf); return false; }
      uint8_t* nb = (uint8_t*)realloc(buf, ncap);
      if (!nb) { free(buf); return false; }
      buf = nb;
      cap = ncap;
    }

    if (!readExact_(s, buf + used, (size_t)chunk, idleTimeoutMs)) { free(buf); return false; }
    used += (size_t)chunk;

    // chunk terminator CRLF
    char crlf[2];
    if (!readExact_(s, (uint8_t*)crlf, 2, idleTimeoutMs)) { free(buf); return false; }
    // tolerate if not CRLF
  }

  *outBuf = buf;
  *outLen = used;
  return used > 0;
}

// ---------- chunked "salvage" (when chunk markers leak into body) ----------
static bool isHexDigit_(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

// Detect pattern like: "10000\r\nRIFF...." at the very beginning
static bool looksLikeChunkedLeak_(const uint8_t* buf, size_t len) {
  if (!buf || len < 10) return false;

  // need at least: "1\r\nRIFF" (but typically "10000\r\nRIFF")
  // Scan first line until \n within a small window.
  size_t i = 0;
  size_t maxScan = (len < 32) ? len : 32;

  // first char must be hex
  if (!isHexDigit_((char)buf[0])) return false;

  // read until LF
  for (; i < maxScan; i++) {
    char c = (char)buf[i];
    if (c == '\n') break;
    // allow \r, hex digits, ';' extensions, spaces/tabs (tolerant)
    if (c == '\r') continue;
    if (c == ';' || c == ' ' || c == '\t') continue;
    if (!isHexDigit_(c)) return false;
  }
  if (i >= maxScan) return false;          // no LF soon -> unlikely chunked leak
  size_t lfPos = i;

  // after LF, the payload should start (often RIFF)
  size_t payloadPos = lfPos + 1;
  if (payloadPos + 4 > len) return false;

  // Many times it's RIFF right away
  if (memcmp(buf + payloadPos, "RIFF", 4) == 0) return true;

  // or sometimes there is an extra CRLF; tolerate one empty line
  if (payloadPos + 2 < len && buf[payloadPos] == '\r' && buf[payloadPos + 1] == '\n') {
    payloadPos += 2;
    if (payloadPos + 4 <= len && memcmp(buf + payloadPos, "RIFF", 4) == 0) return true;
  }

  return false;
}

static bool dechunkMemory_(const uint8_t* in, size_t inLen, uint8_t** outBuf, size_t* outLen) {
  if (!outBuf || !outLen) return false;
  *outBuf = nullptr;
  *outLen = 0;
  if (!in || inLen == 0) return false;

  const size_t kCapMax = 512 * 1024;
  size_t cap = 8192;
  size_t used = 0;
  uint8_t* buf = (uint8_t*)malloc(cap);
  if (!buf) return false;

  auto ensureCap = [&](size_t need) -> bool {
    if (need > kCapMax) return false;
    while (need > cap) {
      size_t ncap = cap * 2;
      if (ncap > kCapMax) return false;
      uint8_t* nb = (uint8_t*)realloc(buf, ncap);
      if (!nb) return false;
      buf = nb;
      cap = ncap;
    }
    return true;
  };

  size_t pos = 0;
  while (pos < inLen) {
    // read line until '\n'
    size_t lineStart = pos;
    size_t lineEnd = pos;
    while (lineEnd < inLen && in[lineEnd] != '\n') lineEnd++;
    if (lineEnd >= inLen) { free(buf); return false; } // no LF -> malformed

    // line is [lineStart, lineEnd] excluding LF; may include CR
    // Copy to temp string (small)
    char line[64];
    size_t L = lineEnd - lineStart;
    if (L >= sizeof(line)) { free(buf); return false; } // too long
    memcpy(line, in + lineStart, L);
    line[L] = 0;

    pos = lineEnd + 1; // skip LF

    // trim CR/spaces
    // remove trailing CR
    while (L > 0 && (line[L - 1] == '\r' || line[L - 1] == ' ' || line[L - 1] == '\t')) {
      line[--L] = 0;
    }
    // skip leading spaces
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    if (*p == 0) continue; // empty line -> ignore

    // cut chunk extensions
    char* semi = strchr(p, ';');
    if (semi) *semi = 0;

    // parse hex
    char* endp = nullptr;
    unsigned long chunk = strtoul(p, &endp, 16);
    if (!endp || endp == p) { free(buf); return false; }

    if (chunk == 0) {
      // chunked end. There may be trailing headers and an empty line.
      // We can just stop here.
      break;
    }

    if (pos + chunk > inLen) { free(buf); return false; }

    if (!ensureCap(used + (size_t)chunk)) { free(buf); return false; }
    memcpy(buf + used, in + pos, (size_t)chunk);
    used += (size_t)chunk;
    pos += (size_t)chunk;

    // skip CRLF after chunk payload if present
    if (pos < inLen && in[pos] == '\r') pos++;
    if (pos < inLen && in[pos] == '\n') pos++;
  }

  if (used == 0) { free(buf); return false; }

  *outBuf = buf;
  *outLen = used;
  return true;
}

static void logHeadBytes_(const uint8_t* buf, size_t len);

static uint32_t s_chunkedSalvageCount = 0;


// === src/azure_tts.cpp : replace whole function ===
static void salvageChunkedLeakIfNeeded_(uint8_t** pBuf, size_t* pLen) {
  if (!pBuf || !pLen) return;
  uint8_t* buf = *pBuf;
  size_t len = *pLen;
  if (!buf || len < 10) return;

  if (memcmp(buf, "RIFF", 4) == 0) return; // already OK
  if (!looksLikeChunkedLeak_(buf, len)) return;

  // 異常系なので L1(WARN) で残す。詳細は DIAG/TRACE に寄せる。
  MC_LOGW("TTS", "chunked markers leaked into body -> salvage");
  MC_LOGT("TTS", "chunked leak head dump follows");

  logHeadBytes_(buf, len);

  uint8_t* fixed = nullptr;
  size_t fixedLen = 0;
  if (dechunkMemory_(buf, len, &fixed, &fixedLen)) {
    s_chunkedSalvageCount++;
    MC_LOGD("TTS", "salvaged #%lu: %u -> %u bytes",
            (unsigned long)s_chunkedSalvageCount,
            (unsigned)len, (unsigned)fixedLen);

    free(buf);
    *pBuf = fixed;
    *pLen = fixedLen;

    logHeadBytes_(fixed, fixedLen);
  } else {
    MC_LOGW("TTS", "salvage failed (dechunkMemory_)");
  }
}



// 入力が
// - "my-speech-app"（サブドメインだけ）
// - "my-speech-app.cognitiveservices.azure.com"（host）
// - "https://my-speech-app.cognitiveservices.azure.com/..."（URL）
// のどれでもOKにして、host だけ返す
static String normalizeCustomHost_(const String& inRaw) {
  String s = trimCopy_(inRaw);

  // Web側のクリア指定用
  if (s == "-" || s.equalsIgnoreCase("none")) return "";

  if (!s.length()) return "";

  // scheme除去
  if (s.startsWith("https://")) s = s.substring(8);
  else if (s.startsWith("http://")) s = s.substring(7);

  // path除去
  int slash = s.indexOf('/');
  if (slash >= 0) s = s.substring(0, slash);

  s.trim();
  if (!s.length()) return "";

  // "xxx" だけなら ".cognitiveservices.azure.com" を補う
  if (s.indexOf('.') < 0) {
    s += ".cognitiveservices.azure.com";
  }
  return s;
}


// ---- WAV parser (PCM) ----
struct WavPcmInfo_ {
  const uint8_t* pcm = nullptr;
  size_t pcmBytes = 0;
  uint32_t sampleRate = 16000;
  uint16_t channels = 1;
  uint16_t bitsPerSample = 16;
};

static uint32_t rd32le_(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16le_(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool parseWavPcm_(const uint8_t* buf, size_t len, WavPcmInfo_* out) {
  if (!buf || len < 44 || !out) return false;

  // "RIFF" .... "WAVE"
  if (memcmp(buf, "RIFF", 4) != 0) return false;
  if (memcmp(buf + 8, "WAVE", 4) != 0) return false;

  bool hasFmt = false;
  bool hasData = false;

  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;

  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t* ch = buf + pos;
    uint32_t csize = rd32le_(ch + 4);
    pos += 8;
    if (pos + csize > len) break;

    if (memcmp(ch, "fmt ", 4) == 0 && csize >= 16) {
      audioFormat   = rd16le_(buf + pos + 0);
      channels      = rd16le_(buf + pos + 2);
      sampleRate    = rd32le_(buf + pos + 4);
      bitsPerSample = rd16le_(buf + pos + 14);
      hasFmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      out->pcm = buf + pos;
      out->pcmBytes = (size_t)csize;
      hasData = true;
    }

    // chunks are word-aligned
    pos += (size_t)csize;
    if (pos & 1) pos++;
  }

  if (!hasFmt || !hasData) return false;
  if (audioFormat != 1) return false;          // PCM only
  if (channels != 1) return false;             // mono only (for now)
  if (bitsPerSample != 16) return false;       // 16-bit only

  out->channels = channels;
  out->sampleRate = sampleRate ? sampleRate : 16000;
  out->bitsPerSample = bitsPerSample;
  return true;
}

// === src/azure_tts.cpp : replace whole function ===
static void logHeadBytes_(const uint8_t* buf, size_t len) {
  if (!buf || len == 0) return;

  // 壁紙化しうるので TRACE に寄せる
  char s[64];
  size_t n = (len < 12) ? len : 12;
  for (size_t i = 0; i < n; i++) {
    sprintf(&s[i * 3], "%02X ", buf[i]);
  }
  MC_LOGT("TTS", "head bytes: %s", s);

  if (len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
    MC_LOGT("TTS", "looks like MP3 (ID3)");
  }
  if (len >= 2 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) {
    MC_LOGT("TTS", "looks like MP3 frame sync (0xFFEx)");
  }
}



static bool isCustomEndpoint_(const String& endpoint) {
  return endpoint.indexOf(".cognitiveservices.azure.com/tts/") >= 0;
}


void AzureTts::begin(uint8_t volume) {
  cfg_ = RuntimeConfig{};
  keepaliveEnabled_ = true;

  // ★runtime config (LittleFS) → fallback (config_private)
  region_ = trimCopy_(mcCfgAzRegion());
  key_    = trimCopy_(mcCfgAzKey());
  defaultVoice_ = trimCopy_(mcCfgAzVoice());

  // 任意：custom host / endpoint
  customHost_ = normalizeCustomHost_(mcCfgAzEndpoint());

  if (customHost_.length()) {
    // custom endpoint
    endpoint_ = "https://" + customHost_ + "/tts/cognitiveservices/v1";
    // URL/hostはログに出さない（環境情報の壁紙化防止）
    MC_LOGD("TTS", "endpoint: custom (len=%u)", (unsigned)endpoint_.length());
  } else if (region_.length()) {
    // region endpoint
    endpoint_ = "https://" + region_ + ".tts.speech.microsoft.com/cognitiveservices/v1";
    MC_LOGD("TTS", "endpoint: region (len=%u)", (unsigned)endpoint_.length());
  } else {
    endpoint_ = "";
    MC_LOGD("TTS", "endpoint: (not set)");
  }

  // 設定詳細は DIAG（運用では不要）
  MC_LOGD("TTS", "azure key: %s", key_.length() ? "set" : "(not set)");
  MC_LOGD("TTS", "voice: %s", defaultVoice_.length() ? defaultVoice_.c_str() : "(not set)");
  MC_LOGD("TTS", "cfg lens: region=%u voice=%u key=%u endpoint=%u",
          (unsigned)region_.length(),
          (unsigned)defaultVoice_.length(),
          (unsigned)key_.length(),
          (unsigned)endpoint_.length());

  // audio
  defaultVolume_ = volume;
  M5.Speaker.setVolume(volume);

  // HTTPS
  client_.setInsecure();
  https_.setReuse(true);

  // token state
  token_ = "";
  tokenExpireMs_ = 0;
  tokenFailUntilMs_ = 0;
  tokenFailCount_ = 0;
  lastRequestMs_ = 0;

  dnsWarmed_ = false;
  sessionResetPending_ = false;

  // cancel state
  cancelMux_ = portMUX_INITIALIZER_UNLOCKED;
  cancelSpeakId_ = 0;
  cancelReason_[0] = 0;
}



bool AzureTts::isBusy() const {
  return state_ != Idle;
}


bool AzureTts::consumeDone(uint32_t* outId, bool* outOk, char* outReason, size_t outReasonLen) {
  uint32_t v = doneSpeakId_;
  if (!v) return false;

  doneSpeakId_ = 0;

  if (outId) *outId = v;
  if (outOk) *outOk = doneOk_;
  if (outReason && outReasonLen > 0) {
    const char* r = (doneReason_[0] ? doneReason_ : "-");
    strncpy(outReason, r, outReasonLen - 1);
    outReason[outReasonLen - 1] = 0;
  }

  // clear (one-shot)
  doneOk_ = false;
  doneReason_[0] = 0;
  return true;
}


void AzureTts::requestSessionReset() {
  sessionResetPending_ = true;
}

void AzureTts::setRuntimeConfig(const RuntimeConfig& cfg) { cfg_ = cfg; }
AzureTts::RuntimeConfig AzureTts::runtimeConfig() const { return cfg_; }

void AzureTts::setPlaybackEnabled(bool en) { playbackEnabled_ = en; }
bool AzureTts::playbackEnabled() const { return playbackEnabled_; }

bool AzureTts::testCredentials() {
  if (state_ != Idle) return false;
  if (!endpoint_.length() || !key_.length() || !defaultVoice_.length()) return false;
  return ensureToken_();
}

AzureTts::LastResult AzureTts::lastResult() const { return last_; }

// ---- task ----
void AzureTts::taskEntry(void* pv) {
  static_cast<AzureTts*>(pv)->taskBody();
  vTaskDelete(nullptr);
}



bool AzureTts::speakAsync(const String& text, uint32_t speakId, const char* voice) {
#if TTS_DEBUG_ENABLED
  // DEBUGでも「全文/先頭(head)」は出さない（秘密・個人情報保護）
  MC_LOGD("TTS", "speakAsync call id=%lu text_bytes=%u",
          (unsigned long)speakId,
          (unsigned)text.length());
#endif

  // --- rejection checks (NO SIDE EFFECTS) ---
  if (state_ != Idle) {
    MC_LOGI_RL("TTS.rej.busy", 1500, "TTS",
               "speakAsync rejected reason=busy id=%lu text_bytes=%u",
               (unsigned long)speakId, (unsigned)text.length());
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    MC_LOGI_RL("TTS.rej.wifi", 3000, "TTS",
               "speakAsync rejected reason=wifi id=%lu",
               (unsigned long)speakId);
    return false;
  }
  if (!endpoint_.length() || !key_.length()) {
    MC_LOGI_RL("TTS.rej.config", 5000, "TTS",
               "speakAsync rejected reason=config id=%lu",
               (unsigned long)speakId);
    return false;
  }

  // Prepare request strings (same behavior as before)
  reqText_  = text;
  reqVoice_ = voice ? String(voice) : defaultVoice_;
  if (!reqVoice_.length()) reqVoice_ = defaultVoice_;
  if (!reqVoice_.length()) {
    MC_LOGI_RL("TTS.rej.voice", 5000, "TTS",
               "speakAsync rejected reason=voice id=%lu",
               (unsigned long)speakId);
    return false;
  }

  // Accepted（EVTは常時：パイプライン追跡用 / ただし text内容は出さない）
  MC_EVT("TTS", "accepted id=%lu text_bytes=%u",
         (unsigned long)speakId,
         (unsigned)text.length());

  currentSpeakId_ = speakId;

  // clear DONE state (for previous id)
  doneSpeakId_ = 0;
  doneOk_ = false;
  doneReason_[0] = 0;

  // clear stale cancel (for previous id)
  portENTER_CRITICAL(&cancelMux_);
  cancelSpeakId_ = 0;
  cancelReason_[0] = 0;
  portEXIT_CRITICAL(&cancelMux_);

  state_ = Fetching;

  if (!task_) {
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "azure_tts", 8192, this, 1, &task_, 1);
    if (ok != pdPASS) {
      task_ = nullptr;
      state_ = Idle;
      MC_EVT("TTS", "fail stage=task_create id=%lu", (unsigned long)speakId);
      MC_LOGE("TTS", "task create failed");
      return false;
    }
  }
  return true;
}


void AzureTts::cancel(uint32_t speakId, const char* reason) {
  if (speakId == 0) return;

  // record cancel request
  portENTER_CRITICAL(&cancelMux_);
  cancelSpeakId_ = speakId;
  if (reason && reason[0]) {
    strncpy(cancelReason_, reason, sizeof(cancelReason_) - 1);
    cancelReason_[sizeof(cancelReason_) - 1] = 0;
  } else {
    cancelReason_[0] = 0;
  }
  portEXIT_CRITICAL(&cancelMux_);

  MC_EVT("TTS", "cancel req id=%lu reason=%s",
         (unsigned long)speakId,
         (cancelReason_[0] ? cancelReason_ : "-"));

  // Best-effort: if already PLAYING, try to stop immediately.
  if (state_ == Playing && currentSpeakId_ == speakId) {
    MC_EVT_D("TTS", "cancel: stop playing id=%lu", (unsigned long)speakId);
    M5.Speaker.stop();
  }
}


void AzureTts::poll() {
  if (state_ == Idle) return;

  // waiting fetch
  if (state_ == Fetching) {
    return;
  }

  auto clearCancel = [&]() {
    portENTER_CRITICAL(&cancelMux_);
    cancelSpeakId_ = 0;
    cancelReason_[0] = 0;
    portEXIT_CRITICAL(&cancelMux_);
  };

  auto makeCanceledReason = [&](char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (cancelReason_[0]) {
      snprintf(out, outLen, "canceled:%s", cancelReason_);
    } else {
      strncpy(out, "canceled", outLen - 1);
      out[outLen - 1] = 0;
    }
  };

  auto setDone = [&](bool ok, const char* reason) {
    doneOk_ = ok;
    if (reason && reason[0]) {
      strncpy(doneReason_, reason, sizeof(doneReason_) - 1);
      doneReason_[sizeof(doneReason_) - 1] = 0;
    } else {
      doneReason_[0] = 0;
    }
    doneSpeakId_ = currentSpeakId_;
  };

  auto setLastDrop = [&](const char* reason) {
    last_.ok = false;
    if (reason && reason[0]) {
      strncpy(last_.err, reason, sizeof(last_.err) - 1);
      last_.err[sizeof(last_.err) - 1] = 0;
    } else {
      last_.err[0] = 0;
    }
  };

  // ---------- Ready: try to play ----------
  if (state_ == Ready) {
    // cancel guard (prevents late play)
    if (cancelSpeakId_ != 0 && cancelSpeakId_ == currentSpeakId_) {
      char r[24];
      makeCanceledReason(r, sizeof(r));
      MC_EVT("TTS", "canceled before play id=%lu reason=%s",
             (unsigned long)currentSpeakId_, r);

      if (wav_) { free(wav_); wav_ = nullptr; }
      wavLen_ = 0;
      state_ = Idle;

      if (i2sLocked_) {
        I2SManager::instance().unlock("TTS.cancel_ready");
        i2sLocked_ = false;
      }

      setLastDrop(r);
      setDone(false, r);
      clearCancel();
      return;
    }

    if (!wav_ || wavLen_ == 0) {
      MC_EVT("TTS", "fail id=%lu reason=no_wav", (unsigned long)currentSpeakId_);
      MC_LOGW("TTS", "no wav -> drop id=%lu", (unsigned long)currentSpeakId_);
      state_ = Idle;
      setLastDrop("no_wav");
      setDone(false, "no_wav");
      return;
    }

    // If speaker is still playing something else, wait here.
    if (M5.Speaker.isPlaying()) return;

    // I2S owner: Speaker
    if (!i2sLocked_) {
      if (!I2SManager::instance().lockForSpeaker("TTS.play", 4000)) {
        I2SManager& m = I2SManager::instance();

        MC_EVT("TTS", "fail id=%lu reason=i2s_deny wav=%uB",
               (unsigned long)currentSpeakId_, (unsigned)wavLen_);

        MC_LOGW("TTS", "I2S lockForSpeaker failed -> drop id=%lu wav=%uB",
                (unsigned long)currentSpeakId_, (unsigned)wavLen_);
        MC_LOGD("TTS", "i2s owner=%u depth=%lu ownerSite=%s",
                (unsigned)m.owner(),
                (unsigned long)m.depth(),
                m.ownerCallsite() ? m.ownerCallsite() : "");

        free(wav_);
        wav_ = nullptr;
        wavLen_ = 0;
        state_ = Idle;

        setLastDrop("i2s_deny");
        setDone(false, "i2s_deny");
        return;
      }
      i2sLocked_ = true;
    }

    // Speaker begin if needed
    if (!M5.Speaker.isEnabled()) {
      MC_LOGD("TTS", "speaker not enabled -> begin");
      M5.Speaker.begin();
    }

    // volume safety（詳細はD/T）
    {
      const int vol = (int)M5.Speaker.getVolume();
      MC_LOGT("TTS", "spk state: enabled=%d playing=%d vol=%d defaultVol=%d",
              M5.Speaker.isEnabled() ? 1 : 0,
              M5.Speaker.isPlaying() ? 1 : 0,
              vol,
              (int)defaultVolume_);

      if (vol == 0 && defaultVolume_ > 0) {
        MC_LOGD("TTS", "spk vol=0 -> restore %d", (int)defaultVolume_);
        M5.Speaker.setVolume(defaultVolume_);
      }
    }

    bool okPlay = M5.Speaker.playWav(wav_, wavLen_);

    if (!okPlay) {
      MC_EVT("TTS", "fail id=%lu reason=play_fail wav=%uB",
             (unsigned long)currentSpeakId_, (unsigned)wavLen_);
      MC_LOGE("TTS", "play failed (wav=%uB)", (unsigned)wavLen_);

      free(wav_);
      wav_ = nullptr;
      wavLen_ = 0;
      state_ = Idle;

      if (i2sLocked_) {
        I2SManager::instance().unlock("TTS.play_fail");
        i2sLocked_ = false;
      }

      setLastDrop("play_fail");
      setDone(false, "play_fail");
      return;
    }

    // play started（EVT）
    MC_EVT("TTS", "play start id=%lu bytes=%u",
           (unsigned long)currentSpeakId_, (unsigned)wavLen_);

    state_ = Playing;
    return;
  }

  // ---------- Playing: wait done ----------
  if (state_ == Playing) {
    // canceled during play
    if (cancelSpeakId_ != 0 && cancelSpeakId_ == currentSpeakId_ && !M5.Speaker.isPlaying()) {
      char r[24];
      makeCanceledReason(r, sizeof(r));
      MC_EVT("TTS", "canceled during play id=%lu reason=%s",
             (unsigned long)currentSpeakId_, r);

      if (wav_) { free(wav_); wav_ = nullptr; }
      wavLen_ = 0;
      state_ = Idle;

      if (i2sLocked_) {
        I2SManager::instance().unlock("TTS.cancel_play");
        i2sLocked_ = false;
      }

      setLastDrop(r);
      setDone(false, r);
      clearCancel();
      return;
    }

    if (!M5.Speaker.isPlaying()) {
      if (wav_) { free(wav_); wav_ = nullptr; }
      wavLen_ = 0;
      state_ = Idle;

      if (i2sLocked_) {
        I2SManager::instance().unlock("TTS.done");
        i2sLocked_ = false;
      }

      // playback ok（EVT）
      MC_EVT("TTS", "play done id=%lu", (unsigned long)currentSpeakId_);

      last_.ok = true;
      strncpy(last_.err, "ok", sizeof(last_.err) - 1);
      last_.err[sizeof(last_.err) - 1] = 0;

      setDone(true, "ok");
      return;
    }
  }
}


// ---------- token / fetch ----------

void AzureTts::warmupDnsOnce_() {
  if (dnsWarmed_) return;
  dnsWarmed_ = true;

  IPAddress ip;

  // custom host
  if (customHost_.length()) {
    (void)WiFi.hostByName(customHost_.c_str(), ip);
  }

  // region hosts
  if (region_.length()) {
    {
      String host = region_ + ".tts.speech.microsoft.com";
      (void)WiFi.hostByName(host.c_str(), ip);
    }
    {
      String host = region_ + ".api.cognitive.microsoft.com";
      (void)WiFi.hostByName(host.c_str(), ip);
    }
  }
}



bool AzureTts::fetchTokenOld_(String* outTok) {
  if (!outTok) return false;
  outTok->clear();

  if (key_.length() == 0) return false;

  constexpr uint32_t kTokenTimeoutMs = 6000;

  auto tryUrl = [&](const String& url) -> bool {
    (void)url; // URL自体はログに出さない（環境依存情報の壁紙化防止）

    WiFiClientSecure c;
    c.setInsecure();

    HTTPClient h;
    h.setReuse(false);
    h.useHTTP10(false);
    h.setTimeout(kTokenTimeoutMs);

    if (!h.begin(c, url)) {
      MC_LOGD("TTS_TOKEN", "begin failed");
      h.end();
      return false;
    }

    h.addHeader("Content-type", "application/x-www-form-urlencoded");
    h.addHeader("Content-length", "0");
    h.addHeader("Ocp-Apim-Subscription-Key", key_);

    int code = h.POST((uint8_t*)nullptr, 0);

    if (code == 200) {
      String tok = h.getString();
      tok.trim();
      h.end();
      if (tok.length()) {
        *outTok = tok; // ★ログに出さない
        return true;
      }
      MC_LOGD("TTS_TOKEN", "HTTP 200 but empty body");
      return false;
    }

    // 本文はトークン系の可能性があるので絶対ログに出さない
    (void)h.getString();
    MC_LOGD("TTS_TOKEN", "HTTP %d", code);
    h.end();
    return false;
  };

  // 1) custom host
  if (customHost_.length()) {
    String url = String("https://") + customHost_ + "/sts/v1.0/issueToken";
    if (tryUrl(url)) return true;
  }

  // 2) region STS
  if (region_.length()) {
    String url = String("https://") + region_ + ".api.cognitive.microsoft.com/sts/v1.0/issueToken";
    if (tryUrl(url)) return true;
  }

  return false;
}




bool AzureTts::ensureToken_() {
  uint32_t now = millis();

  if (token_.length() && now < tokenExpireMs_) return true;
  if (now < tokenFailUntilMs_) return false;

  if (WiFi.status() != WL_CONNECTED) {
    MC_LOGI_RL("TTS.token.wifi", 5000, "TTS_TOKEN", "fetch skipped: wifi not connected");
    return false;
  }

  String tok;
  bool ok = fetchTokenOld_(&tok);
  if (ok && tok.length()) {
    token_ = tok; // ★秘密：ログに出さない
    tokenExpireMs_ = now + 9 * 60 * 1000; // 9min cache
    tokenFailCount_ = 0;

    // トークン更新は頻度低いので L1+ にサマリOK（内容は出さない）
    MC_LOGI("TTS_TOKEN", "ok (cached 9min)");
    return true;
  }

  // backoff
  tokenFailCount_ = (uint8_t)min<int>(tokenFailCount_ + 1, 10);
  uint32_t backoff = 1000u * (1u << min<int>(tokenFailCount_, 6)); // up to ~64s
  tokenFailUntilMs_ = now + backoff;

  // 連続失敗は壁紙化しやすいので RL
  MC_LOGI_RL("TTS.token.fail", 5000, "TTS_TOKEN",
             "fail (cooldown=%us)", (unsigned)(backoff / 1000));
  return false;
}


static String AzureTts_xmlEscape_(const String& s) {
  String o;
  o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&apos;"; break;
      default: o += c; break;
    }
  }
  return o;
}

String AzureTts::xmlEscape_(const String& s) { return AzureTts_xmlEscape_(s); }

String AzureTts::buildSsml_(const String& text, const String& voice) const {
  String v = voice.length() ? voice : defaultVoice_;
  String ssml;
  ssml.reserve(text.length() + v.length() + 128);
  ssml += "<speak version='1.0' xml:lang='ja-JP' xmlns='http://www.w3.org/2001/10/synthesis'>";
  ssml += "<voice name='";
  ssml += v;
  ssml += "'>";
  ssml += xmlEscape_(text);
  ssml += "</voice></speak>";
  return ssml;
}


bool AzureTts::fetchWav_(const String& ssml, uint8_t** outBuf, size_t* outLen) {
  if (!outBuf || !outLen) return false;
  *outBuf = nullptr;
  *outLen = 0;

  if (!endpoint_.length() || !key_.length()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  warmupDnsOnce_();

  // token
  if (!ensureToken_()) return false;

  // keep-alive toggling
  bool useKeepAlive = keepaliveEnabled_;
  uint32_t now = millis();
  if (disable_keepalive_until_ms_ && now < disable_keepalive_until_ms_) useKeepAlive = false;

  https_.setTimeout(cfg_.httpTimeoutMs);
  https_.setReuse(useKeepAlive);

  // begin
  if (!https_.begin(client_, endpoint_)) {
    MC_LOGE("TTS", "http.begin failed");
    return false;
  }

  https_.addHeader("Content-Type", "application/ssml+xml");
  https_.addHeader("X-Microsoft-OutputFormat", "riff-16khz-16bit-mono-pcm");
  https_.addHeader("User-Agent", "Mining-Stackchan");
  https_.addHeader("Accept", "audio/wav");
  https_.addHeader("Accept-Encoding", "identity");
  https_.addHeader("Connection", useKeepAlive ? "keep-alive" : "close");

  // Authorization（★tokenはログに出さない）
  https_.addHeader("Authorization", "Bearer " + token_);

  // custom endpoint requires extra region header sometimes
  if (isCustomEndpoint_(endpoint_) && region_.length()) {
    https_.addHeader("Ocp-Apim-Subscription-Region", region_);
  }

  int code = https_.POST((uint8_t*)ssml.c_str(), ssml.length());
  last_.httpCode = code;

  if (code != 200) {
    // 本文はログに出さない（個人情報/秘密の混入可能性をゼロにする）
    String body = https_.getString();
    const size_t bodyLen = body.length();
    (void)body;

    MC_LOGD("TTS", "HTTP %d body_len=%u", code, (unsigned)bodyLen);

    https_.end();

    // 失敗時はしばらく keep-alive を無効化（セッション破損っぽい対策）
    disable_keepalive_until_ms_ = millis() + 5000;
    return false;
  }

  // read body (chunked or content-length)
  WiFiClient* stream = https_.getStreamPtr();
  if (!stream) {
    https_.end();
    return false;
  }

  uint32_t start = millis();
  while (!stream->available()) {
    if (millis() - start > cfg_.bodyStartTimeoutMs) {
      MC_LOGE("TTS", "body start timeout");
      https_.end();
      return false;
    }
    delay(1);
  }

  // content-length が取れない場合は chunked とみなす
  int total = https_.getSize(); // -1 means unknown (chunked)
  if (total <= 0) {
    uint8_t* buf = nullptr;
    size_t used = 0;
    bool okChunked = readChunkedBody_(stream, &buf, &used, cfg_.chunkDataIdleTimeoutMs);

    https_.end();
    if (!okChunked) {
      if (buf) free(buf);
      return false;
    }

    // ★ extra safety
    salvageChunkedLeakIfNeeded_(&buf, &used);

    MC_LOGT("TTS", "rx wav bytes=%u (chunked keepAlive=%d)",
            (unsigned)used, useKeepAlive ? 1 : 0);

    *outBuf = buf;
    *outLen = used;
    return true;
  }

  // content-length known
  uint8_t* buf = (uint8_t*)malloc((size_t)total);
  if (!buf) { https_.end(); return false; }

  size_t got = 0;
  uint32_t idleStart = millis();
  while (got < (size_t)total) {
    int a = stream->available();
    if (a <= 0) {
      if (!stream->connected()) break;
      if (millis() - idleStart > cfg_.contentReadIdleTimeoutMs) break;
      delay(1);
      continue;
    }
    idleStart = millis();

    int r = stream->readBytes(buf + got, min<int>(a, (int)((size_t)total - got)));
    if (r <= 0) break;
    got += (size_t)r;
  }

  https_.end();
  if (got != (size_t)total) {
    free(buf);
    return false;
  }

  // ★ safety
  size_t outN = got;
  salvageChunkedLeakIfNeeded_(&buf, &outN);

  MC_LOGT("TTS", "rx wav bytes=%u (keepAlive=%d)",
          (unsigned)outN, useKeepAlive ? 1 : 0);

  *outBuf = buf;
  *outLen = outN;
  return true;
}



void AzureTts::taskBody() {
  while (true) {
    if (state_ != Fetching) {
      delay(5);
      continue;
    }

    seq_++;
    last_ = LastResult{};
    last_.seq = seq_;
    uint32_t t0 = millis();

    // fetch開始（EVT：時系列で追えるように）
    MC_EVT("TTS", "fetch start id=%lu", (unsigned long)currentSpeakId_);

    String ssml = buildSsml_(reqText_, reqVoice_); // ★SSML全文はログに出さない
    uint8_t* buf = nullptr;
    size_t len = 0;

    bool ok = fetchWav_(ssml, &buf, &len);
    last_.fetchMs = millis() - t0;
    last_.ok = ok;
    last_.bytes = (uint32_t)len;

    // fetch結果（EVT：要点）
    MC_EVT("TTS", "fetch done id=%lu ok=%d http=%d bytes=%lu took=%lums",
           (unsigned long)currentSpeakId_,
           ok ? 1 : 0,
           last_.httpCode,
           (unsigned long)len,
           (unsigned long)last_.fetchMs);

    auto makeCanceledReason = [&](char* out, size_t outLen) {
      if (!out || outLen == 0) return;
      if (cancelReason_[0]) snprintf(out, outLen, "canceled:%s", cancelReason_);
      else {
        strncpy(out, "canceled", outLen - 1);
        out[outLen - 1] = 0;
      }
    };

    auto clearCancel = [&]() {
      portENTER_CRITICAL(&cancelMux_);
      cancelSpeakId_ = 0;
      cancelReason_[0] = 0;
      portEXIT_CRITICAL(&cancelMux_);
    };

    auto setDone = [&](bool doneOk, const char* reason) {
      doneOk_ = doneOk;
      if (reason && reason[0]) {
        strncpy(doneReason_, reason, sizeof(doneReason_) - 1);
        doneReason_[sizeof(doneReason_) - 1] = 0;
      } else {
        doneReason_[0] = 0;
      }
      doneSpeakId_ = currentSpeakId_;
    };

    auto setLastDrop = [&](const char* reason) {
      last_.ok = false;
      if (reason && reason[0]) {
        strncpy(last_.err, reason, sizeof(last_.err) - 1);
        last_.err[sizeof(last_.err) - 1] = 0;
      } else {
        last_.err[0] = 0;
      }
    };

    // cancel guard: canceled while fetching -> never enqueue for playback
    if (cancelSpeakId_ != 0 && cancelSpeakId_ == currentSpeakId_) {
      char r[24];
      makeCanceledReason(r, sizeof(r));
      MC_EVT("TTS", "canceled while fetching id=%lu reason=%s",
             (unsigned long)currentSpeakId_, r);

      if (buf) free(buf);
      state_ = Idle;

      setLastDrop(r);
      setDone(false, r);
      clearCancel();
      continue;
    }

    if (!ok || !buf || !len) {
      if (buf) free(buf);
      state_ = Idle;

      MC_EVT("TTS", "fail id=%lu reason=fetch_fail http=%d",
             (unsigned long)currentSpeakId_, last_.httpCode);

      setLastDrop("fetch_fail");
      setDone(false, "fetch_fail");
      continue;
    }

    wav_ = buf;
    wavLen_ = len;
    last_ok_ms_ = millis();
    state_ = Ready;
  }
}



void AzureTts::resetSession_() {
  https_.end();
  client_.stop();
  token_ = "";
  tokenExpireMs_ = 0;
}




