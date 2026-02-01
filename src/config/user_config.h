// src/user_config.h
// Module implementation.
#pragma once
// =========================================================
// user_config.h
// ---- Display sleep (seconds) ----
#define MC_DISPLAY_SLEEP_SECONDS 60 // 無操作で画面OFFになるまでの秒数（NVSのdisplay_sleep_sで上書き可）
// ---- Speaker volume (0-255) ----
#define MC_SPK_VOLUME 160 // 起動時のスピーカー音量（M5.Speaker / AzureTtsのデフォルト）
// ---- TTS voice ----
#define MC_AZ_TTS_VOICE "ja-JP-AoiNeural" // Azure TTSの既定ボイス（speakAsyncで未指定時）
// ---- Attention text (UI/UX) ----
#define MC_ATTENTION_TEXT "Hi there!" // Attentionモードのデフォルト表示テキスト
// ---- Custom speech lines (UI/UX) ----
#define MC_SPEECH_SHARE_ACCEPTED "シェア獲得したよ！" // ShareAcceptedイベント時のTTSセリフ
#define MC_SPEECH_HELLO "こんにちはマイニングスタックチャンです" // BtnB押下時に喋る挨拶
// ---- AI state hints (UI/UX) ----
#define MC_AI_IDLE_HINT_TEXT "AI" // AIオーバーレイ右上のヒント（Idle）
#define MC_AI_LISTENING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // AIオーバーレイ右上のヒント（Listening）
#define MC_AI_THINKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // AIオーバーレイ右上のヒント（Thinking）
#define MC_AI_SPEAKING_HINT_TEXT MC_AI_IDLE_HINT_TEXT // AIオーバーレイ右上のヒント（Speaking）
// ---- AI text (UI/UX) ----
#define MC_AI_TEXT_THINKING  "考え中" // AIオーバーレイ左上の表示（Thinking）
#define MC_AI_TEXT_COOLDOWN "......." // AIオーバーレイ左上の表示（Cooldown）
#define MC_AI_TEXT_FALLBACK "わかりません" // STT/LLM失敗時の代替返答
// ---- OpenAI instructions (keep short) ----
#define MC_OPENAI_INSTRUCTIONS \
    "あなたはスタックチャンの会話AIです。日本語で短く答えてください。" \
    "返答は120文字以内。箇条書き禁止。1〜2文。" \
    "相手が『聞こえる？』等の確認なら、明るく短く返してください。" // OpenAI instructions
