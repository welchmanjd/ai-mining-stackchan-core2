// Module implementation.
#pragma once
#include <stdint.h>

enum AppMode : uint8_t {
  Dash = 0,
  Stackchan = 1,
};

enum class AiState : uint8_t {
  Idle = 0,
  Listening,
  Thinking,
  Speaking,
  PostSpeakBlank,
  Cooldown,
};

enum class NetworkStatus : uint8_t {
  Unknown = 0,
  Connected,
  NoSsid,
  ConnectFailed,
  Disconnected, // or other generic error
};
