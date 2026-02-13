// Module implementation.
#include "behavior/servo_driver.h"

#include <math.h>

#include <ServoEasing.hpp>

#include "config/config.h"
#include "utils/logging.h"

namespace {

ServoEasing s_servoX;
ServoEasing s_servoY;
bool s_initialized = false;
bool s_homed = false;
uint32_t s_lastUpdateMs = 0;
float s_cmdX = (float)MC_SERVO_START_DEGREE_X;
float s_cmdY = (float)MC_SERVO_START_DEGREE_Y;
float s_targetGazeX = 0.0f;
float s_targetGazeY = 0.0f;
bool s_targetActive = false;
bool s_isMoving = false;
bool s_moveToHome = false;
uint32_t s_moveStartMs = 0;
uint16_t s_moveDurationMs = MC_SERVO_MOVE_TIME_MS;
uint32_t s_nextMoveAllowedMs = 0;
float s_moveFromX = (float)MC_SERVO_START_DEGREE_X;
float s_moveFromY = (float)MC_SERVO_START_DEGREE_Y;
float s_moveToX = (float)MC_SERVO_START_DEGREE_X;
float s_moveToY = (float)MC_SERVO_START_DEGREE_Y;

constexpr float kMoveStartThresholdDeg = 0.8f;
float clampDegreeF_(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

float gazeToDegree_(float gaze, int startDeg, float gain, int invert, int minDeg,
                    int maxDeg) {
  const float sign = invert ? -1.0f : 1.0f;
  const float target = startDeg + (gain * (gaze * sign));
  return clampDegreeF_(target, (float)minDeg, (float)maxDeg);
}

float lerp_(float a, float b, float t) { return a + (b - a) * t; }

void beginMove_(float toX, float toY, uint16_t durationMs, bool toHome) {
  s_moveFromX = s_cmdX;
  s_moveFromY = s_cmdY;
  s_moveToX = toX;
  s_moveToY = toY;
  s_moveDurationMs = durationMs > 0 ? durationMs : 1;
  s_moveStartMs = millis();
  s_isMoving = true;
  s_moveToHome = toHome;
}

void updateMove_(uint32_t now) {
  if (!s_isMoving) return;
  const uint32_t elapsed = now - s_moveStartMs;
  float t = (float)elapsed / (float)s_moveDurationMs;
  if (t > 1.0f) t = 1.0f;
  s_cmdX = lerp_(s_moveFromX, s_moveToX, t);
  s_cmdY = lerp_(s_moveFromY, s_moveToY, t);
  s_servoX.write(s_cmdX);
  s_servoY.write(s_cmdY);
  if (t >= 1.0f) {
    s_isMoving = false;
    s_nextMoveAllowedMs = now + MC_SERVO_IDLE_TIME_MS;
    if (s_moveToHome) s_homed = true;
  }
}

void writeDegrees_(float degX, float degY) {
  s_servoX.write(degX);
  s_servoY.write(degY);
}

void setHome_(bool smooth) {
  if (smooth) {
    beginMove_((float)MC_SERVO_START_DEGREE_X, (float)MC_SERVO_START_DEGREE_Y,
               MC_SERVO_HOME_MOVE_TIME_MS, true);
    s_homed = false;
  } else {
    s_cmdX = (float)MC_SERVO_START_DEGREE_X;
    s_cmdY = (float)MC_SERVO_START_DEGREE_Y;
    writeDegrees_(s_cmdX, s_cmdY);
    s_isMoving = false;
    s_moveToHome = false;
    s_homed = true;
    s_nextMoveAllowedMs = millis() + MC_SERVO_IDLE_TIME_MS;
  }
}

} // namespace

void servoDriverBegin() {
#if !MC_ENABLE_SERVO
  MC_LOGI("SERVO", "disabled by MC_ENABLE_SERVO=0");
  return;
#else
  if (s_initialized) return;
  s_initialized = true;

  const uint8_t errX = s_servoX.attach(MC_SERVO_PIN_X, MC_SERVO_START_DEGREE_X,
                                       DEFAULT_MICROSECONDS_FOR_0_DEGREE,
                                       DEFAULT_MICROSECONDS_FOR_180_DEGREE);
  const uint8_t errY = s_servoY.attach(MC_SERVO_PIN_Y, MC_SERVO_START_DEGREE_Y,
                                       DEFAULT_MICROSECONDS_FOR_0_DEGREE,
                                       DEFAULT_MICROSECONDS_FOR_180_DEGREE);
  if (errX || errY) {
    MC_LOGE("SERVO", "attach failed err_x=%u err_y=%u pin_x=%d pin_y=%d",
            (unsigned)errX, (unsigned)errY, MC_SERVO_PIN_X, MC_SERVO_PIN_Y);
  }

  s_servoX.setEasingType(EASE_QUADRATIC_IN_OUT);
  s_servoY.setEasingType(EASE_QUADRATIC_IN_OUT);
  setSpeedForAllServos(MC_SERVO_SPEED);

  setHome_(false);
  MC_LOGI("SERVO", "begin pin_x=%d pin_y=%d home=(%d,%d) attach=(%u,%u)",
          MC_SERVO_PIN_X, MC_SERVO_PIN_Y, MC_SERVO_START_DEGREE_X,
          MC_SERVO_START_DEGREE_Y, (unsigned)errX, (unsigned)errY);
#endif
}

void servoDriverHome() {
#if MC_ENABLE_SERVO
  if (!s_initialized) return;
  setHome_(true);
  s_homed = true;
#endif
}

void servoDriverSetTarget(float gazeX, float gazeY, bool active) {
#if MC_ENABLE_SERVO
  s_targetGazeX = gazeX;
  s_targetGazeY = gazeY;
  s_targetActive = active;
#else
  (void)gazeX;
  (void)gazeY;
  (void)active;
#endif
}

void servoDriverTick() {
#if !MC_ENABLE_SERVO
  return;
#else
  if (!s_initialized) return;
  const uint32_t now = millis();
  if (MC_SERVO_UPDATE_INTERVAL_MS > 0 &&
      (uint32_t)(now - s_lastUpdateMs) < MC_SERVO_UPDATE_INTERVAL_MS) {
    return;
  }
  s_lastUpdateMs = now;

  if (s_isMoving) {
    updateMove_(now);
    return;
  }

  if ((int32_t)(now - s_nextMoveAllowedMs) < 0) {
    return;
  }

  if (!s_targetActive) {
    if (!s_homed) {
      servoDriverHome();
    }
    return;
  }

  const float degX = gazeToDegree_(s_targetGazeX, MC_SERVO_START_DEGREE_X, MC_SERVO_GAIN_X,
                                   MC_SERVO_INVERT_X, MC_SERVO_MIN_DEGREE_X,
                                   MC_SERVO_MAX_DEGREE_X);
  const float degY = gazeToDegree_(s_targetGazeY, MC_SERVO_START_DEGREE_Y, MC_SERVO_GAIN_Y,
                                   MC_SERVO_INVERT_Y, MC_SERVO_MIN_DEGREE_Y,
                                   MC_SERVO_MAX_DEGREE_Y);

  if (fabsf(degX - s_cmdX) < kMoveStartThresholdDeg &&
      fabsf(degY - s_cmdY) < kMoveStartThresholdDeg) {
    return;
  }
  beginMove_(degX, degY, MC_SERVO_MOVE_TIME_MS, false);
  s_homed = false;
#endif
}
