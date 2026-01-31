// ===== src/orchestrator.h（全文差し替え）=====
#pragma once

#include <Arduino.h>
#include "core/logging.h"
#include <vector>

enum class AppState : uint8_t { Idle, React, ThinkWait, Speak, ErrorSafe };
enum class OrchPrio : uint8_t { Low = 0, Normal = 1, High = 2 };

class Orchestrator {
public:
  enum class OrchKind : uint8_t {
    None = 0,
    BehaviorSpeak = 1, // stackchan_behavior.cpp 由来の speak
    AiSpeak = 2,       // AI側（AiTalkController など）由来の speak
  };

  // Phase5-B2: cancel 発火元
  enum class CancelSource : uint8_t { AI = 0, Main = 1, Other = 2 };

  struct SpeakStartCmd {
    bool valid = false;
    uint32_t ttsId = 0;
    uint32_t rid = 0;
    OrchKind kind = OrchKind::None;
    
    String text;
    OrchPrio prio = OrchPrio::Normal;
  };

  void init();

  // Reactionを入口にまとめ、ttsId採番をここで行う
  SpeakStartCmd makeSpeakStartCmd(uint32_t rid, const String& text, OrchPrio prio,
                                  OrchKind kind = OrchKind::BehaviorSpeak);
  void enqueueSpeakPending(const SpeakStartCmd& cmd);
  bool hasPendingSpeak() const;
  SpeakStartCmd popNextPending();

  // ---- Step4: expect は Orchestrator 単一ソース（kind も保持）----
  void setExpectedSpeak(uint32_t speakId, uint32_t rid); // legacy（kind=BehaviorSpeak）
  void setExpectedSpeak(uint32_t speakId, uint32_t rid, OrchKind kind);
  void clearExpectedSpeak(const char* reason);

  // Phase5-B2: cancel speak (idempotent + reason/source)
  void cancelSpeak(uint32_t speakId, const char* reason); // legacy (source=Other)
  void cancelSpeak(uint32_t speakId, const char* reason, CancelSource source);


  void onAudioStart(uint32_t speakId);
  // desyncOut: 連続ミスマッチ閾値を超えたら true
  bool onTtsDone(uint32_t gotId, bool* desyncOut = nullptr); // legacy

  // Step4: TTS DONE は rid/kind を返せる形にする
  bool onTtsDone(uint32_t gotId,
                 uint32_t* doneRid,
                 OrchKind* doneKind,
                 bool* desyncOut = nullptr);

  // Step4: AI は rid で自分の発話を識別し、必要時だけ tts_id を照会する
  uint32_t ttsIdForRid(uint32_t rid) const;

  // Step4: rid でキャンセルできるようにする（tts_id を AI が保持しない）
  bool cancelSpeakByRid(uint32_t rid,
                        const char* reason,
                        CancelSource source,
                        uint32_t* outCanceledSpeakId = nullptr);

  AppState state() const { return state_; }
  bool tick(uint32_t nowMs);

private:
  AppState state_ = AppState::Idle;
  uint32_t expectSpeakId_ = 0;
  uint32_t expectRid_ = 0;
  OrchKind expectKind_ = OrchKind::None;
  uint8_t  mismatchCount_ = 0;
  static constexpr uint8_t kDesyncThreshold = 3;
  uint32_t nextTtsId_ = 1;
  static constexpr size_t kMaxSpeakText = 128;

  // ThinkWait(= TTS開始待ち/音声開始待ち)の監視タイムアウト
  // Azure TTS はネットワーク/トークン/生成で 5秒を超えることがあるため長めにする。
  static constexpr uint32_t kThinkWaitTimeoutMs = 30000;

  // ---- Phase5-B2: cancel 冪等化（同一tts_idは1回だけ掃除＆本ログ）----
  struct CancelRecord {
    uint32_t id = 0;
    CancelSource source = CancelSource::Other;
    char reason[24] = {0};
  };
  static constexpr size_t kMaxCanceled = 8;
  std::vector<CancelRecord> canceled_;

  static const char* sourceToStr_(CancelSource s);
  const CancelRecord* findCanceled_(uint32_t id) const;
  void rememberCanceled_(uint32_t id, const char* reason, CancelSource source);

  std::vector<SpeakStartCmd> pending_;
  static constexpr size_t kMaxPending = 4;

  // ThinkWait監視用
  AppState prevState_ = AppState::Idle;
  uint32_t thinkWaitSinceMs_ = 0;
  bool timeoutLogged_ = false;
};

