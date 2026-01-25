// src/config.h
#pragma once
#include <Arduino.h>

// =========================================================
// config.h
// - マジックナンバー排除のための「固定定数」置き場
// - AI関連（タッチ操作/時間/制限/エラーメッセージ）はここ
// - user_config.h：見た目/UX/セリフなど（秘密なし）
// - config_private.h：秘密（キー、Wi-Fi 等）
// =========================================================


// ★ユーザーが触ってOKな設定（秘密なし）
#if __has_include("user_config.h")
  #include "user_config.h"
#endif

// ★配布ビルドでは config_private.h を読まない（秘密が混ざるのを防ぐ）
#if !defined(MC_DISABLE_CONFIG_PRIVATE)
  #if __has_include("config_private.h")
    #include "config_private.h"
  #endif
#endif

#include "mc_config_store.h"

// ---------------------------------------------------------
// 互換/保険：user_config.h が無い場合のデフォルト
// （ここは “ユーザー設定値” だが、ビルドが崩れないために残す）
// ---------------------------------------------------------
#ifndef MC_DISPLAY_SLEEP_SECONDS
  #define MC_DISPLAY_SLEEP_SECONDS 600
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

// 右上のヒント文字（見た目寄りなので user_config.h で上書き可）
#ifndef MC_AI_IDLE_HINT_TEXT
  #define MC_AI_IDLE_HINT_TEXT "AI"
#endif

// 状態別ヒント（未定義なら Idle と同じ）
// Step3: ai_talk_controller.cpp から移設（単一ソース化）
#ifndef MC_AI_LISTENING_HINT_TEXT
  #define MC_AI_LISTENING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_THINKING_HINT_TEXT
  #define MC_AI_THINKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif
#ifndef MC_AI_SPEAKING_HINT_TEXT
  #define MC_AI_SPEAKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT
#endif

// 実装テスト用（user_config.h で上書き可）
#ifndef MC_AI_STT_DEBUG_SHOW_TEXT
  #define MC_AI_STT_DEBUG_SHOW_TEXT 0
#endif

// ---- Trigger / touch area ----
// 上1/3タップ（240px高なら80px）
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
// Step3: 実装側での *1000 をここに閉じ込める（値は変更しない）
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
  // LISTEN上限に合わせる（10秒）
  #define MC_AI_REC_MAX_SECONDS MC_AI_LISTEN_MAX_SECONDS
#endif

// 任意：最後に録った音声を /ai_last.wav に保存（LittleFS）
// 0:保存しない / 1:保存する
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

// overall budget margin (ms) : STT/LLM の残り時間計算に使う（値は変更しない）
#ifndef MC_AI_OVERALL_MARGIN_MS
  #define MC_AI_OVERALL_MARGIN_MS 250
#endif

// 最低限の「考え中」表示（ms）: ブロッキング後でも0にしない
#ifndef MC_AI_THINKING_MOCK_MS
  #define MC_AI_THINKING_MOCK_MS 200
#endif

// 発話後の空白（ms）
#ifndef MC_AI_POST_SPEAK_BLANK_MS
  #define MC_AI_POST_SPEAK_BLANK_MS 500
#endif

// Orchestrator が無い（sandbox等）ときの擬似発話時間（ms）
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
// Step3: ログ短縮のバイト数を単一ソース化（値は変更しない）
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
// TTS取得(ネット遅延) + 音声再生が終わるまで待つ上限。
// ここが短いと、音が鳴っているのに AI 側が tts_timeout で先に進んでしまう。
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



// ---- AI UI short texts（長文は出さない方針なので短文のみ）----
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

// ---- OpenAI (LLM) model name（秘密は config_private.h）----
#ifndef MC_OPENAI_MODEL
  #define MC_OPENAI_MODEL "gpt-5-nano"
#endif

// ---- OpenAI Responses API endpoint（秘密なし：必要なら config_private.h で上書き可）----
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
// ★命名を Web/JSON（index.html / mc_config_store）に合わせる
//   duco_miner_key / az_speech_region / az_speech_key / az_tts_voice など
// ---------------------------------------------------------
struct AppConfig {
  const char* wifi_ssid;
  const char* wifi_pass;

  // duco (Web/JSON: duco_user / duco_miner_key)
  const char* duco_user;
  const char* duco_miner_key;

  // duco (固定値)
  const char* duco_rig_name;
  const char* duco_banner;

  // azure (Web/JSON: az_speech_region / az_speech_key / az_tts_voice)
  const char* az_speech_region;
  const char* az_speech_key;
  const char* az_tts_voice;

  // app
  const char* app_name;
  const char* app_version;

  // attention
  const char* attention_text;

  // speech lines (Web/JSON: share_accepted_text / hello_text)
  const char* share_accepted_text;
  const char* hello_text;
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

  // 実行中にSETされた場合にも反映されるよう、毎回上書き（ポインタ差し替えのみ）
  cfg.wifi_ssid      = mcCfgWifiSsid();
  cfg.wifi_pass      = mcCfgWifiPass();
  cfg.duco_user      = mcCfgDucoUser();
  cfg.duco_miner_key = mcCfgDucoKey();
  cfg.az_speech_region  = mcCfgAzRegion();
  cfg.az_speech_key     = mcCfgAzKey();
  cfg.az_tts_voice      = mcCfgAzVoice();
  cfg.attention_text = mcCfgAttentionText();

  cfg.share_accepted_text = mcCfgShareAcceptedText();
  cfg.hello_text          = mcCfgHelloText();

  return cfg;
}
