// src/i2s_manager.h
// Module implementation.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
class I2SManager {
public:
  enum Owner : uint8_t { None = 0, Mic = 1, Speaker = 2 };
  static I2SManager& instance();
  bool lockForMic(const char* callsite, uint32_t timeoutMs = 2000);
  bool lockForSpeaker(const char* callsite, uint32_t timeoutMs = 2000);
  void unlock(const char* callsite);
  Owner owner() const { return owner_; }
  const char* ownerCallsite() const { return owner_callsite_; }
  uint32_t ownerSinceMs() const { return owner_since_ms_; }
  uint32_t depth() const { return depth_; }
private:
  I2SManager();
  bool lock_(Owner want, const char* callsite, uint32_t timeoutMs);
  const char* ownerStr_(Owner o) const;
private:
  SemaphoreHandle_t mutex_ = nullptr; // recursive mutex
  volatile Owner owner_ = None;
  const char* owner_callsite_ = "";
  uint32_t owner_since_ms_ = 0;
  TaskHandle_t owner_task_ = nullptr;
  uint32_t depth_ = 0;
};
