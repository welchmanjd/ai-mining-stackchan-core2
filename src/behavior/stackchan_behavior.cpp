// src/stackchan_behavior.cpp
// Module implementation.
#include "behavior/stackchan_behavior.h"
#include "core/logging.h"
#include "config/config.h"
namespace {
const char* priorityName(ReactionPriority p) {
  switch (p) {
    case ReactionPriority::Low:    return "Low";
    case ReactionPriority::Normal: return "Normal";
    case ReactionPriority::High:   return "High";
    default: return "Unknown";
  }
}
const char* eventName(StackchanEventType ev) {
  switch (ev) {
    case StackchanEventType::ShareAccepted:    return "ShareAccepted";
    case StackchanEventType::PoolDisconnected: return "PoolDisconnected";
    case StackchanEventType::IdleTick:         return "IdleTick";
    case StackchanEventType::InfoPool:         return "InfoPool";
    case StackchanEventType::InfoPing:         return "InfoPing";
    case StackchanEventType::InfoHashrate:     return "InfoHashrate";
    case StackchanEventType::InfoShares:       return "InfoShares";
    case StackchanEventType::InfoMiningOff:    return "InfoMiningOff";
    case StackchanEventType::None:             return "None";
    case StackchanEventType::Placeholder:      return "Placeholder";
    default:                                   return "Unknown";
  }
}
String shortenText(const String& s, size_t maxChars, size_t& lenOut) {
  lenOut = s.length();
  if (s.length() <= maxChars) return s;
  String out;
  for (size_t i = 0; i < maxChars && i < s.length(); ++i) {
    out += s.charAt(i);
  }
  return out;
}
}  // namespace
void StackchanBehavior::update(const UIMining::PanelData& panel, uint32_t nowMs) {
  // snapshot (for bubble-only formatting)
  infoHrKh_     = panel.hrKh_;
  infoPingMs_   = panel.pingMs_;
  infoAccepted_ = panel.accepted_;
  infoRejected_ = panel.rejected_;
  infoPoolName_ = panel.poolName_;
  if (!poolInit_) {
    poolInit_ = true;
    lastPoolAlive_ = panel.poolAlive_;
    lastEventMs_ = nowMs;
    nextInfoMs_ = nowMs + 15000;
    infoIndex_  = 0;
  }
  if (!panel.miningEnabled_) {
    const uint32_t infoPeriodMs = 15000;
    if (nextInfoMs_ == 0) nextInfoMs_ = nowMs;
    if ((int32_t)(nowMs - nextInfoMs_) >= 0) {
      triggerEvent(StackchanEventType::InfoMiningOff, nowMs);
      nextInfoMs_ = nowMs + infoPeriodMs;
    }
    lastPoolAlive_ = panel.poolAlive_;
    return;
  }
  // Detect: new accepted share
  if (panel.accepted_ != lastAccepted_) {
    if (panel.accepted_ > lastAccepted_) {
      triggerEvent(StackchanEventType::ShareAccepted, nowMs);
    }
    lastAccepted_ = panel.accepted_;
  }
  // Detect: pool disconnected (true -> false)
  if (poolInit_ && lastPoolAlive_ && !panel.poolAlive_) {
    const bool isTimeoutNoFeedback =
        (panel.poolDiag_ == "No result response from the pool.");
    if (!isTimeoutNoFeedback) {
      triggerEvent(StackchanEventType::PoolDisconnected, nowMs);
    } else {
      LOG_EVT_INFO("EVT_BEH_SUPPRESS_POOL_DISCONNECT",
                   "reason=timeout_no_feedback");
    }
  }
  lastPoolAlive_ = panel.poolAlive_;
  // ---- periodic bubble-only info rotation (15s): POOL -> PING -> HR -> SHR ----
  const uint32_t infoPeriodMs = 15000;
  if (nextInfoMs_ == 0) nextInfoMs_ = nowMs + infoPeriodMs;
  if ((int32_t)(nowMs - nextInfoMs_) >= 0) {
    StackchanEventType ev = StackchanEventType::InfoPool;
    switch (infoIndex_ & 0x03) {
      case 0: ev = StackchanEventType::InfoPool;     break;
      case 1: ev = StackchanEventType::InfoPing;     break;
      case 2: ev = StackchanEventType::InfoHashrate; break;
      case 3: ev = StackchanEventType::InfoShares;   break;
      default: break;
    }
    infoIndex_ = (uint8_t)((infoIndex_ + 1) & 0x03);
    nextInfoMs_ = nowMs + infoPeriodMs;
    triggerEvent(ev, nowMs);
  }
  const uint32_t idleMs = 30000;
  if ((uint32_t)(nowMs - lastEventMs_) >= idleMs) {
    triggerEvent(StackchanEventType::IdleTick, nowMs);
  }
}
void StackchanBehavior::setTtsSpeaking(bool speaking) {
  ttsSpeaking_ = speaking;
}
bool StackchanBehavior::popReaction(StackchanReaction* out) {
  if (!out || !hasPending_) return false;
  // If TTS is speaking, drop Low only when it would speak; bubble-only stays.
  if (ttsSpeaking_ && pending_.priority_ == ReactionPriority::Low && pending_.speak_) {
    LOG_EVT_INFO("EVT_BEH_DROP_LOW_WHILE_BUSY",
                 "rid=%lu type=%s prio=%s speak=%d",
                 (unsigned long)pending_.rid_,
                 eventName(pending_.evType_),
                 priorityName(pending_.priority_),
                 pending_.speak_ ? 1 : 0);
    hasPending_ = false;
    return false;
  }
  *out = pending_;
  hasPending_ = false;
  return true;
}
void StackchanBehavior::triggerEvent(StackchanEventType ev, uint32_t nowMs) {
  // Decide -> enqueue one-slot reaction (overwrite).
  StackchanReaction r;
  bool emit = false;
  r.evType_ = ev;
  r.rid_ = nextRid_++;
  if (nextRid_ == 0) nextRid_ = 1;  // avoid 0
  switch (ev) {
    case StackchanEventType::ShareAccepted:
      r.priority_   = ReactionPriority::High;
      r.expression_ = m5avatar::Expression::Happy;
      r.speechText_ = appConfig().shareAcceptedText_;
      r.speak_      = true;
      emit = true;
      break;
    case StackchanEventType::PoolDisconnected:
      r.priority_   = ReactionPriority::High;
      r.expression_ = m5avatar::Expression::Doubt;
      r.speechText_ = "プールが切れたみたい…";
      r.speak_      = true;
      emit = true;
      break;
    case StackchanEventType::InfoPool: {
      r.priority_   = ReactionPriority::Low;
      r.expression_ = m5avatar::Expression::Neutral;
      String name = infoPoolName_;
      if (!name.length()) name = "unknown";
      r.speechText_ = "POOL:" + name;
      r.speak_      = false;
      emit = true;
      break;
    }
    case StackchanEventType::InfoPing: {
      r.priority_   = ReactionPriority::Low;
      r.expression_ = m5avatar::Expression::Neutral;
      if (infoPingMs_ >= 0.0f) {
        r.speechText_ = "PING:" + String((int)(infoPingMs_ + 0.5f)) + "ms";
      } else {
        r.speechText_ = "PING:--";
      }
      r.speak_      = false;
      emit = true;
      break;
    }
    case StackchanEventType::InfoHashrate: {
      r.priority_   = ReactionPriority::Low;
      r.expression_ = m5avatar::Expression::Neutral;
      r.speechText_ = "HR:" + String(infoHrKh_, 1) + "kH/s";
      r.speak_      = false;
      emit = true;
      break;
    }
    case StackchanEventType::InfoShares: {
      r.priority_   = ReactionPriority::Low;
      r.expression_ = m5avatar::Expression::Neutral;
      r.speechText_ = "SHR:" + String(infoAccepted_) + "/" + String(infoRejected_);
      r.speak_      = false;
      emit = true;
      break;
    }
    case StackchanEventType::InfoMiningOff: {
      r.priority_   = ReactionPriority::Low;
      r.expression_ = m5avatar::Expression::Neutral;
      r.speechText_ = "掘ってないよ";
      r.speak_      = false;
      emit = true;
      break;
    }
    case StackchanEventType::IdleTick:
      r.priority_   = ReactionPriority::Low;
      r.expression_ = m5avatar::Expression::Neutral;
      r.speechText_ = "......";
      r.speak_      = false;
      emit = true;
      break;
    default:
      break;
  }
  if (emit) {
    size_t newLen = 0, oldLen = 0;
    const String newShort = shortenText(r.speechText_, 16, newLen);
    if (!hasPending_ || r.priority_ >= pending_.priority_) {
      if (hasPending_) {
        const String oldShort = shortenText(pending_.speechText_, 16, oldLen);
        const char* reason = (r.priority_ > pending_.priority_) ? "prio_win" : "same_prio_latest";
        LOG_EVT_INFO("EVT_BEH_REPLACE",
                     "old_rid=%lu old_type=%s old_prio=%s old_speak=%d old_len=%u old_text=%s "
                     "new_rid=%lu new_type=%s new_prio=%s new_speak=%d new_len=%u new_text=%s "
                     "reason=%s",
                     (unsigned long)pending_.rid_, eventName(pending_.evType_), priorityName(pending_.priority_),
                     pending_.speak_ ? 1 : 0, (unsigned)oldLen, oldShort.c_str(),
                     (unsigned long)r.rid_, eventName(r.evType_), priorityName(r.priority_),
                     r.speak_ ? 1 : 0, (unsigned)newLen, newShort.c_str(),
                     reason);
      } else {
        LOG_EVT_INFO("EVT_BEH_EMIT",
                     "rid=%lu type=%s prio=%s speak=%d len=%u text=%s",
                     (unsigned long)r.rid_, eventName(r.evType_), priorityName(r.priority_),
                     r.speak_ ? 1 : 0, (unsigned)newLen, newShort.c_str());
      }
      pending_ = r;
      hasPending_ = true;
      lastEventMs_ = nowMs;
    } else {
      LOG_EVT_INFO("EVT_BEH_DROP",
                   "rid=%lu type=%s prio=%s speak=%d len=%u text=%s reason=prio_lower",
                   (unsigned long)r.rid_, eventName(r.evType_), priorityName(r.priority_),
                   r.speak_ ? 1 : 0, (unsigned)newLen, shortenText(r.speechText_, 16, oldLen).c_str());
    }
  }
}
