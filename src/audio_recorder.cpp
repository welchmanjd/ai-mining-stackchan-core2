#include "audio_recorder.h"
#include "logging.h"
#include "i2s_manager.h"
#include "config.h"


#include <M5Unified.h>
#include <LittleFS.h>
#include <type_traits>

// Arduino-ESP32 ships ESP-IDF's I2S driver headers.
// We use these *only* to force-uninstall stale drivers after switching Mic <-> Speaker.
#include <driver/i2s.h>
#include <esp_err.h>

#include <esp_task_wdt.h>
#include <esp_log.h>   // ★追加：I2Sタグのログレベル制御に使う

static const char* kTag = "REC";


static void forceUninstallI2S_(const char* reason) {
  // NOTE:
  // i2s_driver_uninstall() を「入ってない port」に対して呼ぶと、
  // ESP-IDF が tag="I2S" で E ログを吐く（"has not installed"）。
  // これは想定内の後始末なので、ここだけ一時的に I2S ログを黙らせる。
  esp_log_level_set("I2S", ESP_LOG_NONE);  // ★I2Sタグのみミュート

  esp_err_t e1 = i2s_driver_uninstall((i2s_port_t)1);
  esp_err_t e0 = i2s_driver_uninstall((i2s_port_t)0);

  esp_log_level_set("I2S", ESP_LOG_ERROR); // ★必要最低限に戻す（ERRORのみ）

  const bool ok1 = (e1 == ESP_OK);
  const bool ok0 = (e0 == ESP_OK);
  if (ok1 || ok0) {
    mc_logf("[REC] i2s uninstall: ok (p1=%d p0=%d) (reason=%s)", (int)ok1, (int)ok0, reason);
    return;
  }

  // 典型：ESP_ERR_INVALID_STATE（未インストール）など
  mc_logf("[REC] i2s uninstall: skipped/invalid_state (e1=%d e0=%d) (reason=%s)", (int)e1, (int)e0, reason);
}





static void waitMicIdle_(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (M5.Mic.isRecording()) {
    if ((millis() - t0) >= timeoutMs) break;
    delay(1);
  }
}

// WAVヘッダ生成（PCM16 mono）
static void writeWavHeader_(File& f, uint32_t sampleRate, uint32_t dataBytes) {
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

// recordの返り値が bool / size_t / bytes の差異を吸収して「サンプル数」に揃える
template <typename R>
static size_t retToSamples_(R ret, size_t requestedSamples) {
  // ※C++17の if constexpr を使わない（toolchainがC++11/14でも通す）
  if (std::is_same<R, bool>::value) {
    return ret ? requestedSamples : 0;
  }
  return (size_t)ret;
}
bool AudioRecorder::begin() {
  initialized_ = true;
  Serial.printf("[%s] begin ok=1\n", kTag);
  return true;
}

void AudioRecorder::stopSpeakerForRec_() {
  // 録音中にスピーカーI2Sが噛むと、以後の再生がノイズ化することがあるので
  // ここでいったん end() して、録音後に begin() で復帰させる。
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

  // Mic -> Speaker 切り替えの直後はI2Sがまだ不安定な場合があるので少し待つ
  delay(20);

  if (!M5.Speaker.isEnabled()) {
    Serial.printf("[%s] speaker begin (restore)\n", kTag);

    // 念のため：残骸が残ってると register failed になり得るので end を叩いてから begin
    M5.Speaker.end();

    // Some builds leave an I2S driver registered even after end(); force-uninstall.
    forceUninstallI2S_("restoreSpeakerAfterRec");

    delay(10);

    M5.Speaker.begin();
    delay(10);

    if (!M5.Speaker.isEnabled()) {
      Serial.printf("[%s] speaker begin failed -> leave disabled (TTS will begin later)\n", kTag);
      savedSpkVolumeValid_ = false;
      return;
    }
  }

  M5.Speaker.setVolume(savedSpkVolume_);
  savedSpkVolumeValid_ = false;

  Serial.printf("[%s] speaker restored vol=%d\n", kTag, (int)savedSpkVolume_);
}


bool AudioRecorder::ensureMicBegun_() {

  if (micBegun_) return true;

  // サンプルレートを先に明示
  M5.Mic.setSampleRate(sampleRate_);

  // まずは通常 begin（Speakerはソフト停止/ミュート済みの想定）
  bool ok = M5.Mic.begin();
  Serial.printf("[%s] mic begin ok=%d sr=%u\n",
                kTag, ok ? 1 : 0, (unsigned)sampleRate_);
  micBegun_ = ok;

  if (ok) return true;

  // フォールバック：Speaker を end() してからリトライ（begin/end多発を避けつつ、環境差に対応）
  if (!speakerEndedByRec_ && M5.Speaker.isEnabled()) {
    Serial.printf("[%s] mic begin failed -> fallback speaker.end and retry\n", kTag);
    M5.Speaker.end();
    speakerEndedByRec_ = true;
    delay(20);

    ok = M5.Mic.begin();
    Serial.printf("[%s] mic begin(retry) ok=%d sr=%u\n",
                  kTag, ok ? 1 : 0, (unsigned)sampleRate_);
    micBegun_ = ok;
  }

  return ok;
}


void AudioRecorder::endMic_() {
  if (!micBegun_) return;

  // I2S lock is expected to be held by REC
  bool tempLock = false;
  if (!i2sLocked_) {
    if (I2SManager::instance().lockForMic("REC.endMic", 2000)) {
      tempLock = true;
    }
  }

  // まず録音が完全に止まるのを待つ
  waitMicIdle_(200);

  Serial.printf("[%s] mic end\n", kTag);
  M5.Mic.end();

  // end直後に少し待つ（I2Sドライバ解放待ち）
  delay(20);

  // 念のため：まだenabled扱いならもう一回 end を試す
  if (M5.Mic.isEnabled()) {
    Serial.printf("[%s] mic still enabled after end -> retry\n", kTag);
    M5.Mic.end();
    delay(20);
  }

  // ★重要：Speakerが元々無効だったケースでも、Mic側のI2Sドライバ残骸を確実に消す
  // これをやらないと、次の Speaker.begin / play で
  //   "register I2S object to platform failed"
  // が出ることがある。
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
    // PSRAM無い/足りない場合のフォールバック
    pcm_ = (int16_t*)malloc(bytes);
  }
  if (!pcm_) {
    Serial.printf("[%s] allocBuffer FAIL bytes=%u\n", kTag, (unsigned)bytes);
    maxSamples_ = 0;
    return false;
  }
  memset(pcm_, 0, bytes);
  capturedSamples_ = 0;
  peakAbs_ = 0;
  Serial.printf("[%s] allocBuffer OK bytes=%u samples=%u\n",
                kTag, (unsigned)bytes, (unsigned)maxSamples_);
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
    Serial.printf("[%s] task create FAIL\n", kTag);
    return false;
  }
  Serial.printf("[%s] task start\n", kTag);
  return true;
}

bool AudioRecorder::start(uint32_t nowMs) {
  if (!initialized_) begin();
  if (recording_) return false;

  // I2S owner: Mic（録音中はSpeakerのI2S利用をブロック）
  if (!i2sLocked_) {
    if (!I2SManager::instance().lockForMic("REC.start", 2000)) {
      I2SManager& m = I2SManager::instance();
      Serial.printf("[%s] start ok=0 (I2S lockForMic failed owner=%u depth=%lu ownerSite=%s)\n",
                    kTag,
                    (unsigned)m.owner(),
                    (unsigned long)m.depth(),
                    m.ownerCallsite() ? m.ownerCallsite() : "");
      return false;
    }
    i2sLocked_ = true;
  }


  stopSpeakerForRec_();  // 録音前にSpeakerを止めてI2Sを空ける

  // ★Mic を必ず begin（録音セッション毎に確実化）
  if (!ensureMicBegun_()) {
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.start.fail");
      i2sLocked_ = false;
    }
    return false;
  }

  if (!allocBuffer_()) {
    endMic_();                 // ★失敗時も解放
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.start.fail");
      i2sLocked_ = false;
    }
    return false;
  }
  if (!startTask_()) {
    endMic_();                 // ★失敗時も解放
    restoreSpeakerAfterRec_();
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
  Serial.printf("[%s] start now=%u\n", kTag, (unsigned)nowMs);
  Serial.printf("[%s] start ok=1\n", kTag);
  return true;
}


void AudioRecorder::requestStop_(bool cancel) {
  if (!initialized_ || !task_) return;

  // stop/cancel を出す前に非常脱出は解除
  forceAbort_ = false;

  if (cancel) cancelReq_ = true;
  else        stopReq_   = true;

  // タスクに起床通知（詰まり対策で複数回OK）
  xTaskNotifyGive(task_);
}

bool AudioRecorder::waitTaskDone_(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (recording_) {
    if ((millis() - t0) > timeoutMs) {
      mc_logf("[REC] waitTaskDone TIMEOUT (timeout=%lums samples=%u stopReq=%d cancelReq=%d)",
              (unsigned long)timeoutMs,
              (unsigned)capturedSamples_,
              stopReq_ ? 1 : 0,
              cancelReq_ ? 1 : 0);

      // ここがキモ：録音ループが返ってこない場合があるので非常脱出
      forceAbort_ = true;

      // 「録れてる」なら、結果は採用できるように recording_ を落として進める
      recording_ = false;

      // それでもタスクが詰まり続けるなら消す（安全策）
      if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
        mc_logf("[REC] task deleted by forceAbort");
      }
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}

// ===== REPLACE: AudioRecorder::stop(uint32_t nowMs) =====
bool AudioRecorder::stop(uint32_t nowMs) {
  if (!recording_) return false;

  Serial.printf("[%s] stop req\n", kTag);
  requestStop_(false);
  bool ok = waitTaskDone_(2000);

  stopMs_ = nowMs;

  // stopReqでタスクが止まっても、I2S内部がまだ録音中の可能性があるので少し待つ
  waitMicIdle_(200);
  Serial.printf("[%s] stop finalize mic: rec=%d en=%d\n", kTag,
                M5.Mic.isRecording() ? 1 : 0,
                M5.Mic.isEnabled() ? 1 : 0);

  // ★順序が重要：Mic -> end、（少し待つ）、Speaker -> begin
  endMic_();
  restoreSpeakerAfterRec_();

  if (i2sLocked_) {
    I2SManager::instance().unlock("REC.stop");
    i2sLocked_ = false;
  }

  // ok=1 系ログは 1回だけ（dur/samples/peak を全部ここに集約）
  Serial.printf("[%s] stop done ok=%d dur=%ums samples=%u peak=%d\n",
                kTag,
                ok ? 1 : 0,
                (unsigned)durationMs(),
                (unsigned)capturedSamples_,
                (int)peakAbs_);

  return ok;
}
// ===== /REPLACE =====


void AudioRecorder::cancel() {
  if (!recording_) {
    freeBuffer_();
    waitMicIdle_(100);
    endMic_();                 // ★ここも解放
    restoreSpeakerAfterRec_();
    if (i2sLocked_) {
      I2SManager::instance().unlock("REC.cancel(idle)");
      i2sLocked_ = false;
    }
    Serial.printf("[%s] cancel done (buffer freed)\n", kTag);
    return;
  }

  Serial.printf("[%s] cancel req\n", kTag);
  requestStop_(true);
  waitTaskDone_(2000);

  freeBuffer_();

  waitMicIdle_(200);
  Serial.printf("[%s] cancel finalize mic: rec=%d en=%d\n", kTag,
                M5.Mic.isRecording() ? 1 : 0,
                M5.Mic.isEnabled() ? 1 : 0);

  // ★順序：Mic end → Speaker復帰
  endMic_();
  restoreSpeakerAfterRec_();

  if (i2sLocked_) {
    I2SManager::instance().unlock("REC.cancel");
    i2sLocked_ = false;
  }

  Serial.printf("[%s] cancel done (buffer freed)\n", kTag);
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
  Serial.printf("[%s] saveWav ok path=%s bytes=%u\n", kTag, path, (unsigned)dataBytes);
  return true;
}

void AudioRecorder::taskEntry_(void* arg) {
  ((AudioRecorder*)arg)->taskLoop_();
  vTaskDelete(nullptr);
}

void AudioRecorder::taskLoop_() {
  for (;;) {
    // 通知待ち（録音開始 or stop/cancel）
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 非常脱出要求が出ていたら、フラグ掃除して待機に戻る
    if (forceAbort_) {
      stopReq_ = false;
      cancelReq_ = false;
      recording_ = false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool naturalEnd = false;  // ★stop()/cancel()要求ではなく自然終了したか

    // start() で recording_=true になったタイミングで録音を進める
    // recordにsampleRateを渡して「狙ったフォーマットのPCM」を得る
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

      // ★重要：M5Unifiedのrecord()は「録音要求→完了待ち」の使い方が安定。
      //   完了前に tmp を読むと、ゴミ/ノイズになり得る。
      bool submitted = M5.Mic.record(tmp, kChunk, sampleRate_, false);
      if (!submitted) {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      // 完了待ち（stop/cancel/abortも見て早めに抜ける）
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


      // バッファ満杯 or 時間満了
      const uint32_t elapsedMs = millis() - startMs_;
      if (capturedSamples_ >= maxSamples_ || elapsedMs >= (maxSeconds_ * 1000UL)) {
        mc_logf("[REC] buffer full/time -> stop (samples=%u peak=%d)",
                (unsigned)capturedSamples_, (int)peakAbs_);
        stopMs_ = millis();
        naturalEnd = true;     // ★自然終了
        recording_ = false;
        break;
      }


      vTaskDelay(pdMS_TO_TICKS(2));
    }

    // ★自然終了の場合：stop() が呼ばれない経路でも I2S を解放する
    if (naturalEnd) {
      waitMicIdle_(200);
      Serial.printf("[%s] autoStop finalize mic: rec=%d en=%d\n", kTag,
                    M5.Mic.isRecording() ? 1 : 0,
                    M5.Mic.isEnabled() ? 1 : 0);

      // Mic end → Speaker復帰 → I2S unlock（stop() と同等の後始末）
      endMic_();
      restoreSpeakerAfterRec_();

      if (i2sLocked_) {
        I2SManager::instance().unlock("REC.autoStop");
        i2sLocked_ = false;
      }

      Serial.printf("[%s] autoStop finalize done samples=%u peak=%d\n",
                    kTag, (unsigned)capturedSamples_, (int)peakAbs_);
    }



    // stop/cancel フラグ掃除
    stopReq_ = false;
    cancelReq_ = false;

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
