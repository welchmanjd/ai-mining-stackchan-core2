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
// User-tunable defaults (override in user_config.h)
#ifndef MC_DISPLAY_SLEEP_SECONDS
  #define MC_DISPLAY_SLEEP_SECONDS 60 // app_runtime.cpp: 画面スリープ判定の初期値（NVS display_sleep_sで上書き可）
#endif
#ifndef MC_SPK_VOLUME
  #define MC_SPK_VOLUME 160 // main.cpp/AzureTts: 起動時のスピーカー音量の初期値
#endif
#ifndef MC_ATTENTION_TEXT
  #define MC_ATTENTION_TEXT "Hi" // ui_mining_core2.cpp: Attentionモードのデフォルト表示
#endif
#ifndef MC_SPEECH_SHARE_ACCEPTED
  #define MC_SPEECH_SHARE_ACCEPTED "シェア獲得したよ！" // stackchan_behavior.cpp: ShareAccepted時のTTS
#endif
#ifndef MC_SPEECH_HELLO
  #define MC_SPEECH_HELLO "こんにちはマイニングスタックチャンです" // app_runtime.cpp: BtnB押下時のTTS
#endif
#ifndef MC_AI_IDLE_HINT_TEXT
  #define MC_AI_IDLE_HINT_TEXT "AI" // ai_talk_controller.cpp: AIオーバーレイ右上のヒント(Idle)
#endif
#ifndef MC_AI_LISTENING_HINT_TEXT
  #define MC_AI_LISTENING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // ai_talk_controller.cpp: AIオーバーレイ右上のヒント(Listening)
#endif
#ifndef MC_AI_THINKING_HINT_TEXT
  #define MC_AI_THINKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // ai_talk_controller.cpp: AIオーバーレイ右上のヒント(Thinking)
#endif
#ifndef MC_AI_SPEAKING_HINT_TEXT
  #define MC_AI_SPEAKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // ai_talk_controller.cpp: AIオーバーレイ右上のヒント(Speaking)
#endif
#ifndef MC_AI_TEXT_THINKING
  #define MC_AI_TEXT_THINKING  "考え中" // ai_talk_controller.cpp: Thinking時のオーバーレイ左上表示
#endif
#ifndef MC_AI_TEXT_COOLDOWN
  #define MC_AI_TEXT_COOLDOWN "......." // ai_talk_controller.cpp: Cooldown時のオーバーレイ左上表示
#endif
#ifndef MC_AI_TEXT_FALLBACK
  #define MC_AI_TEXT_FALLBACK "わかりません" // ai_talk_controller.cpp: STT/LLM失敗時の代替返答
#endif
#ifndef MC_AZ_TTS_VOICE
  #define MC_AZ_TTS_VOICE "ja-JP-AoiNeural" // azure_tts.cpp: defaultVoice_として使用
#endif
#ifndef MC_OPENAI_INSTRUCTIONS
  // openai_llm.cpp: req["instructions"] にそのまま入る初期指示
  #define MC_OPENAI_INSTRUCTIONS \
      "あなたはスタックチャンの会話AIです。日本語で短く答えてください。" \
      "返答は120文字以内。箇条書き禁止。1〜2文。" \
      "相手が『聞こえる？』等の確認なら、明るく短く返してください。"
#endif
// ---------------------------------------------------------
// Core defaults (generally not user-tuned)
#ifndef MC_CPU_FREQ_MHZ
  #define MC_CPU_FREQ_MHZ 240 // main.cpp: setCpuFrequencyMhzの要求値
#endif
// ---------------------------------------------------------
// ===== AI TALK (Lv2) : fixed constants (touch/time/limits) =====
// ---------------------------------------------------------
// ---- Recording (toggle) ----
#ifndef MC_AI_LISTEN_MAX_SECONDS
  #define MC_AI_LISTEN_MAX_SECONDS 10 // ai_talk_controller.cpp: 録音自動停止の基準秒数
#endif
#ifndef MC_AI_LISTEN_CANCEL_WINDOW_SEC
  #define MC_AI_LISTEN_CANCEL_WINDOW_SEC 3 // MC_AI_LISTEN_CANCEL_WINDOW_MSの基準値
#endif
// ---- Recording time (ms; internal use) ----
#ifndef MC_AI_LISTEN_TIMEOUT_MS
  #define MC_AI_LISTEN_TIMEOUT_MS ((uint32_t)MC_AI_LISTEN_MAX_SECONDS * 1000UL) // ai_talk_controller.cpp: 録音の自動停止タイムアウト
#endif
#ifndef MC_AI_LISTEN_CANCEL_WINDOW_MS
  #define MC_AI_LISTEN_CANCEL_WINDOW_MS ((uint32_t)MC_AI_LISTEN_CANCEL_WINDOW_SEC * 1000UL) // ai_talk_controller.cpp: タップキャンセル許容時間
#endif
// ---- Recording params (PCM16 mono) ----
#ifndef MC_AI_REC_SAMPLE_RATE
  #define MC_AI_REC_SAMPLE_RATE 16000 // audio_recorder.cpp/ai_talk_controller.cpp: 録音サンプルレート
#endif
// ---- Cooldown ----
#ifndef MC_AI_COOLDOWN_MS
  #define MC_AI_COOLDOWN_MS 2000 // ai_talk_controller.cpp: Cooldown基本時間
#endif
#ifndef MC_AI_COOLDOWN_ERROR_EXTRA_MS
  #define MC_AI_COOLDOWN_ERROR_EXTRA_MS 1000 // ai_talk_controller.cpp: エラー時の追加Cooldown
#endif
// ---- Timeouts (stage / overall) ----
#ifndef MC_AI_STT_TIMEOUT_MS
  #define MC_AI_STT_TIMEOUT_MS 8000 // ai_talk_controller.cpp: STT呼び出しの上限
#endif
#ifndef MC_AI_LLM_TIMEOUT_MS
  #define MC_AI_LLM_TIMEOUT_MS 10000 // ai_talk_controller.cpp: LLM呼び出しの上限
#endif
#ifndef MC_AI_OVERALL_DEADLINE_MS
  #define MC_AI_OVERALL_DEADLINE_MS 20000 // ai_talk_controller.cpp: STT+LLM全体の予算
#endif
#ifndef MC_AI_OVERALL_MARGIN_MS
  #define MC_AI_OVERALL_MARGIN_MS 250 // ai_talk_controller.cpp: 予算計算のマージン
#endif
#ifndef MC_AI_THINKING_MOCK_MS
  #define MC_AI_THINKING_MOCK_MS 200 // ai_talk_controller.cpp: 最低思考表示時間
#endif
#ifndef MC_AI_POST_SPEAK_BLANK_MS
  #define MC_AI_POST_SPEAK_BLANK_MS 500 // ai_talk_controller.cpp: 発話後のブランク時間
#endif
#ifndef MC_AI_SIMULATED_SPEAK_MS
  #define MC_AI_SIMULATED_SPEAK_MS 2000 // ai_talk_controller.cpp: TTS待ちでない時の擬似発話時間
#endif
// ---- Rate / safety limits ----
#ifndef MC_AI_MAX_INPUT_CHARS
  #define MC_AI_MAX_INPUT_CHARS 200 // ai_talk_controller.cpp: STT結果の最大文字数
#endif
#ifndef MC_AI_TTS_MAX_CHARS
  #define MC_AI_TTS_MAX_CHARS 120 // ai_talk_controller.cpp: TTSに渡す最大文字数
#endif
// ---- Log head limits (bytes) ----
#ifndef MC_AI_LOG_HEAD_BYTES_OVERLAY
  #define MC_AI_LOG_HEAD_BYTES_OVERLAY 40 // ai_talk_controller.cpp: オーバーレイ表示のログ切り詰め
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_LLM_ERRMSG_SHORT
  #define MC_AI_LOG_HEAD_BYTES_LLM_ERRMSG_SHORT 80 // openai_llm.cpp: エラー文の短縮ログ
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_LLM_HTTP_ERRMSG
  #define MC_AI_LOG_HEAD_BYTES_LLM_HTTP_ERRMSG 120 // openai_llm.cpp: HTTPエラー文の短縮ログ
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_LLM_DIAG
  #define MC_AI_LOG_HEAD_BYTES_LLM_DIAG 180 // openai_llm.cpp: 診断ログの短縮
#endif
// ---- AI: TTS done hard timeout (ms) ----
#ifndef MC_AI_TTS_HARD_TIMEOUT_BASE_MS
  #define MC_AI_TTS_HARD_TIMEOUT_BASE_MS  25000 // ai_talk_controller.cpp: TTSハードタイムアウト基準
#endif
#ifndef MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS
  #define MC_AI_TTS_HARD_TIMEOUT_PER_BYTE_MS 90 // ai_talk_controller.cpp: 文字数比例の追加時間
#endif
#ifndef MC_AI_TTS_HARD_TIMEOUT_MIN_MS
  #define MC_AI_TTS_HARD_TIMEOUT_MIN_MS 20000 // ai_talk_controller.cpp: ハードタイムアウト最小
#endif
#ifndef MC_AI_TTS_HARD_TIMEOUT_MAX_MS
  #define MC_AI_TTS_HARD_TIMEOUT_MAX_MS 60000 // ai_talk_controller.cpp: ハードタイムアウト最大
#endif
// ---- Error messages (short, searchable; no codes) ----
#ifndef MC_AI_ERR_TEMP_FAIL_TRY_AGAIN
  #define MC_AI_ERR_TEMP_FAIL_TRY_AGAIN "一時的に失敗。もう一回" // ai_talk_controller.cpp: STT失敗の代替文言
#endif
#ifndef MC_AI_ERR_MIC_TOO_QUIET
  #define MC_AI_ERR_MIC_TOO_QUIET "声が聞こえない。近づいてね" // ai_talk_controller.cpp: 録音が無音時の代替文言
#endif
#ifndef MC_OPENAI_MODEL
  #define MC_OPENAI_MODEL "gpt-5-nano" // openai_llm.cpp: req["model"]
#endif
#ifndef MC_OPENAI_ENDPOINT
  #define MC_OPENAI_ENDPOINT "https://api.openai.com/v1/responses" // openai_llm.cpp: HTTPリクエスト先
#endif
// ---- OpenAI LLM tuning (experiment) ----
#ifndef MC_OPENAI_MAX_OUTPUT_TOKENS
  #define MC_OPENAI_MAX_OUTPUT_TOKENS 1024 // openai_llm.cpp: req["max_output_tokens"]
#endif
#ifndef MC_OPENAI_REASONING_EFFORT
  #define MC_OPENAI_REASONING_EFFORT "low"   // openai_llm.cpp: req["reasoning"]["effort"]
#endif
#ifndef MC_OPENAI_LOG_USAGE
  #define MC_OPENAI_LOG_USAGE 1 // openai_llm.cpp: usageログ出力の有無
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
