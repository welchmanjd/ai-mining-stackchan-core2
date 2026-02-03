// Module implementation.
#pragma once
#include <Arduino.h>

class AzureTts;

struct SerialSetupContext {
  AzureTts* tts_ = nullptr;
  uint32_t* displaySleepTimeoutMs_ = nullptr;
};

void serialSetupInit(const SerialSetupContext& ctx);
void pollSetupSerial();
