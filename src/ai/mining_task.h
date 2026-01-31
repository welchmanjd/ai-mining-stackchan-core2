// src/mining_task.h
#pragma once
#include <Arduino.h>
#include <stdint.h>
struct MiningSummary {
  float totalKh_ = 0.0f;
  uint32_t accepted_ = 0;
  uint32_t rejected_ = 0;
  float maxPingMs_ = 0.0f;
  uint32_t maxDifficulty_ = 0;
  bool anyConnected_ = false;
  String poolName_;
  String poolDiag_;
  uint8_t workThread_ = 255;
  uint32_t workNonce_ = 0;
  uint32_t workMaxNonce_ = 0;
  uint32_t workDifficulty_ = 0;
  char workSeed_[41] = {0};
  char workHashHex_[41] = {0};
  String logLine40_;
  bool miningEnabled_ = false;
};
void setMiningPaused(bool paused);
bool isMiningPaused();
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
