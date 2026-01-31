#pragma once
#include <Arduino.h>
#include "ai/ai_interface.h"
#include "audio/audio_recorder.h"
#include "ai/azure_stt.h"
#include "config/config.h"
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
  void begin(Orchestrator* orch = nullptr);
  bool onTap();
  bool onTap(int x, int y, int screenH);
  void injectText(const String& text);
  void tick() { tick(millis()); }
  void tick(uint32_t nowMs);
  void onSpeakDone(uint32_t rid) { onSpeakDone(rid, millis()); }
  void onSpeakDone(uint32_t rid, uint32_t nowMs);
  bool isBusy() const { return state_ != AiState::Idle; }
  AiState state() const { return state_; }
  AiUiOverlay getOverlay() const { return overlay_; }
  bool consumeBubbleUpdate(String* outText);
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
private:
  Orchestrator* orch_ = nullptr;
  AiState  state_ = AiState::Idle;
  uint32_t listenStartMs_   = 0;
  uint32_t thinkStartMs_    = 0;
  uint32_t speakStartMs_ = 0;
  uint32_t speakHardTimeoutMs_ = 0;
  uint32_t blankStartMs_ = 0;
  uint32_t cooldownStartMs_ = 0;
  uint32_t cooldownDurMs_   = 0;
  uint32_t activeRid_ = 0;
  bool     awaitingOrchSpeak_ = false;
  String inputText_;
  String replyText_;
  bool   bubbleDirty_ = false;
  String bubbleText_;
  AiUiOverlay overlay_;
  uint32_t nextRid_ = 1;
  AudioRecorder recorder_;
  bool lastRecOk_ = false;
  // ---- STT result ----
  String lastUserText_;
  bool   lastSttOk_ = false;
  int    lastSttStatus_ = 0;
  // ---- LLM result ----
  bool     replyReady_ = false;
  bool     lastLlmOk_ = false;
  int      lastLlmHttp_ = 0;
  uint32_t lastLlmTookMs_ = 0;
  String   lastLlmErr_;
  String   lastLlmTextHead_;
  uint32_t overallStartMs_ = 0;
  bool   errorFlag_ = false;
  uint32_t abortTtsId_ = 0;
  char abortTtsReason_[24] = {0};
};
