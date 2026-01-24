#pragma once
#include <Arduino.h>
#include "ai_interface.h"
#include "audio_recorder.h"  // ★追加
#include "azure_stt.h"       // ★追加
#include "config.h"          // ★追加（MC_AI_* 定数）

// forward decl
class Orchestrator;

class AiTalkController {
public:
  enum class AiState : uint8_t {
    Idle = 0,
    Listening,
    Thinking,
    Speaking,
    PostSpeakBlank,
    Cooldown,
  };

  // orch を渡す（本体統合用）。sandbox は nullptr のままでOK
  void begin(Orchestrator* orch = nullptr);

  // sandbox friendly（タップ座標なし＝常に「AI領域タップ」として扱う）
  bool onTap();

  // 本体統合用：上1/3のみAIが消費する（それ以外は false を返す）
  bool onTap(int x, int y, int screenH);

  // sandbox用：入力注入（本体統合フェーズ1では使わない）
  void injectText(const String& text);

  // tick（本体統合：毎ループ呼ぶ）
  void tick() { tick(millis()); }
  void tick(uint32_t nowMs);

  // TTS完了通知（本体統合：Orchestrator ok のタイミングで呼ぶ）
  void onTtsDone(uint32_t ttsId) { onTtsDone(ttsId, millis()); }
  void onTtsDone(uint32_t ttsId, uint32_t nowMs);

  bool isBusy() const { return state_ != AiState::Idle; }
  AiState state() const { return state_; }

  AiUiOverlay getOverlay() const { return overlay_; }

  // 吹き出し更新要求（必ず main 側で UIMining::setStackchanSpeech() へ）
  bool consumeBubbleUpdate(String* outText);

  // TTS中断通知（main が 1回だけ consume して cancel+clear する）
  bool consumeAbortTts(uint32_t* outId, const char** outReason);

private:
  // ---- transitions ----
  void enterIdle_(uint32_t nowMs, const char* reason);
  void enterListening_(uint32_t nowMs);
  void enterThinking_(uint32_t nowMs);
  void enterSpeaking_(uint32_t nowMs);
  void enterPostSpeakBlank_(uint32_t nowMs);
  void enterCooldown_(uint32_t nowMs, bool error, const char* reason);

  void updateOverlay_(uint32_t nowMs);

  static String clampBytes_(const String& s, size_t maxBytes);

private:
  Orchestrator* orch_ = nullptr;

  AiState  state_ = AiState::Idle;

  uint32_t listenStartMs_   = 0;
  uint32_t thinkStartMs_    = 0;
  uint32_t speakStartMs_ = 0;
  uint32_t speakHardTimeoutMs_ = 0; // TTS done待ちの上限（動的に計算）
  uint32_t blankStartMs_ = 0;
  uint32_t cooldownStartMs_ = 0;
  uint32_t cooldownDurMs_   = 0;

  uint32_t expectTtsId_ = 0;

  String inputText_;
  String replyText_;

  // bubble updates (main側がsetStackchanSpeechする)
  bool   bubbleDirty_ = false;
  String bubbleText_;

  AiUiOverlay overlay_;

  // Orchestrator向け rid
  uint32_t nextRid_ = 1;

  // ★録音（LISTENINGの“本物”）
  AudioRecorder recorder_;
  bool lastRecOk_ = false;

  // ---- STT result ----
  String lastUserText_;     // STT結果（最大200文字に丸め）
  bool   lastSttOk_ = false;
  int    lastSttStatus_ = 0;

  // ---- LLM result ----
  bool     replyReady_ = false;
  bool     lastLlmOk_ = false;
  int      lastLlmHttp_ = 0;
  uint32_t lastLlmTookMs_ = 0;
  String   lastLlmErr_;      // 短いエラー（全文禁止）
  String   lastLlmTextHead_; // 先頭だけ（全文禁止）

  // overall deadline base (THINKING入り時刻)
  uint32_t overallStartMs_ = 0;

  // cooldown延長用
  bool   errorFlag_ = false;

  // TTS abort (consume方式)
  uint32_t abortTtsId_ = 0;
  char abortTtsReason_[24] = {0};

};
