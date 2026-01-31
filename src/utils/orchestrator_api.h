// Module implementation.
#pragma once
#include <Arduino.h>

class OrchestratorApi {
public:
  enum class OrchPrio : uint8_t { Low = 0, Normal = 1, High = 2 };
  enum class OrchKind : uint8_t {
    None = 0,
    BehaviorSpeak = 1,
    AiSpeak = 2,
  };
  enum class CancelSource : uint8_t { Ai = 0, Main = 1, Other = 2 };
  struct SpeakStartCmd {
    bool valid_ = false;
    uint32_t ttsId_ = 0;
    uint32_t rid_ = 0;
    OrchKind kind_ = OrchKind::None;
    String text_;
    OrchPrio prio_ = OrchPrio::Normal;
  };
  virtual SpeakStartCmd makeSpeakStartCmd(uint32_t rid, const String& text, OrchPrio prio,
                                          OrchKind kind = OrchKind::BehaviorSpeak) = 0;
  virtual void enqueueSpeakPending(const SpeakStartCmd& cmd) = 0;
  virtual uint32_t ttsIdForRid(uint32_t rid) const = 0;
  virtual bool cancelSpeakByRid(uint32_t rid,
                                const char* reason,
                                CancelSource source,
                                uint32_t* outCanceledSpeakId = nullptr) = 0;
  virtual ~OrchestratorApi() = default;
};
