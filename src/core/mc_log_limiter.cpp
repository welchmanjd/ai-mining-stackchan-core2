// src/mc_log_limiter.cpp
// Module implementation.
#include "core/mc_log_limiter.h"
#include <string.h>
namespace mc_log_limiter {
// Keep this small; we only need a few noisy keys.
static constexpr uint8_t kSlots = 8;
struct Slot {
  const char* key_;
  uint32_t lastMs_;
  uint32_t suppressed_;
};
static Slot g_slots[kSlots];
static Slot* findOrAlloc_(const char* key) {
  if (!key || !*key) return nullptr;
  // 1) exact match (strcmp; keys are usually string literals)
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (g_slots[i].key_ && strcmp(g_slots[i].key_, key) == 0) return &g_slots[i];
  }
  // 2) empty slot
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (!g_slots[i].key_) {
      g_slots[i].key_ = key;
      g_slots[i].lastMs_ = 0;
      g_slots[i].suppressed_ = 0;
      return &g_slots[i];
    }
  }
  // 3) no slot left -> fallback to slot0 (best effort)
  g_slots[0].key_ = key;
  g_slots[0].lastMs_ = 0;
  g_slots[0].suppressed_ = 0;
  return &g_slots[0];
}
bool shouldLog(const char* key, uint32_t windowMs, uint32_t nowMs, uint32_t* out_suppressed) {
  if (out_suppressed) *out_suppressed = 0;
  Slot* s = findOrAlloc_(key);
  if (!s) return true; // no key -> no suppression
  // First time: log immediately.
  if (s->lastMs_ == 0) {
    s->lastMs_ = nowMs;
    return true;
  }
  const uint32_t elapsed = nowMs - s->lastMs_;
  if (elapsed < windowMs) {
    // Within window: suppress.
    s->suppressed_++;
    return false;
  }
  // Window expired: allow, and flush suppression count.
  if (out_suppressed) *out_suppressed = s->suppressed_;
  s->suppressed_ = 0;
  s->lastMs_ = nowMs;
  return true;
}
void resetAll() {
  for (uint8_t i = 0; i < kSlots; ++i) {
    g_slots[i].key_ = nullptr;
    g_slots[i].lastMs_ = 0;
    g_slots[i].suppressed_ = 0;
  }
}
} // namespace mc_log_limiter
