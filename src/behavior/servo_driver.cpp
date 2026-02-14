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
bool s_moveToHome = false;
uint16_t s_moveDurationMs = MC_SERVO_MOVE_TIME_MS;
float s_moveToX = (float)MC_SERVO_START_DEGREE_X;
float s_moveToY = (float)MC_SERVO_START_DEGREE_Y;
float s_filteredTargetX = (float)MC_SERVO_START_DEGREE_X;
float s_filteredTargetY = (float)MC_SERVO_START_DEGREE_Y;
float s_prevRawTargetX = (float)MC_SERVO_START_DEGREE_X;
float s_prevRawTargetY = (float)MC_SERVO_START_DEGREE_Y;

constexpr float kMoveStartThresholdDeg = 0.05f;
constexpr float kMoveUpdateThresholdDeg = 0.01f;
float clampDegreeF_(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

float clamp01_(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

float gazeToDegree_(float gaze, int startDeg, float gain, int invert, int minDeg,
                    int maxDeg) {
  const float sign = invert ? -1.0f : 1.0f;
  const float target = startDeg + (gain * (gaze * sign));
  return clampDegreeF_(target, (float)minDeg, (float)maxDeg);
}

float computeAdaptiveAlpha_(float rawTargetX, float rawTargetY, float dtSec) {
#if MC_SERVO_DYNAMIC_ALPHA_ENABLE
  float alphaMin = MC_SERVO_SMOOTH_ALPHA_MIN;
  float alphaMax = MC_SERVO_SMOOTH_ALPHA_MAX;
  if (alphaMin > alphaMax) {
    const float tmp = alphaMin;
    alphaMin = alphaMax;
    alphaMax = tmp;
  }

  const float safeDt = dtSec > 0.0005f ? dtSec : 0.02f;
  const float errX = fabsf(rawTargetX - s_filteredTargetX);
  const float errY = fabsf(rawTargetY - s_filteredTargetY);
  const float errDeg = errX > errY ? errX : errY;
  const float dRawX = fabsf(rawTargetX - s_prevRawTargetX);
  const float dRawY = fabsf(rawTargetY - s_prevRawTargetY);
  const float velDegPerSec = (dRawX > dRawY ? dRawX : dRawY) / safeDt;

  float errNorm = 0.0f;
  if (MC_SERVO_ALPHA_ERR_REF_DEG > 0.0f) {
    errNorm = clamp01_(errDeg / MC_SERVO_ALPHA_ERR_REF_DEG);
  }
  float velNorm = 0.0f;
  if (MC_SERVO_ALPHA_VEL_REF_DPS > 0.0f) {
    velNorm = clamp01_(velDegPerSec / MC_SERVO_ALPHA_VEL_REF_DPS);
  }

  const float drive = clamp01_(errNorm * 0.7f + velNorm * 0.3f);
  return alphaMin + (alphaMax - alphaMin) * drive;
#else
  (void)rawTargetX;
  (void)rawTargetY;
  (void)dtSec;
  return MC_SERVO_SMOOTH_ALPHA;
#endif
}

void beginMove_(float toX, float toY, uint16_t durationMs, bool toHome) {
  s_moveToX = toX;
  s_moveToY = toY;
  s_moveDurationMs = durationMs > 0 ? durationMs : 1;

  if (toHome) {
    s_servoX.setEaseToD(toX, s_moveDurationMs);
    s_servoY.setEaseToD(toY, s_moveDurationMs);
  } else {
    s_servoX.setEaseTo(toX, MC_SERVO_TRACK_SPEED_DPS);
    s_servoY.setEaseTo(toY, MC_SERVO_TRACK_SPEED_DPS);
  }
  if (!areInterruptsActive()) {
    synchronizeAllServosAndStartInterrupt();
  }

  s_moveToHome = toHome;
}

void updateMove_(uint32_t now) {
  (void)now;
  if (!areInterruptsActive()) {
    s_cmdX = s_moveToX;
    s_cmdY = s_moveToY;
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
    s_moveToHome = false;
    s_homed = true;
    s_moveToX = s_cmdX;
    s_moveToY = s_cmdY;
    s_filteredTargetX = s_cmdX;
    s_filteredTargetY = s_cmdY;
    s_prevRawTargetX = s_cmdX;
    s_prevRawTargetY = s_cmdY;
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
  updateMove_(now);

  if (MC_SERVO_UPDATE_INTERVAL_MS > 0 &&
      (uint32_t)(now - s_lastUpdateMs) < MC_SERVO_UPDATE_INTERVAL_MS) {
    return;
  }
  const float dtSec = s_lastUpdateMs == 0 ? 0.02f
                                          : (float)(now - s_lastUpdateMs) / 1000.0f;
  s_lastUpdateMs = now;

  if (!s_targetActive) {
    const bool homeRequested = fabsf(s_moveToX - (float)MC_SERVO_START_DEGREE_X) > kMoveStartThresholdDeg ||
                               fabsf(s_moveToY - (float)MC_SERVO_START_DEGREE_Y) > kMoveStartThresholdDeg;
    if (!s_homed && homeRequested) {
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
  const float alpha = computeAdaptiveAlpha_(degX, degY, dtSec);
  s_filteredTargetX += (degX - s_filteredTargetX) * alpha;
  s_filteredTargetY += (degY - s_filteredTargetY) * alpha;
  s_prevRawTargetX = degX;
  s_prevRawTargetY = degY;

  const bool currentlyMoving = areInterruptsActive();
  const float threshold = currentlyMoving ? kMoveUpdateThresholdDeg
                                          : kMoveStartThresholdDeg;
  if (fabsf(s_filteredTargetX - s_moveToX) < threshold &&
      fabsf(s_filteredTargetY - s_moveToY) < threshold) {
    return;
  }
  beginMove_(s_filteredTargetX, s_filteredTargetY, MC_SERVO_MOVE_TIME_MS, false);
  s_homed = false;
#endif
}
