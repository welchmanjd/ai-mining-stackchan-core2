// src/app_presenter.h
#pragma once
#include <Arduino.h>
#include "ai/mining_task.h"     // MiningSummary
#include "ui/ui_mining_core2.h" // UIMining / PanelData
String buildTicker(const MiningSummary& s);
void buildPanelData(const MiningSummary& summary, UIMining& ui, UIMining::PanelData& data);
