// src/logging.h
#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// シンプルな printf 互換ログ出力（ヘッダ内インライン実装）
inline void mc_logf(const char* fmt, ...) {
  char buf[256];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  Serial.println(buf);
}

// ===== Event logging helpers =====
//
// Toggle DEBUG output by defining EVT_DEBUG_ENABLED (1/0) before including this header.
// Default is OFF to keep normal logs clean.
#ifndef EVT_DEBUG_ENABLED
#define EVT_DEBUG_ENABLED 0
#endif

// Heartbeat logs can be enabled separately (also requires EVT_DEBUG_ENABLED=1).
#ifndef EVT_HEARTBEAT_ENABLED
#define EVT_HEARTBEAT_ENABLED 0
#endif

#define LOG_EVT_INFO(tag, fmt, ...) \
  mc_logf("[EVT] " tag " " fmt, ##__VA_ARGS__)

#define LOG_EVT_DEBUG(tag, fmt, ...) \
  do { if (EVT_DEBUG_ENABLED) mc_logf("[EVT] " tag " " fmt, ##__VA_ARGS__); } while (0)

// Heartbeat (wallpaper) logs: OFF by default. Enable only when needed.
#define LOG_EVT_HEARTBEAT(tag, fmt, ...) \
  do { if (EVT_DEBUG_ENABLED && EVT_HEARTBEAT_ENABLED) mc_logf("[EVT] " tag " " fmt, ##__VA_ARGS__); } while (0)
