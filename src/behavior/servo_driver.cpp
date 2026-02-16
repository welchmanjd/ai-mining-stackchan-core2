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
float s_trackSpeedCmdDps = (float)MC_SERVO_TRACK_SPEED_DPS;
float s_deadbandResidualX = 0.0f;
float s_deadbandResidualY = 0.0f;
float s_prevFilteredX = (float)MC_SERVO_START_DEGREE_X;
float s_prevFilteredY = (float)MC_SERVO_START_DEGREE_Y;
int8_t s_prevErrSignX = 0;
int8_t s_prevErrSignY = 0;
uint8_t s_nonHomeMoveCount = 0;
uint8_t s_moveTriggerPhase = 0;

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

float clampF_(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int8_t signWithDeadzone_(float value, float deadzone) {
  if (value > deadzone) return 1;
  if (value < -deadzone) return -1;
  return 0;
}

float gazeToDegree_(float gaze, int startDeg, float gain, int invert, int minDeg,
                    int maxDeg) {
  const float sign = invert ? -1.0f : 1.0f;
  const float target =
      startDeg + ((gain * MC_SERVO_RANGE_SCALE) * (gaze * sign));
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

  const float safeDt = dtSec > MC_SERVO_DT_MIN_SEC ? dtSec : MC_SERVO_DT_FALLBACK_SEC;
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

  const float drive = clamp01_(errNorm * MC_SERVO_ALPHA_ERR_WEIGHT +
                               velNorm * MC_SERVO_ALPHA_VEL_WEIGHT);
  return alphaMin + (alphaMax - alphaMin) * drive;
#else
  (void)rawTargetX;
  (void)rawTargetY;
  (void)dtSec;
  return MC_SERVO_SMOOTH_ALPHA;
#endif
}

void computeDiagSweepTargets_(uint32_t now, float* outX, float* outY) {
  const uint32_t halfPeriodMs =
      (MC_SERVO_DIAG_SWEEP_PERIOD_MS >= MC_SERVO_DIAG_SWEEP_MIN_PERIOD_MS)
          ? MC_SERVO_DIAG_SWEEP_PERIOD_MS
          : MC_SERVO_DIAG_SWEEP_MIN_PERIOD_MS;
  const bool positive = ((now / halfPeriodMs) & 1U) != 0U;
  const float sign = positive ? 1.0f : -1.0f;
  const float amp = fabsf(MC_SERVO_DIAG_SWEEP_AMPLITUDE_DEG);
  const float x = clampDegreeF_((float)MC_SERVO_START_DEGREE_X + (sign * amp),
                                (float)MC_SERVO_MIN_DEGREE_X,
                                (float)MC_SERVO_MAX_DEGREE_X);
  const float y = clampDegreeF_((float)MC_SERVO_START_DEGREE_Y + (sign * amp),
                                (float)MC_SERVO_MIN_DEGREE_Y,
                                (float)MC_SERVO_MAX_DEGREE_Y);
  if (outX) *outX = x;
  if (outY) *outY = y;
}

float computeTrackSpeedCommand_(float toX, float toY, float dtSec) {
#if MC_SERVO_TRACK_ACCEL_LIMIT_ENABLE
  float minSpeed = MC_SERVO_TRACK_MIN_SPEED_DPS;
  float maxSpeed = (float)MC_SERVO_TRACK_SPEED_DPS;
  if (minSpeed > maxSpeed) {
    const float tmp = minSpeed;
    minSpeed = maxSpeed;
    maxSpeed = tmp;
  }

  const float safeDt = dtSec > MC_SERVO_DT_MIN_SEC ? dtSec : MC_SERVO_DT_FALLBACK_SEC;
  const float stepX = fabsf(toX - s_moveToX);
  const float stepY = fabsf(toY - s_moveToY);
  const float stepDeg = stepX > stepY ? stepX : stepY;

  float desired = stepDeg / safeDt;
  desired = clampF_(desired, minSpeed, maxSpeed);

  const float accel = MC_SERVO_TRACK_ACCEL_DPS2 > 1.0f ? MC_SERVO_TRACK_ACCEL_DPS2
                                                        : 1.0f;
  const float maxDelta = accel * safeDt;
  float delta = desired - s_trackSpeedCmdDps;
  if (delta > maxDelta) delta = maxDelta;
  if (delta < -maxDelta) delta = -maxDelta;
  s_trackSpeedCmdDps += delta;
  s_trackSpeedCmdDps = clampF_(s_trackSpeedCmdDps, minSpeed, maxSpeed);
  return s_trackSpeedCmdDps;
#else
  (void)toX;
  (void)toY;
  (void)dtSec;
  return (float)MC_SERVO_TRACK_SPEED_DPS;
#endif
}

float applyDeadbandCompensation_(float filtered, float moveTo, float minDeg,
                                 float maxDeg, float* residual,
                                 float* prevFiltered, int8_t* prevErrSign) {
#if MC_SERVO_DEADBAND_COMP_ENABLE
  const float inputDelta = filtered - *prevFiltered;
  *prevFiltered = filtered;
  *residual += inputDelta;

  const float err = filtered - moveTo;
  const int8_t errSign = signWithDeadzone_(err, MC_SERVO_ERR_SIGN_DEADZONE_DEG);
  if (errSign != 0 && *prevErrSign != 0 && errSign != *prevErrSign) {
    const float damp = clampF_(MC_SERVO_DEADBAND_COMP_REVERSE_DAMP, 0.0f, 1.0f);
    *residual *= damp;
  }
  if (errSign != 0) {
    *prevErrSign = errSign;
  }

  if (fabsf(err) > MC_SERVO_DEADBAND_COMP_RANGE_DEG) {
    *residual = 0.0f;
    return clampDegreeF_(filtered, minDeg, maxDeg);
  }

  if (fabsf(*residual) >= MC_SERVO_DEADBAND_COMP_DEG) {
    const float kick = (*residual > 0.0f) ? MC_SERVO_DEADBAND_COMP_KICK_DEG
                                          : -MC_SERVO_DEADBAND_COMP_KICK_DEG;
    filtered += kick;
    *residual -= (*residual > 0.0f) ? MC_SERVO_DEADBAND_COMP_DEG
                                    : -MC_SERVO_DEADBAND_COMP_DEG;
  }
  return clampDegreeF_(filtered, minDeg, maxDeg);
#else
  (void)moveTo;
  (void)minDeg;
  (void)maxDeg;
  (void)residual;
  (void)prevFiltered;
  (void)prevErrSign;
  return filtered;
#endif
}

void beginMove_(float toX, float toY, uint16_t durationMs, bool toHome,
                float trackSpeedDps) {
  s_moveToX = toX;
  s_moveToY = toY;
  s_moveDurationMs = durationMs > 0 ? durationMs : 1;

  if (toHome) {
    s_servoX.setEaseToD(toX, s_moveDurationMs);
    s_servoY.setEaseToD(toY, s_moveDurationMs);
  } else {
    s_servoX.setEaseTo(toX, trackSpeedDps);
    s_servoY.setEaseTo(toY, trackSpeedDps);
  }
  if (!areInterruptsActive()) {
    synchronizeAllServosAndStartInterrupt();
  }

  s_moveToHome = toHome;
}

bool shouldIssueMoveByTriggerDecimation_() {
  if (MC_SERVO_MOVE_TRIGGER_DIVIDER <= 1) {
    return true;
  }
  ++s_moveTriggerPhase;
  if (s_moveTriggerPhase < MC_SERVO_MOVE_TRIGGER_DIVIDER) {
    return false;
  }
  s_moveTriggerPhase = 0;
  return true;
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
               MC_SERVO_HOME_MOVE_TIME_MS, true, (float)MC_SERVO_TRACK_SPEED_DPS);
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
    s_trackSpeedCmdDps = (float)MC_SERVO_TRACK_SPEED_DPS;
    s_deadbandResidualX = 0.0f;
    s_deadbandResidualY = 0.0f;
    s_prevFilteredX = s_cmdX;
    s_prevFilteredY = s_cmdY;
    s_prevErrSignX = 0;
    s_prevErrSignY = 0;
    s_nonHomeMoveCount = 0;
    s_moveTriggerPhase = 0;
  }
}

} // namespace

void servoDriverBegin() {
#if !MC_ENABLE_SERVO
  MC_LOGI("SERVO", "disabled by MC_ENABLE_SERVO=0");
  return;
#else
  if (s_initialized) return;

  const uint8_t attachRetX =
      s_servoX.attach(MC_SERVO_PIN_X, MC_SERVO_START_DEGREE_X,
                      DEFAULT_MICROSECONDS_FOR_0_DEGREE,
                      DEFAULT_MICROSECONDS_FOR_180_DEGREE);
  const uint8_t attachRetY =
      s_servoY.attach(MC_SERVO_PIN_Y, MC_SERVO_START_DEGREE_Y,
                      DEFAULT_MICROSECONDS_FOR_0_DEGREE,
                      DEFAULT_MICROSECONDS_FOR_180_DEGREE);
  const bool attachedX = s_servoX.attached();
  const bool attachedY = s_servoY.attached();

  if (!attachedX || !attachedY) {
    MC_LOGE("SERVO",
            "attach failed attached_x=%d attached_y=%d ret_x=%u ret_y=%u "
            "pin_x=%d pin_y=%d",
            attachedX ? 1 : 0, attachedY ? 1 : 0, (unsigned)attachRetX,
            (unsigned)attachRetY, MC_SERVO_PIN_X, MC_SERVO_PIN_Y);
    if (!attachedX && !attachedY) {
      return;
    }
  }

  s_initialized = true;

  s_servoX.setEasingType(EASE_QUADRATIC_IN_OUT);
  s_servoY.setEasingType(EASE_QUADRATIC_IN_OUT);
  setSpeedForAllServos(MC_SERVO_SPEED);

  setHome_(false);
  MC_LOGI("SERVO",
          "begin pin_x=%d pin_y=%d home=(%d,%d) attached=(%d,%d) ret=(%u,%u)",
          MC_SERVO_PIN_X, MC_SERVO_PIN_Y, MC_SERVO_START_DEGREE_X,
          MC_SERVO_START_DEGREE_Y, attachedX ? 1 : 0, attachedY ? 1 : 0,
          (unsigned)attachRetX, (unsigned)attachRetY);
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
  const float dtSec = s_lastUpdateMs == 0 ? MC_SERVO_DT_FALLBACK_SEC
                                          : (float)(now - s_lastUpdateMs) / 1000.0f;
  s_lastUpdateMs = now;

#if MC_SERVO_DIAG_SWEEP_ENABLE
  {
    float diagTargetX = (float)MC_SERVO_START_DEGREE_X;
    float diagTargetY = (float)MC_SERVO_START_DEGREE_Y;
    computeDiagSweepTargets_(now, &diagTargetX, &diagTargetY);

    const bool currentlyMoving = areInterruptsActive();
    const float threshold = currentlyMoving ? kMoveUpdateThresholdDeg
                                            : kMoveStartThresholdDeg;
    if (fabsf(diagTargetX - s_moveToX) >= threshold ||
        fabsf(diagTargetY - s_moveToY) >= threshold) {
      beginMove_(diagTargetX, diagTargetY, MC_SERVO_MOVE_TIME_MS, false,
                 (float)MC_SERVO_TRACK_SPEED_DPS);
      s_homed = false;
    }
    s_filteredTargetX = diagTargetX;
    s_filteredTargetY = diagTargetY;
    s_prevRawTargetX = diagTargetX;
    s_prevRawTargetY = diagTargetY;
  }
  return;
#endif

  if (!s_targetActive) {
    s_moveTriggerPhase = 0;
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
  const float compensatedTargetX =
      applyDeadbandCompensation_(s_filteredTargetX, s_moveToX,
                                 (float)MC_SERVO_MIN_DEGREE_X,
                                 (float)MC_SERVO_MAX_DEGREE_X,
                                 &s_deadbandResidualX, &s_prevFilteredX,
                                 &s_prevErrSignX);
  const float compensatedTargetY =
      applyDeadbandCompensation_(s_filteredTargetY, s_moveToY,
                                 (float)MC_SERVO_MIN_DEGREE_Y,
                                 (float)MC_SERVO_MAX_DEGREE_Y,
                                 &s_deadbandResidualY, &s_prevFilteredY,
                                 &s_prevErrSignY);
  float nextTargetX = compensatedTargetX;
  float nextTargetY = compensatedTargetY;

  if (MC_SERVO_RECENTER_INTERVAL_MOVES > 0 &&
      s_nonHomeMoveCount >= MC_SERVO_RECENTER_INTERVAL_MOVES) {
    nextTargetX = (float)MC_SERVO_START_DEGREE_X;
    nextTargetY = (float)MC_SERVO_START_DEGREE_Y;
    s_nonHomeMoveCount = 0;
  }
  s_filteredTargetX = nextTargetX;
  s_filteredTargetY = nextTargetY;

  const bool currentlyMoving = areInterruptsActive();
  const float threshold = currentlyMoving ? kMoveUpdateThresholdDeg
                                          : kMoveStartThresholdDeg;
  if (fabsf(s_filteredTargetX - s_moveToX) < threshold &&
      fabsf(s_filteredTargetY - s_moveToY) < threshold) {
    return;
  }
  if (!shouldIssueMoveByTriggerDecimation_()) {
    return;
  }
  const float trackSpeedDps =
      computeTrackSpeedCommand_(s_filteredTargetX, s_filteredTargetY, dtSec);
  beginMove_(s_filteredTargetX, s_filteredTargetY, MC_SERVO_MOVE_TIME_MS, false,
             trackSpeedDps);
  if (MC_SERVO_RECENTER_INTERVAL_MOVES > 0 &&
      !(fabsf(s_filteredTargetX - (float)MC_SERVO_START_DEGREE_X) <=
            kMoveStartThresholdDeg &&
        fabsf(s_filteredTargetY - (float)MC_SERVO_START_DEGREE_Y) <=
            kMoveStartThresholdDeg)) {
    if (s_nonHomeMoveCount < 255) {
      ++s_nonHomeMoveCount;
    }
  }
  s_homed = false;
#endif
}
