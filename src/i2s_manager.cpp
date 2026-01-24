#include "i2s_manager.h"
#include "logging.h"

I2SManager& I2SManager::instance() {
  static I2SManager g;
  return g;
}

I2SManager::I2SManager() {
  mutex_ = xSemaphoreCreateRecursiveMutex();
  if (!mutex_) {
    mc_logf("[I2S] ERROR: create mutex failed");
  } else {
    mc_logf("[I2S] mutex created");
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

  // タイムアウトログ用：ブロック前のスナップショット
  const Owner snapOwner = owner_;
  const char* snapSite = owner_callsite_;
  const uint32_t snapSince = owner_since_ms_;

  const BaseType_t ok = xSemaphoreTakeRecursive(mutex_, ticks);
  const uint32_t waited = millis() - t0;

  if (ok != pdTRUE) {
    const uint32_t heldMs = (snapOwner == None) ? 0 : (millis() - snapSince);
    mc_logf("[I2S] lock FAIL want=%s waited=%lums cur=%s held=%lums curSite=%s reqSite=%s",
            ownerStr_(want),
            (unsigned long)waited,
            ownerStr_(snapOwner),
            (unsigned long)heldMs,
            snapSite ? snapSite : "",
            callsite ? callsite : "");
    LOG_EVT_INFO("I2S_OWNER", "lock_fail want=%s waited=%lums cur=%s held=%lums",
                 ownerStr_(want),
                 (unsigned long)waited,
                 ownerStr_(snapOwner),
                 (unsigned long)heldMs);
    return false;
  }

  const TaskHandle_t curTask = xTaskGetCurrentTaskHandle();

  // ---- Phase4の核心：owner整合性ルール（再入は owner一致のみ許可） ----
  // recursive mutex なので、同一タスクからの再入は xSemaphoreTakeRecursive() が即成功する。
  // その場合でも「owner不一致」の要求は deny して false を返す（Speaker中にMic開始など）。
  if (depth_ > 0) {
    const uint32_t heldMs = (owner_ == None) ? 0 : (millis() - owner_since_ms_);

    // 本来、depth_>0 で take に成功したなら同一タスク再入のはず。違うなら安全側でdeny。
    if (owner_task_ != curTask) {
      mc_logf("[I2S] lock DENY cross_task cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
              ownerStr_(owner_),
              ownerStr_(want),
              (unsigned long)depth_,
              (unsigned long)waited,
              (unsigned long)heldMs,
              owner_callsite_ ? owner_callsite_ : "",
              callsite ? callsite : "");
      LOG_EVT_INFO("I2S_OWNER", "deny_cross_task cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
                   ownerStr_(owner_),
                   ownerStr_(want),
                   (unsigned long)depth_,
                   (unsigned long)waited,
                   (unsigned long)heldMs,
                   owner_callsite_ ? owner_callsite_ : "",
                   callsite ? callsite : "");

      xSemaphoreGiveRecursive(mutex_);
      return false;
    }

    if (owner_ != want) {
      mc_logf("[I2S] lock DENY reenter_mismatch cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
              ownerStr_(owner_),
              ownerStr_(want),
              (unsigned long)depth_,
              (unsigned long)waited,
              (unsigned long)heldMs,
              owner_callsite_ ? owner_callsite_ : "",
              callsite ? callsite : "");
      LOG_EVT_INFO("I2S_OWNER", "deny_reenter cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
                   ownerStr_(owner_),
                   ownerStr_(want),
                   (unsigned long)depth_,
                   (unsigned long)waited,
                   (unsigned long)heldMs,
                   owner_callsite_ ? owner_callsite_ : "",
                   callsite ? callsite : "");

      // 注意：recursive take は成功しているので、必ず give してから return する（持ち逃げ防止）
      xSemaphoreGiveRecursive(mutex_);
      return false;
    }

    // 同ownerの再入のみ許可
    mc_logf("[I2S] lock reenter owner=%s depth=%lu waited=%lums reqSite=%s ownerSite=%s",
            ownerStr_(owner_),
            (unsigned long)depth_,
            (unsigned long)waited,
            callsite ? callsite : "",
            owner_callsite_ ? owner_callsite_ : "");
    depth_++;
    return true;
  }

  // first acquire => owner transition
  {
    const Owner prev = owner_;
    const uint32_t prevHeldMs = (prev == None) ? 0 : (millis() - owner_since_ms_);

    owner_ = want;
    owner_callsite_ = callsite ? callsite : "";
    owner_since_ms_ = millis();
    owner_task_ = curTask;

    mc_logf("[I2S] owner %s -> %s waited=%lums prevHeld=%lums site=%s",
            ownerStr_(prev),
            ownerStr_(want),
            (unsigned long)waited,
            (unsigned long)prevHeldMs,
            owner_callsite_);

    LOG_EVT_INFO("I2S_OWNER", "owner %s->%s waited=%lums prevHeld=%lums site=%s",
                 ownerStr_(prev),
                 ownerStr_(want),
                 (unsigned long)waited,
                 (unsigned long)prevHeldMs,
                 owner_callsite_);
  }

  depth_ = 1;
  return true;
}



void I2SManager::unlock(const char* callsite) {
  if (!mutex_) return;

  if (depth_ == 0) {
    mc_logf("[I2S] unlock WARN depth=0 site=%s", callsite ? callsite : "");
    return;
  }

  depth_--;

  if (depth_ == 0) {
    const Owner prev = owner_;
    const uint32_t heldMs = (prev == None) ? 0 : (millis() - owner_since_ms_);

    owner_ = None;
    owner_callsite_ = "";
    owner_since_ms_ = 0;
    owner_task_ = nullptr;

    mc_logf("[I2S] owner %s -> None held=%lums unlockSite=%s",
            ownerStr_(prev),
            (unsigned long)heldMs,
            callsite ? callsite : "");
    LOG_EVT_INFO("I2S_OWNER", "owner %s->None held=%lums unlockSite=%s",
                 ownerStr_(prev),
                 (unsigned long)heldMs,
                 callsite ? callsite : "");
  }

  xSemaphoreGiveRecursive(mutex_);
}
