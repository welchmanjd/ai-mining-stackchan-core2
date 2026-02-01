// Module implementation.
#pragma once
#include <stdint.h>

#include "core/app_types.h"
#include "utils/orchestrator_api.h"

class AiTalkController;
class AzureTts;
class Orchestrator;
class StackchanBehavior;

struct TtsCoordinatorContext {
  AzureTts* tts_ = nullptr;
  Orchestrator* orch_ = nullptr;
  AiTalkController* ai_ = nullptr;
  StackchanBehavior* behavior_ = nullptr;
  bool* attentionActive_ = nullptr;
  void (*bubbleClearFn_)(const char* reason, bool forceUiClear) = nullptr;
  AppMode* mode_ = nullptr;
};

void ttsCoordinatorInit(const TtsCoordinatorContext& ctx);
void ttsCoordinatorTick(uint32_t now);
bool ttsCoordinatorIsBusy();
void ttsCoordinatorMaybeSpeak(const OrchestratorApi::SpeakStartCmd& cmd, int evType);
void ttsCoordinatorClearInflight();
