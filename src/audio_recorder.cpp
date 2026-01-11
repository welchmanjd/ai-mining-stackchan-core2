#include "audio_recorder.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "logging.h"

#if __has_include(<LittleFS.h>)
  #include <LittleFS.h>
  #define MC_HAS_LITTLEFS 1
#else
  #define MC_HAS_LITTLEFS 0
#endif

// ===== local helpers =====
static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool AudioRecorder::begin() {
  if (initialized_) return true;

  // config.h の定数を採用（無ければデフォルト）
#ifdef MC_AI_REC_SAMPLE_RATE
  sampleRate_ = (uint32_t)MC_AI_REC_SAMPLE_RATE;
#endif
#ifdef MC_AI_REC_MAX_SECONDS
  maxSeconds_ = (uint32_t)MC_AI_REC_MAX_SECONDS;
#else
  maxSeconds_ = (uint32_t)MC_AI_LISTEN_MAX_SECONDS;
#endif

  sampleRate_ = clamp_u32(sampleRate_, 8000, 48000);
  maxSeconds_ = clamp_u32(maxSeconds_, 1, 30);

  // M5Unified Mic init
  // ※ main.cpp で cfg_m5.internal_mic = true にしている前提だが、
  //    念のため begin() を呼んでおく（成功/失敗はログに出す）
  const bool ok = M5.Mic.begin();
  initialized_ = ok;

  mc_logf("[REC] begin: mic=%s sampleRate=%lu maxSec=%lu",
          ok ? "OK" : "NG",
          (unsigned long)sampleRate_,
          (unsigned long)maxSeconds_);

  return ok;
}

bool AudioRecorder::allocBuffer_() {
  freeBuffer_();

  maxSamples_ = (size_t)sampleRate_ * (size_t)maxSeconds_;
  if (maxSamples_ == 0) return false;

  const size_t bytes = maxSamples_ * sizeof(int16_t);

  // PSRAM 優先（無ければ通常heap）
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(bytes);

  if (!p) {
    mc_logf("[REC] allocBuffer FAIL bytes=%u", (unsigned)bytes);
    maxSamples_ = 0;
    return false;
  }

  pcm_ = (int16_t*)p;
  memset(pcm_, 0, bytes);
  capturedSamples_ = 0;

  mc_logf("[REC] allocBuffer OK bytes=%u samples=%u",
          (unsigned)bytes, (unsigned)maxSamples_);
  return true;
}

void AudioRecorder::freeBuffer_() {
  if (pcm_) {
    free(pcm_);
    pcm_ = nullptr;
  }
  maxSamples_ = 0;
  capturedSamples_ = 0;
}

bool AudioRecorder::startTask_() {
  if (task_) return true;

  // task作成（録音中だけ回す）
  BaseType_t ok = xTaskCreatePinnedToCore(
      &AudioRecorder::taskEntry_,
      "mc_rec",
      4096,
      this,
      2,          // 低〜中優先度（マイニング/描画を阻害しない）
      &task_,
      1           // Core1推奨（Core0はWiFi等が多いことが多い）
  );

  if (ok != pdPASS) {
    task_ = nullptr;
    mc_logf("[REC] task create FAIL");
    return false;
  }
  return true;
}

void AudioRecorder::taskEntry_(void* arg) {
  auto* self = (AudioRecorder*)arg;
  self->taskLoop_();
  vTaskDelete(nullptr);
}

void AudioRecorder::taskLoop_() {
  // 1回のrecord()は短いチャンクで回す（キャンセル/停止に追従させる）
  static constexpr size_t kChunkSamples = 256; // 16kHzなら約16ms

  int16_t chunk[kChunkSamples];

  mc_logf("[REC] task start");

  while (true) {
    if (cancelReq_) break;
    if (stopReq_) break;

    // バッファ満了（安全）
    if (capturedSamples_ >= maxSamples_) {
      mc_logf("[REC] buffer full -> stop");
      break;
    }

    const size_t remain = maxSamples_ - capturedSamples_;
    const size_t n = (remain < kChunkSamples) ? remain : kChunkSamples;

    // record()：nサンプルぶんを取る（ブロッキングでも最大 ~16ms 程度）
    bool ok = M5.Mic.record(chunk, n, sampleRate_, false /*mono*/);
    if (!ok) {
      mc_logf("[REC] record() FAIL");
      // ここで即終了（上位でエラー扱い）
      break;
    }

    // コピー
    memcpy((void*)(pcm_ + capturedSamples_), chunk, n * sizeof(int16_t));
    capturedSamples_ += n;
  }

  // 終了
  recording_ = false;

  // 完了通知
  if (task_) {
    xTaskNotifyGive(task_);
  }

  mc_logf("[REC] task end cancel=%d stop=%d samples=%u",
          cancelReq_ ? 1 : 0,
          stopReq_ ? 1 : 0,
          (unsigned)capturedSamples_);
}

void AudioRecorder::requestStop_(bool cancel) {
  if (!recording_) return;
  if (cancel) cancelReq_ = true;
  else stopReq_ = true;
}

bool AudioRecorder::waitTaskDone_(uint32_t timeoutMs) {
  if (!task_) return true;

  // task_自身にnotifyしてるので、ここでは「少し待ってから」task_を消す方式にする
  // （record()のチャンクが短いので、基本すぐ返る）
  const uint32_t start = millis();
  while (recording_) {
    if ((millis() - start) > timeoutMs) {
      mc_logf("[REC] waitTaskDone TIMEOUT");
      return false;
    }
    delay(5);
  }

  // task handle 解放（vTaskDeleteはtask側で行うので handleをNULLにするだけ）
  task_ = nullptr;
  return true;
}

bool AudioRecorder::start(uint32_t nowMs) {
  if (!initialized_) {
    if (!begin()) return false;
  }
  if (recording_) return false;

  if (!allocBuffer_()) return false;

  startMs_ = nowMs;
  stopMs_  = 0;

  stopReq_ = false;
  cancelReq_ = false;
  recording_ = true;

  if (!startTask_()) {
    recording_ = false;
    freeBuffer_();
    return false;
  }

  mc_logf("[REC] start now=%lu", (unsigned long)nowMs);
  return true;
}

bool AudioRecorder::stop(uint32_t nowMs) {
  if (!recording_) {
    // 既に止まっていても「確定済み」として扱う（ただしデータ0ならfalse）
    stopMs_ = nowMs;
    return (capturedSamples_ > 0);
  }

  mc_logf("[REC] stop req");
  requestStop_(false);

  const bool ok = waitTaskDone_(800);
  stopMs_ = nowMs;

  mc_logf("[REC] stop done ok=%d samples=%u", ok ? 1 : 0, (unsigned)capturedSamples_);
  return ok && (capturedSamples_ > 0);
}

void AudioRecorder::cancel() {
  if (recording_) {
    mc_logf("[REC] cancel req");
    requestStop_(true);
    (void)waitTaskDone_(800);
  }

  recording_ = false;
  stopReq_ = false;
  cancelReq_ = false;

  startMs_ = 0;
  stopMs_  = 0;

  freeBuffer_();
  mc_logf("[REC] cancel done (buffer freed)");
}

uint32_t AudioRecorder::durationMs() const {
  if (sampleRate_ == 0) return 0;
  const uint32_t ms = (uint32_t)((capturedSamples_ * 1000UL) / sampleRate_);
  return ms;
}

bool AudioRecorder::saveWavToFs(const char* path) {
#if !MC_HAS_LITTLEFS
  (void)path;
  mc_logf("[REC] saveWavToFs skipped: LittleFS not available");
  return false;
#else
  if (!path || !path[0]) return false;
  if (!pcm_ || capturedSamples_ == 0) return false;

  // LittleFSが既にbegin済みの可能性があるので、失敗したらformatしない
  if (!LittleFS.begin(false)) {
    mc_logf("[REC] LittleFS.begin(false) FAIL");
    return false;
  }

  File f = LittleFS.open(path, "w");
  if (!f) {
    mc_logf("[REC] open FAIL path=%s", path);
    return false;
  }

  // WAV header (PCM16 mono)
  const uint32_t dataBytes = (uint32_t)(capturedSamples_ * sizeof(int16_t));
  const uint32_t riffSize  = 36 + dataBytes;
  const uint16_t audioFmt  = 1;
  const uint16_t channels  = 1;
  const uint32_t sampleRate = sampleRate_;
  const uint16_t bitsPerSample = 16;
  const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  const uint16_t blockAlign = channels * (bitsPerSample / 8);

  auto w32 = [&](uint32_t v){ f.write((uint8_t*)&v, 4); };
  auto w16 = [&](uint16_t v){ f.write((uint8_t*)&v, 2); };

  f.write((const uint8_t*)"RIFF", 4);
  w32(riffSize);
  f.write((const uint8_t*)"WAVE", 4);

  f.write((const uint8_t*)"fmt ", 4);
  w32(16);
  w16(audioFmt);
  w16(channels);
  w32(sampleRate);
  w32(byteRate);
  w16(blockAlign);
  w16(bitsPerSample);

  f.write((const uint8_t*)"data", 4);
  w32(dataBytes);

  // PCM body
  f.write((const uint8_t*)pcm_, dataBytes);
  f.close();

  mc_logf("[REC] WAV saved path=%s bytes=%lu", path, (unsigned long)(44 + dataBytes));
  return true;
#endif
}
