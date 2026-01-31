// src/audio_recorder.h
#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "config/config.h"
class AudioRecorder {
public:
  AudioRecorder() = default;
  bool begin();
  bool start(uint32_t nowMs);
  bool stop(uint32_t nowMs);
  void cancel();
  bool isRecording() const { return recording_; }
  const int16_t* data() const { return pcm_; }
  size_t samples() const { return capturedSamples_; }
  uint32_t durationMs() const;
  bool saveWavToFs(const char* path);
private:
  bool allocBuffer_();
  void freeBuffer_();
  bool startTask_();
  void requestStop_(bool cancel);
  bool waitTaskDone_(uint32_t timeoutMs);
  void stopSpeakerForRec_();
  void restoreSpeakerAfterRec_();
  static void taskEntry_(void* arg);
  void taskLoop_();
  bool ensureMicBegun_();
  void endMic_();
  bool micBegun_ = false;
  uint32_t sampleRate_ = MC_AI_REC_SAMPLE_RATE;
  uint32_t maxSeconds_ = 10;
  int16_t* pcm_ = nullptr;
  size_t maxSamples_ = 0;
  volatile size_t capturedSamples_ = 0;
  volatile bool recording_ = false;
  volatile bool stopReq_ = false;
  volatile bool cancelReq_ = false;
  volatile bool forceAbort_ = false;
  uint32_t startMs_ = 0;
  uint32_t stopMs_ = 0;
  uint8_t savedSpkVolume_ = 128;
  bool savedSpkVolumeValid_ = false;
  bool speakerEndedByRec_ = false;
  bool initialized_ = false;
  bool i2sLocked_ = false;
  int peakAbs_ = 0;
  TaskHandle_t task_ = nullptr;
};
