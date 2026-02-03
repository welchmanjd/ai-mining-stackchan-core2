// Module implementation.
#pragma once
#include <stdint.h>

#include "utils/app_types.h"

class AiTalkController;
class AzureTts;
class Orchestrator;
class StackchanBehavior;

struct AppRuntimeContext {
  AiTalkController* ai_ = nullptr;
  AzureTts* tts_ = nullptr;
  Orchestrator* orch_ = nullptr;
  StackchanBehavior* behavior_ = nullptr;
};

using BubbleClearFn = void (*)(const char* reason, bool forceUiClear);

void appRuntimeInit(const AppRuntimeContext& ctx);
void appRuntimeTick(uint32_t now);

uint32_t* appRuntimeDisplaySleepTimeoutMsPtr();
bool* appRuntimeAttentionActivePtr();
AppMode* appRuntimeModePtr();
BubbleClearFn appRuntimeBubbleClearFn();
