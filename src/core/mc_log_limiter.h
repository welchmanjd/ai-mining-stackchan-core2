// src/mc_log_limiter.h
// Module implementation.
// Small rate-limit helper for noisy logs.
//
// Goal:
// - Suppress the same log line within a window
// - When the window expires, emit ONE summary line about suppressed count
//
// NOTE:
// - This utility is intentionally tiny (no dynamic alloc, no STL).
// - Keys should be stable string literals.
#pragma once
#include <Arduino.h>
namespace mc_log_limiter {
// Returns true if caller should print the log now.
// If suppressed logs existed and the window expired, out_suppressed will be set (>0)
// and the caller should print a single summary line before the main log.
bool shouldLog(const char* key, uint32_t windowMs, uint32_t nowMs, uint32_t* out_suppressed);
// Manual reset (rarely needed).
void resetAll();
} // namespace mc_log_limiter
