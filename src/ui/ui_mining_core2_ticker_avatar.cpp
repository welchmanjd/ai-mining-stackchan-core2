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
  if (incoming.length() > 0 && incoming != ticker_last_) {
    ticker_last_ = incoming;
    if (ticker_log_.length() > 0) {
      ticker_log_ += "|";
    }
    ticker_log_ += incoming;
    const size_t MAX_LEN = 300;
    if (ticker_log_.length() > MAX_LEN) {
      ticker_log_ = ticker_log_.substring(ticker_log_.length() - MAX_LEN);
    }
  }
  String s = ticker_log_.length() ? ticker_log_ : incoming;
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
  if (now - last_tick_ms_ >= interval) {
    last_tick_ms_ = now;
    ticker_offset_ -= step;
    if (ticker_offset_ <= -span) {
      ticker_offset_ += span;
    }
  }
  int x = ticker_offset_;
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
  const uint32_t kMoodPeriodicLogMs = 60 * 1000;
  int8_t prevMood = mood_level_;
  if (now - mood_last_calc_ms_ >= 800) {
    mood_last_calc_ms_ = now;
    int8_t target = 0;
    if (WiFi.status() != WL_CONNECTED) {
      target = -2;
    } else if (!p.poolAlive_) {
      target = -1;
    } else if (isMiningPaused()) {
      target = mood_level_;
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
      } else if (hr_ref_kh_ > 0.1f) {
        float r = p.hrKh_ / hr_ref_kh_;
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
    if (target > mood_level_) mood_level_++;
    else if (target < mood_level_) mood_level_--;
  }
  if (mood_level_ != prevMood) {
    uint32_t age = lastShareAgeSec();
    MC_LOGI_RL("mood_change", 3000, "MOOD",
               "%d -> %d (wifi=%d pool=%d age=%us A=%u R=%u HR=%.2fk ref=%.2fk)",
               (int)prevMood, (int)mood_level_,
               (int)WiFi.status(),
               p.poolAlive_ ? 1 : 0,
               (unsigned)age,
               (unsigned)p.accepted_, (unsigned)p.rejected_,
               (double)p.hrKh_, (double)hr_ref_kh_);
    mood_last_report_ms_ = now;
  }
  if (now - mood_last_report_ms_ >= kMoodPeriodicLogMs) {
    mood_last_report_ms_ = now;
    uint32_t age = lastShareAgeSec();
    MC_LOGT("MOOD",
            "current=%d (wifi=%d pool=%d age=%us A=%u R=%u HR=%.2fk ref=%.2fk)",
            (int)mood_level_,
            (int)WiFi.status(),
            p.poolAlive_ ? 1 : 0,
            (unsigned)age,
            (unsigned)p.accepted_, (unsigned)p.rejected_,
            (double)p.hrKh_, (double)hr_ref_kh_);
  }
  bool talking = in_stackchan_mode_ && stackchan_talking_;
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
  const bool bubble_active = in_stackchan_mode_ && (stackchan_bubble_text_.length() > 0);
  float energy = 0.9f;      // neutral
  float eyeOpen = 1.0f;
  float gazeAmp = 1.0f;
  if (mood_level_ >= 2) { energy = 1.15f; eyeOpen = 1.0f;  gazeAmp = 1.10f; }
  else if (mood_level_ == 1) { energy = 1.00f; eyeOpen = 1.0f;  gazeAmp = 1.00f; }
  else if (mood_level_ == 0) { energy = 0.90f; eyeOpen = 1.0f;  gazeAmp = 0.90f; }
  else if (mood_level_ == -1){ energy = 0.75f; eyeOpen = 0.88f; gazeAmp = 0.70f; }
  else { /* -2 */            energy = 0.60f; eyeOpen = 0.75f; gazeAmp = 0.55f; }
  struct State {
    bool     initialized;
    uint32_t saccade_interval;
    uint32_t last_saccade_ms;
    float    vertical;
    float    horizontal;
    uint32_t blink_interval;
    uint32_t last_blink_ms;
    bool     eye_open;
    int      count;
    uint32_t last_update_ms;
  };
  static State s;
  if (!s.initialized) {
    s.initialized       = true;
    s.saccade_interval  = 1000;
    s.last_saccade_ms   = now;
    s.vertical          = 0.0f;
    s.horizontal        = 0.0f;
    s.blink_interval    = 2500;
    s.last_blink_ms     = now;
    s.eye_open          = true;
    s.count             = 0;
    s.last_update_ms    = now;
  }
  if (bubble_active) {
    avatar_.setGaze(0.0f, 0.0f);
  } else if (now - s.last_saccade_ms > s.saccade_interval) {
    s.vertical   = (((float)random(-1000, 1001)) / 1000.0f) * gazeAmp;
    s.horizontal = (((float)random(-1000, 1001)) / 1000.0f) * gazeAmp;
    // clamp
    if (s.vertical > 1.0f) s.vertical = 1.0f;
    if (s.vertical < -1.0f) s.vertical = -1.0f;
    if (s.horizontal > 1.0f) s.horizontal = 1.0f;
    if (s.horizontal < -1.0f) s.horizontal = -1.0f;
    avatar_.setGaze(s.vertical, s.horizontal);
    if (mood_level_ >= 2)      s.saccade_interval = 350 + 80  * (uint32_t)random(0, 15);
    else if (mood_level_ == 1) s.saccade_interval = 450 + 90  * (uint32_t)random(0, 15);
    else if (mood_level_ == 0) s.saccade_interval = 500 + 100 * (uint32_t)random(0, 20);
    else                       s.saccade_interval = 900 + 150 * (uint32_t)random(0, 20);
    s.last_saccade_ms  = now;
  }
  if (now - s.last_blink_ms > s.blink_interval) {
    if (s.eye_open) {
      avatar_.setEyeOpenRatio(0.0f);
      s.blink_interval = 300 + 10 * (uint32_t)random(0, 20);
    } else {
      avatar_.setEyeOpenRatio(eyeOpen);
      s.blink_interval = 2500 + 100 * (uint32_t)random(0, 20);
    }
    s.eye_open       = !s.eye_open;
    s.last_blink_ms  = now;
  }
  uint32_t dt   = now - s.last_update_ms;
  s.last_update_ms = now;
  int step = dt / 33;
  if (step < 1) step = 1;
  s.count = (s.count + step) % 100;
  float breath = sinf(s.count * 2.0f * PI / 100.0f);
  avatar_.setBreath(breath * energy);
  if (bubble_active) {
    float t = millis() * 0.02f;
    float mouth = 0.35f + 0.35f * (sinf(t) * 0.5f + 0.5f);
    avatar_.setMouthOpenRatio(mouth);
  } else {
    avatar_.setMouthOpenRatio(0.0f);
  }
  //
  //
  if (in_stackchan_mode_) {
    struct BodyState {
      bool     initialized;
      float    px, py;
      float    tx, ty;
      uint32_t next_change_ms;
    };
    static BodyState b;
    if (!b.initialized) {
      b.initialized     = true;
      b.px = b.py       = 0.0f;
      b.tx = b.ty       = 0.0f;
      b.next_change_ms  = now + 2000;
    }
    if ((int32_t)(now - b.next_change_ms) >= 0) {
      float rangeX = 20.0f * energy;
      float rangeY = 12.0f * energy;
      b.tx = ((float)random(-1000, 1001)) / 1000.0f * rangeX;
      b.ty = ((float)random(-1000, 1001)) / 1000.0f * rangeY;
      b.next_change_ms = now + 1000 + (uint32_t)random(0, 4000);
    }
    float follow = 0.1f* energy;
    b.px += (b.tx - b.px) * follow;
    b.py += (b.ty - b.py) * follow;
    avatar_.setPosition((int)b.px, (int)b.py);
  }
}
