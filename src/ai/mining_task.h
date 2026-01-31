// src/mining_task.h
// Module implementation.
#pragma once
#include <Arduino.h>

#include "utils/mining_status.h"
#include "utils/mining_summary.h"
void setMiningPaused(bool paused);
void startMiner();
void updateMiningSummary(MiningSummary& out);
struct MiningYieldProfile {
  uint16_t every_ = 1024;
  uint8_t delayMs_ = 1;
  MiningYieldProfile(uint16_t every = 1024, uint8_t delayMs = 1)
      : every_(every), delayMs_(delayMs) {}
};
void setMiningActiveThreads(uint8_t activeThreads);
uint8_t getMiningActiveThreads();
void setMiningYieldProfile(MiningYieldProfile p);
MiningYieldProfile getMiningYieldProfile();
inline MiningYieldProfile MiningYieldNormal() { return MiningYieldProfile(1024, 1); }
inline MiningYieldProfile MiningYieldStrong() { return MiningYieldProfile(64, 3); }
