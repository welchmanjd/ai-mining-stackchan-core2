// Module implementation.
#pragma once

#include <Arduino.h>

void servoDriverBegin();
void servoDriverSetTarget(float gazeX, float gazeY, bool active);
void servoDriverTick();
void servoDriverHome();
