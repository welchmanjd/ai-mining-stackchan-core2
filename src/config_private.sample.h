// src/config_private.sample.h
#pragma once

// =========================================================
// config_private.sample.h
// - このファイルを config_private.h にコピーして使ってください。
// - 秘密情報だけ（Git管理しない運用推奨）
// =========================================================

// ---- Wi-Fi (secret) ----
#define MC_WIFI_SSID "your-ssid"
#define MC_WIFI_PASS "your-password"

// ---- Duino-coin (account/personal) ----
#define MC_DUCO_USER      "your-duco-user"
#define MC_DUCO_MINER_KEY "None"  // ある場合

// ---- Azure Speech (STT/TTS secret) ----
// Speech Service の「キー」と「リージョン」
#define MC_AZ_SPEECH_REGION "japaneast"
#define MC_AZ_SPEECH_KEY    "your-azure-speech-key"

// 任意：Speech リソースのカスタムサブドメイン（空なら未使用）
// 例) "my-speech-app"  または  "my-speech-app.cognitiveservices.azure.com"
#define MC_AZ_CUSTOM_SUBDOMAIN ""

// ---- OpenAI (LLM secret) ----
#define MC_OPENAI_API_KEY "your-openai-api-key"
