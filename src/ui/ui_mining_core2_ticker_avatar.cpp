// Module implementation.
#include "ui/ui_mining_core2.h"
#include "core/logging.h"
#include "ai/mining_task.h"
// ===== Ticker =====
void UIMining::drawTicker(const String& text) {
  String incoming = text;
  incoming.replace('\n', ' ');
  incoming.replace('\r', ' ');
  incoming.trim();
  uint32_t now = millis();
  if (incoming.length() > 0 && incoming != tickerLast_) {
    tickerLast_ = incoming;
    if (tickerLog_.length() > 0) {
      tickerLog_ += "|";
    }
    tickerLog_ += incoming;
    const size_t maxLen = 300;
    if (tickerLog_.length() > maxLen) {
      tickerLog_ = tickerLog_.substring(tickerLog_.length() - maxLen);
    }
  }
  String s = tickerLog_.length() ? tickerLog_ : incoming;
  if (s.length() == 0) {
    tick_.fillScreen(BLACK);
    tick_.pushSprite(0, Y_LOG);
    return;
  }
  tick_.fillScreen(BLACK);
  tick_.setFont(&fonts::Font0);
  tick_.setTextSize(1);
  tick_.setTextColor(0xC618, BLACK);
  tick_.setTextWrap(false);
  int tw = tick_.textWidth(s);
  if (tw <= 0) {
    tick_.pushSprite(0, Y_LOG);
    return;
  }
  const int gap  = 32;
  const int span = tw + gap;
  const uint32_t interval = 10;
  const int      step     = 8;
  if (now - lastTickMs_ >= interval) {
    lastTickMs_ = now;
    tickerOffset_ -= step;
    if (tickerOffset_ <= -span) {
      tickerOffset_ += span;
    }
  }
  int x = tickerOffset_;
  while (x < W) {
    tick_.setCursor(x, 8);
    tick_.print(s);
    x += span;
  }
  tick_.pushSprite(0, Y_LOG);
}
// ===== Avatar mood =====
// === src/ui_mining_core2_ticker_avatar.cpp : replace whole function ===
// ===== Avatar mood =====
void UIMining::updateAvatarMood(const PanelData& p) {
  uint32_t now = millis();
  const uint32_t moodPeriodicLogMs = 60 * 1000;
  int8_t prevMood = moodLevel_;
  if (now - moodLastCalcMs_ >= 800) {
    moodLastCalcMs_ = now;
    int8_t target = 0;
    if (WiFi.status() != WL_CONNECTED) {
      target = -2;
    } else if (!p.poolAlive_) {
      target = -1;
    } else if (isMiningPaused()) {
      target = moodLevel_;
    } else {
      int score = 0;
      uint32_t age = lastShareAgeSec();
      if (age <= 120)       score += 1;
      else if (age <= 300)  score += 0;
      else if (age <= 900)  score -= 1;
      else                  score -= 2;
      uint32_t total = p.accepted_ + p.rejected_;
      if (total >= 10) {
        float success = 100.0f * (float)p.accepted_ / (float)total;
        if (success >= 85.0f)      score += 1;
        else if (success >= 70.0f) score += 0;
        else if (success >= 50.0f) score -= 1;
        else                       score -= 2;
      }
      if (p.hrKh_ <= 0.05f) {
        score -= 2;
      } else if (hrRefKh_ > 0.1f) {
        float r = p.hrKh_ / hrRefKh_;
        if (r >= 0.90f)      score += 1;
        else if (r >= 0.70f) score += 0;
        else                 score -= 1;
      }
      if      (score >=  2) target =  2;
      else if (score ==  1) target =  1;
      else if (score ==  0) target =  0;
      else if (score == -1) target = -1;
      else                  target = -2;
    }
    if (target > moodLevel_) moodLevel_++;
    else if (target < moodLevel_) moodLevel_--;
  }
  if (moodLevel_ != prevMood) {
    uint32_t age = lastShareAgeSec();
    MC_LOGI_RL("mood_change", 3000, "MOOD",
               "%d -> %d (wifi=%d pool=%d age=%us A=%u R=%u HR=%.2fk ref=%.2fk)",
               (int)prevMood, (int)moodLevel_,
               (int)WiFi.status(),
               p.poolAlive_ ? 1 : 0,
               (unsigned)age,
               (unsigned)p.accepted_, (unsigned)p.rejected_,
               (double)p.hrKh_, (double)hrRefKh_);
    moodLastReportMs_ = now;
  }
  if (now - moodLastReportMs_ >= moodPeriodicLogMs) {
    moodLastReportMs_ = now;
    uint32_t age = lastShareAgeSec();
    MC_LOGT("MOOD",
            "current=%d (wifi=%d pool=%d age=%us A=%u R=%u HR=%.2fk ref=%.2fk)",
            (int)moodLevel_,
            (int)WiFi.status(),
            p.poolAlive_ ? 1 : 0,
            (unsigned)age,
            (unsigned)p.accepted_, (unsigned)p.rejected_,
            (double)p.hrKh_, (double)hrRefKh_);
  }
  bool talking = inStackchanMode_ && stackchanTalking_;
  if (talking) {
    float t = millis() * 0.02f;
    float mouth = 0.20f + 0.20f * (sinf(t) * 0.5f + 0.5f);
    avatar_.setMouthOpenRatio(mouth);
  } else {
    avatar_.setMouthOpenRatio(0.0f);
  }
}
// ===== Avatar liveliness (blink / gaze / breath) =====
//
//
void UIMining::updateAvatarLiveliness() {
  uint32_t now = millis();
  // bubble text showing? used to modify gaze/mouth behavior
  const bool bubbleActive = inStackchanMode_ && (stackchanBubbleText_.length() > 0);
  float energy = 0.9f;      // neutral
  float eyeOpen = 1.0f;
  float gazeAmp = 1.0f;
  if (moodLevel_ >= 2) { energy = 1.15f; eyeOpen = 1.0f;  gazeAmp = 1.10f; }
  else if (moodLevel_ == 1) { energy = 1.00f; eyeOpen = 1.0f;  gazeAmp = 1.00f; }
  else if (moodLevel_ == 0) { energy = 0.90f; eyeOpen = 1.0f;  gazeAmp = 0.90f; }
  else if (moodLevel_ == -1){ energy = 0.75f; eyeOpen = 0.88f; gazeAmp = 0.70f; }
  else { /* -2 */            energy = 0.60f; eyeOpen = 0.75f; gazeAmp = 0.55f; }
  struct State {
    bool     initialized;
    uint32_t saccadeInterval;
    uint32_t lastSaccadeMs;
    float    vertical;
    float    horizontal;
    uint32_t blinkInterval;
    uint32_t lastBlinkMs;
    bool     eyeOpen;
    int      count;
    uint32_t lastUpdateMs;
  };
  static State s_state;
  if (!s_state.initialized) {
    s_state.initialized       = true;
    s_state.saccadeInterval   = 1000;
    s_state.lastSaccadeMs     = now;
    s_state.vertical          = 0.0f;
    s_state.horizontal        = 0.0f;
    s_state.blinkInterval     = 2500;
    s_state.lastBlinkMs       = now;
    s_state.eyeOpen           = true;
    s_state.count             = 0;
    s_state.lastUpdateMs      = now;
  }
  if (bubbleActive) {
    avatar_.setGaze(0.0f, 0.0f);
  } else if (now - s_state.lastSaccadeMs > s_state.saccadeInterval) {
    s_state.vertical   = (((float)random(-1000, 1001)) / 1000.0f) * gazeAmp;
    s_state.horizontal = (((float)random(-1000, 1001)) / 1000.0f) * gazeAmp;
    // clamp
    if (s_state.vertical > 1.0f) s_state.vertical = 1.0f;
    if (s_state.vertical < -1.0f) s_state.vertical = -1.0f;
    if (s_state.horizontal > 1.0f) s_state.horizontal = 1.0f;
    if (s_state.horizontal < -1.0f) s_state.horizontal = -1.0f;
    avatar_.setGaze(s_state.vertical, s_state.horizontal);
    if (moodLevel_ >= 2)      s_state.saccadeInterval = 350 + 80  * (uint32_t)random(0, 15);
    else if (moodLevel_ == 1) s_state.saccadeInterval = 450 + 90  * (uint32_t)random(0, 15);
    else if (moodLevel_ == 0) s_state.saccadeInterval = 500 + 100 * (uint32_t)random(0, 20);
    else                       s_state.saccadeInterval = 900 + 150 * (uint32_t)random(0, 20);
    s_state.lastSaccadeMs  = now;
  }
  if (now - s_state.lastBlinkMs > s_state.blinkInterval) {
    if (s_state.eyeOpen) {
      avatar_.setEyeOpenRatio(0.0f);
      s_state.blinkInterval = 300 + 10 * (uint32_t)random(0, 20);
    } else {
      avatar_.setEyeOpenRatio(eyeOpen);
      s_state.blinkInterval = 2500 + 100 * (uint32_t)random(0, 20);
    }
    s_state.eyeOpen       = !s_state.eyeOpen;
    s_state.lastBlinkMs   = now;
  }
  uint32_t dt   = now - s_state.lastUpdateMs;
  s_state.lastUpdateMs = now;
  int step = dt / 33;
  if (step < 1) step = 1;
  s_state.count = (s_state.count + step) % 100;
  float breath = sinf(s_state.count * 2.0f * PI / 100.0f);
  avatar_.setBreath(breath * energy);
  if (bubbleActive) {
    float t = millis() * 0.02f;
    float mouth = 0.35f + 0.35f * (sinf(t) * 0.5f + 0.5f);
    avatar_.setMouthOpenRatio(mouth);
  } else {
    avatar_.setMouthOpenRatio(0.0f);
  }
  //
  //
  if (inStackchanMode_) {
    struct BodyState {
      bool     initialized;
      float    px, py;
      float    tx, ty;
      uint32_t nextChangeMs;
    };
    static BodyState s_bodyState;
    if (!s_bodyState.initialized) {
      s_bodyState.initialized     = true;
      s_bodyState.px = s_bodyState.py       = 0.0f;
      s_bodyState.tx = s_bodyState.ty       = 0.0f;
      s_bodyState.nextChangeMs  = now + 2000;
    }
    if ((int32_t)(now - s_bodyState.nextChangeMs) >= 0) {
      float rangeX = 20.0f * energy;
      float rangeY = 12.0f * energy;
      s_bodyState.tx = ((float)random(-1000, 1001)) / 1000.0f * rangeX;
      s_bodyState.ty = ((float)random(-1000, 1001)) / 1000.0f * rangeY;
      s_bodyState.nextChangeMs = now + 1000 + (uint32_t)random(0, 4000);
    }
    float follow = 0.1f* energy;
    s_bodyState.px += (s_bodyState.tx - s_bodyState.px) * follow;
    s_bodyState.py += (s_bodyState.ty - s_bodyState.py) * follow;
    avatar_.setPosition((int)s_bodyState.px, (int)s_bodyState.py);
  }
}

