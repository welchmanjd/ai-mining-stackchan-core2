// Module implementation.
#include "ui/ui_mining_core2.h"

#include <WiFi.h>

#include "config/config.h"
#include "utils/logging.h"
#include "utils/mining_status.h"

static bool isAiStatusBubbleText_(const String& rawText) {
  String t = rawText;
  t.replace("\r", "");
  t.replace("\n", "");
  t.trim();
  if (t == "聞いてるよ") return true;
  if (t == "聞いているよ") return true;
  if (t == String(MC_AI_TEXT_THINKING)) return true;
  return false;
}

static float clampGazeUnit_(float value) {
  if (value > MC_UI_GAZE_UNIT_LIMIT) return MC_UI_GAZE_UNIT_LIMIT;
  if (value < -MC_UI_GAZE_UNIT_LIMIT) return -MC_UI_GAZE_UNIT_LIMIT;
  return value;
}

static float clamp01_(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static uint32_t randomJitterMs_(uint32_t stepMs) {
  return stepMs * (uint32_t)random(0, MC_UI_GAZE_RANDOM_STEPS);
}

static uint32_t computeMovePhaseDurationMs_(int8_t moodLevel) {
  if (moodLevel >= 1) {
    return MC_UI_GAZE_MOVE_HIGH_BASE_MS +
           randomJitterMs_(MC_UI_GAZE_MOVE_HIGH_JITTER_MS);
  }
  if (moodLevel == 0) {
    return MC_UI_GAZE_MOVE_NEUTRAL_BASE_MS +
           randomJitterMs_(MC_UI_GAZE_MOVE_NEUTRAL_JITTER_MS);
  }
  return MC_UI_GAZE_MOVE_LOW_BASE_MS +
         randomJitterMs_(MC_UI_GAZE_MOVE_LOW_JITTER_MS);
}

static uint32_t computeHoldPhaseDurationMs_(int8_t moodLevel) {
  uint32_t holdMs = MC_UI_GAZE_HOLD_NEUTRAL_BASE_MS;
  if (moodLevel >= 1) {
    holdMs = MC_UI_GAZE_HOLD_HIGH_BASE_MS +
             randomJitterMs_(MC_UI_GAZE_HOLD_HIGH_JITTER_MS);
  } else if (moodLevel == 0) {
    holdMs = MC_UI_GAZE_HOLD_NEUTRAL_BASE_MS +
             randomJitterMs_(MC_UI_GAZE_HOLD_NEUTRAL_JITTER_MS);
  } else {
    holdMs = MC_UI_GAZE_HOLD_LOW_BASE_MS +
             randomJitterMs_(MC_UI_GAZE_HOLD_LOW_JITTER_MS);
  }
  if (holdMs < MC_UI_GAZE_HOLD_TIME_MIN_MS) {
    holdMs = MC_UI_GAZE_HOLD_TIME_MIN_MS;
  }
  if (holdMs > MC_UI_GAZE_HOLD_TIME_MAX_MS) {
    holdMs = MC_UI_GAZE_HOLD_TIME_MAX_MS;
  }
  return holdMs;
}
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
    } else if (p.miningEnabled_ && !p.poolAlive_) {
      target = -1;
    } else if (!p.miningEnabled_) {
      target = 0;
    } else if (isMiningPaused()) {
      target = moodLevel_;
    } else {
      int score = 0;
      uint32_t age = lastShareAgeSec();
      if (age <= 60) {
        score += 1;
      } else if (age <= 300) {
        score += 0;
      } else {
        score -= 1;
      }
      uint32_t total = p.accepted_ + p.rejected_;
      if (total >= 10) {
        float success = 100.0f * (float)p.accepted_ / (float)total;
        if (success >= 95.0f) {
          score += 1;
        } else if (success >= 70.0f) {
          score += 0;
        } else {
          score -= 1;
        }
      }
      if (p.hrKh_ >= 40.0f) {
        score += 1;
      } else if (p.hrKh_ >= 20.0f) {
        score += 0;
      } else {
        score -= 1;
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
}
// ===== Avatar liveliness (blink / gaze / breath) =====
namespace {

struct LivelinessMoodParams {
  float energy = 0.9f;
  float eyeOpen = 1.0f;
  float gazeAmp = 1.0f;
};

struct AvatarLivelinessState {
  bool initialized = false;
  float vertical = 0.0f;
  float horizontal = 0.0f;
  float driftV = 0.0f;
  float driftH = 0.0f;
  float phaseV = 0.0f;
  float phaseH = 0.0f;
  uint32_t driftRetargetMs = 900;
  uint32_t lastDriftRetargetMs = 0;
  uint32_t blinkInterval = 2500;
  uint32_t lastBlinkMs = 0;
  bool eyeOpen = true;
  int count = 0;
  uint32_t lastUpdateMs = 0;
  bool gazeMovePhase = true;
  uint32_t gazePhaseStartMs = 0;
  uint32_t gazePhaseDurMs = MC_UI_GAZE_MOVE_PHASE_INIT_MS;
  float servoPublishedX = 0.0f;
  float servoPublishedY = 0.0f;
};

struct AvatarBodyMotionState {
  bool initialized = false;
  float px = 0.0f;
  float py = 0.0f;
  float tx = 0.0f;
  float ty = 0.0f;
  uint32_t nextChangeMs = 0;
};

LivelinessMoodParams computeLivelinessMoodParams_(int8_t moodLevel) {
  LivelinessMoodParams params;
  if (moodLevel >= 2) {
    params.energy = 1.28f;
    params.eyeOpen = 1.0f;
    params.gazeAmp = 1.24f;
  } else if (moodLevel == 1) {
    params.energy = 1.08f;
    params.eyeOpen = 1.0f;
    params.gazeAmp = 1.08f;
  } else if (moodLevel == 0) {
    params.energy = 0.90f;
    params.eyeOpen = 1.0f;
    params.gazeAmp = 0.90f;
  } else if (moodLevel == -1) {
    params.energy = 0.75f;
    params.eyeOpen = 0.88f;
    params.gazeAmp = 0.70f;
  } else {
    params.energy = 0.60f;
    params.eyeOpen = 0.75f;
    params.gazeAmp = 0.55f;
  }
  return params;
}

AvatarLivelinessState& getLivelinessState_() {
  static AvatarLivelinessState s_state;
  return s_state;
}

AvatarBodyMotionState& getBodyMotionState_() {
  static AvatarBodyMotionState s_state;
  return s_state;
}

void initLivelinessStateIfNeeded_(AvatarLivelinessState* s_state, uint32_t now) {
  if (s_state->initialized) return;
  s_state->initialized = true;
  s_state->phaseV = ((float)random(0, MC_UI_GAZE_PHASE_SEED_MAX_MRAD)) / 1000.0f;
  s_state->phaseH = ((float)random(0, MC_UI_GAZE_PHASE_SEED_MAX_MRAD)) / 1000.0f;
  s_state->lastDriftRetargetMs = now;
  s_state->lastBlinkMs = now;
  s_state->lastUpdateMs = now;
  s_state->gazePhaseStartMs = now;
}

float consumeDeltaSec_(AvatarLivelinessState* s_state, uint32_t now, uint32_t* outDtMs) {
  const uint32_t dtMs = now - s_state->lastUpdateMs;
  s_state->lastUpdateMs = now;
  if (outDtMs) *outDtMs = dtMs;
  float dtSec = (float)dtMs / 1000.0f;
  if (dtSec < 0.0f) dtSec = 0.0f;
  if (dtSec > MC_UI_GAZE_DT_MAX_SEC) dtSec = MC_UI_GAZE_DT_MAX_SEC;
  return dtSec;
}

void updateGazePhase_(AvatarLivelinessState* s_state, uint32_t now, int8_t moodLevel) {
  if ((uint32_t)(now - s_state->gazePhaseStartMs) < s_state->gazePhaseDurMs) return;
  s_state->gazeMovePhase = !s_state->gazeMovePhase;
  s_state->gazePhaseStartMs = now;
  if (s_state->gazeMovePhase) {
    s_state->gazePhaseDurMs = computeMovePhaseDurationMs_(moodLevel);
  } else {
    s_state->gazePhaseDurMs = computeHoldPhaseDurationMs_(moodLevel);
  }
}

void retargetDriftIfNeeded_(AvatarLivelinessState* s_state, uint32_t now,
                            float gazeAmp, int8_t moodLevel) {
  if ((now - s_state->lastDriftRetargetMs) <= s_state->driftRetargetMs) return;
  const float maxVel = MC_UI_GAZE_DRIFT_MAX_VEL_BASE * gazeAmp;
  s_state->driftV = (((float)random(-1000, 1001)) / 1000.0f) * maxVel;
  s_state->driftH = (((float)random(-1000, 1001)) / 1000.0f) * maxVel;
  if (moodLevel >= 2) {
    s_state->driftRetargetMs =
        420 + 70 * (uint32_t)random(0, MC_UI_GAZE_RANDOM_STEPS);
  } else if (moodLevel == 1) {
    s_state->driftRetargetMs =
        520 + 90 * (uint32_t)random(0, MC_UI_GAZE_RANDOM_STEPS);
  } else if (moodLevel == 0) {
    s_state->driftRetargetMs =
        620 + 110 * (uint32_t)random(0, MC_UI_GAZE_RANDOM_STEPS);
  } else {
    s_state->driftRetargetMs =
        900 + 140 * (uint32_t)random(0, MC_UI_GAZE_RANDOM_STEPS);
  }
  s_state->lastDriftRetargetMs = now;
}

void updateGazeAndServoTarget_(m5avatar::Avatar* avatar, AvatarLivelinessState* s_state,
                               float dtSec, float energy, float gazeAmp,
                               float* outServoGazeX, float* outServoGazeY) {
  s_state->phaseV += dtSec * (1.2f + 0.2f * energy);
  s_state->phaseH += dtSec * (1.0f + 0.2f * energy);
  const float microScale =
      s_state->gazeMovePhase ? 1.0f : MC_UI_GAZE_HOLD_MICRO_SCALE;
  const float microV = MC_UI_GAZE_MICRO_SWAY_AMPLITUDE * gazeAmp * microScale *
                       sinf(s_state->phaseV);
  const float microH = MC_UI_GAZE_MICRO_SWAY_AMPLITUDE * gazeAmp * microScale *
                       cosf(s_state->phaseH);

  float nextV = s_state->vertical;
  float nextH = s_state->horizontal;
  if (s_state->gazeMovePhase) {
    nextV += s_state->driftV * dtSec;
    nextH += s_state->driftH * dtSec;
  } else {
    s_state->driftV *= 0.90f;
    s_state->driftH *= 0.90f;
  }
  const float centerPull =
      clamp01_(MC_UI_GAZE_CENTER_PULL_PER_SEC * dtSec);
  nextV += (0.0f - nextV) * centerPull;
  nextH += (0.0f - nextH) * centerPull;

  const float limit = 1.0f * gazeAmp;
  if (nextV > limit) {
    nextV = limit;
    s_state->driftV *= -0.55f;
  }
  if (nextV < -limit) {
    nextV = -limit;
    s_state->driftV *= -0.55f;
  }
  if (nextH > limit) {
    nextH = limit;
    s_state->driftH *= -0.55f;
  }
  if (nextH < -limit) {
    nextH = -limit;
    s_state->driftH *= -0.55f;
  }
  s_state->vertical = nextV;
  s_state->horizontal = nextH;

  const float outV = clampGazeUnit_(s_state->vertical + microV);
  const float outH = clampGazeUnit_(s_state->horizontal + microH);
  avatar->setGaze(outV, outH);

#if MC_UI_GAZE_SERVO_HOLD_FREEZE
  if (!s_state->gazeMovePhase) {
    *outServoGazeX = s_state->servoPublishedX;
    *outServoGazeY = s_state->servoPublishedY;
    return;
  }
#endif
  s_state->servoPublishedX = outH;
  s_state->servoPublishedY = outV;
  *outServoGazeX = s_state->servoPublishedX;
  *outServoGazeY = s_state->servoPublishedY;
}

void updateBlink_(m5avatar::Avatar* avatar, AvatarLivelinessState* s_state,
                  uint32_t now, float eyeOpen) {
  if ((now - s_state->lastBlinkMs) <= s_state->blinkInterval) return;
  if (s_state->eyeOpen) {
    avatar->setEyeOpenRatio(0.0f);
    s_state->blinkInterval = 300 + 10 * (uint32_t)random(0, 20);
  } else {
    avatar->setEyeOpenRatio(eyeOpen);
    s_state->blinkInterval = 2500 + 100 * (uint32_t)random(0, 20);
  }
  s_state->eyeOpen = !s_state->eyeOpen;
  s_state->lastBlinkMs = now;
}

void updateBreath_(m5avatar::Avatar* avatar, AvatarLivelinessState* s_state,
                   uint32_t dtMs, float energy) {
  int step = (int)(dtMs / 33);
  if (step < 1) step = 1;
  s_state->count = (s_state->count + step) % 100;
  const float breath = sinf(s_state->count * 2.0f * PI / 100.0f);
  avatar->setBreath(breath * energy);
}

void updateBodyMotion_(m5avatar::Avatar* avatar, uint32_t now, float energy) {
  AvatarBodyMotionState& s_state = getBodyMotionState_();
  if (!s_state.initialized) {
    s_state.initialized = true;
    s_state.nextChangeMs = now + 2000;
  }
  if ((int32_t)(now - s_state.nextChangeMs) >= 0) {
    const float rangeX = 20.0f * energy;
    const float rangeY = 12.0f * energy;
    s_state.tx = ((float)random(-1000, 1001)) / 1000.0f * rangeX;
    s_state.ty = ((float)random(-1000, 1001)) / 1000.0f * rangeY;
    s_state.nextChangeMs = now + 1000 + (uint32_t)random(0, 4000);
  }
  const float follow = 0.1f * energy;
  s_state.px += (s_state.tx - s_state.px) * follow;
  s_state.py += (s_state.ty - s_state.py) * follow;
  avatar->setPosition((int)s_state.px, (int)s_state.py);
}

} // namespace

void UIMining::updateAvatarLiveliness() {
  const uint32_t now = millis();
  const bool bubbleActive = inStackchanMode_ && (stackchanBubbleText_.length() > 0);
  const bool aiStatusBubble =
      bubbleActive && isAiStatusBubbleText_(stackchanBubbleText_);
  const LivelinessMoodParams mood = computeLivelinessMoodParams_(moodLevel_);

  AvatarLivelinessState& s_state = getLivelinessState_();
  initLivelinessStateIfNeeded_(&s_state, now);

  uint32_t dtMs = 0;
  const float dtSec = consumeDeltaSec_(&s_state, now, &dtMs);
  float servoGazeX = 0.0f;
  float servoGazeY = 0.0f;

  if (bubbleActive) {
    avatar_.setGaze(0.0f, 0.0f);
    s_state.servoPublishedX = 0.0f;
    s_state.servoPublishedY = 0.0f;
  } else {
    updateGazePhase_(&s_state, now, moodLevel_);
    retargetDriftIfNeeded_(&s_state, now, mood.gazeAmp, moodLevel_);
    updateGazeAndServoTarget_(&avatar_, &s_state, dtSec, mood.energy, mood.gazeAmp,
                              &servoGazeX, &servoGazeY);
  }

  updateBlink_(&avatar_, &s_state, now, mood.eyeOpen);
  updateBreath_(&avatar_, &s_state, dtMs, mood.energy);
  if (bubbleActive && !aiStatusBubble) {
    const float t = millis() * 0.02f;
    const float mouth = 0.35f + 0.35f * (sinf(t) * 0.5f + 0.5f);
    avatar_.setMouthOpenRatio(mouth);
  } else {
    avatar_.setMouthOpenRatio(0.0f);
  }

  if (inStackchanMode_) {
    updateBodyMotion_(&avatar_, now, mood.energy);
  }

  // Publish gaze target only; actual servo stepping runs in core tick.
  servoDriveGazeX_ = servoGazeX;
  servoDriveGazeY_ = servoGazeY;
  servoDriveActive_ = inStackchanMode_;
}
