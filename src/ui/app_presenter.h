// Module implementation.
#pragma once
#include <Arduino.h>

#include "utils/app_types.h"
#include "ui/ui_mining_core2.h" // UIMining / PanelData
#include "utils/mining_summary.h"
String buildTicker(const MiningSummary &s);
void buildPanelData(const MiningSummary &summary, UIMining &ui,
                    UIMining::PanelData &data, NetworkStatus netStatus,
                    bool aiEnabled, bool aiReady, const String &aiDiag,
                    int bootStatus, uint8_t bootWifiState,
                    uint8_t bootMiningState, uint8_t bootOpenAiState,
                    uint8_t bootAzureState, const String &bootWifiDiag,
                    const String &bootMiningDiag, const String &bootOpenAiDiag,
                    const String &bootAzureDiag, const String &bootActiveDiag);
