// src/config_private.sample.h
// Module implementation.
#pragma once
// =========================================================
// config_private.sample.h
// ---- Wi-Fi (secret) ----
#define MC_WIFI_SSID "your-ssid" // Wi-Fi SSID
#define MC_WIFI_PASS "your-password" // Wi-Fi パスワード
// ---- Duino-coin (account/personal) ----
#define MC_DUCO_USER      "your-duco-user" // Duino-coin ユーザー名
#define MC_DUCO_MINER_KEY "None" // Duino-coin miner key（未設定なら "None"）
// ---- Azure Speech (STT/TTS secret) ----
#define MC_AZ_SPEECH_REGION "your-azure-region" // Azure Speech のリージョン名
#define MC_AZ_SPEECH_KEY    "your-azure-speech-key" // Azure Speech のAPIキー
#define MC_AZ_CUSTOM_SUBDOMAIN "" // カスタムエンドポイント（必要ならURL or ホスト）
// ---- OpenAI (LLM secret) ----
#define MC_OPENAI_API_KEY "your-openai-api-key" // OpenAI APIキー
// #define MC_OPENAI_ENDPOINT "https://api.openai.com/v1/responses" // 変更時のみ有効化
