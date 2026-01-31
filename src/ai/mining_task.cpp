// src/mining_task.cpp
#include "ai/mining_task.h"
#include "config/config.h"
#include "core/logging.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/sha1.h>
#include "core/runtime_features.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static volatile bool g_miningPaused = false;
void setMiningPaused(bool paused) {
  g_miningPaused = paused;
}
bool isMiningPaused() {
  return g_miningPaused;
}
static inline void waitWhilePaused_() {
  while (g_miningPaused) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
static const uint8_t kDucoMinerThreads = 2;
static const char*   kDucoPoolUrl      = "https://server.duinocoin.com/getPool";
struct DucoThreadStats {
  bool     connected_    = false;
  float    hashrateKh_  = 0.0f;
  uint32_t shares_       = 0;
  uint32_t difficulty_   = 0;
  uint32_t accepted_     = 0;
  uint32_t rejected_     = 0;
  float    lastPingMs_ = 0.0f;
  bool     workValid_      = false;
  uint32_t workNonce_      = 0;
  uint32_t workMaxNonce_  = 0;
  uint32_t workDiff_       = 0;
  uint8_t  workOut_[20]    = {0};
  char     workSeed_[41]   = {0};
};
static DucoThreadStats   g_thr[kDucoMinerThreads];
static SemaphoreHandle_t g_shaMutex = nullptr;
static portMUX_TYPE g_statsMux = portMUX_INITIALIZER_UNLOCKED;
static String   g_node_name;
static String   g_host;
static uint16_t g_port = 0;
static uint32_t g_acc_all = 0, g_rej_all = 0;
static String   g_status = "boot";
static bool     g_any_connected = false;
static char     g_chip_id[16] = {0};
static int      g_walletid = 0;
static String   g_poolDiagText = "";
// ===== mining control knobs (for attention mode etc.) =====
static volatile uint8_t  g_mining_active_threads = kDucoMinerThreads; // 0..kDucoMinerThreads
static volatile uint16_t g_yield_every = 1024;   // power-of-two recommended
static volatile uint8_t  g_yield_ms    = 1;      // delay in ms at yield points
static inline uint16_t normalizePow2_(uint16_t v) {
  if (v < 8) v = 8;
  uint16_t p = 1;
  while ((uint16_t)(p << 1) != 0 && (uint16_t)(p << 1) <= v) p <<= 1;
  return p;
}
// Solver abort marker (distinct from "not found")
static const uint32_t kDucoAborted = UINT32_MAX - 1;
// === src/mining_task.cpp : replace whole function ===
static bool ducoGetPool_() {
  WiFiClientSecure s;
  s.setInsecure();
  HTTPClient http;
  http.setTimeout(7000);
  if (!http.begin(s, kDucoPoolUrl)) {
    g_poolDiagText = "Cannot connect to the pool info server.";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    g_poolDiagText = "Pool info server responded with an error.";
    return false;
  }
  String body = http.getString();
  http.end();
  JsonDocument doc;  // ArduinoJson v7
  if (deserializeJson(doc, body)) {
    g_poolDiagText = "Failed to parse pool info response.";
    return false;
  }
  g_node_name = doc["name"].as<String>();
  g_host      = doc["ip"].as<String>();
  g_port      = (uint16_t)doc["port"].as<int>();
  MC_EVT("DUCO", "Pool: %s (%s:%u)",
         g_node_name.c_str(),
         g_host.c_str(), (unsigned)g_port);
  if (g_port != 0 && g_host.length()) {
    g_poolDiagText = "";
    return true;
  }
  g_poolDiagText = "Pool info response is incomplete.";
  return false;
}
static inline int u32ToDec_(char* dst, uint32_t v) {
  if (v == 0) {
    dst[0] = '0';
    return 1;
  }
  char tmp[10];
  int n = 0;
  while (v) {
    tmp[n++] = char('0' + (v % 10));
    v /= 10;
  }
  for (int i = 0; i < n; ++i) {
    dst[i] = tmp[n - 1 - i];
  }
  return n;
}
// ---------- SHA1 helper (mbedTLS) ----------
static inline void sha1Calc_(const unsigned char* data,
                             size_t len,
                             unsigned char out[20]) {
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x03000000)
  mbedtls_sha1(data, len, out);
#else
  mbedtls_sha1_ret(data, len, out);
#endif
}
static uint32_t ducoSolveDucoS1_(const String& seed,
                                  const unsigned char* expected20,
                                  uint32_t difficulty,
                                  uint32_t& hashes_done,
                                  DucoThreadStats* stats) {
  const uint32_t maxNonce = difficulty * 100U;
  hashes_done = 0;
  char buf[96];
  int seed_len = seed.length();
  if (seed_len > (int)sizeof(buf) - 12) seed_len = sizeof(buf) - 12;
  memcpy(buf, seed.c_str(), seed_len);
  char* nonce_ptr = buf + seed_len;
  unsigned char out[20];
// thread index (0/1..) for control checks
const int tidx = (stats) ? int(stats - g_thr) : -1;
if (tidx >= 0 && tidx >= (int)g_mining_active_threads) {
  return kDucoAborted;
}
  for (uint32_t nonce = 0; nonce <= maxNonce; ++nonce) {
    // When paused, we yield here and resume from the same nonce (no disconnect / no job drop).
    if (g_miningPaused) {
      waitWhilePaused_();
      // If this thread got disabled while paused, abort cleanly.
      if (tidx >= 0 && tidx >= (int)g_mining_active_threads) {
        return kDucoAborted;
      }
    }
    int nlen = u32ToDec_(nonce_ptr, nonce);
    sha1Calc_((const unsigned char*)buf, seed_len + nlen, out);
    hashes_done++;
    if (memcmp(out, expected20, 20) == 0) {
      if (stats) {
        portENTER_CRITICAL(&g_statsMux);
        stats->workNonce_     = nonce;
        stats->workMaxNonce_ = maxNonce;
        memcpy(stats->workOut_, out, 20);
        stats->workValid_ = true;
        portEXIT_CRITICAL(&g_statsMux);
      }
      return nonce;
    }
    uint16_t every = g_yield_every;
    uint32_t mask  = (every >= 1) ? (uint32_t)(every - 1) : 0xFFFFFFFFu;
    if ((nonce & mask) == 0) {
      if (stats) {
        portENTER_CRITICAL(&g_statsMux);
        stats->workNonce_     = nonce;
        stats->workMaxNonce_ = maxNonce;
        memcpy(stats->workOut_, out, 20);
        stats->workValid_ = true;
        portEXIT_CRITICAL(&g_statsMux);
      }
      // If this thread got disabled mid-job, abort cleanly.
      if (tidx >= 0 && tidx >= (int)g_mining_active_threads) {
        return kDucoAborted;
      }
      uint8_t dms = g_yield_ms;
      if (dms) vTaskDelay(pdMS_TO_TICKS(dms));
    }
  }
  return UINT32_MAX;
}
// === src/mining_task.cpp : replace whole function ===
static void ducoTask_(void* pv) {
  int idx = (int)(intptr_t)pv;
  if (idx < 0 || idx >= kDucoMinerThreads) idx = 0;
  auto& me = g_thr[idx];
  char tag[8];
  snprintf(tag, sizeof(tag), "T%d", idx);
  MC_LOGI("DUCO", "miner task start %s", tag);
  const auto& cfg = appConfig();
  for (;;) {
    // ----- mining control: idle if this thread is disabled (STOP/HALF) -----
    if (idx >= (int)g_mining_active_threads) {
      me.connected_   = false;
      me.hashrateKh_ = 0.0f;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    // WiFi
    while (WiFi.status() != WL_CONNECTED) {
      // disabled while waiting for WiFi -> just idle
      if (idx >= (int)g_mining_active_threads) {
        me.connected_   = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      me.connected_ = false;
      g_status = "WiFi connecting...";
      g_poolDiagText = "Waiting for WiFi connection.";
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // Pool
    if (g_port == 0) {
      if (!ducoGetPool_()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
    }
    WiFiClient cli;
    cli.setTimeout(15);
    MC_LOGI_RL("duco_connect", 10000, "DUCO",
               "%s connect %s:%u ...",
               tag, g_host.c_str(), g_port);
    if (!cli.connect(g_host.c_str(), g_port)) {
      me.connected_ = false;
      g_poolDiagText = "Cannot connect to the pool node.";
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    // banner
    unsigned long t0 = millis();
    while (!cli.available() && cli.connected() && millis() - t0 < 5000) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!cli.available()) {
      cli.stop();
      g_poolDiagText = "Pool node is not responding.";
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    String serverVer = cli.readStringUntil('\n');
    g_status       = String("connected (") + tag + ") " + g_node_name;
    serverVer.trim();
    g_poolDiagText = "";
    MC_LOGD("DUCO", "%s server version: %s", tag, serverVer.c_str());
    me.connected_ = true;
    g_status = String("connected (") + tag + ") " + g_node_name;
    // ===== JOB loop =====
    while (cli.connected()) {
      // disabled mid-connection -> disconnect and go idle
      if (idx >= (int)g_mining_active_threads) {
        MC_LOGI("DUCO", "%s disabled -> disconnect", tag);
        cli.stop();
        me.connected_   = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
        break;
      }
      // NOTE:
      String req = String("JOB,") + cfg.ducoUser_ + ",LOW," +
                  cfg.ducoMinerKey_ + "\n";
      MC_LOGT("DUCO", "%s send JOB user=%s board=LOW", tag, cfg.ducoUser_);
      unsigned long ping0 = millis();
      cli.print(req);
      t0 = millis();
      while (!cli.available() && cli.connected() && millis() - t0 < 10000) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (!cli.available()) {
        me.connected_ = false;
        g_status = String("no job (") + tag + ")";
        MC_LOGI_RL("duco_no_job", 10000, "DUCO",
                   "%s no job (timeout)", tag);
        g_poolDiagText = "No job response from the pool.";
        break;
      }
      me.lastPingMs_ = (float)(millis() - ping0);
      MC_LOGT("DUCO", "%s job ping = %.1f ms", tag, me.lastPingMs_);
      // job: previousHash,expectedHash,difficulty\n
      String prev     = cli.readStringUntil(',');
      String expected = cli.readStringUntil(',');
      String diffStr  = cli.readStringUntil('\n');
      prev.trim();
      expected.trim();
      diffStr.trim();
      int difficulty = diffStr.toInt();
      if (difficulty <= 0) difficulty = 1;
      me.difficulty_ = (uint32_t)difficulty;
      portENTER_CRITICAL(&g_statsMux);
      me.workDiff_ = (uint32_t)difficulty;
      me.workValid_ = false;
      strncpy(me.workSeed_, prev.c_str(), 40);
      me.workSeed_[40] = '\0';
      portEXIT_CRITICAL(&g_statsMux);
      MC_LOGT("DUCO", "%s job diff=%d prev=%s expected=%s",
              tag, difficulty, prev.c_str(), expected.c_str());
      const size_t SHA_LEN = 20;
      unsigned char expBytes[SHA_LEN];
      memset(expBytes, 0, sizeof(expBytes));
      size_t elen = expected.length() / 2;
      const char* ce = expected.c_str();
      auto h = [](char c) -> uint8_t {
        c = toupper((uint8_t)c);
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
        return 0;
      };
      for (size_t i = 0, j = 0; j < elen && j < SHA_LEN; i += 2, ++j) {
        expBytes[j] = (h(ce[i]) << 4) | h(ce[i + 1]);
      }
      // solve
      uint32_t hashes = 0;
      unsigned long tStart = micros();
      uint32_t foundNonce =
          ducoSolveDucoS1_(prev, expBytes, (uint32_t)difficulty, hashes, &me);
      if (foundNonce == kDucoAborted) {
        // mining control requested to stop this thread
        MC_EVT("DUCO", "%s job aborted by control", tag);
        cli.stop();
        me.connected_   = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
        break;
      }
      float sec = (micros() - tStart) / 1000000.0f;
      if (sec <= 0) sec = 0.001f;
      float hps = hashes / (sec > 0 ? sec : 0.001f);
      MC_LOGT("DUCO", "%s solved nonce=%u hashes=%u time=%.3fs (%.1f H/s)",
              tag,
              (unsigned)foundNonce,
              (unsigned)hashes,
              sec,
              hps);
      if (foundNonce == UINT32_MAX) {
        g_status = String("no share (") + tag + ")";
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      me.hashrateKh_ = hps / 1000.0f;
      me.shares_++;
      // Submit: nonce,hashrate,banner ver,rig,DUCOID<chip>,<walletid>\n
      String submit =
          String(foundNonce) + "," + String(hps) + "," +
          String(cfg.ducoBanner_) + " " + cfg.appVersion_ + "," +
          cfg.ducoRigName_ + "," +
          "DUCOID" + String((char*)g_chip_id) + "," +
          String(g_walletid) + "\n";
      cli.print(submit);
      MC_LOGT("DUCO", "%s submit nonce=%u hps=%.1f",
              tag, (unsigned)foundNonce, hps);
      // feedback
      t0 = millis();
      while (!cli.available() && cli.connected() && millis() - t0 < 10000) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (!cli.available()) {
        g_status = String("no feedback (") + tag + ")";
        ++me.rejected_;
        ++g_rej_all;
        MC_LOGI_RL("duco_no_feedback", 10000, "DUCO",
                   "%s no feedback (timeout)", tag);
        g_poolDiagText = "No result response from the pool.";
        break;
      }
      String fb = cli.readStringUntil('\n');
      fb.trim();
      MC_LOGD("DUCO", "%s feedback: '%s'", tag, fb.c_str());
      bool ok = fb.startsWith("GOOD");
      if (ok) {
        ++me.accepted_;
        ++g_acc_all;
        g_status = String("share GOOD (#") + String(me.shares_) +
                   ", " + tag + ")";
        g_poolDiagText = "";
      } else {
        ++me.rejected_;
        ++g_rej_all;
        g_status = String("share BAD (#") + String(me.shares_) +
                   ", " + tag + ")";
      }
      MC_LOGI_RL("duco_share_result", 3000, "DUCO",
                 "%s share %s (#%lu)",
                 tag, ok ? "GOOD" : "BAD", (unsigned long)me.shares_);
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    cli.stop();
    me.connected_ = false;
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
void startMiner() {
  const auto features = getRuntimeFeatures();
  if (!features.miningEnabled_) {
    g_status = "disabled";
    g_poolDiagText = "Mining is disabled (Duco user is empty).";
    return;
  }
  g_shaMutex = xSemaphoreCreateMutex();
  uint64_t chipid = ESP.getEfuseMac();
  uint16_t chip   = (uint16_t)(chipid >> 32);
  snprintf((char*)g_chip_id, sizeof(g_chip_id),
           "%04X%08X", chip, (uint32_t)chipid);
  randomSeed((uint32_t)millis());
  g_walletid = random(0, 2811);
  WiFi.setSleep(false);
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    g_thr[i] = DucoThreadStats();
  }
  g_acc_all = g_rej_all = 0;
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    int core = (i == 0) ? 0 : 1;
    UBaseType_t prio = 1;
    String name = String("DucoMiner") + String(i);
    xTaskCreatePinnedToCore(ducoTask_,
                            name.c_str(),
                            8192,
                            (void*)(intptr_t)i,
                            prio,
                            nullptr,
                            core);
  }
}
void updateMiningSummary(MiningSummary& out) {
  const auto features = getRuntimeFeatures();
  float    totalKh = 0.0f;
  float    maxPing  = 0.0f;
  uint32_t acc = 0, rej = 0, diff = 0;
  g_any_connected = false;
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    totalKh += g_thr[i].hashrateKh_;
    acc      += g_thr[i].accepted_;
    rej      += g_thr[i].rejected_;
    if (g_thr[i].difficulty_ > diff) diff = g_thr[i].difficulty_;
    if (g_thr[i].connected_) g_any_connected = true;
    if (g_thr[i].lastPingMs_ > maxPing) {
      maxPing = g_thr[i].lastPingMs_;
    }
  }
  out.totalKh_      = totalKh;
  out.accepted_      = acc;
  out.rejected_      = rej;
  out.maxDifficulty_ = diff;
  out.anyConnected_  = g_any_connected;
  out.poolName_      = g_node_name;
  out.maxPingMs_     = maxPing;
  out.miningEnabled_ = features.miningEnabled_;
  char logbuf[64];
  snprintf(logbuf, sizeof(logbuf),
           "%s A%u R%u HR %.1fkH/s d%u",
           g_status.startsWith("share GOOD") ? "good " :
           g_status.startsWith("share BAD")  ? "rej  " :
           g_any_connected ? "alive" : "dead ",
           (unsigned)acc, (unsigned)rej, totalKh, (unsigned)diff);
  out.logLine40_ = String(logbuf);
  out.poolDiag_ = g_poolDiagText;
  auto hexDigit = [](uint8_t v) -> char {
    return (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
  };
  int wi_connected = -1;
  int wi_any = -1;
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    if (g_thr[i].workValid_) {
      if (wi_any < 0) wi_any = i;
      if (g_thr[i].connected_ && wi_connected < 0) wi_connected = i;
    }
  }
  int wi = (wi_connected >= 0) ? wi_connected : wi_any;
  if (wi >= 0) {
    uint8_t out20[20];
    char seed40[41];
    uint32_t nonce = 0, maxNonce = 0, diffv = 0;
    portENTER_CRITICAL(&g_statsMux);
    nonce   = g_thr[wi].workNonce_;
    maxNonce= g_thr[wi].workMaxNonce_;
    diffv   = g_thr[wi].workDiff_;
    memcpy(out20, g_thr[wi].workOut_, 20);
    strncpy(seed40, g_thr[wi].workSeed_, 40);
    seed40[40] = '\0';
    portEXIT_CRITICAL(&g_statsMux);
    out.workThread_     = (uint8_t)wi;
    out.workNonce_      = nonce;
    out.workMaxNonce_   = maxNonce;
    out.workDifficulty_ = diffv;
    strncpy(out.workSeed_, seed40, 40);
    out.workSeed_[40] = '\0';
    for (int j = 0; j < 20; ++j) {
      out.workHashHex_[j * 2 + 0] = hexDigit((out20[j] >> 4) & 0x0F);
      out.workHashHex_[j * 2 + 1] = hexDigit(out20[j] & 0x0F);
    }
    out.workHashHex_[40] = '\0';
  } else {
    out.workThread_ = 255;
    out.workNonce_ = out.workMaxNonce_ = out.workDifficulty_ = 0;
    out.workSeed_[0] = '\0';
    out.workHashHex_[0] = '\0';
  }
}
// ===== Mining control API (public) =====
void setMiningActiveThreads(uint8_t activeThreads) {
  if (activeThreads > kDucoMinerThreads) activeThreads = kDucoMinerThreads;
  g_mining_active_threads = activeThreads;
}
uint8_t getMiningActiveThreads() {
  return g_mining_active_threads;
}
void setMiningYieldProfile(MiningYieldProfile p) {
  // normalize 'every' to power-of-two (fast bitmask check)
  p.every_ = normalizePow2_(p.every_);
  g_yield_every = p.every_;
  g_yield_ms    = p.delayMs_;
}
MiningYieldProfile getMiningYieldProfile() {
  MiningYieldProfile p;
  p.every_    = g_yield_every;
  p.delayMs_ = g_yield_ms;
  return p;
}
