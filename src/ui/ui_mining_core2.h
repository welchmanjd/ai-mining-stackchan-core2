// src/ui_mining_core2.h
// Module implementation.
#pragma once
// ===== Mining stackchan UI for Core2 (Spec-aligned + anti-flicker) =====
// Layout:
//   - Left  : Avatar 144x216
//   - Right : Info   176x216
//   - Bottom: Ticker 320x24
//
//   Font0 x2 (approx 12x16 cell), 14 cols x 4 rows + header
//   Label 4 chars + space + value up to 9 chars
//   Header title centered + 3-dot indicator
//   Pages: 0 MINING, 1 DEVICE, 2 NETWORK
//   Tap inside right panel => next page
//
// Anti-flicker:
//   - Draw right panel into sprite then push once
//   - Draw ticker into sprite then push once
#include <Arduino.h>
#include <Avatar.h>
#include <M5GFX.h>

#include "utils/mining_panel_data.h"
#include "ui/ui_types.h"
class UIMining {
public:
  using PanelData = MiningPanelData;
  enum : int {
    W = 320, H = 240,
    LOG_H = 24,
    AV_W = 144, AV_H = 216,
    INF_W = 176, INF_H = 216,
    X_INF = 144, Y_LOG = 216
  };
  static UIMining& instance();
  void begin(const char* appName, const char* appVer);
  String shortFwString() const;
  uint32_t uptimeSeconds() const;
  void setHashrateReference(float kh);
  void setAutoPageMs(uint32_t ms);
  void drawAll(const PanelData& p, const String& tickerText, bool suppressTouchBeep = false);
  // Touch snapshot: main loop should read touch (I2C) once and pass it to UI.
  // This avoids occasional I2C hangs/freezes caused by multiple touch reads per frame.
  struct TouchSnapshot {
    bool enabled_ = false;
    bool pressed_ = false;
    bool down_    = false;  // rising edge (pressed && !prev)
    int  x_       = 0;
    int  y_       = 0;
  };
  void setTouchSnapshot(const TouchSnapshot& s);
  void drawSplash(const String& wifiText,  uint16_t wifiCol,
                  const String& poolText,  uint16_t poolCol,
                  const String& wifiHint,  const String& poolHint);
  void drawSleepMessage();
  void drawStackchanScreen(const PanelData& p);
  void onEnterStackchanMode();
  void onLeaveStackchanMode();
  // durationMs==0 -> clear.
  void triggerAttention(uint32_t durationMs, const char* text = nullptr);
  bool isAttentionActive() const;
  void setAttentionDefaultText(const char* text);
  void updateAvatarLiveliness();
  void setStackchanSpeechTiming(uint32_t talkMinMs, uint32_t talkVarMs,
                                uint32_t silentMinMs, uint32_t silentVarMs);
  uint32_t stackchanSpeechSeq() const { return stackchanSpeechSeq_; }
  const String& stackchanSpeechText() const { return stackchanSpeechText_; }
  void setStackchanSpeech(const String& text);
  void setStackchanExpression(m5avatar::Expression exp);
  void setAiOverlay(const AiUiOverlay& ov);
private:
private:
  AiUiOverlay aiOverlay_{};
  UIMining();
  // ---------- Spec constants (relative to right panel origin) ----------
  static constexpr int kPadLr = 4;
  static constexpr int kPadT  = 4;
  static constexpr int kCharW = 12; // Font0 * 2
  static constexpr int kCharH = 16;
  static constexpr int kXLabel = kPadLr;             // abs148 -> rel4
  static constexpr int kXValue = kPadLr + kCharW*5;  // abs208 -> rel64
  static constexpr int kYHeader = 4;
  static constexpr int kY1 = 24;
  static constexpr int kY2 = 42;
  static constexpr int kY3 = 60;
  static constexpr int kY4 = 78;
  static constexpr int kIndR = 2;
  static constexpr int kIndY = 12;
  // abs 302/308/314 -> rel 158/164/170
  static constexpr int kIndX1 = 158;
  static constexpr int kIndX2 = 164;
  static constexpr int kIndX3 = 170;
  static constexpr uint16_t kColLabel = 0xC618; // light grey
  static constexpr uint16_t kColDark  = 0x4208;
  struct TextLayoutY {
    int header, y1, y2, y3, y4;
    int indY;
  };
  TextLayoutY computeTextLayoutY() const;
  TouchSnapshot touch_;
  // ---------- State ----------
  String appName_;
  String appVer_;
  m5avatar::Avatar avatar_;
  M5Canvas info_;
  M5Canvas tick_;
  int      infoPage_      = 0;
  uint32_t lastPageMs_    = 0;
  uint32_t autoPageMs_    = 0;
  uint32_t lastTotalShares_ = 0;
  uint32_t lastShareMs_     = 0;
  // ticker
  String   tickerLast_;
  String   tickerLog_;
  int32_t  tickerOffset_ = 0;
  uint32_t lastTickMs_   = 0;
  float hrRefKh_ = 0.0f;
  bool     splashActive_   = false;
  uint32_t splashStartMs_  = 0;
  uint32_t splashReadyMs_  = 0;
  String   splashWifiText_;
  String   splashPoolText_;
  String   splashWifiHint_;
  String   splashPoolHint_;
  uint16_t splashWifiCol_  = WHITE;
  uint16_t splashPoolCol_  = WHITE;
  bool   inStackchanMode_    = false;
  bool   stackchanNeedsClear_ = false;
  bool     stackchanTalking_        = false;
  uint32_t stackchanPhaseStartMs_   = 0;
  uint32_t stackchanPhaseDurMs_     = 0;
  String   stackchanBubbleText_;
  // ---- Stackchan avatar updates (deferred) ----
  // M5Stack-Avatar sometimes freezes when setExpression/setSpeechText is called
  // from outside the draw loop. So we only *request* updates here and apply them
  // inside drawStackchanScreen().
  bool                  stackchanExprPending_   = false;
  m5avatar::Expression  stackchanExprDesired_   = m5avatar::Expression::Neutral;
  bool                  stackchanSpeechPending_ = false;
  String                stackchanSpeechDesired_;
  uint32_t stackchanSpeechSeq_ = 0;
  String   stackchanSpeechText_;
  // "attention" state ("WHAT?" mode)
  bool     attentionActive_       = false;
  uint32_t attentionUntilMs_      = 0;
  String   attentionDefaultText_  = "WHAT?";
  String   attentionText_         = "WHAT?";
  uint32_t stackchanTalkMinMs_    = 2500;
  uint32_t stackchanTalkVarMs_    = 1500;
  uint32_t stackchanSilentMinMs_  = 10000;
  uint32_t stackchanSilentVarMs_  = 0;
  int8_t   moodLevel_        = 0;
  uint32_t moodLastCalcMs_   = 0;
  uint32_t moodLastReportMs_ = 0;
  // ---------- Static frame ----------
  void drawStaticFrame();
  // ---------- Page input ----------
  void handlePageInput(bool suppressTouchBeep);
  // ---------- Last share age ----------
  void updateLastShareClock(const PanelData& p);
  uint32_t lastShareAgeSec() const;
  // ---------- Font prep ----------
  void prepInfoFont();
  void prepBodyFont();
  void prepHeaderFont();
  // ---------- Header + dots ----------
  void drawDots(const TextLayoutY& ly);
  void drawHeader(const char* title, const TextLayoutY& ly);
  // ---------- Line primitive ----------
  void drawLine(int y, const char* label4, const String& value,
                uint16_t colLabel, uint16_t colValue);
  // ---------- Value formatters ----------
  String vHash(float kh) const;
  String vShare(uint32_t acc, uint32_t rej, uint8_t& successOut);
  String vDiff(float diff) const;
  String vLast(uint32_t age) const;
  String vUp(uint32_t s) const;
  String vTemp(float c) const;
  String vHeap() const;
  String vNet(const PanelData& p) const;
  String vRssi() const;
  String vPool(const String& name) const;
  // ---------- Colors ----------
  uint16_t cHash(const PanelData& p) const;
  uint16_t cShare(uint8_t s) const;
  uint16_t cSharePct(float s) const;
  uint16_t cLast(uint32_t age) const;
  uint16_t cTemp(float c) const;
  uint16_t cHeap(uint32_t kb) const;
  uint16_t cNet(const String& v) const;
  uint16_t cRssi(int rssi) const;
  uint16_t cBatt(int pct) const;
  // ---------- Temperature / Power ----------
  float   readTempC();
  int     batteryPct();
  bool    isExternalPower();
  String  vBatt();
  // ---------- Pages ----------
  void drawPage0(const PanelData& p);
  void drawPage1(const PanelData& p);
  void drawPage2(const PanelData& p);
  void drawPoolNameSmall(const TextLayoutY& ly, const String& name);
  // ---------- Right panel draw ----------
  void drawInfo(const PanelData& p);
  // ---------- Ticker ----------
  void drawTicker(const String& text);
  // ---------- Avatar ----------
  void updateAvatarMood(const PanelData& p);
  String buildStackchanBubble(const PanelData& p);
};
