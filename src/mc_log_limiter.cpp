// src/mc_log_limiter.cpp

#include "mc_log_limiter.h"

#include <string.h>

namespace McLogLimiter {

// Keep this small; we only need a few noisy keys.
static constexpr uint8_t kSlots = 8;

struct Slot {
  const char* key;
  uint32_t lastMs;
  uint32_t suppressed;
};

static Slot g_slots[kSlots];

static Slot* findOrAlloc_(const char* key) {
  if (!key || !*key) return nullptr;

  // 1) exact match (strcmp; keys are usually string literals)
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (g_slots[i].key && strcmp(g_slots[i].key, key) == 0) return &g_slots[i];
  }

  // 2) empty slot
  for (uint8_t i = 0; i < kSlots; ++i) {
    if (!g_slots[i].key) {
      g_slots[i].key = key;
      g_slots[i].lastMs = 0;
      g_slots[i].suppressed = 0;
      return &g_slots[i];
    }
  }

  // 3) no slot left -> fallback to slot0 (best effort)
  g_slots[0].key = key;
  g_slots[0].lastMs = 0;
  g_slots[0].suppressed = 0;
  return &g_slots[0];
}

bool shouldLog(const char* key, uint32_t windowMs, uint32_t nowMs, uint32_t* out_suppressed) {
  if (out_suppressed) *out_suppressed = 0;

  Slot* s = findOrAlloc_(key);
  if (!s) return true; // no key -> no suppression

  // First time: log immediately.
  if (s->lastMs == 0) {
    s->lastMs = nowMs;
    return true;
  }

  const uint32_t elapsed = nowMs - s->lastMs;
  if (elapsed < windowMs) {
    // Within window: suppress.
    s->suppressed++;
    return false;
  }

  // Window expired: allow, and flush suppression count.
  if (out_suppressed) *out_suppressed = s->suppressed;
  s->suppressed = 0;
  s->lastMs = nowMs;
  return true;
}

void resetAll() {
  for (uint8_t i = 0; i < kSlots; ++i) {
    g_slots[i].key = nullptr;
    g_slots[i].lastMs = 0;
    g_slots[i].suppressed = 0;
  }
}

} // namespace McLogLimiter
