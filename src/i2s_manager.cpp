// === src/i2s_manager.cpp : replace whole file ===
#include "i2s_manager.h"
#include "logging.h"

I2SManager& I2SManager::instance() {
  static I2SManager g;
  return g;
}

I2SManager::I2SManager() {
  mutex_ = xSemaphoreCreateRecursiveMutex();
  if (!mutex_) {
    MC_LOGE("I2S", "create mutex failed");
  } else {
    // 起動時に1回だけなので DIAG 寄せ
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

  // タイムアウトログ用：ブロック前のスナップショット
  const Owner snapOwner = owner_;
  const char* snapSite = owner_callsite_;
  const uint32_t snapSince = owner_since_ms_;

  const BaseType_t ok = xSemaphoreTakeRecursive(mutex_, ticks);
  const uint32_t waited = millis() - t0;

  if (ok != pdTRUE) {
    const uint32_t heldMs = (snapOwner == None) ? 0 : (millis() - snapSince);

    // EVT中心（L0でも残る）：要点のみ
    LOG_EVT_INFO("I2S_OWNER", "acquire_fail want=%s cur=%s waited=%lums",
                 ownerStr_(want),
                 ownerStr_(snapOwner),
                 (unsigned long)waited);

    // 詳細は L2+（EVT_DEBUG_ENABLED）へ
    LOG_EVT_DEBUG("I2S_OWNER",
                  "acquire_fail_d want=%s cur=%s waited=%lums held=%lums curSite=%s reqSite=%s",
                  ownerStr_(want),
                  ownerStr_(snapOwner),
                  (unsigned long)waited,
                  (unsigned long)heldMs,
                  snapSite ? snapSite : "",
                  callsite ? callsite : "");

    // 失敗詳細は DIAG へ（壁紙化防止）
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

  // ---- Phase4の核心：owner整合性ルール（再入は owner一致のみ許可） ----
  // recursive mutex なので、同一タスクからの再入は xSemaphoreTakeRecursive() が即成功する。
  // その場合でも「owner不一致」の要求は deny して false を返す（Speaker中にMic開始など）。
  if (depth_ > 0) {
    const uint32_t heldMs = (owner_ == None) ? 0 : (millis() - owner_since_ms_);

    // 本来、depth_>0 で take に成功したなら同一タスク再入のはず。違うなら安全側でdeny。
    if (owner_task_ != curTask) {
      // EVT中心（L0でも残る）：要点のみ
      LOG_EVT_INFO("I2S_OWNER", "deny reason=cross_task cur=%s want=%s depth=%lu",
                   ownerStr_(owner_),
                   ownerStr_(want),
                   (unsigned long)depth_);

      // 詳細は L2+
      LOG_EVT_DEBUG("I2S_OWNER",
                    "deny_cross_task_d cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
                    ownerStr_(owner_),
                    ownerStr_(want),
                    (unsigned long)depth_,
                    (unsigned long)waited,
                    (unsigned long)heldMs,
                    owner_callsite_ ? owner_callsite_ : "",
                    callsite ? callsite : "");

      MC_LOGD("I2S",
              "lock DENY cross_task cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
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
      // EVT中心（L0でも残る）：要点のみ
      LOG_EVT_INFO("I2S_OWNER", "deny reason=reenter_mismatch cur=%s want=%s depth=%lu",
                   ownerStr_(owner_),
                   ownerStr_(want),
                   (unsigned long)depth_);

      // 詳細は L2+
      LOG_EVT_DEBUG("I2S_OWNER",
                    "deny_reenter_d cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
                    ownerStr_(owner_),
                    ownerStr_(want),
                    (unsigned long)depth_,
                    (unsigned long)waited,
                    (unsigned long)heldMs,
                    owner_callsite_ ? owner_callsite_ : "",
                    callsite ? callsite : "");

      MC_LOGD("I2S",
              "lock DENY reenter_mismatch cur=%s want=%s depth=%lu waited=%lums held=%lums curSite=%s reqSite=%s",
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

    // 同ownerの再入のみ許可（詳細は DIAG）
    MC_LOGD("I2S", "lock reenter owner=%s depth=%lu waited=%lums reqSite=%s ownerSite=%s",
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

    // EVT中心（L0でも残る）：要点のみ
    LOG_EVT_INFO("I2S_OWNER", "acquire owner=%s waited=%lums site=%s",
                 ownerStr_(want),
                 (unsigned long)waited,
                 owner_callsite_);

    // 詳細は L2+
    LOG_EVT_DEBUG("I2S_OWNER", "acquire_d prev=%s owner=%s waited=%lums prevHeld=%lums site=%s",
                  ownerStr_(prev),
                  ownerStr_(want),
                  (unsigned long)waited,
                  (unsigned long)prevHeldMs,
                  owner_callsite_);

    MC_LOGD("I2S", "owner %s -> %s waited=%lums prevHeld=%lums site=%s",
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
    // 想定外（ただし復旧可能）なので L1(WARN)
    MC_LOGW("I2S", "unlock WARN depth=0 site=%s", callsite ? callsite : "");
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

    // EVT中心（L0でも残る）：要点のみ
    LOG_EVT_INFO("I2S_OWNER", "release owner=%s held=%lums unlockSite=%s",
                 ownerStr_(prev),
                 (unsigned long)heldMs,
                 callsite ? callsite : "");

    // 詳細は L2+
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
