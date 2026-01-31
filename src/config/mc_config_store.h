// src/mc_config_store.h
#pragma once
#include <Arduino.h>

// LittleFS にある設定を読み込み、ランタイムに反映する。
void mcConfigBegin();

// WebSerial からの key/value を検証して一時反映する（保存はしない）。
bool mcConfigSetKV(const String& key, const String& val, String& err);

// LittleFS に保存する。
bool mcConfigSave(String& err);

// 設定値をマスクした JSON を返す（@CFG 用）。
// *_set 系の bool は含めない。
String mcConfigGetMaskedJson();

// ---- getters ----
// runtime(=LittleFS) で上書きできない値は config_private.h の値を優先して返す。
const char* mcCfgWifiSsid();
const char* mcCfgWifiPass();

const char* mcCfgDucoUser();
const char* mcCfgDucoKey();

const char* mcCfgAzRegion();
const char* mcCfgAzKey();
const char* mcCfgAzVoice();

// カスタムサブドメイン/エンドポイント（未設定なら空文字）。
const char* mcCfgAzEndpoint();

const char* mcCfgAttentionText();

uint8_t mcCfgSpkVolume();

const char* mcCfgShareAcceptedText();
const char* mcCfgHelloText();

uint32_t mcCfgCpuMhz();
