#include "core/orchestrator.h"

// ===== src/orchestrator.cpp：#include直後に追加 =====
const char* Orchestrator::sourceToStr_(CancelSource s) {
  switch (s) {
    case CancelSource::AI:   return "AI";
    case CancelSource::Main: return "MAIN";
    default:                 return "OTHER";
  }
}

const Orchestrator::CancelRecord* Orchestrator::findCanceled_(uint32_t id) const {
  if (id == 0) return nullptr;
  for (const auto& r : canceled_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

void Orchestrator::rememberCanceled_(uint32_t id, const char* reason, CancelSource source) {
  if (id == 0) return;
  if (canceled_.size() >= kMaxCanceled) {
    canceled_.erase(canceled_.begin());
  }
  CancelRecord rec;
  rec.id = id;
  rec.source = source;
  if (reason && reason[0]) {
    strncpy(rec.reason, reason, sizeof(rec.reason) - 1);
    rec.reason[sizeof(rec.reason) - 1] = 0;
  }
  canceled_.push_back(rec);
}


// ===== src/orchestrator.cpp：init() 関数全文差し替え =====
void Orchestrator::init() {
  state_ = AppState::Idle;
  expectSpeakId_ = 0;
  expectRid_ = 0;
  expectKind_ = OrchKind::None;
  mismatchCount_ = 0;
  nextTtsId_ = 1;
  pending_.clear();
  canceled_.clear();

  prevState_ = AppState::Idle;
  thinkWaitSinceMs_ = 0;
  timeoutLogged_ = false;

  LOG_EVT_INFO("EVT_ORCH_INIT", "state=%d", (int)state_);
}


Orchestrator::SpeakStartCmd Orchestrator::makeSpeakStartCmd(uint32_t rid, const String& text, OrchPrio prio, OrchKind kind) {
  SpeakStartCmd cmd;
  cmd.rid = rid;
  cmd.prio = prio;
  cmd.kind = kind;

  if (text.length() == 0) {
    cmd.valid = false;
    return cmd;
  }

  cmd.text = text;
  if (cmd.text.length() > kMaxSpeakText) {
    cmd.text = cmd.text.substring(0, kMaxSpeakText);
    LOG_EVT_INFO("EVT_ORCH_SPEAK_TRUNC", "rid=%lu len=%u->%u",
                 (unsigned long)cmd.rid,
                 (unsigned)text.length(),
                 (unsigned)cmd.text.length());
  }

  // 採番 (0 は使わない)
  uint32_t id = nextTtsId_++;
  if (nextTtsId_ == 0) nextTtsId_ = 1;
  cmd.ttsId = id;
  cmd.valid = true;

  LOG_EVT_INFO("EVT_ORCH_SPEAK_CMD", "rid=%lu tts_id=%lu prio=%d kind=%d len=%u",
               (unsigned long)cmd.rid, (unsigned long)cmd.ttsId,
               (int)cmd.prio, (int)cmd.kind, (unsigned)cmd.text.length());
  return cmd;
}

void Orchestrator::enqueueSpeakPending(const SpeakStartCmd& cmd) {
  if (!cmd.valid) {
    LOG_EVT_INFO("EVT_ORCH_DROP_INVALID", "rid=%lu", (unsigned long)cmd.rid);
    return;
  }

  if (!pending_.empty() && pending_.back().kind == cmd.kind) {
    // 同種最新置換（kind が同じときのみ）
    SpeakStartCmd replaced = pending_.back();
    pending_.back() = cmd;
    LOG_EVT_INFO("EVT_ORCH_REPLACE",
                 "old_rid=%lu old_tts_id=%lu new_rid=%lu new_tts_id=%lu kind=%d",
                 (unsigned long)replaced.rid, (unsigned long)replaced.ttsId,
                 (unsigned long)cmd.rid, (unsigned long)cmd.ttsId, (int)cmd.kind);
  } else {
    pending_.push_back(cmd);
  }

  if (pending_.size() > kMaxPending) {
    SpeakStartCmd dropped = pending_.front();
    pending_.erase(pending_.begin());
    LOG_EVT_INFO("EVT_ORCH_DROP_OLD",
                 "rid=%lu tts_id=%lu kind=%d size=%u",
                 (unsigned long)dropped.rid, (unsigned long)dropped.ttsId, (int)dropped.kind,
                 (unsigned)pending_.size());
  }
}

bool Orchestrator::hasPendingSpeak() const {
  return !pending_.empty();
}

Orchestrator::SpeakStartCmd Orchestrator::popNextPending() {
  SpeakStartCmd out;
  if (pending_.empty()) return out;
  out = pending_.front();
  pending_.erase(pending_.begin());
  LOG_EVT_INFO("EVT_ORCH_POP_PENDING",
               "rid=%lu tts_id=%lu prio=%d kind=%d len=%u size_rem=%u",
               (unsigned long)out.rid, (unsigned long)out.ttsId,
               (int)out.prio, (int)out.kind, (unsigned)out.text.length(),
               (unsigned)pending_.size());
  return out;
}

void Orchestrator::setExpectedSpeak(uint32_t speakId, uint32_t rid) {
  // legacy: kind は BehaviorSpeak 扱い
  setExpectedSpeak(speakId, rid, OrchKind::BehaviorSpeak);
}

void Orchestrator::setExpectedSpeak(uint32_t speakId, uint32_t rid, OrchKind kind) {
  expectSpeakId_ = speakId;
  expectRid_ = rid;
  expectKind_ = kind;
  mismatchCount_ = 0;
  const AppState from = state_;
  state_ = AppState::ThinkWait;
  LOG_EVT_INFO("EVT_ORCH_STATE",
               "from=%d to=%d reason=expect_speak rid=%lu speak_id=%lu kind=%d",
               (int)from, (int)state_,
               (unsigned long)rid, (unsigned long)speakId, (int)kind);
  LOG_EVT_INFO("EVT_ORCH_EXPECT_SPEAK", "expect=%lu rid=%lu kind=%d",
               (unsigned long)speakId, (unsigned long)rid, (int)kind);
}

void Orchestrator::clearExpectedSpeak(const char* reason) {
  const uint32_t old = expectSpeakId_;
  const AppState from = state_;

  expectSpeakId_ = 0;
  expectRid_ = 0;
  expectKind_ = OrchKind::None;
  mismatchCount_ = 0;
  state_ = AppState::Idle;

  LOG_EVT_INFO("EVT_ORCH_CLEAR_EXPECT",
               "from=%d to=%d reason=%s old_expect=%lu",
               (int)from, (int)state_,
               reason ? reason : "-",
               (unsigned long)old);
}

// ===== orchestrator.cpp：cancelSpeak(legacy)＋cancelSpeak(B2本体)（全文差し替え）=====
void Orchestrator::cancelSpeak(uint32_t speakId, const char* reason) {
  cancelSpeak(speakId, reason, CancelSource::Other);
}

void Orchestrator::cancelSpeak(uint32_t speakId, const char* reason, CancelSource source) {
  if (speakId == 0) return;

  // idempotent guard
  const CancelRecord* already = findCanceled_(speakId);
  if (already) {
    LOG_EVT_INFO("EVT_ORCH_CANCEL_IGNORED",
                 "tts_id=%lu source=%s reason=%s orig_source=%s orig_reason=%s",
                 (unsigned long)speakId,
                 sourceToStr_(source),
                 (reason && reason[0]) ? reason : "-",
                 sourceToStr_(already->source),
                 (already->reason[0]) ? already->reason : "-");
    return;
  }
  rememberCanceled_(speakId, reason, source);

  const AppState from = state_;
  const uint32_t oldExpect = expectSpeakId_;

  // pending_ から該当IDを除去
  size_t removed = 0;
  if (!pending_.empty()) {
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (it->ttsId == speakId) {
        it = pending_.erase(it);
        removed++;
      } else {
        ++it;
      }
    }
  }

  // expect解除
  bool clearedExpect = false;
  if (expectSpeakId_ != 0 && expectSpeakId_ == speakId) {
    expectSpeakId_ = 0;
    expectRid_ = 0;
    expectKind_ = OrchKind::None;
    mismatchCount_ = 0;
    state_ = AppState::Idle;
    clearedExpect = true;
  }

  // ThinkWait監視のリセット
  if (state_ != AppState::ThinkWait) {
    thinkWaitSinceMs_ = 0;
    timeoutLogged_ = false;
  }
  prevState_ = state_;

  LOG_EVT_INFO("EVT_ORCH_CANCEL_SPEAK",
               "from=%d to=%d tts_id=%lu source=%s reason=%s old_expect=%lu cleared_expect=%d pending_removed=%u pending_left=%u",
               (int)from, (int)state_,
               (unsigned long)speakId,
               sourceToStr_(source),
               (reason && reason[0]) ? reason : "-",
               (unsigned long)oldExpect,
               clearedExpect ? 1 : 0,
               (unsigned)removed,
               (unsigned)pending_.size());
}


// ===== Step4: rid→tts_id 参照 =====
uint32_t Orchestrator::ttsIdForRid(uint32_t rid) const {
  if (rid == 0) return 0;
  if (expectRid_ != 0 && rid == expectRid_) {
    return expectSpeakId_;
  }
  for (const auto& cmd : pending_) {
    if (cmd.rid == rid) return cmd.ttsId;
  }
  return 0;
}


// ===== Step4: rid 指定キャンセル（AIの hard-timeout 用）=====
bool Orchestrator::cancelSpeakByRid(uint32_t rid,
                                   const char* reason,
                                   CancelSource source,
                                   uint32_t* outCanceledSpeakId) {
  if (outCanceledSpeakId) *outCanceledSpeakId = 0;
  if (rid == 0) return false;

  // 1) expect（実再生中/開始待ち）
  if (expectRid_ != 0 && rid == expectRid_ && expectSpeakId_ != 0) {
    const uint32_t sid = expectSpeakId_;
    cancelSpeak(sid, reason, source);
    if (outCanceledSpeakId) *outCanceledSpeakId = sid;
    return true;
  }

  // 2) pending（未開始）
  for (const auto& cmd : pending_) {
    if (cmd.rid == rid && cmd.ttsId != 0) {
      const uint32_t sid = cmd.ttsId;
      cancelSpeak(sid, reason, source);
      // pending のみ除去の場合は Azure cancel 不要なので 0 を返す
      if (outCanceledSpeakId) *outCanceledSpeakId = 0;
      return true;
    }
  }

  return false;
}



void Orchestrator::onAudioStart(uint32_t speakId) {
  if (expectSpeakId_ != 0 && speakId == expectSpeakId_) {
    const AppState from = state_;
    state_ = AppState::Speak;
    LOG_EVT_INFO("EVT_ORCH_STATE",
                 "from=%d to=%d reason=audio_start speak_id=%lu",
                 (int)from, (int)state_, (unsigned long)speakId);
  } else {
    LOG_EVT_INFO("EVT_ORCH_AUDIO_START_IGNORED",
                 "got=%lu expect=%lu state=%d",
                 (unsigned long)speakId,
                 (unsigned long)expectSpeakId_,
                 (int)state_);
  }
}

bool Orchestrator::onTtsDone(uint32_t gotId, bool* desyncOut) {
  // legacy wrapper
  return onTtsDone(gotId, nullptr, nullptr, desyncOut);
}

bool Orchestrator::onTtsDone(uint32_t gotId,
                            uint32_t* doneRid,
                            OrchKind* doneKind,
                            bool* desyncOut) {
  if (desyncOut) *desyncOut = false;
  if (doneRid) *doneRid = 0;
  if (doneKind) *doneKind = OrchKind::None;

  const uint32_t expect = expectSpeakId_;
  const bool ok = (expect != 0) && (gotId == expect);

  LOG_EVT_INFO("EVT_TTS_DONE_RX_ORCH", "got=%lu expect=%lu ok=%d",
               (unsigned long)gotId, (unsigned long)expect, ok ? 1 : 0);

  if (ok) {
    if (doneRid) *doneRid = expectRid_;
    if (doneKind) *doneKind = expectKind_;

    const AppState from = state_;
    state_ = AppState::Idle;
    LOG_EVT_INFO("EVT_ORCH_STATE",
                 "from=%d to=%d reason=tts_done speak_id=%lu",
                 (int)from, (int)state_, (unsigned long)gotId);
    expectSpeakId_ = 0;
    expectRid_ = 0;
    expectKind_ = OrchKind::None;
    mismatchCount_ = 0;
    return true;
  }

  // mismatch
  if (expect != 0) mismatchCount_++;
  LOG_EVT_INFO("EVT_ORCH_SPEAK_MISMATCH",
               "got=%lu expect=%lu count=%u",
               (unsigned long)gotId, (unsigned long)expect, (unsigned)mismatchCount_);

  if (mismatchCount_ >= kDesyncThreshold) {
    if (desyncOut) *desyncOut = true;
  }
  return false;
}

bool Orchestrator::tick(uint32_t nowMs) {
  bool didTimeout = false;

  // 遷移検知
  if (state_ != prevState_) {
    if (state_ == AppState::ThinkWait) {
      thinkWaitSinceMs_ = nowMs;
      timeoutLogged_ = false;
    } else {
      thinkWaitSinceMs_ = 0;
      timeoutLogged_ = false;
    }
    prevState_ = state_;
  }

  // ThinkWait中のみ監視
  if (state_ == AppState::ThinkWait && !timeoutLogged_) {
    if (thinkWaitSinceMs_ != 0 && (uint32_t)(nowMs - thinkWaitSinceMs_) >= kThinkWaitTimeoutMs) {
      const size_t cleared = pending_.size();
      const AppState from = state_;

      pending_.clear();
      expectSpeakId_ = 0;
      expectRid_ = 0;
      expectKind_ = OrchKind::None;
      mismatchCount_ = 0;
      state_ = AppState::Idle;

      timeoutLogged_ = true;
      didTimeout = true;

      LOG_EVT_INFO("EVT_ORCH_TIMEOUT",
                   "from=%d elapsed_ms=%lu action=clear_pending_idle cleared=%u",
                   (int)from, (unsigned long)(nowMs - thinkWaitSinceMs_),
                   (unsigned)cleared);

    }
  }

  return didTimeout;
}

