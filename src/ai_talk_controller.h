#pragma once
#include <Arduino.h>
#include "ai_interface.h"

class AiTalkController {
public:
  void begin();
  void tick();
  void onTap();                 // 画面上1/3タップ
  bool isBusy() const;          // Idle以外ならtrue
  AiUiOverlay getOverlay() const;
  void injectText(const String& text); // Serial :say 用（LISTENING/THINKINGを飛ばす）

private:
  // ---- 固定仕様（ms）----
  static constexpr uint32_t kListeningMaxMs      = 10 * 1000; // LISTENING最大10秒
  static constexpr uint32_t kListeningCancelable = 3 * 1000;  // 開始3秒以内のみキャンセル
  static constexpr uint32_t kThinkingMockMs      = 1 * 1000;  // モック返答生成タイミング
  static constexpr uint32_t kCooldownMs          = 2 * 1000;  // COOLDOWN 2秒
  static constexpr uint32_t kCooldownErrExtraMs  = 1 * 1000;  // エラー時 +1秒
  static constexpr uint32_t kSpeakingShowMs      = 1200;      // 「喋ってる扱い」表示
  static constexpr uint32_t kSpeakingBlankMs     = 500;       // 0.5秒 空吹き出し
  // “枠”だけ用意（サンドボックスでは実処理しない）
  static constexpr uint32_t kSttTimeoutMs        = 8 * 1000;
  static constexpr uint32_t kAiTimeoutMs         = 10 * 1000;
  static constexpr uint32_t kTtsTimeoutMs        = 10 * 1000;
  static constexpr uint32_t kOverallTimeoutMs    = 20 * 1000;

  static constexpr size_t   kInputLimitChars     = 200;
  static constexpr size_t   kTtsLimitChars       = 120;

  // ---- 状態 ----
  AiState   state_ = AiState::Idle;
  uint32_t  stateStartMs_ = 0;
  uint32_t  convoStartMs_ = 0;   // 全体20s監視用（Idleでは無効）
  bool      error_ = false;

  // Speaking内のサブフェーズ（応答表示→空吹き出し）
  bool      speakingBlankPhase_ = false;
  uint32_t  speakingPhaseStartMs_ = 0;

  // テキスト
  String    inputText_;   // LISTENING→THINKING用（モック）
  String    replyText_;   // THINKING生成 or injectText

  // overlay（毎tickで更新）
  AiUiOverlay overlay_;

  // ---- 内部 ----
  void transitionTo_(AiState next);
  void setErrorAndCooldown_();
  uint32_t nowMs_() const { return millis(); }
  uint32_t elapsedStateMs_(uint32_t now) const { return now - stateStartMs_; }
  uint32_t elapsedConvoMs_(uint32_t now) const { return (convoStartMs_ == 0) ? 0 : (now - convoStartMs_); }

  String stateName_(AiState s) const;
  uint32_t remainingMs_(uint32_t now) const;  // 状態に応じた残り（表示用）
  int remainingSecCeil_(uint32_t now) const;
  String clampLen_(const String& s, size_t maxChars) const;
};
