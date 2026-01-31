// Module implementation.
#pragma once
#include <Arduino.h>

// Shared snapshot for UI/behavior and presenters.
struct MiningPanelData {
  float    hrKh_      = 0.0f;
  uint32_t accepted_  = 0;
  uint32_t rejected_  = 0;
  float    pingMs_    = -1.0f;
  float    rejPct_    = 0.0f;
  float    bestShare_ = -1.0f;
  bool     poolAlive_     = false;
  bool     miningEnabled_ = false;
  float    diff_          = 0.0f;
  uint32_t elapsedS_  = 0;
  String   sw_;
  String   fw_;
  String   poolName_;
  String   worker_;
  String   wifiDiag_;
  String   poolDiag_;
};
