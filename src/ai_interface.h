#pragma once
#include <Arduino.h>

enum class AiState {
  Idle,
  Listening,
  Thinking,
  Speaking,
  Cooldown
};

struct AiUiOverlay {
  bool active = true;
  AiState state = AiState::Idle;
  String line1;  // 例: "IDLE 0s"
  String line2;  // 例: "Tap top 1/3"
  String hint;   // 例: ":say こんにちは"
};
