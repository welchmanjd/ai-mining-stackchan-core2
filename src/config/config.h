// src/config.h
// Module implementation.
#pragma once
#include <Arduino.h>
// =========================================================
// config.h
// =========================================================
#if __has_include("user_config.h")
  #include "user_config.h"
#endif
#if !defined(MC_DISABLE_CONFIG_PRIVATE)
  #if __has_include("config_private.h")
    #include "config_private.h"
  #endif
#endif
#include "mc_config_store.h"
// ---------------------------------------------------------
// ---------------------------------------------------------
#ifndef MC_DISPLAY_SLEEP_SECONDS
  #define MC_DISPLAY_SLEEP_SECONDS 60 //default:600
#endif
#ifndef MC_TTS_ACTIVE_THREADS_DURING_TTS
  #define MC_TTS_ACTIVE_THREADS_DURING_TTS 0
#endif
#ifndef MC_ATTENTION_TEXT
  #define MC_ATTENTION_TEXT "Hi"
#endif
#ifndef MC_SPK_VOLUME
  #define MC_SPK_VOLUME 160
#endif
#ifndef MC_CPU_FREQ_MHZ
  #define MC_CPU_FREQ_MHZ 240
#endif
#ifndef MC_AZ_TTS_VOICE
  #define MC_AZ_TTS_VOICE "ja-JP-AoiNeural"
#endif
#ifndef MC_SPEECH_SHARE_ACCEPTED
  #define MC_SPEECH_SHARE_ACCEPTED "シェア獲得したよ！"
#endif
#ifndef MC_SPEECH_HELLO
  #define MC_SPEECH_HELLO "こんにちはマイニングスタックチャンです"
#endif
// ---------------------------------------------------------
// ===== AI TALK (Lv2) : fixed constants (touch/time/limits) =====
// ---------------------------------------------------------
#ifndef MC_AI_TALK_ENABLED
  #define MC_AI_TALK_ENABLED 1
#endif
#ifndef MC_AI_IDLE_HINT_TEXT
  #define MC_AI_IDLE_HINT_TEXT "AI"
#endif
#ifndef MC_AI_LISTENING_HINT_TEXT
  #define MC_AI_LISTENING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_THINKING_HINT_TEXT
  #define MC_AI_THINKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_SPEAKING_HINT_TEXT
  #define MC_AI_SPEAKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_STT_DEBUG_SHOW_TEXT
  #define MC_AI_STT_DEBUG_SHOW_TEXT 0
#endif
// ---- Trigger / touch area ----
#ifndef MC_AI_TAP_AREA_TOP_HEIGHT_PX
  #define MC_AI_TAP_AREA_TOP_HEIGHT_PX 80
#endif
#ifndef MC_AI_TAP_DEBOUNCE_MS
  #define MC_AI_TAP_DEBOUNCE_MS 150
#endif
// ---- Recording (toggle) ----
#ifndef MC_AI_LISTEN_MAX_SECONDS
  #define MC_AI_LISTEN_MAX_SECONDS 10
#endif
#ifndef MC_AI_LISTEN_MIN_SECONDS
  #define MC_AI_LISTEN_MIN_SECONDS 3
#endif
#ifndef MC_AI_LISTEN_CANCEL_WINDOW_SEC
  #define MC_AI_LISTEN_CANCEL_WINDOW_SEC 3
#endif
#ifndef MC_AI_COUNTDOWN_UPDATE_MS
  #define MC_AI_COUNTDOWN_UPDATE_MS 250
#endif
// ---- Recording time (ms; internal use) ----
#ifndef MC_AI_LISTEN_TIMEOUT_MS
  #define MC_AI_LISTEN_TIMEOUT_MS ((uint32_t)MC_AI_LISTEN_MAX_SECONDS * 1000UL)
#endif
#ifndef MC_AI_LISTEN_MIN_MS
  #define MC_AI_LISTEN_MIN_MS ((uint32_t)MC_AI_LISTEN_MIN_SECONDS * 1000UL)
#endif
#ifndef MC_AI_LISTEN_CANCEL_WINDOW_MS
  #define MC_AI_LISTEN_CANCEL_WINDOW_MS ((uint32_t)MC_AI_LISTEN_CANCEL_WINDOW_SEC * 1000UL)
#endif
// ---- Recording params (PCM16 mono) ----
#ifndef MC_AI_REC_SAMPLE_RATE
  #define MC_AI_REC_SAMPLE_RATE 16000
#endif
#ifndef MC_AI_REC_MAX_SECONDS
  #define MC_AI_REC_MAX_SECONDS MC_AI_LISTEN_MAX_SECONDS
#endif
#ifndef MC_AI_REC_SAVE_LAST_WAV
  #define MC_AI_REC_SAVE_LAST_WAV 0
#endif
// ---- Cooldown ----
#ifndef MC_AI_COOLDOWN_MS
  #define MC_AI_COOLDOWN_MS 2000
#endif
#ifndef MC_AI_COOLDOWN_ERROR_EXTRA_MS
  #define MC_AI_COOLDOWN_ERROR_EXTRA_MS 1000
#endif
// ---- Timeouts (stage / overall) ----
#ifndef MC_AI_STT_TIMEOUT_MS
  #define MC_AI_STT_TIMEOUT_MS 8000
#endif
#ifndef MC_AI_LLM_TIMEOUT_MS
  #define MC_AI_LLM_TIMEOUT_MS 10000
#endif
#ifndef MC_AI_TTS_TIMEOUT_MS
  #define MC_AI_TTS_TIMEOUT_MS 10000
#endif
#ifndef MC_AI_OVERALL_DEADLINE_MS
  #define MC_AI_OVERALL_DEADLINE_MS 20000
#endif
#ifndef MC_AI_OVERALL_MARGIN_MS
  #define MC_AI_OVERALL_MARGIN_MS 250
#endif
#ifndef MC_AI_THINKING_MOCK_MS
  #define MC_AI_THINKING_MOCK_MS 200
#endif
#ifndef MC_AI_POST_SPEAK_BLANK_MS
  #define MC_AI_POST_SPEAK_BLANK_MS 500
#endif
#ifndef MC_AI_SIMULATED_SPEAK_MS
  #define MC_AI_SIMULATED_SPEAK_MS 2000
#endif
// ---- Rate / safety limits ----
#ifndef MC_AI_MAX_TALKS_PER_MIN
  #define MC_AI_MAX_TALKS_PER_MIN 6
#endif
#ifndef MC_AI_MAX_INPUT_CHARS
  #define MC_AI_MAX_INPUT_CHARS 200
#endif
#ifndef MC_AI_TTS_MAX_CHARS
  #define MC_AI_TTS_MAX_CHARS 120
#endif
// ---- Log head limits (bytes) ----
#ifndef MC_AI_LOG_HEAD_BYTES_STT_TEXT
  #define MC_AI_LOG_HEAD_BYTES_STT_TEXT 30
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_OVERLAY
  #define MC_AI_LOG_HEAD_BYTES_OVERLAY 40
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_LLM_ERRMSG_SHORT
  #define MC_AI_LOG_HEAD_BYTES_LLM_ERRMSG_SHORT 80
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_LLM_HTTP_ERRMSG
  #define MC_AI_LOG_HEAD_BYTES_LLM_HTTP_ERRMSG 120
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_LLM_DIAG
  #define MC_AI_LOG_HEAD_BYTES_LLM_DIAG 180
#endif
// ---- AI: TTS done hard timeout (ms) ----
#ifndef MC_AI_TTS_HARD_TIMEOUT_BASE_MS
  #define MC_AI_TTS_HARD_TIMEOUT_BASE_MS  25000
#endif
#ifndef MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS
  #define MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS 90
#endif
#ifndef MC_AI_TTS_HARD_TIMEOUT_MIN_MS
  #define MC_AI_TTS_HARD_TIMEOUT_MIN_MS 20000
#endif
#ifndef MC_AI_TTS_HARD_TIMEOUT_MAX_MS
  #define MC_AI_TTS_HARD_TIMEOUT_MAX_MS 60000
#endif
#ifndef MC_AI_TEXT_LISTENING
  #define MC_AI_TEXT_LISTENING "聞いています"
#endif
#ifndef MC_AI_TEXT_THINKING
  #define MC_AI_TEXT_THINKING  "考え中"
#endif
#ifndef MC_AI_TEXT_CANCEL_HINT
  #define MC_AI_TEXT_CANCEL_HINT "タップでキャンセルできるよ"
#endif
#ifndef MC_AI_TEXT_COOLDOWN
  #define MC_AI_TEXT_COOLDOWN "......."
#endif
#ifndef MC_AI_TEXT_FALLBACK
  #define MC_AI_TEXT_FALLBACK "わかりません"
#endif
// ---- Error messages (short, searchable; no codes) ----
#ifndef MC_AI_ERR_NET_UNSTABLE
  #define MC_AI_ERR_NET_UNSTABLE "Wi-Fi/ネットが不安定"
#endif
#ifndef MC_AI_ERR_BUSY_TRY_LATER
  #define MC_AI_ERR_BUSY_TRY_LATER "混雑中。少し待ってね"
#endif
#ifndef MC_AI_ERR_TEMP_FAIL_TRY_AGAIN
  #define MC_AI_ERR_TEMP_FAIL_TRY_AGAIN "一時的に失敗。もう一回"
#endif
#ifndef MC_AI_ERR_SPEECH_KEY_CHECK
  #define MC_AI_ERR_SPEECH_KEY_CHECK "Speechキーを確認してね"
#endif
#ifndef MC_AI_ERR_SPEECH_REGION_CHECK
  #define MC_AI_ERR_SPEECH_REGION_CHECK "Speech地域(リージョン)を確認"
#endif
#ifndef MC_AI_ERR_SPEECH_QUOTA_MAYBE
  #define MC_AI_ERR_SPEECH_QUOTA_MAYBE "Speech利用上限(クォータ)かも"
#endif
#ifndef MC_AI_ERR_OPENAI_KEY_CHECK
  #define MC_AI_ERR_OPENAI_KEY_CHECK "OpenAIキーを確認してね"
#endif
#ifndef MC_AI_ERR_INPUT_TOO_LONG
  #define MC_AI_ERR_INPUT_TOO_LONG "入力が長いかも。短くしてね"
#endif
#ifndef MC_AI_ERR_MIC_TOO_QUIET
  #define MC_AI_ERR_MIC_TOO_QUIET "声が聞こえない。近づいてね"
#endif
#ifndef MC_AI_ERR_AUDIO_OUT_FAIL
  #define MC_AI_ERR_AUDIO_OUT_FAIL "音が出ないみたい"
#endif
// ---- Sounds ----
#ifndef MC_AI_BEEP_FREQ_HZ
  #define MC_AI_BEEP_FREQ_HZ 880
#endif
#ifndef MC_AI_BEEP_DUR_MS
  #define MC_AI_BEEP_DUR_MS 80
#endif
#ifndef MC_AI_ERROR_BEEP_FREQ_HZ
  #define MC_AI_ERROR_BEEP_FREQ_HZ 220
#endif
#ifndef MC_AI_ERROR_BEEP_DUR_MS
  #define MC_AI_ERROR_BEEP_DUR_MS 200
#endif
#ifndef MC_OPENAI_MODEL
  #define MC_OPENAI_MODEL "gpt-5-nano"
#endif
#ifndef MC_OPENAI_ENDPOINT
  #define MC_OPENAI_ENDPOINT "https://api.openai.com/v1/responses"
#endif
// ---- OpenAI LLM tuning (experiment) ----
#ifndef MC_OPENAI_MAX_OUTPUT_TOKENS
  #define MC_OPENAI_MAX_OUTPUT_TOKENS 1024
#endif
#ifndef MC_OPENAI_REASONING_EFFORT
  #define MC_OPENAI_REASONING_EFFORT "low"   // low/medium/high
#endif
#ifndef MC_OPENAI_LOG_USAGE
  #define MC_OPENAI_LOG_USAGE 1
#endif
// ---------------------------------------------------------
// ---------------------------------------------------------
struct AppConfig {
  const char* wifiSsid_;
  const char* wifiPass_;
  // duco (Web/JSON: duco_user / duco_miner_key)
  const char* ducoUser_;
  const char* ducoMinerKey_;
  const char* ducoRigName_;
  const char* ducoBanner_;
  // azure (Web/JSON: az_speech_region / az_speech_key / az_tts_voice)
  const char* azSpeechRegion_;
  const char* azSpeechKey_;
  const char* azTtsVoice_;
  // app
  const char* appName_;
  const char* appVersion_;
  // attention
  const char* attentionText_;
  // speech lines (Web/JSON: share_accepted_text / hello_text)
  const char* shareAcceptedText_;
  const char* helloText_;
};
inline const AppConfig& appConfig() {
  static AppConfig cfg{
    // wifi
    mcCfgWifiSsid(),
    mcCfgWifiPass(),
    // duco
    mcCfgDucoUser(),
    mcCfgDucoKey(),
    "Mining-Stackchan-Core2", // duco_rig_name
    "M5StackCore2",           // duco_banner
    // azure tts
    mcCfgAzRegion(),
    mcCfgAzKey(),
    mcCfgAzVoice(),
    // app
    "Mining-Stackchan-Core2", // app_name
    "0.681",                  // app_version
    // attention
    mcCfgAttentionText(),
    // speech lines
    mcCfgShareAcceptedText(),
    mcCfgHelloText()
  };
  cfg.wifiSsid_      = mcCfgWifiSsid();
  cfg.wifiPass_      = mcCfgWifiPass();
  cfg.ducoUser_      = mcCfgDucoUser();
  cfg.ducoMinerKey_ = mcCfgDucoKey();
  cfg.azSpeechRegion_  = mcCfgAzRegion();
  cfg.azSpeechKey_     = mcCfgAzKey();
  cfg.azTtsVoice_      = mcCfgAzVoice();
  cfg.attentionText_ = mcCfgAttentionText();
  cfg.shareAcceptedText_ = mcCfgShareAcceptedText();
  cfg.helloText_          = mcCfgHelloText();
  return cfg;
}
