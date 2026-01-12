// src/user_config.h
#pragma once

// =========================================================
// user_config.h
// - 見た目 / UX / セリフ / 触ってOKな設定だけ
// - 秘密情報（APIキー、Wi-Fi SSID/PASS）は入れない
// =========================================================

// ---- Display sleep (seconds) ----
// 画面だけOFFにして、マイニングは継続
#define MC_DISPLAY_SLEEP_SECONDS 60

// ---- CPU frequency (MHz) ----
// setCpuFrequencyMhz() の代表値: 80 / 160 / 240
#define MC_CPU_FREQ_MHZ 240

// ---- Speaker volume (0-255) ----
#define MC_SPK_VOLUME 160

// ---- Azure TTS voice (secretではないのでここ) ----
#define MC_AZ_TTS_VOICE "ja-JP-AoiNeural"

// ---- Attention text (UI/UX) ----
#define MC_ATTENTION_TEXT "Hi there!"

// ---- Custom speech lines (UI/UX) ----
#define MC_SPEECH_SHARE_ACCEPTED "掘れたよ！"
#define MC_SPEECH_HELLO "こんにちはマイニングスタックチャンです"

// ---- AI talk UI hints（見た目寄りなのでここで上書きしたい場合）----
// 右上に表示するヒント文字
#define MC_AI_IDLE_HINT_TEXT "AI"

// 実装テスト用：STT結果（ユーザーの言った内容）を表示する
// 0 にすると表示しない（後でOFF運用にする想定）
#define MC_AI_STT_DEBUG_SHOW_TEXT 1
