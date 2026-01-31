// Module implementation.
#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>

#include "utils/mc_log_limiter.h"
// ================================
// ================================
inline void mc_logf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
}
// ================================
//   0: QUIET, 1: NORMAL, 2: DIAG, 3: TRACE
// ================================
#ifndef MC_LOG_LEVEL
#define MC_LOG_LEVEL 1
#endif
#if (MC_LOG_LEVEL < 0) || (MC_LOG_LEVEL > 3)
#error "MC_LOG_LEVEL must be 0..3"
#endif
// ================================
// ================================
#define MC__LOG(prefix, tag, fmt, ...) \
  mc_logf(prefix " " tag " " fmt, ##__VA_ARGS__)
// Always on (errors)
#define MC_LOGE(tag, fmt, ...) MC__LOG("[E]", tag, fmt, ##__VA_ARGS__)
// L1+
#if (MC_LOG_LEVEL >= 1)
  #define MC_LOGW(tag, fmt, ...) MC__LOG("[W]", tag, fmt, ##__VA_ARGS__)
  #define MC_LOGI(tag, fmt, ...) MC__LOG("[I]", tag, fmt, ##__VA_ARGS__)
#else
  #define MC_LOGW(tag, fmt, ...) do {} while (0)
  #define MC_LOGI(tag, fmt, ...) do {} while (0)
#endif
// L2+
#if (MC_LOG_LEVEL >= 2)
  #define MC_LOGD(tag, fmt, ...) MC__LOG("[D]", tag, fmt, ##__VA_ARGS__)
#else
  #define MC_LOGD(tag, fmt, ...) do {} while (0)
#endif
// L3 only
#if (MC_LOG_LEVEL >= 3)
  #define MC_LOGT(tag, fmt, ...) MC__LOG("[T]", tag, fmt, ##__VA_ARGS__)
#else
  #define MC_LOGT(tag, fmt, ...) do {} while (0)
#endif
// ================================
// ================================
#define MC_EVT(tag, fmt, ...) \
  mc_logf("[EVT] " tag " " fmt, ##__VA_ARGS__)
#if (MC_LOG_LEVEL >= 2)
  #define MC_EVT_D(tag, fmt, ...) \
    mc_logf("[EVT] " tag " " fmt, ##__VA_ARGS__)
#else
  #define MC_EVT_D(tag, fmt, ...) do {} while (0)
#endif
// ================================
// ================================
//
#ifndef EVT_DEBUG_ENABLED
  #if (MC_LOG_LEVEL >= 2)
    #define EVT_DEBUG_ENABLED 1
  #else
    #define EVT_DEBUG_ENABLED 0
  #endif
#endif
#ifndef EVT_HEARTBEAT_ENABLED
#define EVT_HEARTBEAT_ENABLED 0
#endif
#define LOG_EVT_INFO(tag, fmt, ...) \
  MC_EVT(tag, fmt, ##__VA_ARGS__)
#define LOG_EVT_DEBUG(tag, fmt, ...) \
  do { if (EVT_DEBUG_ENABLED) MC_EVT_D(tag, fmt, ##__VA_ARGS__); } while (0)
#define LOG_EVT_HEARTBEAT(tag, fmt, ...) \
  do { if (EVT_HEARTBEAT_ENABLED) MC_LOGT(tag, fmt, ##__VA_ARGS__); } while (0)
// ================================
// ================================
#ifndef TOUCH_DEBUG_ENABLED
  #if (MC_LOG_LEVEL >= 3)
    #define TOUCH_DEBUG_ENABLED 1
  #else
    #define TOUCH_DEBUG_ENABLED 0
  #endif
#endif
#define LOG_TOUCH_DEBUG(fmt, ...) \
  do { if (TOUCH_DEBUG_ENABLED) mc_logf("[TOUCH] " fmt, ##__VA_ARGS__); } while (0)
// ================================
// ================================
#if (MC_LOG_LEVEL >= 1)
  #define MC_LOGI_RL(key, windowMs, tag, fmt, ...)                                        \
    do {                                                                                  \
      uint32_t _mc_sup = 0;                                                               \
      uint32_t _mc_now = (uint32_t)millis();                                              \
      if (mc_log_limiter::shouldLog((key), (windowMs), _mc_now, &_mc_sup)) {                \
        if (_mc_sup > 0) {                                                                \
          mc_logf("[I] " tag " suppressed %lu", (unsigned long)_mc_sup);                  \
        }                                                                                 \
        mc_logf("[I] " tag " " fmt, ##__VA_ARGS__);                                       \
      }                                                                                   \
    } while (0)
#else
  #define MC_LOGI_RL(key, windowMs, tag, fmt, ...) do {} while (0)
#endif
