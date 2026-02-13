// src/mc_config_store.h
// Module implementation.
#pragma once
#include <Arduino.h>
// Runtime config store (NVS + defaults)
const char* mcCfgWifiSsid();
const char* mcCfgWifiPass();
const char* mcCfgDucoUser();
const char* mcCfgDucoKey();
const char* mcCfgAzRegion();
const char* mcCfgAzKey();
const char* mcCfgAzVoice();
const char* mcCfgAzEndpoint();
const char* mcCfgOpenAiKey();
const char* mcCfgOpenAiModel();
const char* mcCfgOpenAiInstructions();
const char* mcCfgAttentionText();
uint8_t mcCfgSpkVolume();
const char* mcCfgShareAcceptedText();
const char* mcCfgHelloText();
bool mcCfgWifiEnabled();
bool mcCfgMiningEnabled();
bool mcCfgAiEnabled();
uint32_t mcCfgCpuMhz();

// Config edit helpers
void mcConfigBegin();
bool mcConfigSetKV(const String& key, const String& value, String& err);
bool mcConfigSave(String& err);
String mcConfigGetMaskedJson();
