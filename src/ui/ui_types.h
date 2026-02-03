// Module implementation.
#pragma once
#include <Arduino.h>

#include "utils/app_types.h"

struct AiUiOverlay {
  bool active_ = true;
  AiState state_ = AiState::Idle;
  String line1_;
  String line2_;
  String hint_;
};
