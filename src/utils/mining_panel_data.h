// Module implementation.
#pragma once
#include <Arduino.h>

// Shared snapshot for UI/behavior and presenters.
struct MiningPanelData {
  enum BootCheckState : uint8_t {
    BootWaiting = 0,
    BootConnecting = 1,
    BootOk = 2,
    BootFail = 3,
    BootSkip = 4
  };

  float hrKh_ = 0.0f;
  uint32_t accepted_ = 0;
  uint32_t rejected_ = 0;
  float pingMs_ = -1.0f;
  float rejPct_ = 0.0f;
  float bestShare_ = -1.0f;
  bool poolAlive_ = false;
  bool miningEnabled_ = false;
  float diff_ = 0.0f;
  uint32_t elapsedS_ = 0;
  String sw_;
  String fw_;
  String poolName_;
  String worker_;
  String wifiDiag_;
  String poolDiag_;
  // New: AI Mode Status
  bool aiEnabled_ = false;
  bool aiReady_ = false;
  String aiDiag_;
  int bootStatus_ = 0; // Legacy single-state field (kept for compatibility).
  uint8_t bootWifiState_ = BootWaiting;
  uint8_t bootMiningState_ = BootWaiting;
  uint8_t bootOpenAiState_ = BootWaiting;
  uint8_t bootAzureState_ = BootWaiting;
  String bootWifiDiag_;
  String bootMiningDiag_;
  String bootOpenAiDiag_;
  String bootAzureDiag_;
  String bootActiveDiag_;
};
