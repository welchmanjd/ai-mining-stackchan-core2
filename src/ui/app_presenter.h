// src/app_presenter.h
// Module implementation.
#pragma once
#include <Arduino.h>

#include "utils/app_types.h"
#include "ui/ui_mining_core2.h" // UIMining / PanelData
#include "utils/mining_summary.h"
String buildTicker(const MiningSummary &s);
void buildPanelData(const MiningSummary &summary, UIMining &ui,
                    UIMining::PanelData &data, NetworkStatus netStatus);
