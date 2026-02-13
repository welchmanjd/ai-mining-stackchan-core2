// Module implementation.
#pragma once

#include <Arduino.h>

void servoDriverBegin();
void servoDriverUpdate(float gazeX, float gazeY, bool active);
void servoDriverHome();
