// Module implementation.
#pragma once
// =========================================================
// user_config.h
// - Override only what you want to change from config.h defaults.
// - Leave unused items commented to avoid redundant definitions.

// ---- Servo diagnostics (temporary; keep OFF for normal behavior tests) ----
#define MC_SERVO_DIAG_SWEEP_ENABLE 0
#define MC_SERVO_DIAG_POWER_LOG_ENABLE 0
// ---- Display sleep (seconds) ----
// #define MC_DISPLAY_SLEEP_SECONDS 60 // 無操作で画面OFFになるまでの秒数（NVSのdisplay_sleep_sで上書き可）
// ---- Speaker volume (0-255) ----
// #define MC_SPK_VOLUME 160 // 起動時のスピーカー音量（M5.Speaker / AzureTtsのデフォルト）
// ---- TTS voice ----
// #define MC_AZ_TTS_VOICE "ja-JP-AoiNeural" // Azure TTSの既定ボイス（speakAsyncで未指定時）
// ---- Attention text (UI/UX) ----
// #define MC_ATTENTION_TEXT "Hi there!" // Attentionモードのデフォルト表示テキスト
// ---- Custom speech lines (UI/UX) ----
// #define MC_SPEECH_SHARE_ACCEPTED "シェア獲得したよ！" // ShareAcceptedイベント時のTTSセリフ
// #define MC_SPEECH_HELLO "こんにちはマイニングスタックチャンです" // BtnB押下時に喋る挨拶
// ---- AI state hints (UI/UX) ----
// #define MC_AI_IDLE_HINT_TEXT "AI" // AIオーバーレイ右上のヒント（Idle）
// #define MC_AI_LISTENING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // AIオーバーレイ右上のヒント（Listening）
// #define MC_AI_THINKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // AIオーバーレイ右上のヒント（Thinking）
// #define MC_AI_SPEAKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // AIオーバーレイ右上のヒント（Speaking）
// ---- AI text (UI/UX) ----
// #define MC_AI_TEXT_THINKING  "考え中" // AIオーバーレイ左上の表示（Thinking）
// #define MC_AI_TEXT_COOLDOWN "......." // AIオーバーレイ左上の表示（Cooldown）
// #define MC_AI_TEXT_FALLBACK "わかりません" // STT/LLM失敗時の代替返答
// ---- OpenAI instructions (keep short) ----
// #define MC_OPENAI_INSTRUCTIONS \
//     "あなたはスタックチャンの会話AIです。日本語で短く答えてください。" \
//     "返答は120文字以内。箇条書き禁止。1〜2文。" \
//     "相手が『聞こえる？』等の確認なら、明るく短く返してください。" // OpenAI instructions

// ---- Servo smoothing (jitter reduction) ----
// Most important: disable move command thinning (default 100 = once per 2s -> 1 = every cycle)
#define MC_SERVO_MOVE_TRIGGER_DIVIDER 1
// Disable periodic recentering (stop auto-centering every 10 moves)
#define MC_SERVO_RECENTER_INTERVAL_MOVES 0
// Lower tracking speed (SG90 is smoother at lower speed)
#define MC_SERVO_TRACK_SPEED_DPS 8.0f
// Lower LPF alpha values (make target changes more gradual)
#define MC_SERVO_SMOOTH_ALPHA_MIN 0.10f
#define MC_SERVO_SMOOTH_ALPHA_MAX 0.35f
// UI side: shorten hold phase for better continuity
#define MC_UI_GAZE_SERVO_HOLD_FREEZE 0
#define MC_UI_GAZE_HOLD_NEUTRAL_BASE_MS 2600
#define MC_UI_GAZE_HOLD_HIGH_BASE_MS 2200
#define MC_UI_GAZE_HOLD_LOW_BASE_MS 3000
// Deadband compensation (set to 0 if SG90 feels kicky)
// #define MC_SERVO_DEADBAND_COMP_ENABLE 0

// ---- Setup-managed user defaults (non-secret) ----
// These are compile-time defaults. Runtime SET/SAVE values in /mc_config.json
// override them after first save from ai-mining-stackchan-setup.
//
// #define MC_DISPLAY_SLEEP_SECONDS 90
// #define MC_SPK_VOLUME 120
// #define MC_ATTENTION_TEXT "Hi"
// #define MC_SPEECH_SHARE_ACCEPTED "Share accepted!"
// #define MC_SPEECH_HELLO "Hello"
// #define MC_AZ_TTS_VOICE "ja-JP-AoiNeural"
// #define MC_OPENAI_MODEL "gpt-5-nano"
// #define MC_OPENAI_INSTRUCTIONS "Please answer briefly in Japanese."
// #define MC_CPU_FREQ_MHZ 240
