// Module implementation.
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
  bool active_ = true;
  AiState state_ = AiState::Idle;
  String line1_;
  String line2_;
  String hint_;
};
