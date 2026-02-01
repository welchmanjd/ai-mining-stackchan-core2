// Module implementation.
#pragma once
#include <vector>

#include <Arduino.h>

#include "utils/orchestrator_api.h"
enum class AppState : uint8_t { Idle, React, ThinkWait, Speak, ErrorSafe };
using OrchPrio = OrchestratorApi::OrchPrio;
class Orchestrator : public OrchestratorApi {
public:
  using OrchKind = OrchestratorApi::OrchKind;
  using CancelSource = OrchestratorApi::CancelSource;
  using SpeakStartCmd = OrchestratorApi::SpeakStartCmd;
  void init();
  SpeakStartCmd makeSpeakStartCmd(uint32_t rid, const String& text, OrchPrio prio,
                                  OrchKind kind = OrchKind::BehaviorSpeak) override;
  void enqueueSpeakPending(const SpeakStartCmd& cmd) override;
  bool hasPendingSpeak() const;
  SpeakStartCmd popNextPending();
  void setExpectedSpeak(uint32_t speakId, uint32_t rid);
  void setExpectedSpeak(uint32_t speakId, uint32_t rid, OrchKind kind);
  void clearExpectedSpeak(const char* reason);
  // Phase5-B2: cancel speak (idempotent + reason/source)
  void cancelSpeak(uint32_t speakId, const char* reason); // legacy (source=Other)
  void cancelSpeak(uint32_t speakId, const char* reason, CancelSource source);
  void onAudioStart(uint32_t speakId);
  bool onTtsDone(uint32_t gotId, bool* desyncOut = nullptr); // legacy
  bool onTtsDone(uint32_t gotId,
                 uint32_t* doneRid,
                 OrchKind* doneKind,
                 bool* desyncOut = nullptr);
  uint32_t ttsIdForRid(uint32_t rid) const override;
  uint32_t expectSpeakId() const { return expectSpeakId_; }
  uint32_t expectRid() const { return expectRid_; }
  OrchKind expectKind() const { return expectKind_; }
  bool cancelSpeakByRid(uint32_t rid,
                        const char* reason,
                        CancelSource source,
                        uint32_t* outCanceledSpeakId = nullptr) override;
  AppState state() const { return state_; }
  bool tick(uint32_t nowMs);
private:
  AppState state_ = AppState::Idle;
  uint32_t expectSpeakId_ = 0;
  uint32_t expectRid_ = 0;
  OrchKind expectKind_ = OrchKind::None;
  uint8_t  mismatchCount_ = 0;
  static constexpr uint8_t kDesyncThreshold = 3;
  uint32_t nextTtsId_ = 1;
  static constexpr size_t kMaxSpeakText = 128;
  static constexpr uint32_t kThinkWaitTimeoutMs = 30000;
  struct CancelRecord {
    uint32_t id = 0;
    CancelSource source = CancelSource::Other;
    char reason[24] = {0};
  };
  static constexpr size_t kMaxCanceled = 8;
  std::vector<CancelRecord> canceled_;
  static const char* sourceToStr_(CancelSource s);
  const CancelRecord* findCanceled_(uint32_t id) const;
  void rememberCanceled_(uint32_t id, const char* reason, CancelSource source);
  std::vector<SpeakStartCmd> pending_;
  static constexpr size_t kMaxPending = 4;
  AppState prevState_ = AppState::Idle;
  uint32_t thinkWaitSinceMs_ = 0;
  bool timeoutLogged_ = false;
};
