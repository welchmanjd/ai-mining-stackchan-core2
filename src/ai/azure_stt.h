// src/azure_stt.h
// Module implementation.
#pragma once
#include <stdint.h>
#include <Arduino.h>

#include "config/config.h"
namespace azure_stt {
struct SttResult {
  bool ok_ = false;
  String text_;
  String err_;
  int status_ = 0;
};
static constexpr uint32_t kDefaultTimeoutMs = MC_AI_STT_TIMEOUT_MS;
static constexpr size_t kMaxKeepChars = 200;
static constexpr size_t kLogHeadChars = 60;
SttResult transcribePcm16Mono(
    const int16_t* pcm,
    size_t samples,
    int sampleRate = 16000,
    uint32_t timeoutMs = kDefaultTimeoutMs);
} // namespace azure_stt
