// src/ui_mining_core2.cpp
// Module implementation.
#include "ui/ui_mining_core2.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <M5Unified.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <WiFi.h>

#include "utils/logging.h"
// ===== Singleton / ctor =====
UIMining &UIMining::instance() {
  static UIMining s_instance;
  return s_instance;
}
UIMining::UIMining()
    : avatar_(), info_(&M5.Display), tick_(&M5.Display),
      overlaySprite_(&M5.Display) {
  inStackchanMode_ = false;
  stackchanNeedsClear_ = false;
}
// ===== Public API =====
void UIMining::begin(const char *appName, const char *appVer) {
  appName_ = appName ? appName : "";
  appVer_ = appVer ? appVer : "";
  auto &d = M5.Display;
  // Base display setup: rotation, brightness, and sprite buffers.
  d.setRotation(1);
  d.setBrightness(128);
  avatar_.setScale(0.45f);
  avatar_.setPosition(-12, -88);
  // Use a readable Japanese font for speech balloons.
  avatar_.setSpeechFont(&fonts::efontJA_12);
  avatar_.setSpeechText("");
  // Off-screen sprites reduce flicker during partial redraws.
  info_.setColorDepth(8);
  info_.createSprite(INF_W, INF_H);
  info_.setTextWrap(false);
  tick_.setColorDepth(8);
  tick_.createSprite(W, LOG_H);
  tick_.setTextWrap(false);
  overlaySprite_.setColorDepth(8);
  overlaySprite_.createSprite(W, 30);
  overlaySprite_.setTextWrap(false);
  aiOverlayDirty_ = true;
  aiOverlayVisible_ = false;
  lastPageMs_ = millis();
  lastShareMs_ = 0;
  lastTotalShares_ = 0;
  tickerOffset_ = W;
  splashActive_ = true;
  splashStartMs_ = millis();
  splashReadyMs_ = 0;
  splashWifiText_ = "Connecting...";
  splashMiningText_ = "Waiting";
  splashOpenAiText_ = "Waiting";
  splashAzureText_ = "Waiting";
  splashDispWifiState_ = PanelData::BootConnecting;
  splashDispMiningState_ = PanelData::BootWaiting;
  splashDispOpenAiState_ = PanelData::BootWaiting;
  splashDispAzureState_ = PanelData::BootWaiting;
  splashDispStepMs_ = splashStartMs_;
  splashHint_ = "";
  splashWifiCol_ = 0xFD20;
  splashMiningCol_ = kColLabel;
  splashOpenAiCol_ = kColLabel;
  splashAzureCol_ = kColLabel;
  drawSplash(splashWifiText_, splashWifiCol_, splashMiningText_, splashMiningCol_,
             splashOpenAiText_, splashOpenAiCol_, splashAzureText_,
             splashAzureCol_, splashHint_);
  tick_.fillScreen(BLACK);
  tick_.pushSprite(0, Y_LOG);
}
void UIMining::setTouchSnapshot(const TouchSnapshot &s) { touch_ = s; }
String UIMining::shortFwString() const { return String("r25-12-06"); }
uint32_t UIMining::uptimeSeconds() const {
  return static_cast<uint32_t>(millis() / 1000);
}
void UIMining::setHashrateReference(float kh) { hrRefKh_ = kh; }
void UIMining::setAutoPageMs(uint32_t ms) { autoPageMs_ = ms; }
void UIMining::onEnterStackchanMode() {
  inStackchanMode_ = true;
  stackchanNeedsClear_ = true;
  stackchanTalking_ = false;
  stackchanPhaseStartMs_ = millis();
  stackchanPhaseDurMs_ = 0;
  stackchanBubbleText_ = "";
  avatar_.setScale(1.0f);
  avatar_.setPosition(0, 0);
  avatar_.setSpeechText("");
}
void UIMining::onLeaveStackchanMode() {
  inStackchanMode_ = false;
  stackchanNeedsClear_ = false;
  stackchanTalking_ = false;
  stackchanPhaseStartMs_ = 0;
  stackchanPhaseDurMs_ = 0;
  stackchanBubbleText_ = "";
  avatar_.setSpeechText("");
  avatar_.setScale(0.45f);
  avatar_.setPosition(-12, -88);
}
void UIMining::triggerAttention(uint32_t durationMs, const char *text) {
  if (durationMs == 0) {
    LOG_EVT_INFO("EVT_ATTENTION_EXIT", "attn=0");
    attentionActive_ = false;
    attentionUntilMs_ = 0;
    attentionText_ = attentionDefaultText_;
    if (inStackchanMode_) {
      setStackchanSpeech("");
    }
    return;
  }
  // "Attention" is a short-lived focus state that overrides bubble text.
  attentionActive_ = true;
  attentionUntilMs_ = millis() + durationMs;
  attentionText_ = (text && *text) ? String(text) : attentionDefaultText_;
  LOG_EVT_INFO("EVT_ATTENTION_ENTER", "attn=1 text=%s", attentionText_.c_str());
  if (inStackchanMode_) {
    setStackchanSpeech(attentionText_);
    stackchanSpeechText_ = attentionText_;
    stackchanSpeechSeq_++;
  }
}
void UIMining::setAttentionDefaultText(const char *text) {
  attentionDefaultText_ = (text && *text) ? String(text) : String("WHAT?");
  if (!attentionActive_) {
    attentionText_ = attentionDefaultText_;
  }
}
bool UIMining::isAttentionActive() const {
  if (!attentionActive_)
    return false;
  // handle millis wrap-around safely
  return (int32_t)(attentionUntilMs_ - millis()) > 0;
}
void UIMining::drawAll(const PanelData &p, const String &tickerText,
                       bool suppressTouchBeep) {
  uint32_t now = millis();
  if (splashActive_) {
    // Splash shows relay checks: WiFi -> Mining -> OpenAI -> Azure.
    auto makeConnecting = [&](const char *base) -> String {
      uint32_t elapsed = now - splashStartMs_;
      const uint32_t period = 200;
      uint32_t phase = (elapsed / period) % 6;
      uint8_t dots;
      if (phase <= 3)
        dots = 1 + phase; // 1,2,3,4
      else
        dots = 6 - phase; // 3,2
      String s(base);
      for (uint8_t i = 0; i < dots; ++i) {
        s += '.';
      }
      return s;
    };
    auto mapState = [&](uint8_t st, String &text, uint16_t &col) {
      switch (st) {
      case PanelData::BootOk:
        text = "OK";
        col = 0x07E0;
        break;
      case PanelData::BootFail:
        text = "NG";
        col = 0xF800;
        break;
      case PanelData::BootSkip:
        text = "SKIP";
        col = kColLabel;
        break;
      case PanelData::BootWaiting:
        text = "Waiting";
        col = kColLabel;
        break;
      case PanelData::BootConnecting:
      default:
        text = makeConnecting("Connecting");
        col = 0xFD20;
        break;
      }
    };
    auto stepDisplayState = [&](uint8_t current, uint8_t target,
                                bool allowStep) -> uint8_t {
      if (current == target) {
        return current;
      }
      if (target == PanelData::BootFail || target == PanelData::BootSkip ||
          target == PanelData::BootWaiting) {
        return target;
      }
      if (!allowStep) {
        return current;
      }
      if (target == PanelData::BootConnecting) {
        if (current == PanelData::BootWaiting || current == PanelData::BootConnecting) {
          return PanelData::BootConnecting;
        }
        return target;
      }
      if (target == PanelData::BootOk) {
        if (current == PanelData::BootWaiting) {
          return PanelData::BootConnecting;
        }
        if (current == PanelData::BootConnecting) {
          return PanelData::BootOk;
        }
        return PanelData::BootOk;
      }
      return target;
    };
    uint8_t rawStates[4] = {p.bootWifiState_, p.bootMiningState_,
                            p.bootOpenAiState_, p.bootAzureState_};
    uint8_t *dispStates[4] = {&splashDispWifiState_, &splashDispMiningState_,
                              &splashDispOpenAiState_, &splashDispAzureState_};
    const uint32_t kStepIntervalMs = 280UL;
    bool stepped = false;
    for (int i = 0; i < 4; ++i) {
      uint8_t current = *dispStates[i];
      uint8_t target = rawStates[i];
      if (current == target) {
        continue;
      }
      const bool allowStep =
          (splashDispStepMs_ == 0) || ((now - splashDispStepMs_) >= kStepIntervalMs);
      uint8_t next = stepDisplayState(current, target, allowStep);
      if (next != current) {
        *dispStates[i] = next;
        splashDispStepMs_ = now;
        stepped = true;
      }
      if (!stepped && !allowStep) {
        break;
      }
      break;
    }

    String wifiText, miningText, openAiText, azureText;
    uint16_t wifiCol = kColLabel;
    uint16_t miningCol = kColLabel;
    uint16_t openAiCol = kColLabel;
    uint16_t azureCol = kColLabel;
    mapState(splashDispWifiState_, wifiText, wifiCol);
    mapState(splashDispMiningState_, miningText, miningCol);
    mapState(splashDispOpenAiState_, openAiText, openAiCol);
    mapState(splashDispAzureState_, azureText, azureCol);
    const String hint = p.bootActiveDiag_;
    if (wifiText != splashWifiText_ || wifiCol != splashWifiCol_ ||
        miningText != splashMiningText_ || miningCol != splashMiningCol_ ||
        openAiText != splashOpenAiText_ || openAiCol != splashOpenAiCol_ ||
        azureText != splashAzureText_ || azureCol != splashAzureCol_ ||
        hint != splashHint_) {
      splashWifiText_ = wifiText;
      splashWifiCol_ = wifiCol;
      splashMiningText_ = miningText;
      splashMiningCol_ = miningCol;
      splashOpenAiText_ = openAiText;
      splashOpenAiCol_ = openAiCol;
      splashAzureText_ = azureText;
      splashAzureCol_ = azureCol;
      splashHint_ = hint;
      drawSplash(splashWifiText_, splashWifiCol_, splashMiningText_,
                 splashMiningCol_, splashOpenAiText_, splashOpenAiCol_,
                 splashAzureText_, splashAzureCol_, splashHint_);
    }
    const bool allPassedOrSkip =
        (splashDispWifiState_ == PanelData::BootOk ||
         splashDispWifiState_ == PanelData::BootSkip) &&
        (splashDispMiningState_ == PanelData::BootOk ||
         splashDispMiningState_ == PanelData::BootSkip) &&
        (splashDispOpenAiState_ == PanelData::BootOk ||
         splashDispOpenAiState_ == PanelData::BootSkip) &&
        (splashDispAzureState_ == PanelData::BootOk ||
         splashDispAzureState_ == PanelData::BootSkip);

    if (allPassedOrSkip) {
      if (splashReadyMs_ == 0) {
        splashReadyMs_ = now;
      }
    } else {
      splashReadyMs_ = 0;
    }
    bool ready = allPassedOrSkip && (now - splashStartMs_ > 3000) &&
                 (splashReadyMs_ != 0) && (now - splashReadyMs_ > 1000);
    if (!ready) {
      return;
    }
    splashActive_ = false;
    drawStaticFrame();
  }
  handlePageInput(suppressTouchBeep);
  if (p.miningEnabled_) {
    drawTicker(tickerText);
  } else {
    tick_.fillScreen(BLACK);
    tick_.pushSprite(0, Y_LOG);
  }
  static uint32_t s_lastDrawMs = 0;
  if (now - s_lastDrawMs < 80) {
    return;
  }
  s_lastDrawMs = now;
  updateLastShareClock(p);
  drawInfo(p);
#ifndef DISABLE_AVATAR
  auto &d = M5.Display;
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
void UIMining::drawStackchanScreen(const PanelData &p) {
  auto &d = M5.Display;
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
    if (stackchanBubbleText_.charAt(i) == '\n')
      bubbleLines++;
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
  bool attnActiveNow =
      attentionActive_ && ((int32_t)(attentionUntilMs_ - now) > 0);
  bool attnChanged = (attnActiveNow != s_prevAttnActive);
  if (attnChanged || (now - s_lastUiHbMs) >= uiHeartbeatMs) {
    LOG_EVT_HEARTBEAT("EVT_UI_HEARTBEAT", "screen=stackchan attn=%d",
                      attnActiveNow ? 1 : 0);
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
    LOG_EVT_DEBUG("EVT_UI_AVATAR_SET_EXP", "exp=%d",
                  (int)stackchanExprDesired_);
    avatar_.setExpression(stackchanExprDesired_);
    stackchanExprPending_ = false;
  }
  if (stackchanSpeechPending_) {
    // NOTE: This is the most suspicious freeze point; log before/after.
    LOG_EVT_INFO("EVT_UI_AVATAR_SET_SPEECH", "len=%u",
                 (unsigned)stackchanSpeechDesired_.length());
    avatar_.setSpeechText(stackchanSpeechDesired_.c_str());
    LOG_EVT_INFO("EVT_UI_AVATAR_SET_SPEECH_DONE", "ok=1");
    stackchanSpeechPending_ = false;
  }
  updateAvatarMood(p);
  updateAvatarLiveliness();
  d.setClipRect(0, 0, W, H);
  avatar_.draw();
  d.clearClipRect();
}
void UIMining::setStackchanSpeech(const String &text) {
  // Defer avatar touching to drawStackchanScreen().
  // (Direct calls to avatar_.setSpeechText() here may freeze on Core2.)
  // Format: trim to 20 chars and insert a manual wrap to keep the balloon
  // narrow.
  auto formatBubble = [](const String &in) -> String {
    auto bytesForUtf8Chars = [](const String &s, size_t maxChars) -> size_t {
      const size_t n = s.length();
      size_t i = 0;
      size_t chars = 0;
      while (i < n && chars < maxChars) {
        uint8_t c = (uint8_t)s[i];
        size_t L = 1;
        if (c < 0x80) {
          L = 1;
        } else if ((c & 0xE0) == 0xC0) {
          L = 2;
        } else if ((c & 0xF0) == 0xE0) {
          L = 3;
        } else if ((c & 0xF8) == 0xF0) {
          L = 4;
        }
        if (i + L > n) break;
        bool ok = true;
        for (size_t k = 1; k < L; ++k) {
          uint8_t cc = (uint8_t)s[i + k];
          if ((cc & 0xC0) != 0x80) {
            ok = false;
            break;
          }
        }
        i += ok ? L : 1;
        chars++;
      }
      return i;
    };

    const size_t maxChars = 20;
    String s = in;
    const size_t maxBytes = bytesForUtf8Chars(s, maxChars);
    if (maxBytes < s.length()) {
      s = s.substring(0, maxBytes);
      s += "..."; // ellipsis after trim
    }
    // Insert a newline after ~8 UTF-8 chars to clamp width.
    const size_t wrapChars = 8;
    const size_t wrapBytes = bytesForUtf8Chars(s, wrapChars);
    if (wrapBytes < s.length()) {
      String first = s.substring(0, wrapBytes);
      String rest = s.substring(wrapBytes);
      s = first + "\n" + rest;
    }
    return s;
  };
  const String formatted = formatBubble(text);
  if (formatted == stackchanBubbleText_ && !stackchanSpeechPending_) {
    return;
  }
  stackchanBubbleText_ = formatted;
  stackchanSpeechDesired_ = formatted;
  stackchanSpeechPending_ = true;
}
void UIMining::setAiOverlay(const AiUiOverlay &ov) {
  const bool changed = (aiOverlay_.active_ != ov.active_) ||
                       (aiOverlay_.line1_ != ov.line1_) ||
                       (aiOverlay_.line2_ != ov.line2_) ||
                       (aiOverlay_.hint_ != ov.hint_) ||
                       (aiOverlay_.state_ != ov.state_);
  aiOverlay_ = ov;
  if (changed) {
    aiOverlayDirty_ = true;
  }
}
void UIMining::setStackchanExpression(m5avatar::Expression exp) {
  // Defer avatar touching to drawStackchanScreen().
  stackchanExprDesired_ = exp;
  stackchanExprPending_ = true;
}
void UIMining::setStackchanSpeechTiming(uint32_t talkMinMs, uint32_t talkVarMs,
                                        uint32_t silentMinMs,
                                        uint32_t silentVarMs) {
  stackchanTalkMinMs_ = talkMinMs;
  stackchanTalkVarMs_ = talkVarMs;
  stackchanSilentMinMs_ = silentMinMs;
  stackchanSilentVarMs_ = silentVarMs;
}
String UIMining::buildStackchanBubble(const PanelData &p) {
  int kind = random(0, 6); // 0?5
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
  const int blockH = lines * kCharH + (lines - 1) * gap;
  int top = (INF_H - blockH) / 2;
  if (top < 6)
    top = 6;
  TextLayoutY ly;
  ly.header = top;
  ly.y1 = ly.header + kCharH + gap;
  ly.y2 = ly.y1 + kCharH + gap;
  ly.y3 = ly.y2 + kCharH + gap;
  ly.y4 = ly.y3 + kCharH + gap;
  ly.indY = ly.header + (kCharH / 2);
  return ly;
}
void UIMining::drawSplash(const String &wifiText, uint16_t wifiCol,
                          const String &miningText, uint16_t miningCol,
                          const String &openAiText, uint16_t openAiCol,
                          const String &azureText, uint16_t azureCol,
                          const String &hint) {
  auto &d = M5.Display;
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
  auto drawCenter = [&](const String &s) {
    int tw = info_.textWidth(s);
    int x = (INF_W - tw) / 2;
    if (x < kPadLr)
      x = kPadLr;
    info_.setCursor(x, y);
    info_.print(s);
    y += 18;
  };
  drawCenter("AI-Mining");
  drawCenter("Stackchan");
  y += 6;
  auto drawGroup = [&](const char *label, const String &status, uint16_t col) {
    info_.setTextSize(1);
    info_.setTextColor(kColLabel, BLACK);
    info_.setCursor(kPadLr, y);
    info_.print(label);
    y += 10;
    info_.setTextSize(2);
    info_.setTextColor(col, BLACK);
    int tw = info_.textWidth(status);
    int sx = INF_W - kPadLr - tw;
    if (sx < kPadLr)
      sx = kPadLr;
    info_.setCursor(sx, y);
    info_.print(status);
    y += 20;
  };
  drawGroup("WiFi", wifiText, wifiCol);
  drawGroup("Mining", miningText, miningCol);
  drawGroup("OpenAI", openAiText, openAiCol);
  drawGroup("Azure", azureText, azureCol);
  if (hint.length()) {
    info_.setTextSize(1);
    info_.setTextColor(kColLabel, BLACK);
    int maxW = INF_W - kPadLr * 2;
    auto clampLine = [&](const String &src) {
      String s = src;
      while (s.length() && info_.textWidth(s) > maxW) {
        s.remove(s.length() - 1);
      }
      return s;
    };
    String line1 = hint;
    String line2 = "";
    int split = hint.lastIndexOf(' ');
    if (split > 0) {
      line1 = hint.substring(0, split);
      line2 = hint.substring(split + 1);
    }
    line1 = clampLine(line1);
    line2 = clampLine(line2);
    int hintY = INF_H - 38;
    info_.setCursor(kPadLr, hintY);
    info_.print(line1);
    if (line2.length()) {
      info_.setCursor(kPadLr, hintY + 10);
      info_.print(line2);
    }
  }
  info_.setTextSize(1);
  info_.setTextColor(kColLabel, BLACK);
  String ver = String("v") + appVer_;
  int tw = info_.textWidth(ver);
  int vx = INF_W - kPadLr - tw;
  int vy = INF_H - 12;
  if (vx < kPadLr)
    vx = kPadLr;
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
  auto drawCenter = [&](const String &s, int lineHeight) {
    int tw = info_.textWidth(s);
    int x = (INF_W - tw) / 2;
    if (x < kPadLr)
      x = kPadLr;
    info_.setCursor(x, y);
    info_.print(s);
    y += lineHeight;
  };
  drawCenter("Zzz...", 18);
  info_.setTextSize(1);
  drawCenter("Screen off", 14);
  info_.pushSprite(X_INF, 0);
  tick_.pushSprite(0, Y_LOG);
}
// ===== Static frame =====
void UIMining::drawStaticFrame() {
  auto &d = M5.Display;
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
    LOG_TOUCH_DEBUG("pressed=%d x=%d y=%d", static_cast<int>(pressed), x, y);
  }
  if (pressed && !s_prevPressed) {
    if (!suppressTouchBeep) {
      M5.Speaker.tone(1500, 50);
    }
    if (x >= X_INF && x < X_INF + INF_W && y >= 0 && y < INF_H) {
      infoPage_ = (infoPage_ + 1) % 3;
      lastPageMs_ = millis();
    }
  }
  s_prevPressed = pressed;
}
// ===== Last share age =====
void UIMining::updateLastShareClock(const PanelData &p) {
  uint32_t total = p.accepted_ + p.rejected_;
  uint32_t now = millis();
  if (lastShareMs_ == 0) {
    lastShareMs_ = now;
    lastTotalShares_ = total;
    return;
  }
  if (total > lastTotalShares_) {
    lastTotalShares_ = total;
    lastShareMs_ = now;
  }
}
uint32_t UIMining::lastShareAgeSec() const {
  if (lastShareMs_ == 0)
    return 99999;
  return (millis() - lastShareMs_) / 1000;
}
