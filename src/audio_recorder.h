#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// 録音は「薄いクラス」：開始/停止/破棄 と バッファ参照だけを提供する
class AudioRecorder {
public:
  AudioRecorder() = default;

  // I2S/Mic 初期化（M5Unified想定）
  bool begin();

  // 録音開始（最大秒数ぶんのバッファ確保もここで）
  bool start(uint32_t nowMs);

  // 録音停止（確定）
  bool stop(uint32_t nowMs);

  // キャンセル（破棄）
  void cancel();

  bool isRecording() const { return recording_; }

  const int16_t* data() const { return pcm_; }
  size_t samples() const { return capturedSamples_; }
  uint32_t durationMs() const;

  // 任意：WAVで保存（LittleFS 1ファイルだけ上書き）
  bool saveWavToFs(const char* path);

private:
  bool allocBuffer_();
  void freeBuffer_();
  bool startTask_();
  void requestStop_(bool cancel);
  bool waitTaskDone_(uint32_t timeoutMs);

  static void taskEntry_(void* arg);
  void taskLoop_();

private:
  // 設定
  uint32_t sampleRate_ = 16000;
  uint32_t maxSeconds_ = 10;

  // バッファ
  int16_t* pcm_ = nullptr;
  size_t maxSamples_ = 0;
  volatile size_t capturedSamples_ = 0;

  // 状態
  volatile bool initialized_ = false;
  volatile bool recording_ = false;
  volatile bool stopReq_ = false;
  volatile bool cancelReq_ = false;

  uint32_t startMs_ = 0;
  uint32_t stopMs_  = 0;

  // task
  TaskHandle_t task_ = nullptr;
};
