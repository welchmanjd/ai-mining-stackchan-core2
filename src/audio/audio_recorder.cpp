// === src/audio_recorder.cpp : replace whole file ===
// Module implementation.
#include "audio/audio_recorder.h"
#include "core/logging.h"
#include "audio/i2s_manager.h"
#include "config/config.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include <type_traits>
// Arduino-ESP32 ships ESP-IDF's I2S driver headers.
// We use these *only* to force-uninstall stale drivers after switching Mic <-> Speaker.
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <esp_log.h>
static void forceUninstallI2S_(const char* reason) {
  // Defensive cleanup for stale I2S drivers after mode switching.
  MC_EVT("REC", "i2s_uninstall exec reason=%s", reason ? reason : "");
  // NOTE:
  esp_log_level_set("I2S", ESP_LOG_NONE);
  esp_err_t e1 = i2s_driver_uninstall((i2s_port_t)1);
  esp_err_t e0 = i2s_driver_uninstall((i2s_port_t)0);
  esp_log_level_set("I2S", ESP_LOG_ERROR);
  const bool ok1 = (e1 == ESP_OK);
  const bool ok0 = (e0 == ESP_OK);
  LOG_EVT_DEBUG("REC", "i2s_uninstall_d result p1_ok=%d p0_ok=%d e1=%d e0=%d reason=%s",
                (int)ok1, (int)ok0, (int)e1, (int)e0, reason ? reason : "");
  if (ok1 || ok0) {
    MC_LOGD("REC", "i2s uninstall ok (p1=%d p0=%d) reason=%s", (int)ok1, (int)ok0, reason ? reason : "");
    return;
  }
  MC_LOGI_RL("REC_i2s_uninstall_invalid", 60000UL, "REC",
             "i2s uninstall skipped/invalid_state (e1=%d e0=%d) reason=%s",
             (int)e1, (int)e0, reason ? reason : "");
}
static void waitMicIdle_(uint32_t timeoutMs) {
  // Wait for the hardware FIFO to drain before stopping the mic.
  const uint32_t t0 = millis();
  while (M5.Mic.isRecording()) {
    if ((millis() - t0) >= timeoutMs) break;
    delay(1);
  }
}
static void writeWavHeader_(File& f, uint32_t sampleRate, uint32_t dataBytes) {
  // Minimal WAV header for PCM16 mono.
  const uint32_t riffSize = 36 + dataBytes;
  const uint16_t audioFormat = 1;   // PCM
  const uint16_t numChannels = 1;   // mono
  const uint16_t bitsPerSample = 16;
  const uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
  const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
  f.write((const uint8_t*)"RIFF", 4);
  f.write((const uint8_t*)&riffSize, 4);
  f.write((const uint8_t*)"WAVE", 4);
  f.write((const uint8_t*)"fmt ", 4);
  uint32_t subchunk1Size = 16;
  f.write((const uint8_t*)&subchunk1Size, 4);
  f.write((const uint8_t*)&audioFormat, 2);
  f.write((const uint8_t*)&numChannels, 2);
  f.write((const uint8_t*)&sampleRate, 4);
  f.write((const uint8_t*)&byteRate, 4);
  f.write((const uint8_t*)&blockAlign, 2);
  f.write((const uint8_t*)&bitsPerSample, 2);
  f.write((const uint8_t*)"data", 4);
  f.write((const uint8_t*)&dataBytes, 4);
}
template <typename R>
static size_t retToSamples_(R ret, size_t requestedSamples) {
  if (std::is_same<R, bool>::value) {
    return ret ? requestedSamples : 0;
  }
  return (size_t)ret;
}
bool AudioRecorder::begin() {
  initialized_ = true;
  MC_LOGD("REC", "begin ok=1");
  return true;
}
void AudioRecorder::stopSpeakerForRec_() {
  savedSpkVolumeValid_ = false;
  if (M5.Speaker.isEnabled()) {
    savedSpkVolume_ = M5.Speaker.getVolume();
    savedSpkVolumeValid_ = true;
    M5.Speaker.stop();
    M5.Speaker.end();
  }
}
void AudioRecorder::restoreSpeakerAfterRec_() {
  if (!savedSpkVolumeValid_) return;
  delay(20);
  if (!M5.Speaker.isEnabled()) {
    MC_LOGD("REC", "speaker begin (restore)");
    M5.Speaker.end();
    // Some builds leave an I2S driver registered even after end(); force-uninstall.
    forceUninstallI2S_("restoreSpeakerAfterRec");
    delay(10);
    M5.Speaker.begin();
    delay(10);
    if (!M5.Speaker.isEnabled()) {
      MC_LOGW("REC", "speaker begin failed -> leave disabled (TTS will begin later)");
      savedSpkVolumeValid_ = false;
      return;
    }
  }
  M5.Speaker.setVolume(savedSpkVolume_);
  savedSpkVolumeValid_ = false;
  MC_LOGD("REC", "speaker restored vol=%d", (int)savedSpkVolume_);
}
bool AudioRecorder::ensureMicBegun_() {
  if (micBegun_) return true;
  // Match the recorder sample rate with the config.
  M5.Mic.setSampleRate(sampleRate_);
  bool ok = M5.Mic.begin();
  MC_LOGD("REC", "mic begin ok=%d sr=%u", ok ? 1 : 0, (unsigned)sampleRate_);
  micBegun_ = ok;
  if (ok) return true;
  if (!speakerEndedByRec_ && M5.Speaker.isEnabled()) {
    MC_LOGW("REC", "mic begin failed -> fallback speaker.end and retry");
    M5.Speaker.end();
    speakerEndedByRec_ = true;
    delay(20);
    ok = M5.Mic.begin();
    MC_LOGD("REC", "mic begin(retry) ok=%d sr=%u", ok ? 1 : 0, (unsigned)sampleRate_);
    micBegun_ = ok;
  }
  return ok;
}
// === src/audio_recorder.cpp : replace whole function AudioRecorder::endMic_() ===
void AudioRecorder::endMic_() {
  if (!micBegun_) return;
  // I2S lock is expected to be held by REC
  bool tempLock = false;
  if (!i2sLocked_) {
    if (I2SManager::instance().lockForMic("REC.endMic", 2000)) {
      tempLock = true;
    } else {
      MC_LOGW("REC", "endMic: temp lockForMic failed (continue cleanup)");
    }
  }
  waitMicIdle_(200);
  MC_LOGD("REC", "mic end");
  M5.Mic.end();
  delay(20);
  if (M5.Mic.isEnabled()) {
    MC_LOGI_RL("REC_mic_end_retry", 10000UL, "REC", "mic still enabled after end -> retry");
    M5.Mic.end();
    delay(20);
  }
  forceUninstallI2S_("endMic");
  micBegun_ = false;
  if (tempLock) {
    I2SManager::instance().unlock("REC.endMic");
  }
}
bool AudioRecorder::allocBuffer_() {
  if (pcm_) return true;
  maxSamples_ = (size_t)sampleRate_ * (size_t)maxSeconds_;
  const size_t bytes = maxSamples_ * sizeof(int16_t);
  pcm_ = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
  if (!pcm_) {
    pcm_ = (int16_t*)malloc(bytes);
  }
  if (!pcm_) {
    MC_LOGE("REC", "allocBuffer FAIL bytes=%u", (unsigned)bytes);
    maxSamples_ = 0;
    return false;
  }
  memset(pcm_, 0, bytes);
  capturedSamples_ = 0;
  peakAbs_ = 0;
  MC_LOGD("REC", "allocBuffer OK bytes=%u samples=%u", (unsigned)bytes, (unsigned)maxSamples_);
  return true;
}
void AudioRecorder::freeBuffer_() {
  if (pcm_) {
    free(pcm_);
    pcm_ = nullptr;
  }
  maxSamples_ = 0;
  capturedSamples_ = 0;
  peakAbs_ = 0;
}
bool AudioRecorder::startTask_() {
  if (task_) return true;
  BaseType_t ok = xTaskCreatePinnedToCore(
      taskEntry_, "recTask", 4096, this, 2, &task_, 1 /* core1 */);
  if (ok != pdPASS) {
    task_ = nullptr;
    MC_LOGE("REC", "task create FAIL");
    return false;
  }
  MC_LOGD("REC", "task start");
  return true;
}
bool AudioRecorder::start(uint32_t nowMs) {
  if (!initialized_) begin();
  if (recording_) return false;
  // Acquire the I2S lock before touching mic/speaker.
  if (!i2sLocked_) {
    if (!I2SManager::instance().lockForMic("REC.start", 2000)) {
      I2SManager& m = I2SManager::instance();
      MC_EVT("REC", "start_fail reason=i2s_deny curOwner=%u depth=%lu curSite=%s",
             (unsigned)m.owner(),
             (unsigned long)m.depth(),
             m.ownerCallsite() ? m.ownerCallsite() : "");
      MC_LOGW("REC", "start FAIL: I2S busy (curOwner=%u depth=%lu curSite=%s)",
              (unsigned)m.owner(),
              (unsigned long)m.depth(),
              m.ownerCallsite() ? m.ownerCallsite() : "");
      return false;
    }
    i2sLocked_ = true;
  }
  stopSpeakerForRec_();
  if (!ensureMicBegun_()) {
    MC_EVT("REC", "start_fail reason=mic_begin");
    MC_LOGW("REC", "start FAIL: mic begin failed");
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.start.fail");
      i2sLocked_ = false;
    }
    return false;
  }
  if (!allocBuffer_()) {
    MC_EVT("REC", "start_fail reason=alloc");
    MC_LOGW("REC", "start FAIL: allocBuffer failed");
    endMic_();
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.start.fail");
      i2sLocked_ = false;
    }
    return false;
  }
  if (!startTask_()) {
    MC_EVT("REC", "start_fail reason=task_create");
    MC_LOGW("REC", "start FAIL: task create failed");
    endMic_();
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.start.fail");
      i2sLocked_ = false;
    }
    return false;
  }
  stopReq_ = false;
  cancelReq_ = false;
  capturedSamples_ = 0;
  peakAbs_ = 0;
  startMs_ = nowMs;
  stopMs_ = 0;
  recording_ = true;
  xTaskNotifyGive(task_);
  MC_EVT("REC", "start now=%u sr=%u maxSec=%u",
         (unsigned)nowMs, (unsigned)sampleRate_, (unsigned)maxSeconds_);
  MC_LOGD("REC", "start ok=1");
  return true;
}
void AudioRecorder::requestStop_(bool cancel) {
  if (!initialized_ || !task_) return;
  forceAbort_ = false;
  if (cancel) cancelReq_ = true;
  else        stopReq_   = true;
  xTaskNotifyGive(task_);
}
bool AudioRecorder::waitTaskDone_(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (recording_) {
    if ((millis() - t0) > timeoutMs) {
      MC_EVT("REC", "timeout waitTaskDone timeout=%lums samples=%u stopReq=%d cancelReq=%d",
             (unsigned long)timeoutMs,
             (unsigned)capturedSamples_,
             stopReq_ ? 1 : 0,
             cancelReq_ ? 1 : 0);
      MC_LOGW("REC", "waitTaskDone TIMEOUT (timeout=%lums samples=%u stopReq=%d cancelReq=%d)",
              (unsigned long)timeoutMs,
              (unsigned)capturedSamples_,
              stopReq_ ? 1 : 0,
              cancelReq_ ? 1 : 0);
      forceAbort_ = true;
      recording_ = false;
      if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
        MC_EVT("REC", "abort forceAbort task_deleted=1");
        MC_LOGW("REC", "task deleted by forceAbort");
      } else {
        MC_EVT("REC", "abort forceAbort task_deleted=0");
      }
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}
bool AudioRecorder::stop(uint32_t nowMs) {
  if (!recording_) return false;
  MC_LOGD("REC", "stop req");
  requestStop_(false);
  bool ok = waitTaskDone_(2000);
  stopMs_ = nowMs;
  waitMicIdle_(200);
  MC_LOGD("REC", "stop finalize mic: rec=%d en=%d",
          M5.Mic.isRecording() ? 1 : 0,
          M5.Mic.isEnabled() ? 1 : 0);
  endMic_();
  restoreSpeakerAfterRec_();
  if (i2sLocked_) {
    I2SManager::instance().unlock("REC.stop");
    i2sLocked_ = false;
  }
  MC_EVT("REC", "stop ok=%d dur=%ums samples=%u peak=%d",
         ok ? 1 : 0,
         (unsigned)durationMs(),
         (unsigned)capturedSamples_,
         (int)peakAbs_);
  MC_LOGD("REC", "stop done ok=%d", ok ? 1 : 0);
  return ok;
}
void AudioRecorder::cancel() {
  if (!recording_) {
    freeBuffer_();
    waitMicIdle_(100);
    endMic_();
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.cancel(idle)");
      i2sLocked_ = false;
    }
    MC_EVT("REC", "cancel (idle) buffer_freed=1");
    MC_LOGD("REC", "cancel done (idle, buffer freed)");
    return;
  }
  MC_LOGD("REC", "cancel req");
  requestStop_(true);
  waitTaskDone_(2000);
  freeBuffer_();
  waitMicIdle_(200);
  MC_LOGD("REC", "cancel finalize mic: rec=%d en=%d",
          M5.Mic.isRecording() ? 1 : 0,
          M5.Mic.isEnabled() ? 1 : 0);
  endMic_();
  restoreSpeakerAfterRec_();
  if (i2sLocked_) {
    I2SManager::instance().unlock("REC.cancel");
    i2sLocked_ = false;
  }
  MC_EVT("REC", "cancel buffer_freed=1");
  MC_LOGD("REC", "cancel done (buffer freed)");
}
uint32_t AudioRecorder::durationMs() const {
  if (sampleRate_ == 0) return 0;
  const uint32_t ms = (uint32_t)((capturedSamples_ * 1000ULL) / sampleRate_);
  return ms;
}
bool AudioRecorder::saveWavToFs(const char* path) {
  if (!pcm_ || capturedSamples_ == 0) return false;
  if (!LittleFS.begin(true)) return false;
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  const uint32_t dataBytes = (uint32_t)(capturedSamples_ * sizeof(int16_t));
  writeWavHeader_(f, sampleRate_, dataBytes);
  f.write((const uint8_t*)pcm_, dataBytes);
  f.close();
  MC_LOGD("REC", "saveWav ok path=%s bytes=%u", path ? path : "", (unsigned)dataBytes);
  return true;
}
void AudioRecorder::taskEntry_(void* arg) {
  ((AudioRecorder*)arg)->taskLoop_();
  vTaskDelete(nullptr);
}
void AudioRecorder::taskLoop_() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (forceAbort_) {
      stopReq_ = false;
      cancelReq_ = false;
      recording_ = false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    bool naturalEnd = false;
    while (recording_) {
      if (forceAbort_) break;
      if (cancelReq_) {
        capturedSamples_ = 0;
        stopMs_ = millis();
        recording_ = false;
        break;
      }
      if (stopReq_) {
        stopMs_ = millis();
        recording_ = false;
        break;
      }
      constexpr size_t kChunk = 256;
      int16_t tmp[kChunk];
      bool submitted = M5.Mic.record(tmp, kChunk, sampleRate_, false);
      if (!submitted) {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
      while (M5.Mic.isRecording()) {
        if (forceAbort_ || stopReq_ || cancelReq_) break;
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      if (forceAbort_ || stopReq_ || cancelReq_) {
        continue;
      }
      size_t got = kChunk;
      if (got > 0) {
        const size_t space = (capturedSamples_ < maxSamples_) ? (maxSamples_ - capturedSamples_) : 0;
        const size_t n = (got < space) ? got : space;
        if (n > 0) {
          memcpy(&pcm_[capturedSamples_], tmp, n * sizeof(int16_t));
          capturedSamples_ += n;
          // debug: peak
          for (size_t i = 0; i < n; ++i) {
            int v = (int)tmp[i];
            if (v < 0) v = -v;
            if (v > peakAbs_) peakAbs_ = v;
          }
        }
      }
      const uint32_t elapsedMs = millis() - startMs_;
      if (capturedSamples_ >= maxSamples_ || elapsedMs >= (maxSeconds_ * 1000UL)) {
        stopMs_ = millis();
        naturalEnd = true;
        recording_ = false;
        MC_EVT("REC", "timeout reason=buffer_full_or_time dur=%lums samples=%u peak=%d",
               (unsigned long)elapsedMs,
               (unsigned)capturedSamples_,
               (int)peakAbs_);
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (naturalEnd) {
      waitMicIdle_(200);
      MC_LOGD("REC", "autoStop finalize mic: rec=%d en=%d",
              M5.Mic.isRecording() ? 1 : 0,
              M5.Mic.isEnabled() ? 1 : 0);
      endMic_();
      restoreSpeakerAfterRec_();
      if (i2sLocked_) {
        I2SManager::instance().unlock("REC.autoStop");
        i2sLocked_ = false;
      }
      MC_LOGD("REC", "autoStop finalize done samples=%u peak=%d",
              (unsigned)capturedSamples_, (int)peakAbs_);
    }
    stopReq_ = false;
    cancelReq_ = false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
