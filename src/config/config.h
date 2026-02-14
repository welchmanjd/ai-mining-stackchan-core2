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
  #define MC_DISPLAY_SLEEP_SECONDS 600 // app_runtime.cpp: 画面スリープ判定の初期値（NVS display_sleep_sで上書き可）
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
#ifndef MC_AI_LLM_TASK_STACK
  #define MC_AI_LLM_TASK_STACK 8192 // ai_talk_controller.cpp: LLMタスクのスタック
#endif
#ifndef MC_AI_LLM_TASK_PRIO
  #define MC_AI_LLM_TASK_PRIO 1 // ai_talk_controller.cpp: LLMタスク優先度
#endif
#ifndef MC_AI_LLM_TASK_CORE
  #define MC_AI_LLM_TASK_CORE 0 // ai_talk_controller.cpp: LLMタスクの実行コア
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
#ifndef MC_AI_LOG_HEAD_BYTES_STT_LOG
  #define MC_AI_LOG_HEAD_BYTES_STT_LOG 40 // ai_talk_controller.cpp: STT文字列ログの切り詰め
#endif
#ifndef MC_AI_LOG_HEAD_BYTES_TTS_LOG
  #define MC_AI_LOG_HEAD_BYTES_TTS_LOG 40 // ai_talk_controller.cpp: TTS文字列ログの切り詰め
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

// ---------------------------------------------------------
// ===== MINING (Duco) =====
// ---------------------------------------------------------
#ifndef MC_DUCO_POOL_TIMEOUT_MS
  #define MC_DUCO_POOL_TIMEOUT_MS 7000 // mining_task.cpp: pool info HTTP timeout
#endif
#ifndef MC_DUCO_CLI_TIMEOUT_MS
  #define MC_DUCO_CLI_TIMEOUT_MS 15000 // mining_task.cpp: pool node TCP timeout
#endif
#ifndef MC_DUCO_BANNER_TIMEOUT_MS
  #define MC_DUCO_BANNER_TIMEOUT_MS 5000 // mining_task.cpp: banner wait timeout
#endif
#ifndef MC_DUCO_JOB_TIMEOUT_MS
  #define MC_DUCO_JOB_TIMEOUT_MS 10000 // mining_task.cpp: job wait timeout
#endif
#ifndef MC_DUCO_FEEDBACK_TIMEOUT_MS
  #define MC_DUCO_FEEDBACK_TIMEOUT_MS 10000 // mining_task.cpp: feedback wait timeout
#endif
#ifndef MC_DUCO_POOL_RECOVERY_DELAY_MS
  #define MC_DUCO_POOL_RECOVERY_DELAY_MS 5000 // mining_task.cpp: retry delay after pool lookup fail
#endif
#ifndef MC_DUCO_TASK_STACK_SIZE
  #define MC_DUCO_TASK_STACK_SIZE 8192 // mining_task.cpp: Duco miner task stack
#endif
#ifndef MC_DUCO_TASK_PRIO
  #define MC_DUCO_TASK_PRIO 1 // mining_task.cpp: Duco miner task priority
#endif

// ---------------------------------------------------------
// ===== UI =====
// ---------------------------------------------------------
#ifndef MC_UI_DEFAULT_BRIGHTNESS
  #define MC_UI_DEFAULT_BRIGHTNESS 128 // ui_mining_core2.cpp/app_runtime.cpp: active brightness
#endif
#ifndef MC_UI_AVATAR_SCALE_MINI
  #define MC_UI_AVATAR_SCALE_MINI 0.45f // ui_mining_core2.cpp: dashboard avatar scale
#endif
#ifndef MC_UI_AVATAR_SCALE_FULL
  #define MC_UI_AVATAR_SCALE_FULL 1.0f // ui_mining_core2.cpp: stackchan avatar scale
#endif
#ifndef MC_UI_REFRESH_INTERVAL_MS
  #define MC_UI_REFRESH_INTERVAL_MS 80 // ui_mining_core2.cpp: redraw interval
#endif
#ifndef MC_UI_HEARTBEAT_INTERVAL_MS
  #define MC_UI_HEARTBEAT_INTERVAL_MS 5000 // ui_mining_core2.cpp: UI heartbeat log interval
#endif
#ifndef MC_UI_TOUCH_BEEP_FREQ
  #define MC_UI_TOUCH_BEEP_FREQ 1500 // ui_mining_core2.cpp: touch beep frequency
#endif
#ifndef MC_UI_TOUCH_BEEP_MS
  #define MC_UI_TOUCH_BEEP_MS 50 // ui_mining_core2.cpp: touch beep duration
#endif

// ---------------------------------------------------------
// ===== APP RUNTIME =====
// ---------------------------------------------------------
#ifndef MC_WIFI_CONNECT_TIMEOUT_MS
  #define MC_WIFI_CONNECT_TIMEOUT_MS 20000 // app_runtime.cpp: WiFi connect timeout
#endif
#ifndef MC_MINING_BOOT_TIMEOUT_MS
  #define MC_MINING_BOOT_TIMEOUT_MS 15000 // app_runtime.cpp: mining boot timeout
#endif
#ifndef MC_BUBBLE_MIN_MS
  #define MC_BUBBLE_MIN_MS 1500 // app_runtime.cpp: bubble minimum display time
#endif
#ifndef MC_BUBBLE_PER_CHAR_MS
  #define MC_BUBBLE_PER_CHAR_MS 120 // app_runtime.cpp: per-character bubble time
#endif
#ifndef MC_BOOT_PROBE_BACKOFF_MS
  #define MC_BOOT_PROBE_BACKOFF_MS {5000, 10000, 30000} // app_runtime.cpp: probe retry backoff
#endif
#ifndef MC_OPENAI_PROBE_TIMEOUT_MS
  #define MC_OPENAI_PROBE_TIMEOUT_MS 8000 // app_runtime.cpp/serial_setup.cpp: OpenAI probe timeout
#endif
#ifndef MC_BOOT_PROBE_TASK_STACK
  #define MC_BOOT_PROBE_TASK_STACK 8192 // app_runtime.cpp: boot probe task stack
#endif
#ifndef MC_BOOT_PROBE_TASK_PRIO
  #define MC_BOOT_PROBE_TASK_PRIO 1 // app_runtime.cpp: boot probe task priority
#endif
#ifndef MC_BOOT_PROBE_TASK_CORE
  #define MC_BOOT_PROBE_TASK_CORE 1 // app_runtime.cpp: boot probe task core
#endif
#ifndef MC_APP_BTN_A_BEEP_FREQ
  #define MC_APP_BTN_A_BEEP_FREQ 1500 // app_runtime.cpp: BtnA mode toggle beep frequency
#endif
#ifndef MC_APP_BTN_A_BEEP_MS
  #define MC_APP_BTN_A_BEEP_MS 50 // app_runtime.cpp: BtnA mode toggle beep duration
#endif
#ifndef MC_APP_ATTENTION_BEEP_FREQ
  #define MC_APP_ATTENTION_BEEP_FREQ 1800 // app_runtime.cpp: attention enter beep frequency
#endif
#ifndef MC_APP_ATTENTION_BEEP_MS
  #define MC_APP_ATTENTION_BEEP_MS 30 // app_runtime.cpp: attention enter beep duration
#endif
#ifndef MC_BOOT_WIFI_STABILIZE_MS
  #define MC_BOOT_WIFI_STABILIZE_MS 2000 // app_runtime.cpp: wait after WiFi connect before probes
#endif
#ifndef MC_TOUCH_POLL_INTERVAL_MS
  #define MC_TOUCH_POLL_INTERVAL_MS 25 // app_runtime.cpp: touch poll interval
#endif
#ifndef MC_PRESENTER_HEARTBEAT_MS
  #define MC_PRESENTER_HEARTBEAT_MS 10000 // app_runtime.cpp: presenter heartbeat interval
#endif
#ifndef MC_APP_UI_FRAME_INTERVAL_MS
  #define MC_APP_UI_FRAME_INTERVAL_MS 33 // app_runtime.cpp: UI frame update interval (~30Hz)
#endif

// ---------------------------------------------------------
// ===== BEHAVIOR =====
// ---------------------------------------------------------
#ifndef MC_BEHAVIOR_INFO_PERIOD_MS
  #define MC_BEHAVIOR_INFO_PERIOD_MS 15000 // stackchan_behavior.cpp: periodic info bubble interval
#endif
#ifndef MC_BEHAVIOR_IDLE_TICK_MS
  #define MC_BEHAVIOR_IDLE_TICK_MS 30000 // stackchan_behavior.cpp: idle tick interval
#endif
#ifndef MC_UI_GAZE_MOVE_PHASE_INIT_MS
  #define MC_UI_GAZE_MOVE_PHASE_INIT_MS 450 // ui_mining_core2_ticker_avatar.cpp: initial move phase duration
#endif
#ifndef MC_UI_GAZE_MOVE_HIGH_BASE_MS
  #define MC_UI_GAZE_MOVE_HIGH_BASE_MS 225 // ui_mining_core2_ticker_avatar.cpp: move phase base when mood >= 1
#endif
#ifndef MC_UI_GAZE_MOVE_HIGH_JITTER_MS
  #define MC_UI_GAZE_MOVE_HIGH_JITTER_MS 15 // ui_mining_core2_ticker_avatar.cpp: move phase jitter step when mood >= 1
#endif
#ifndef MC_UI_GAZE_MOVE_NEUTRAL_BASE_MS
  #define MC_UI_GAZE_MOVE_NEUTRAL_BASE_MS 275 // ui_mining_core2_ticker_avatar.cpp: move phase base when mood == 0
#endif
#ifndef MC_UI_GAZE_MOVE_NEUTRAL_JITTER_MS
  #define MC_UI_GAZE_MOVE_NEUTRAL_JITTER_MS 20 // ui_mining_core2_ticker_avatar.cpp: move phase jitter step when mood == 0
#endif
#ifndef MC_UI_GAZE_MOVE_LOW_BASE_MS
  #define MC_UI_GAZE_MOVE_LOW_BASE_MS 325 // ui_mining_core2_ticker_avatar.cpp: move phase base when mood < 0
#endif
#ifndef MC_UI_GAZE_MOVE_LOW_JITTER_MS
  #define MC_UI_GAZE_MOVE_LOW_JITTER_MS 25 // ui_mining_core2_ticker_avatar.cpp: move phase jitter step when mood < 0
#endif
#ifndef MC_UI_GAZE_HOLD_HIGH_BASE_MS
  #define MC_UI_GAZE_HOLD_HIGH_BASE_MS 2200 // ui_mining_core2_ticker_avatar.cpp: hold phase base when mood >= 1
#endif
#ifndef MC_UI_GAZE_HOLD_HIGH_JITTER_MS
  #define MC_UI_GAZE_HOLD_HIGH_JITTER_MS 120 // ui_mining_core2_ticker_avatar.cpp: hold phase jitter step when mood >= 1
#endif
#ifndef MC_UI_GAZE_HOLD_NEUTRAL_BASE_MS
  #define MC_UI_GAZE_HOLD_NEUTRAL_BASE_MS 2600 // ui_mining_core2_ticker_avatar.cpp: hold phase base when mood == 0
#endif
#ifndef MC_UI_GAZE_HOLD_NEUTRAL_JITTER_MS
  #define MC_UI_GAZE_HOLD_NEUTRAL_JITTER_MS 140 // ui_mining_core2_ticker_avatar.cpp: hold phase jitter step when mood == 0
#endif
#ifndef MC_UI_GAZE_HOLD_LOW_BASE_MS
  #define MC_UI_GAZE_HOLD_LOW_BASE_MS 3000 // ui_mining_core2_ticker_avatar.cpp: hold phase base when mood < 0
#endif
#ifndef MC_UI_GAZE_HOLD_LOW_JITTER_MS
  #define MC_UI_GAZE_HOLD_LOW_JITTER_MS 160 // ui_mining_core2_ticker_avatar.cpp: hold phase jitter step when mood < 0
#endif
#ifndef MC_UI_GAZE_HOLD_MICRO_SCALE
  #define MC_UI_GAZE_HOLD_MICRO_SCALE 0.35f // ui_mining_core2_ticker_avatar.cpp: micro sway scale in hold phase
#endif
#ifndef MC_UI_GAZE_SERVO_HOLD_FREEZE
  #define MC_UI_GAZE_SERVO_HOLD_FREEZE 1 // ui_mining_core2_ticker_avatar.cpp: 1=freeze servo target during hold phase
#endif
#ifndef MC_UI_GAZE_SERVO_MAX_STEP_PER_SEC
  #define MC_UI_GAZE_SERVO_MAX_STEP_PER_SEC 0.35f // ui_mining_core2_ticker_avatar.cpp: max servo gaze target delta per second in move phase
#endif
#ifndef MC_UI_GAZE_SERVO_MAX_STEP_MAX_PER_SEC
  #define MC_UI_GAZE_SERVO_MAX_STEP_MAX_PER_SEC 1.40f // ui_mining_core2_ticker_avatar.cpp: upper cap for adaptive max step
#endif
#ifndef MC_UI_GAZE_SERVO_ERR_GAIN_PER_SEC
  #define MC_UI_GAZE_SERVO_ERR_GAIN_PER_SEC 1.10f // ui_mining_core2_ticker_avatar.cpp: extra step speed per unit target error
#endif
#ifndef MC_UI_GAZE_MOVE_ACTIVE_EPS
  #define MC_UI_GAZE_MOVE_ACTIVE_EPS 0.003f // ui_mining_core2_ticker_avatar.cpp: movement threshold for active-time accounting
#endif
#ifndef MC_UI_GAZE_HOLD_PER_MOVE_RATIO
  #define MC_UI_GAZE_HOLD_PER_MOVE_RATIO 1.0f // ui_mining_core2_ticker_avatar.cpp: hold duration ratio against measured move-active time
#endif
#ifndef MC_UI_GAZE_HOLD_TIME_MIN_MS
  #define MC_UI_GAZE_HOLD_TIME_MIN_MS 1200 // ui_mining_core2_ticker_avatar.cpp: lower bound for hold duration
#endif
#ifndef MC_UI_GAZE_HOLD_TIME_MAX_MS
  #define MC_UI_GAZE_HOLD_TIME_MAX_MS 5000 // ui_mining_core2_ticker_avatar.cpp: upper bound for hold duration
#endif

// ---------------------------------------------------------
// ===== SERVO (ROBO8080-compatible baseline) =====
// ---------------------------------------------------------
#ifndef MC_ENABLE_SERVO
  #define MC_ENABLE_SERVO 1 // behavior/servo_driver.cpp: master enable
#endif
#ifndef MC_SERVO_PIN_X
  #define MC_SERVO_PIN_X 33 // behavior/servo_driver.cpp: pan servo pin (Core2/ROBO8080 PORT A)
#endif
#ifndef MC_SERVO_PIN_Y
  #define MC_SERVO_PIN_Y 32 // behavior/servo_driver.cpp: tilt servo pin (Core2/ROBO8080 PORT A)
#endif
#ifndef MC_SERVO_START_DEGREE_X
  #define MC_SERVO_START_DEGREE_X 90 // behavior/servo_driver.cpp: pan home degree
#endif
#ifndef MC_SERVO_START_DEGREE_Y
  #define MC_SERVO_START_DEGREE_Y 85 // behavior/servo_driver.cpp: tilt home degree
#endif
#ifndef MC_SERVO_MIN_DEGREE_X
  #define MC_SERVO_MIN_DEGREE_X 10 // behavior/servo_driver.cpp: pan lower clamp
#endif
#ifndef MC_SERVO_MAX_DEGREE_X
  #define MC_SERVO_MAX_DEGREE_X 170 // behavior/servo_driver.cpp: pan upper clamp
#endif
#ifndef MC_SERVO_MIN_DEGREE_Y
  #define MC_SERVO_MIN_DEGREE_Y 65 // behavior/servo_driver.cpp: tilt lower clamp
#endif
#ifndef MC_SERVO_MAX_DEGREE_Y
  #define MC_SERVO_MAX_DEGREE_Y 100 // behavior/servo_driver.cpp: tilt upper clamp
#endif
#ifndef MC_SERVO_GAIN_X
  #define MC_SERVO_GAIN_X 25.0f // behavior/servo_driver.cpp: pan gain for gazeX
#endif
#ifndef MC_SERVO_GAIN_Y
  #define MC_SERVO_GAIN_Y 18.0f // behavior/servo_driver.cpp: tilt gain for gazeY
#endif
#ifndef MC_SERVO_INVERT_X
  #define MC_SERVO_INVERT_X 0 // behavior/servo_driver.cpp: 1 = invert gazeX sign
#endif
#ifndef MC_SERVO_INVERT_Y
  #define MC_SERVO_INVERT_Y 0 // behavior/servo_driver.cpp: 1 = invert gazeY sign
#endif
#ifndef MC_SERVO_SPEED
  #define MC_SERVO_SPEED 6 // behavior/servo_driver.cpp: ServoEasing speed (max-smooth baseline)
#endif
#ifndef MC_SERVO_UPDATE_INTERVAL_MS
  #define MC_SERVO_UPDATE_INTERVAL_MS 20 // behavior/servo_driver.cpp: update period (~50Hz)
#endif
#ifndef MC_SERVO_SMOOTH_ALPHA
  #define MC_SERVO_SMOOTH_ALPHA 0.35f // behavior/servo_driver.cpp: low-pass smoothing ratio
#endif
#ifndef MC_SERVO_DYNAMIC_ALPHA_ENABLE
  #define MC_SERVO_DYNAMIC_ALPHA_ENABLE 1 // behavior/servo_driver.cpp: 1=adaptive alpha by error/velocity
#endif
#ifndef MC_SERVO_SMOOTH_ALPHA_MIN
  #define MC_SERVO_SMOOTH_ALPHA_MIN 0.20f // behavior/servo_driver.cpp: alpha at tiny motion
#endif
#ifndef MC_SERVO_SMOOTH_ALPHA_MAX
  #define MC_SERVO_SMOOTH_ALPHA_MAX 0.62f // behavior/servo_driver.cpp: alpha at large/fast motion
#endif
#ifndef MC_SERVO_ALPHA_ERR_REF_DEG
  #define MC_SERVO_ALPHA_ERR_REF_DEG 2.0f // behavior/servo_driver.cpp: error(deg) where alpha nears max
#endif
#ifndef MC_SERVO_ALPHA_VEL_REF_DPS
  #define MC_SERVO_ALPHA_VEL_REF_DPS 10.0f // behavior/servo_driver.cpp: target velocity(deg/s) where alpha nears max
#endif
#ifndef MC_SERVO_DIAG_SWEEP_ENABLE
  #define MC_SERVO_DIAG_SWEEP_ENABLE 0 // behavior/servo_driver.cpp: 1=ignore gaze and run deterministic sweep
#endif
#ifndef MC_SERVO_DIAG_SWEEP_PERIOD_MS
  #define MC_SERVO_DIAG_SWEEP_PERIOD_MS 2000 // behavior/servo_driver.cpp: half-cycle period for sweep test
#endif
#ifndef MC_SERVO_DIAG_SWEEP_AMPLITUDE_DEG
  #define MC_SERVO_DIAG_SWEEP_AMPLITUDE_DEG 12.0f // behavior/servo_driver.cpp: +/- degree from home in sweep test
#endif
#ifndef MC_SERVO_DIAG_POWER_LOG_ENABLE
  #define MC_SERVO_DIAG_POWER_LOG_ENABLE 0 // core/app_runtime.cpp: 1=periodic battery diagnostics while servo active
#endif
#ifndef MC_SERVO_DIAG_POWER_LOG_INTERVAL_MS
  #define MC_SERVO_DIAG_POWER_LOG_INTERVAL_MS 1000 // core/app_runtime.cpp: interval for power diagnostics
#endif
#ifndef MC_SERVO_TRACK_SPEED_DPS
  #define MC_SERVO_TRACK_SPEED_DPS 22 // behavior/servo_driver.cpp: tracking speed (deg/sec)
#endif
#ifndef MC_SERVO_TRACK_ACCEL_LIMIT_ENABLE
  #define MC_SERVO_TRACK_ACCEL_LIMIT_ENABLE 1 // behavior/servo_driver.cpp: 1=slew-limit tracking speed command
#endif
#ifndef MC_SERVO_TRACK_MIN_SPEED_DPS
  #define MC_SERVO_TRACK_MIN_SPEED_DPS 8.0f // behavior/servo_driver.cpp: minimum tracking speed in accel-limited mode
#endif
#ifndef MC_SERVO_TRACK_ACCEL_DPS2
  #define MC_SERVO_TRACK_ACCEL_DPS2 90.0f // behavior/servo_driver.cpp: max speed change rate (deg/s^2)
#endif
#ifndef MC_SERVO_DEADBAND_COMP_ENABLE
  #define MC_SERVO_DEADBAND_COMP_ENABLE 1 // behavior/servo_driver.cpp: 1=conservative residual deadband compensation
#endif
#ifndef MC_SERVO_DEADBAND_COMP_DEG
  #define MC_SERVO_DEADBAND_COMP_DEG 0.35f // behavior/servo_driver.cpp: residual threshold to trigger compensation
#endif
#ifndef MC_SERVO_DEADBAND_COMP_KICK_DEG
  #define MC_SERVO_DEADBAND_COMP_KICK_DEG 0.18f // behavior/servo_driver.cpp: compensation kick amount
#endif
#ifndef MC_SERVO_DEADBAND_COMP_RANGE_DEG
  #define MC_SERVO_DEADBAND_COMP_RANGE_DEG 1.2f // behavior/servo_driver.cpp: apply compensation only near target
#endif
#ifndef MC_SERVO_DEADBAND_COMP_REVERSE_DAMP
  #define MC_SERVO_DEADBAND_COMP_REVERSE_DAMP 0.45f // behavior/servo_driver.cpp: residual damping on direction reversal
#endif
#ifndef MC_SERVO_MOVE_TIME_MS
  #define MC_SERVO_MOVE_TIME_MS 2000 // behavior/servo_driver.cpp: time for one movement step
#endif
#ifndef MC_SERVO_HOME_MOVE_TIME_MS
  #define MC_SERVO_HOME_MOVE_TIME_MS 2000 // behavior/servo_driver.cpp: time for home movement
#endif
#ifndef MC_SERVO_IDLE_TIME_MS
  #define MC_SERVO_IDLE_TIME_MS 0 // behavior/servo_driver.cpp: dwell after one movement
#endif

// ---------------------------------------------------------
// ===== AUDIO RECORDER =====
// ---------------------------------------------------------
#ifndef MC_REC_TASK_STACK_SIZE
  #define MC_REC_TASK_STACK_SIZE 4096 // audio_recorder.cpp: recorder task stack
#endif
#ifndef MC_REC_TASK_PRIO
  #define MC_REC_TASK_PRIO 2 // audio_recorder.cpp: recorder task priority
#endif
#ifndef MC_REC_MIC_IDLE_WAIT_MS
  #define MC_REC_MIC_IDLE_WAIT_MS 200 // audio_recorder.cpp: idle wait before mic end
#endif
#ifndef MC_REC_MIC_IDLE_WAIT_CANCEL_MS
  #define MC_REC_MIC_IDLE_WAIT_CANCEL_MS 100 // audio_recorder.cpp: idle wait during cancel(idle)
#endif
#ifndef MC_REC_I2S_LOCK_TIMEOUT_MS
  #define MC_REC_I2S_LOCK_TIMEOUT_MS 2000 // audio_recorder.cpp: I2S lock timeout for REC
#endif
#ifndef MC_REC_TASK_DONE_TIMEOUT_MS
  #define MC_REC_TASK_DONE_TIMEOUT_MS 2000 // audio_recorder.cpp: wait timeout for recorder task stop
#endif

// ---------------------------------------------------------
// ===== AZURE TTS =====
// ---------------------------------------------------------
#ifndef MC_AZURE_TTS_TASK_STACK
  #define MC_AZURE_TTS_TASK_STACK 8192 // azure_tts.cpp: worker task stack
#endif
#ifndef MC_AZURE_TTS_TASK_PRIO
  #define MC_AZURE_TTS_TASK_PRIO 1 // azure_tts.cpp: worker task priority
#endif
#ifndef MC_AZURE_TTS_TASK_CORE
  #define MC_AZURE_TTS_TASK_CORE 1 // azure_tts.cpp: worker task core
#endif
#ifndef MC_AZURE_TTS_PLAY_LOCK_TIMEOUT_MS
  #define MC_AZURE_TTS_PLAY_LOCK_TIMEOUT_MS 4000 // azure_tts.cpp: speaker lock timeout before play
#endif
#ifndef MC_AZURE_TTS_TOKEN_TIMEOUT_MS
  #define MC_AZURE_TTS_TOKEN_TIMEOUT_MS 6000 // azure_tts.cpp: token HTTP timeout
#endif
#ifndef MC_AZURE_TTS_TOKEN_BODY_TIMEOUT_MS
  #define MC_AZURE_TTS_TOKEN_BODY_TIMEOUT_MS 1500 // azure_tts.cpp: token body read timeout
#endif
#ifndef MC_AZURE_TTS_TOKEN_CACHE_MS
  #define MC_AZURE_TTS_TOKEN_CACHE_MS (9 * 60 * 1000) // azure_tts.cpp: token cache duration
#endif
#ifndef MC_AZURE_TTS_DISABLE_KEEPALIVE_MS
  #define MC_AZURE_TTS_DISABLE_KEEPALIVE_MS 5000 // azure_tts.cpp: keep-alive cooldown after error
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
// OpenAI API key (config_private.h or runtime config can override)
#ifndef MC_OPENAI_API_KEY
  #define MC_OPENAI_API_KEY ""
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
#ifndef MC_BUILD_ID
  #define MC_BUILD_ID "unknown"
#endif
#ifndef MC_APP_VERSION
  #define MC_APP_VERSION "0.0.0"
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
  const char* appBuildId_;
  // attention
  const char* attentionText_;
  // speech lines (Web/JSON: share_accepted_text / hello_text)
  const char* shareAcceptedText_;
  const char* helloText_;
};
inline const AppConfig& appConfig() {
  static AppConfig cfg{
    // wifi
    "",
    "",
    // duco
    "",
    "",
    "Mining-Stackchan-Core2", // duco_rig_name
    "M5StackCore2",           // duco_banner
    // azure tts
    "",
    "",
    "",
    // app
    "Mining-Stackchan-Core2", // app_name
    MC_APP_VERSION,           // app_version
    MC_BUILD_ID,              // app_build_id
    // attention
    "",
    // speech lines
    "",
    ""
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
