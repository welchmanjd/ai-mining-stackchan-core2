// Module implementation.
#pragma once
#include <Arduino.h>
struct RuntimeFeatures {
  bool wifiConfigured_ = false;
  bool miningEnabled_  = false;
  bool ttsEnabled_     = false;
};
RuntimeFeatures getRuntimeFeatures();
