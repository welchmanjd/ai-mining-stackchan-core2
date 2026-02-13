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

int clampDegree_(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

int gazeToDegree_(float gaze, int startDeg, float gain, int invert, int minDeg,
                  int maxDeg) {
  const float sign = invert ? -1.0f : 1.0f;
  const float target = startDeg + (gain * (gaze * sign));
  return clampDegree_((int)lroundf(target), minDeg, maxDeg);
}

void applyDegreesNonBlocking_(int degX, int degY, uint16_t moveMs) {
  s_servoX.startEaseToD(degX, moveMs);
  s_servoY.startEaseToD(degY, moveMs);
}

void setHome_(bool smooth) {
  if (smooth) {
    s_cmdX += ((float)MC_SERVO_START_DEGREE_X - s_cmdX) * MC_SERVO_SMOOTH_ALPHA;
    s_cmdY += ((float)MC_SERVO_START_DEGREE_Y - s_cmdY) * MC_SERVO_SMOOTH_ALPHA;
    applyDegreesNonBlocking_((int)lroundf(s_cmdX), (int)lroundf(s_cmdY),
                             MC_SERVO_HOME_MOVE_TIME_MS);
  } else {
    s_cmdX = (float)MC_SERVO_START_DEGREE_X;
    s_cmdY = (float)MC_SERVO_START_DEGREE_Y;
    applyDegreesNonBlocking_(MC_SERVO_START_DEGREE_X, MC_SERVO_START_DEGREE_Y,
                             MC_SERVO_HOME_MOVE_TIME_MS);
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
  s_homed = true;
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

void servoDriverUpdate(float gazeX, float gazeY, bool active) {
#if !MC_ENABLE_SERVO
  (void)gazeX;
  (void)gazeY;
  (void)active;
  return;
#else
  if (!s_initialized) return;
  const uint32_t now = millis();
  if ((uint32_t)(now - s_lastUpdateMs) < MC_SERVO_UPDATE_INTERVAL_MS) return;
  s_lastUpdateMs = now;

  if (!active) {
    if (!s_homed) {
      servoDriverHome();
    }
    return;
  }

  const int degX = gazeToDegree_(gazeX, MC_SERVO_START_DEGREE_X, MC_SERVO_GAIN_X,
                                 MC_SERVO_INVERT_X, MC_SERVO_MIN_DEGREE_X,
                                 MC_SERVO_MAX_DEGREE_X);
  const int degY = gazeToDegree_(gazeY, MC_SERVO_START_DEGREE_Y, MC_SERVO_GAIN_Y,
                                 MC_SERVO_INVERT_Y, MC_SERVO_MIN_DEGREE_Y,
                                 MC_SERVO_MAX_DEGREE_Y);
  s_cmdX += ((float)degX - s_cmdX) * MC_SERVO_SMOOTH_ALPHA;
  s_cmdY += ((float)degY - s_cmdY) * MC_SERVO_SMOOTH_ALPHA;
  applyDegreesNonBlocking_((int)lroundf(s_cmdX), (int)lroundf(s_cmdY),
                           MC_SERVO_MOVE_TIME_MS);
  s_homed = false;
#endif
}
