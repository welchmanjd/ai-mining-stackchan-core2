// === src/i2s_manager.cpp : replace whole file ===
// Module implementation.
#include "audio/i2s_manager.h"
#include "core/logging.h"
I2SManager& I2SManager::instance() {
  static I2SManager g;
  return g;
}
I2SManager::I2SManager() {
  mutex_ = xSemaphoreCreateRecursiveMutex();
  if (!mutex_) {
    MC_LOGE("I2S", "create mutex failed");
  } else {
    MC_LOGD("I2S", "mutex created");
  }
}
const char* I2SManager::ownerStr_(Owner o) const {
  switch (o) {
    case None:    return "None";
    case Mic:     return "Mic";
    case Speaker: return "Speaker";
    default:      return "?";
  }
}
bool I2SManager::lockForMic(const char* callsite, uint32_t timeoutMs) {
  return lock_(Mic, callsite, timeoutMs);
}
bool I2SManager::lockForSpeaker(const char* callsite, uint32_t timeoutMs) {
  return lock_(Speaker, callsite, timeoutMs);
}
bool I2SManager::lock_(Owner want, const char* callsite, uint32_t timeoutMs) {
  if (!mutex_) return false;
  const uint32_t t0 = millis();
  const TickType_t ticks = (timeoutMs == 0) ? 0 : pdMS_TO_TICKS(timeoutMs);
  const Owner snapOwner = owner_;
  const char* snapSite = ownerCallsite_;
  const uint32_t snapSince = ownerSinceMs_;
  const BaseType_t ok = xSemaphoreTakeRecursive(mutex_, ticks);
  const uint32_t waited = millis() - t0;
  if (ok != pdTRUE) {
    const uint32_t heldMs = (snapOwner == None) ? 0 : (millis() - snapSince);
    LOG_EVT_INFO("I2S_OWNER", "acquire_fail want=%s cur=%s waited=%lums",
                 ownerStr_(want),
                 ownerStr_(snapOwner),
                 (unsigned long)waited);
    LOG_EVT_DEBUG("I2S_OWNER",
                  "acquire_fail_d want=%s cur=%s waited=%lums held=%lums curSite=%s reqSite=%s",
                  ownerStr_(want),
                  ownerStr_(snapOwner),
                  (unsigned long)waited,
                  (unsigned long)heldMs,
                  snapSite ? snapSite : "",
                  callsite ? callsite : "");
    MC_LOGD("I2S",
            "lock FAIL want=%s waited=%lums cur=%s held=%lums curSite=%s reqSite=%s",
            ownerStr_(want),
            (unsigned long)waited,
            ownerStr_(snapOwner),
            (unsigned long)heldMs,
            snapSite ? snapSite : "",
            callsite ? callsite : "");
    return false;
  }
  const TaskHandle_t curTask = xTaskGetCurrentTaskHandle();
  if (depth_ > 0) {
    // Recursive lock is allowed only by the same task and same owner type.
    const uint32_t heldMs = (owner_ == None) ? 0 : (millis() - ownerSinceMs_);
    if (ownerTask_ != curTask) {
      LOG_EVT_INFO("I2S_OWNER", "deny reason=cross_task cur=%s want=%s depth=%lu",
                   ownerStr_(owner_),
                   ownerStr_(want),
                   (unsigned long)depth_);
      LOG_EVT_DEBUG("I2S_OWNER",
                    "deny_cross_task_d cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
                    ownerStr_(owner_),
                    ownerStr_(want),
                    (unsigned long)depth_,
                    (unsigned long)waited,
                    (unsigned long)heldMs,
                    ownerCallsite_ ? ownerCallsite_ : "",
                    callsite ? callsite : "");
      MC_LOGD("I2S",
              "lock DENY cross_task cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
              ownerStr_(owner_),
              ownerStr_(want),
              (unsigned long)depth_,
              (unsigned long)waited,
              (unsigned long)heldMs,
              ownerCallsite_ ? ownerCallsite_ : "",
              callsite ? callsite : "");
      xSemaphoreGiveRecursive(mutex_);
      return false;
    }
    if (owner_ != want) {
      LOG_EVT_INFO("I2S_OWNER", "deny reason=reenter_mismatch cur=%s want=%s depth=%lu",
                   ownerStr_(owner_),
                   ownerStr_(want),
                   (unsigned long)depth_);
      LOG_EVT_DEBUG("I2S_OWNER",
                    "deny_reenter_d cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
                    ownerStr_(owner_),
                    ownerStr_(want),
                    (unsigned long)depth_,
                    (unsigned long)waited,
                    (unsigned long)heldMs,
                    ownerCallsite_ ? ownerCallsite_ : "",
                    callsite ? callsite : "");
      MC_LOGD("I2S",
              "lock DENY reenter_mismatch cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
              ownerStr_(owner_),
              ownerStr_(want),
              (unsigned long)depth_,
              (unsigned long)waited,
              (unsigned long)heldMs,
              ownerCallsite_ ? ownerCallsite_ : "",
              callsite ? callsite : "");
      xSemaphoreGiveRecursive(mutex_);
      return false;
    }
    MC_LOGD("I2S", "lock reenter owner=%s depth=%lu waited=%lums reqSite=%s ownerSite=%s",
            ownerStr_(owner_),
            (unsigned long)depth_,
            (unsigned long)waited,
            callsite ? callsite : "",
            ownerCallsite_ ? ownerCallsite_ : "");
    depth_++;
    return true;
  }
  // First acquire => owner transition.
  {
    const Owner prev = owner_;
    const uint32_t prevHeldMs = (prev == None) ? 0 : (millis() - ownerSinceMs_);
    owner_ = want;
    ownerCallsite_ = callsite ? callsite : "";
    ownerSinceMs_ = millis();
    ownerTask_ = curTask;
    LOG_EVT_INFO("I2S_OWNER", "acquire owner=%s waited=%lums site=%s",
                 ownerStr_(want),
                 (unsigned long)waited,
                 ownerCallsite_);
    LOG_EVT_DEBUG("I2S_OWNER", "acquire_d prev=%s owner=%s waited=%lums prevHeld=%lums site=%s",
                  ownerStr_(prev),
                  ownerStr_(want),
                  (unsigned long)waited,
                  (unsigned long)prevHeldMs,
                  ownerCallsite_);
    MC_LOGD("I2S", "owner %s -> %s waited=%lums prevHeld=%lums site=%s",
            ownerStr_(prev),
            ownerStr_(want),
            (unsigned long)waited,
            (unsigned long)prevHeldMs,
            ownerCallsite_);
  }
  depth_ = 1;
  return true;
}
void I2SManager::unlock(const char* callsite) {
  if (!mutex_) return;
  if (depth_ == 0) {
    MC_LOGW("I2S", "unlock WARN depth=0 site=%s", callsite ? callsite : "");
    return;
  }
  depth_--;
  if (depth_ == 0) {
    const Owner prev = owner_;
    const uint32_t heldMs = (prev == None) ? 0 : (millis() - ownerSinceMs_);
    owner_ = None;
    ownerCallsite_ = "";
    ownerSinceMs_ = 0;
    ownerTask_ = nullptr;
    LOG_EVT_INFO("I2S_OWNER", "release owner=%s held=%lums unlockSite=%s",
                 ownerStr_(prev),
                 (unsigned long)heldMs,
                 callsite ? callsite : "");
    LOG_EVT_DEBUG("I2S_OWNER", "release_d owner=%s held=%lums unlockSite=%s",
                  ownerStr_(prev),
                  (unsigned long)heldMs,
                  callsite ? callsite : "");
    MC_LOGD("I2S", "owner %s -> None held=%lums unlockSite=%s",
            ownerStr_(prev),
            (unsigned long)heldMs,
            callsite ? callsite : "");
  }
  xSemaphoreGiveRecursive(mutex_);
}
