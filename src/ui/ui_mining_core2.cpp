// src/ui_mining_core2.cpp
// Module implementation.
#include "ui/ui_mining_core2.h"
#include <WiFi.h>
#include "core/logging.h"
// ===== Singleton / ctor =====
UIMining& UIMining::instance() {
  static UIMining s_instance;
  return s_instance;
}
UIMining::UIMining()
  : avatar_()
  , info_(&M5.Display)
  , tick_(&M5.Display)
{
  inStackchanMode_    = false;
  stackchanNeedsClear_ = false;
}
// ===== Public API =====
void UIMining::begin(const char* appName, const char* appVer) {
  appName_ = appName ? appName : "";
  appVer_  = appVer  ? appVer  : "";
  auto& d = M5.Display;
  // Base display setup: rotation, brightness, and sprite buffers.
  d.setRotation(1);
  d.setBrightness(128);
  avatar_.setScale(0.45f);
  avatar_.setPosition(-12, -88);
  // Use a Japanese-capable font (size ~8) so bubble text renders correctly.
  avatar_.setSpeechFont(&fonts::lgfxJapanMinchoP_8);
  avatar_.setSpeechText("");
  // Off-screen sprites reduce flicker during partial redraws.
  info_.setColorDepth(8);
  info_.createSprite(INF_W, INF_H);
  info_.setTextWrap(false);
  tick_.setColorDepth(8);
  tick_.createSprite(W, LOG_H);
  tick_.setTextWrap(false);
  lastPageMs_      = millis();
  lastShareMs_     = 0;
  lastTotalShares_ = 0;
  tickerOffset_ = W;
  splashActive_   = true;
  splashStartMs_  = millis();
  splashReadyMs_  = 0;
  splashWifiText_ = "Connecting...";
  splashPoolText_ = "Waiting";
  splashWifiCol_  = 0xFD20;
  splashPoolCol_  = COL_LABEL;
  splashWifiHint_ = "";
  splashPoolHint_ = "";
  drawSplash(splashWifiText_,  splashWifiCol_,
             splashPoolText_,  splashPoolCol_,
             splashWifiHint_,  splashPoolHint_);
  tick_.fillScreen(BLACK);
  tick_.pushSprite(0, Y_LOG);
}
void UIMining::setTouchSnapshot(const TouchSnapshot& s) {
  touch_ = s;
}
String UIMining::shortFwString() const {
  return String("r25-12-06");
}
uint32_t UIMining::uptimeSeconds() const {
  return static_cast<uint32_t>(millis() / 1000);
}
void UIMining::setHashrateReference(float kh) {
  hrRefKh_ = kh;
}
void UIMining::setAutoPageMs(uint32_t ms) {
  autoPageMs_ = ms;
}
void UIMining::onEnterStackchanMode() {
  inStackchanMode_     = true;
  stackchanNeedsClear_ = true;
  stackchanTalking_        = false;
  stackchanPhaseStartMs_   = millis();
  stackchanPhaseDurMs_     = 0;
  stackchanBubbleText_     = "";
  avatar_.setScale(1.0f);
  avatar_.setPosition(0, 0);
  avatar_.setSpeechText("");
}
void UIMining::onLeaveStackchanMode() {
  inStackchanMode_     = false;
  stackchanNeedsClear_ = false;
  stackchanTalking_        = false;
  stackchanPhaseStartMs_   = 0;
  stackchanPhaseDurMs_     = 0;
  stackchanBubbleText_     = "";
  avatar_.setSpeechText("");
  avatar_.setScale(0.45f);
  avatar_.setPosition(-12, -88);
}
void UIMining::triggerAttention(uint32_t durationMs, const char* text) {
  if (durationMs == 0) {
    LOG_EVT_INFO("EVT_ATTENTION_EXIT", "attn=0");
    attentionActive_   = false;
    attentionUntilMs_  = 0;
    attentionText_     = attentionDefaultText_;
    if (inStackchanMode_) {
      setStackchanSpeech("");
    }
    return;
  }
  // "Attention" is a short-lived focus state that overrides bubble text.
  attentionActive_   = true;
  attentionUntilMs_  = millis() + durationMs;
  attentionText_     = (text && *text) ? String(text) : attentionDefaultText_;
  LOG_EVT_INFO("EVT_ATTENTION_ENTER", "attn=1 text=%s", attentionText_.c_str());
  if (inStackchanMode_) {
    setStackchanSpeech(attentionText_);
    stackchanSpeechText_ = attentionText_;
    stackchanSpeechSeq_++;
  }
}
void UIMining::setAttentionDefaultText(const char* text) {
  attentionDefaultText_ = (text && *text) ? String(text) : String("WHAT?");
  if (!attentionActive_) {
    attentionText_ = attentionDefaultText_;
  }
}
bool UIMining::isAttentionActive() const {
  if (!attentionActive_) return false;
  // handle millis wrap-around safely
  return (int32_t)(attentionUntilMs_ - millis()) > 0;
}
void UIMining::drawAll(const PanelData& p, const String& tickerText, bool suppressTouchBeep) {
  uint32_t now = millis();
  if (splashActive_) {
    // Splash shows connection progress until Wi-Fi + pool are ready.
    wl_status_t w = WiFi.status();
    uint32_t    dt_splash = now - splashStartMs_;
    auto makeConnecting = [&](const char* base) -> String {
      uint32_t elapsed = now - splashStartMs_;
      const uint32_t period = 200;
      uint32_t phase = (elapsed / period) % 6;
      uint8_t dots;
      if (phase <= 3) dots = 1 + phase;  // 1,2,3,4
      else            dots = 6 - phase;  // 3,2
      String s(base);
      for (uint8_t i = 0; i < dots; ++i) {
        s += '.';
      }
      return s;
    };
    String   wifiText;
    uint16_t wifiCol;
    if (w == WL_CONNECTED) {
      wifiText = "OK";
      wifiCol  = 0x07E0;
    } else if (dt_splash < 10000) {
      wifiText = makeConnecting("Connecting");
      wifiCol  = 0xFD20;
    } else if (dt_splash < 15000) {
      wifiText = makeConnecting("Retrying");
      wifiCol  = 0xFD20;
    } else {
      wifiText = "NG";
      wifiCol  = 0xF800;
    }
    String   poolText;
    uint16_t poolCol;
    bool     wifi_ok = (w == WL_CONNECTED);
    if (!wifi_ok) {
      poolText = "Waiting";
      poolCol  = COL_LABEL;
    } else if (!p.miningEnabled_) {
      poolText = "OFF";
      poolCol  = COL_LABEL;
    } else if (p.poolAlive_) {
      poolText = "OK";
      poolCol  = 0x07E0;
    } else if (dt_splash < 10000) {
      poolText = makeConnecting("Connecting");
      poolCol  = 0xFD20;
    } else if (dt_splash < 15000) {
      poolText = makeConnecting("Retrying");
      poolCol  = 0xFD20;
    } else {
      poolText = "NG";
      poolCol  = 0xF800;
    }
    String wifiHint;
    if (wifiText == "NG" && p.wifiDiag_.length()) {
      wifiHint = p.wifiDiag_;
    } else {
      wifiHint = "";
    }
    String poolHint;
    if (poolText == "OFF") {
      poolHint = "Duco user is empty. Mining is disabled.";
    } else if ((poolText == "NG" || poolText == "Waiting") && p.poolDiag_.length()) {
      poolHint = p.poolDiag_;
    } else {
      poolHint = "";
    }
    if (wifiText  != splashWifiText_  || wifiCol  != splashWifiCol_  ||
        poolText  != splashPoolText_  || poolCol  != splashPoolCol_  ||
        wifiHint  != splashWifiHint_  || poolHint != splashPoolHint_) {
      splashWifiText_  = wifiText;
      splashPoolText_  = poolText;
      splashWifiCol_   = wifiCol;
      splashPoolCol_   = poolCol;
      splashWifiHint_  = wifiHint;
      splashPoolHint_  = poolHint;
      drawSplash(splashWifiText_,  splashWifiCol_,
                 splashPoolText_,  splashPoolCol_,
                 splashWifiHint_,  splashPoolHint_);
    }
    bool ok_now = (w == WL_CONNECTED) && (p.miningEnabled_ ? p.poolAlive_ : true);
    if (ok_now) {
      if (splashReadyMs_ == 0) {
        splashReadyMs_ = now;
      }
    } else {
      splashReadyMs_ = 0;
    }
    bool ready =
      ok_now &&
      (now - splashStartMs_ > 3000) &&
      (splashReadyMs_ != 0) &&
      (now - splashReadyMs_ > 1000);
    if (!ready) {
      return;
    }
    splashActive_ = false;
    drawStaticFrame();
  }
  handlePageInput(suppressTouchBeep);
  drawTicker(tickerText);
  static uint32_t s_lastDrawMs = 0;
  if (now - s_lastDrawMs < 80) {
    return;
  }
  s_lastDrawMs = now;
  updateLastShareClock(p);
  drawInfo(p);
#ifndef DISABLE_AVATAR
  auto& d = M5.Display;
  avatar_.setScale(0.45f);
  avatar_.setPosition(-12, -88);
  avatar_.setSpeechText("");
  d.setClipRect(0, 0, AV_W, AV_H);
  updateAvatarMood(p);
  updateAvatarLiveliness();
  avatar_.draw();
  d.clearClipRect();
#endif
}
void UIMining::drawStackchanScreen(const PanelData& p) {
  auto& d = M5.Display;
  uint32_t now = millis();
  static uint32_t s_lastFrameMs = 0;
  if (now - s_lastFrameMs < 80) {
    return;
  }
  s_lastFrameMs = now;
  updateLastShareClock(p);
  if (stackchanNeedsClear_) {
    d.fillScreen(BLACK);
    stackchanNeedsClear_ = false;
  }
  avatar_.setScale(1.0f);
  int bubbleLines = 1;
  for (int i = 0; i < stackchanBubbleText_.length(); ++i) {
    if (stackchanBubbleText_.charAt(i) == '\n') bubbleLines++;
  }
  const int bubbleHeight = 32 + bubbleLines * 16;
  int offsetY = 0;
  const int margin = 4;
  const int availableH = H;
  int overflow = (bubbleHeight + margin) - availableH;
  if (overflow > 0) {
    offsetY = -overflow;
  }
  avatar_.setPosition(offsetY, 0);
  // ---- UI heartbeat (log meaning: "UI draw loop alive") ----
  // Log only on attention state changes and with low-rate heartbeat.
  static uint32_t s_lastUiHbMs = 0;
  static bool s_prevAttnActive = false;
  const uint32_t uiHeartbeatMs = 5000;
  bool attnActiveNow = attentionActive_ && ((int32_t)(attentionUntilMs_ - now) > 0);
  bool attnChanged = (attnActiveNow != s_prevAttnActive);
  if (attnChanged || (now - s_lastUiHbMs) >= uiHeartbeatMs) {
    LOG_EVT_HEARTBEAT("EVT_UI_HEARTBEAT", "screen=stackchan attn=%d", attnActiveNow ? 1 : 0);
    s_lastUiHbMs = now;
    s_prevAttnActive = attnActiveNow;
  }
  // ===== REPLACE START: Attention override block (disable) =====
  if (attentionActive_) {
  }
  // ===== REPLACE END =====
  // normal stackchan draw
  // ---- Apply deferred avatar updates (safe point) ----
  if (stackchanExprPending_) {
    // Avoid noisy logs: only when changed/pending.
    LOG_EVT_DEBUG("EVT_UI_AVATAR_SET_EXP", "exp=%d", (int)stackchanExprDesired_);
    avatar_.setExpression(stackchanExprDesired_);
    stackchanExprPending_ = false;
  }
  if (stackchanSpeechPending_) {
    // NOTE: This is the most suspicious freeze point; log before/after.
    LOG_EVT_INFO("EVT_UI_AVATAR_SET_SPEECH", "len=%u", (unsigned)stackchanSpeechDesired_.length());
    avatar_.setSpeechText(stackchanSpeechDesired_.c_str());
    LOG_EVT_INFO("EVT_UI_AVATAR_SET_SPEECH_DONE", "ok=1");
    stackchanSpeechPending_ = false;
  }
  updateAvatarMood(p);
  updateAvatarLiveliness();
  d.setClipRect(0, 0, W, H);
  avatar_.draw();
  // ---- AI overlay ----
  if (aiOverlay_.active_) {
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    if (aiOverlay_.line1_.length() > 0) {
      M5.Display.drawString(aiOverlay_.line1_, 4, 4);
    }
    if (aiOverlay_.line2_.length() > 0) {
      M5.Display.drawString(aiOverlay_.line2_, 4, 4 + 12);
    }
    if (aiOverlay_.hint_.length() > 0) {
      M5.Display.setTextDatum(textdatum_t::top_right);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.setTextSize(1);
      M5.Display.drawString(aiOverlay_.hint_, M5.Display.width() - 4, 4);
    }
  }
  d.clearClipRect();
}
void UIMining::setStackchanSpeech(const String& text) {
  // Defer avatar touching to drawStackchanScreen().
  // (Direct calls to avatar_.setSpeechText() here may freeze on Core2.)
  // Format: trim to 20 chars and insert a manual wrap to keep the balloon narrow.
  auto formatBubble = [](const String& in) -> String {
    const size_t maxLen = 20;
    String s = in;
    if (s.length() > maxLen) {
      s = s.substring(0, maxLen);
      s += "...";  // ellipsis after trim
    }
    // Insert a newline after ~8 chars to clamp width (only if there's more than 8 chars).
    const size_t wrapPos = 8;
    if (s.length() > wrapPos) {
      String first = s.substring(0, wrapPos);
      String rest  = s.substring(wrapPos);
      s = first + "\n" + rest;
    }
    return s;
  };
  stackchanBubbleText_ = formatBubble(text);
  stackchanSpeechDesired_ = stackchanBubbleText_;
  stackchanSpeechPending_ = true;
  stackchanNeedsClear_ = true;
}
void UIMining::setAiOverlay(const AiUiOverlay& ov) {
  aiOverlay_ = ov;
}
void UIMining::setStackchanExpression(m5avatar::Expression exp) {
  // Defer avatar touching to drawStackchanScreen().
  stackchanExprDesired_ = exp;
  stackchanExprPending_ = true;
}
void UIMining::setStackchanSpeechTiming(uint32_t talkMinMs, uint32_t talkVarMs,
                                        uint32_t silentMinMs, uint32_t silentVarMs) {
  stackchanTalkMinMs_   = talkMinMs;
  stackchanTalkVarMs_   = talkVarMs;
  stackchanSilentMinMs_ = silentMinMs;
  stackchanSilentVarMs_ = silentVarMs;
}
String UIMining::buildStackchanBubble(const PanelData& p) {
  int kind = random(0, 6);  // 0?5
  switch (kind) {
    case 0: {
      return String("HASH") + vHash(p.hrKh_);
    }
    case 1: {
      float tc = readTempC();
      return String("TEMP") + vTemp(tc);
    }
    case 2: {
      return String("BATT") + vBatt();
    }
    case 3: { // PING
      if (p.pingMs_ >= 0.0f) {
        char buf[16];
        snprintf(buf, sizeof(buf), " %.0f ms", p.pingMs_);
        return String("PING") + String(buf);
      } else {
        return String("PING -- ms");
      }
    }
    case 4: { // POOL
      if (p.poolName_.length()) {
        return String("POOL ") + p.poolName_;
      } else {
        return String("NO POOL");
      }
    }
    default: { // SHARES
      uint8_t success = 0;
      String s = vShare(p.accepted_, p.rejected_, success);
      return String("SHR ") + s;
    }
  }
}
// ===== Layout helper =====
UIMining::TextLayoutY UIMining::computeTextLayoutY() const {
  const int lines = 5;
  const int gap = 12;
  const int block_h = lines * CHAR_H + (lines - 1) * gap;
  int top = (INF_H - block_h) / 2;
  if (top < 6) top = 6;
  TextLayoutY ly;
  ly.header = top;
  ly.y1 = ly.header + CHAR_H + gap;
  ly.y2 = ly.y1 + CHAR_H + gap;
  ly.y3 = ly.y2 + CHAR_H + gap;
  ly.y4 = ly.y3 + CHAR_H + gap;
  ly.ind_y = ly.header + (CHAR_H / 2);
  return ly;
}
void UIMining::drawSplash(const String& wifiText,  uint16_t wifiCol,
                          const String& poolText,  uint16_t poolCol,
                          const String& wifiHint,  const String& poolHint) {
  auto& d = M5.Display;
  d.drawFastVLine(X_INF, 0, INF_H, 0x18C3);
  d.drawFastHLine(0, Y_LOG - 1, W, 0x18C3);
#ifndef DISABLE_AVATAR
  PanelData p;
  avatar_.setScale(0.45f);
  avatar_.setPosition(-12, -88);
  avatar_.setSpeechText("");
  d.setClipRect(0, 0, AV_W, AV_H);
  updateAvatarMood(p);
  updateAvatarLiveliness();
  avatar_.draw();
  d.clearClipRect();
#endif
  info_.fillScreen(BLACK);
  info_.setFont(&fonts::Font0);
  int y = 4;
  info_.setTextSize(2);
  info_.setTextColor(WHITE, BLACK);
  auto drawCenter = [&](const String& s) {
    int tw = info_.textWidth(s);
    int x  = (INF_W - tw) / 2;
    if (x < PAD_LR) x = PAD_LR;
    info_.setCursor(x, y);
    info_.print(s);
    y += 18;
  };
  drawCenter("Mining-");
  drawCenter("Stackchan");
  y += 6;
  auto drawGroup = [&](const char* label, const String& status, uint16_t col,
                       const String& hint) {
    info_.setTextSize(1);
    info_.setTextColor(COL_LABEL, BLACK);
    info_.setCursor(PAD_LR, y);
    info_.print(label);
    y += 12;
    info_.setTextSize(2);
    info_.setTextColor(col, BLACK);
    int tw = info_.textWidth(status);
    int sx = INF_W - PAD_LR - tw;
    if (sx < PAD_LR) sx = PAD_LR;
    info_.setCursor(sx, y);
    info_.print(status);
    y += 22;
    if (hint.length()) {
      info_.setTextSize(1);
      info_.setTextColor(COL_LABEL, BLACK);
      int max_w = INF_W - PAD_LR * 2;
      auto fillLine = [&](String& src, String& dest) {
        dest = "";
        while (src.length()) {
          int spacePos = src.indexOf(' ');
          String word;
          if (spacePos == -1) {
            word = src;
            src  = "";
          } else {
            word = src.substring(0, spacePos + 1);
            src.remove(0, spacePos + 1);
          }
          String candidate = dest + word;
          if (info_.textWidth(candidate) > max_w) {
            if (dest.length() == 0) {
              dest = candidate;
            } else {
              src = word + src;
            }
            break;
          }
          dest = candidate;
        }
        dest.trim();
      };
      String remaining = hint;
      String line1, line2;
      fillLine(remaining, line1);
      if (remaining.length()) {
        fillLine(remaining, line2);
      }
      if (line1.length()) {
        info_.setCursor(PAD_LR, y);
        info_.print(line1);
        y += 12;
      }
      if (line2.length()) {
        info_.setCursor(PAD_LR, y);
        info_.print(line2);
        y += 12;
      }
      y += 2;
    }
    y += 4;
  };
  drawGroup("WiFi", wifiText, wifiCol, wifiHint);
  drawGroup("Pool", poolText, poolCol, poolHint);
  info_.setTextSize(1);
  info_.setTextColor(COL_LABEL, BLACK);
  String ver = String("v") + appVer_;
  int tw = info_.textWidth(ver);
  int vx = INF_W - PAD_LR - tw;
  int vy = INF_H - 12;
  if (vx < PAD_LR) vx = PAD_LR;
  info_.setCursor(vx, vy);
  info_.print(ver);
  info_.pushSprite(X_INF, 0);
}
void UIMining::drawSleepMessage() {
  info_.fillScreen(BLACK);
  tick_.fillScreen(BLACK);
  int y = 70;
  info_.setFont(&fonts::Font0);
  info_.setTextColor(WHITE, BLACK);
  info_.setTextSize(2);
  auto drawCenter = [&](const String& s, int lineHeight) {
    int tw = info_.textWidth(s);
    int x  = (INF_W - tw) / 2;
    if (x < PAD_LR) x = PAD_LR;
    info_.setCursor(x, y);
    info_.print(s);
    y += lineHeight;
  };
  drawCenter("Zzz...", 18);
  info_.setTextSize(1);
  drawCenter("Screen off, mining on", 14);
  info_.pushSprite(X_INF, 0);
  tick_.pushSprite(0, Y_LOG);
}
// ===== Static frame =====
void UIMining::drawStaticFrame() {
  auto& d = M5.Display;
  // d.fillScreen(BLACK);
  d.drawFastVLine(X_INF, 0, INF_H, 0x18C3);
  d.drawFastHLine(0, Y_LOG - 1, W, 0x18C3);
}
// ===== Page input =====
void UIMining::handlePageInput(bool suppressTouchBeep) {
  static bool s_prevPressed = false;
  // NOTE: Touch is read in main loop (I2C) and cached via setTouchSnapshot().
  // UI must not touch I2C to avoid rare freezes/hangs.
  if (!touch_.enabled_) {
    s_prevPressed = false;
    return;
  }
  bool pressed = touch_.pressed_;
  int x = touch_.x_;
  int y = touch_.y_;
  if (pressed != s_prevPressed) {
    LOG_TOUCH_DEBUG("pressed=%d x=%d y=%d",
                    static_cast<int>(pressed), x, y);
  }
  if (pressed && !s_prevPressed) {
    if (!suppressTouchBeep) {
      M5.Speaker.tone(1500, 50);
    }
    if (x >= X_INF && x < X_INF + INF_W &&
        y >= 0     && y < INF_H) {
      infoPage_   = (infoPage_ + 1) % 3;
      lastPageMs_ = millis();
    }
  }
  s_prevPressed = pressed;
}
// ===== Last share age =====
void UIMining::updateLastShareClock(const PanelData& p) {
  uint32_t total = p.accepted_ + p.rejected_;
  uint32_t now   = millis();
  if (lastShareMs_ == 0) {
    lastShareMs_     = now;
    lastTotalShares_ = total;
    return;
  }
  if (total > lastTotalShares_) {
    lastTotalShares_ = total;
    lastShareMs_     = now;
  }
}
uint32_t UIMining::lastShareAgeSec() const {
  if (lastShareMs_ == 0) return 99999;
  return (millis() - lastShareMs_) / 1000;
}
