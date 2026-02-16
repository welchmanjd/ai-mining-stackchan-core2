// Module implementation.
#include "ai/mining_task.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha1.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/config.h"
#include "config/runtime_features.h"
#include "utils/logging.h"

static volatile bool g_miningPaused = false;
static volatile bool g_miningBootHold = false;
// Pause flag checked by mining loops to reduce CPU without tearing down
// connections.
void setMiningPaused(bool paused) { g_miningPaused = paused; }
void setMiningBootHold(bool hold) { g_miningBootHold = hold; }
bool isMiningPaused() { return g_miningPaused; }
static inline bool miningPauseActive_() {
  return g_miningPaused || g_miningBootHold;
}
static inline void waitWhilePaused_() {
  // Busy wait with a tiny delay to keep WiFi alive without heavy load.
  while (miningPauseActive_()) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static const uint8_t kDucoMinerThreads = 2;
static const char *kDucoPoolUrl = "https://server.duinocoin.com/getPool";
struct DucoThreadStats {
  bool connected_ = false;
  float hashrateKh_ = 0.0f;
  uint32_t shares_ = 0;
  uint32_t difficulty_ = 0;
  uint32_t accepted_ = 0;
  uint32_t rejected_ = 0;
  float lastPingMs_ = 0.0f;
  bool workValid_ = false;
  uint32_t workNonce_ = 0;
  uint32_t workMaxNonce_ = 0;
  uint32_t workDiff_ = 0;
  uint8_t workOut_[20] = {0};
  char workSeed_[41] = {0};
};
static DucoThreadStats g_thr[kDucoMinerThreads];
static SemaphoreHandle_t g_shaMutex = nullptr;
static portMUX_TYPE g_statsMux = portMUX_INITIALIZER_UNLOCKED;
static char g_nodeName[48] = {0};
static char g_host[64] = {0};
static uint16_t g_port = 0;
static uint32_t g_accAll = 0, g_rejAll = 0;
static char g_status[64] = "boot";
static bool g_anyConnected = false;
static char g_chipId[16] = {0};
static int g_walletId = 0;
static char g_poolDiagText[128] = "";
// ===== mining control knobs (for attention mode etc.) =====
static volatile uint8_t g_miningActiveThreads =
    kDucoMinerThreads;                        // 0..kDucoMinerThreads
static volatile uint16_t g_yieldEvery = 1024; // power-of-two recommended
static volatile uint8_t g_yieldMs = 1;        // delay in ms at yield points
static inline uint16_t normalizePow2_(uint16_t v) {
  // Force to power-of-two for a cheap bitmask in the nonce loop.
  if (v < 8)
    v = 8;
  uint16_t p = 1;
  while ((uint16_t)(p << 1) != 0 && (uint16_t)(p << 1) <= v)
    p <<= 1;
  return p;
}
// Solver abort marker (distinct from "not found")
static const uint32_t kDucoAborted = UINT32_MAX - 1;
// Helper: thread-safe set for g_status
static void setStatus_(const char *s) {
  portENTER_CRITICAL(&g_statsMux);
  strncpy(g_status, s ? s : "", sizeof(g_status) - 1);
  g_status[sizeof(g_status) - 1] = '\0';
  portEXIT_CRITICAL(&g_statsMux);
}
// Helper: thread-safe set for g_poolDiagText
static void setPoolDiag_(const char *s) {
  portENTER_CRITICAL(&g_statsMux);
  strncpy(g_poolDiagText, s ? s : "", sizeof(g_poolDiagText) - 1);
  g_poolDiagText[sizeof(g_poolDiagText) - 1] = '\0';
  portEXIT_CRITICAL(&g_statsMux);
}
static bool ducoGetPool_() {
  WiFiClientSecure s;
  s.setInsecure();
  HTTPClient http;
  http.setTimeout(MC_DUCO_POOL_TIMEOUT_MS);
  if (!http.begin(s, kDucoPoolUrl)) {
    setPoolDiag_("Cannot connect to the pool info server.");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    setPoolDiag_("Pool info server responded with an error.");
    return false;
  }
  String body = http.getString();
  http.end();
  JsonDocument doc; // ArduinoJson v7
  if (deserializeJson(doc, body)) {
    setPoolDiag_("Failed to parse pool info response.");
    return false;
  }
  const char *name = doc["name"] | "";
  const char *ip = doc["ip"] | "";
  uint16_t port = (uint16_t)doc["port"].as<int>();
  portENTER_CRITICAL(&g_statsMux);
  strncpy(g_nodeName, name, sizeof(g_nodeName) - 1);
  g_nodeName[sizeof(g_nodeName) - 1] = '\0';
  strncpy(g_host, ip, sizeof(g_host) - 1);
  g_host[sizeof(g_host) - 1] = '\0';
  g_port = port;
  portEXIT_CRITICAL(&g_statsMux);
  MC_EVT("DUCO", "Pool: %s (%s:%u)", g_nodeName, g_host, (unsigned)g_port);
  if (g_port != 0 && g_host[0]) {
    setPoolDiag_("");
    return true;
  }
  setPoolDiag_("Pool info response is incomplete.");
  return false;
}
static inline int u32ToDec_(char *dst, uint32_t v) {
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
static inline void sha1Calc_(const unsigned char *data, size_t len,
                             unsigned char out[20]) {
#if defined(MBEDTLS_VERSION_NUMBER) && (MBEDTLS_VERSION_NUMBER >= 0x03000000)
  mbedtls_sha1(data, len, out);
#else
  mbedtls_sha1_ret(data, len, out);
#endif
}
static uint32_t ducoSolveDucoS1_(const String &seed,
                                 const unsigned char *expected20,
                                 uint32_t difficulty, uint32_t &hashesDone,
                                 DucoThreadStats *stats) {
  // Tight loop: compute SHA1(seed + nonce) until the hash matches expected20.
  const uint32_t maxNonce = difficulty * 100U;
  hashesDone = 0;
  char buf[96];
  int seedLen = seed.length();
  if (seedLen > (int)sizeof(buf) - 12)
    seedLen = sizeof(buf) - 12;
  memcpy(buf, seed.c_str(), seedLen);
  char *noncePtr = buf + seedLen;
  unsigned char out[20];
  // thread index (0/1..) for control checks
  const int tIdx = (stats) ? int(stats - g_thr) : -1;
  if (tIdx >= 0 && tIdx >= (int)g_miningActiveThreads) {
    return kDucoAborted;
  }
  for (uint32_t nonce = 0; nonce <= maxNonce; ++nonce) {
    // When paused, we yield here and resume from the same nonce (no disconnect
    // / no job drop).
    if (miningPauseActive_()) {
      waitWhilePaused_();
      // If this thread got disabled while paused, abort cleanly.
      if (tIdx >= 0 && tIdx >= (int)g_miningActiveThreads) {
        return kDucoAborted;
      }
    }
    int nlen = u32ToDec_(noncePtr, nonce);
    sha1Calc_((const unsigned char *)buf, seedLen + nlen, out);
    hashesDone++;
    if (memcmp(out, expected20, 20) == 0) {
      // Found a valid share; publish progress atomically.
      if (stats) {
        portENTER_CRITICAL(&g_statsMux);
        stats->workNonce_ = nonce;
        stats->workMaxNonce_ = maxNonce;
        memcpy(stats->workOut_, out, 20);
        stats->workValid_ = true;
        portEXIT_CRITICAL(&g_statsMux);
      }
      return nonce;
    }
    uint16_t every = g_yieldEvery;
    uint32_t mask = (every >= 1) ? (uint32_t)(every - 1) : 0xFFFFFFFFu;
    if ((nonce & mask) == 0) {
      // Periodic progress update and cooperative yield.
      if (stats) {
        portENTER_CRITICAL(&g_statsMux);
        stats->workNonce_ = nonce;
        stats->workMaxNonce_ = maxNonce;
        memcpy(stats->workOut_, out, 20);
        stats->workValid_ = true;
        portEXIT_CRITICAL(&g_statsMux);
      }
      // If this thread got disabled mid-job, abort cleanly.
      if (tIdx >= 0 && tIdx >= (int)g_miningActiveThreads) {
        return kDucoAborted;
      }
      uint8_t dms = g_yieldMs;
      if (dms)
        vTaskDelay(pdMS_TO_TICKS(dms));
    }
  }
  return UINT32_MAX;
}
static void ducoTask_(void *pv) {
  int idx = (int)(intptr_t)pv;
  if (idx < 0 || idx >= kDucoMinerThreads)
    idx = 0;
  auto &me = g_thr[idx];
  char tag[8];
  snprintf(tag, sizeof(tag), "T%d", idx);
  MC_LOGI("DUCO", "miner task start %s", tag);
  for (;;) {
    if (miningPauseActive_()) {
      me.connected_ = false;
      me.hashrateKh_ = 0.0f;
      setStatus_("Mining paused");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    // ----- mining control: idle if this thread is disabled (STOP/HALF) -----
    if (idx >= (int)g_miningActiveThreads) {
      me.connected_ = false;
      me.hashrateKh_ = 0.0f;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    // WiFi
    while (WiFi.status() != WL_CONNECTED) {
      if (miningPauseActive_()) {
        me.connected_ = false;
        me.hashrateKh_ = 0.0f;
        setStatus_("Mining paused");
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      // disabled while waiting for WiFi -> just idle
      if (idx >= (int)g_miningActiveThreads) {
        me.connected_ = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      me.connected_ = false;
      setStatus_("WiFi connecting...");
      setPoolDiag_("Waiting for WiFi connection.");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // Pool discovery / reconnect (guarded to prevent parallel calls)
    if (g_port == 0) {
      if (g_shaMutex)
        xSemaphoreTake(g_shaMutex, portMAX_DELAY);
      const bool needPool = (g_port == 0); // re-check under lock
      bool poolOk = true;
      if (needPool)
        poolOk = ducoGetPool_();
      if (g_shaMutex)
        xSemaphoreGive(g_shaMutex);
      if (!poolOk) {
        vTaskDelay(pdMS_TO_TICKS(MC_DUCO_POOL_RECOVERY_DELAY_MS));
        continue;
      }
    }
    WiFiClient cli;
    cli.setTimeout(MC_DUCO_CLI_TIMEOUT_MS);
    MC_LOGI_RL("duco_connect", 10000, "DUCO", "%s connect %s:%u ...", tag,
               g_host, g_port);
    if (!cli.connect(g_host, g_port)) {
      me.connected_ = false;
      setPoolDiag_("Cannot connect to the pool node.");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    // banner
    unsigned long t0 = millis();
    while (!cli.available() && cli.connected() &&
           millis() - t0 < MC_DUCO_BANNER_TIMEOUT_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!cli.available()) {
      cli.stop();
      setPoolDiag_("Pool node is not responding.");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    String serverVer = cli.readStringUntil('\n');
    serverVer.trim();
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "connected (%s) %s", tag, g_nodeName);
      setStatus_(buf);
    }
    setPoolDiag_("");
    MC_LOGD("DUCO", "%s server version: %s", tag, serverVer.c_str());
    me.connected_ = true;
    // ===== JOB loop =====
    while (cli.connected()) {
      if (miningPauseActive_()) {
        MC_LOGI("DUCO", "%s paused -> disconnect", tag);
        cli.stop();
        me.connected_ = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(100));
        break;
      }
      // disabled mid-connection -> disconnect and go idle
      if (idx >= (int)g_miningActiveThreads) {
        MC_LOGI("DUCO", "%s disabled -> disconnect", tag);
        cli.stop();
        me.connected_ = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
        break;
      }
      // NOTE:
      // Fetch config each job to avoid dangling pointers after SET command
      const auto &jobCfg = appConfig();
      String req = String("JOB,") + jobCfg.ducoUser_ + ",LOW," +
                   jobCfg.ducoMinerKey_ + "\n";
      MC_LOGT("DUCO", "%s send JOB user=%s board=LOW", tag, jobCfg.ducoUser_);
      unsigned long ping0 = millis();
      cli.print(req);
      t0 = millis();
      while (!cli.available() && cli.connected() &&
             millis() - t0 < MC_DUCO_JOB_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (!cli.available()) {
        me.connected_ = false;
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "no job (%s)", tag);
          setStatus_(buf);
        }
        MC_LOGI_RL("duco_no_job", 10000, "DUCO", "%s no job (timeout)", tag);
        setPoolDiag_("No job response from the pool.");
        break;
      }
      me.lastPingMs_ = (float)(millis() - ping0);
      MC_LOGT("DUCO", "%s job ping = %.1f ms", tag, me.lastPingMs_);
      // job: previousHash,expectedHash,difficulty\n
      String prev = cli.readStringUntil(',');
      String expected = cli.readStringUntil(',');
      String diffStr = cli.readStringUntil('\n');
      prev.trim();
      expected.trim();
      diffStr.trim();
      int difficulty = diffStr.toInt();
      if (difficulty <= 0)
        difficulty = 1;
      me.difficulty_ = (uint32_t)difficulty;
      portENTER_CRITICAL(&g_statsMux);
      me.workDiff_ = (uint32_t)difficulty;
      me.workValid_ = false;
      strncpy(me.workSeed_, prev.c_str(), 40);
      me.workSeed_[40] = '\0';
      portEXIT_CRITICAL(&g_statsMux);
      MC_LOGT("DUCO", "%s job diff=%d prev=%s expected=%s", tag, difficulty,
              prev.c_str(), expected.c_str());
      const size_t shaLen = 20;
      unsigned char expBytes[shaLen];
      memset(expBytes, 0, sizeof(expBytes));
      size_t elen = expected.length() / 2;
      const char *ce = expected.c_str();
      auto h = [](char c) -> uint8_t {
        c = toupper((uint8_t)c);
        if (c >= '0' && c <= '9')
          return (uint8_t)(c - '0');
        if (c >= 'A' && c <= 'F')
          return (uint8_t)(c - 'A' + 10);
        return 0;
      };
      for (size_t i = 0, j = 0; j < elen && j < shaLen; i += 2, ++j) {
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
        me.connected_ = false;
        me.hashrateKh_ = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
        break;
      }
      float sec = (micros() - tStart) / 1000000.0f;
      if (sec <= 0)
        sec = 0.001f;
      float hps = hashes / (sec > 0 ? sec : 0.001f);
      MC_LOGT("DUCO", "%s solved nonce=%u hashes=%u time=%.3fs (%.1f H/s)", tag,
              (unsigned)foundNonce, (unsigned)hashes, sec, hps);
      if (foundNonce == UINT32_MAX) {
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "no share (%s)", tag);
          setStatus_(buf);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      me.hashrateKh_ = hps / 1000.0f;
      me.shares_++;
      // Submit: nonce,hashrate,banner ver,rig,DUCOID<chip>,<walletid>\n
      const auto &submitCfg = appConfig();
      String submit = String(foundNonce) + "," + String(hps) + "," +
                      String(submitCfg.ducoBanner_) + " " +
                      submitCfg.appVersion_ + "," + submitCfg.ducoRigName_ +
                      "," + "DUCOID" + String((char *)g_chipId) + "," +
                      String(g_walletId) + "\n";
      cli.print(submit);
      MC_LOGT("DUCO", "%s submit nonce=%u hps=%.1f", tag, (unsigned)foundNonce,
              hps);
      // feedback
      t0 = millis();
      while (!cli.available() && cli.connected() &&
             millis() - t0 < MC_DUCO_FEEDBACK_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (!cli.available()) {
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "no feedback (%s)", tag);
          setStatus_(buf);
        }
        ++me.rejected_;
        ++g_rejAll;
        MC_LOGI_RL("duco_no_feedback", 10000, "DUCO",
                   "%s no feedback (timeout)", tag);
        setPoolDiag_("No result response from the pool.");
        break;
      }
      String fb = cli.readStringUntil('\n');
      fb.trim();
      MC_LOGD("DUCO", "%s feedback: '%s'", tag, fb.c_str());
      bool ok = fb.startsWith("GOOD");
      if (ok) {
        ++me.accepted_;
        ++g_accAll;
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "share GOOD (#%lu, %s)",
                   (unsigned long)me.shares_, tag);
          setStatus_(buf);
        }
        setPoolDiag_("");
      } else {
        ++me.rejected_;
        ++g_rejAll;
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "share BAD (#%lu, %s)",
                   (unsigned long)me.shares_, tag);
          setStatus_(buf);
        }
      }
      MC_LOGI_RL("duco_share_result", 3000, "DUCO", "%s share %s (#%lu)", tag,
                 ok ? "GOOD" : "BAD", (unsigned long)me.shares_);
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
    setStatus_("disabled");
    setPoolDiag_("Mining is disabled (Duco user is empty).");
    return;
  }
  g_shaMutex = xSemaphoreCreateMutex();
  uint64_t chipid = ESP.getEfuseMac();
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf((char *)g_chipId, sizeof(g_chipId), "%04X%08X", chip,
           (uint32_t)chipid);
  randomSeed((uint32_t)millis());
  g_walletId = random(0, 2811);
  WiFi.setSleep(false);
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    g_thr[i] = DucoThreadStats();
  }
  g_accAll = g_rejAll = 0;
  static const char *kTaskNames[] = {"DucoMiner0", "DucoMiner1"};
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    int core = (i == 0) ? 0 : 1;
    UBaseType_t prio = MC_DUCO_TASK_PRIO;
    xTaskCreatePinnedToCore(ducoTask_, kTaskNames[i], MC_DUCO_TASK_STACK_SIZE,
                            (void *)(intptr_t)i, prio, nullptr, core);
  }
}
void updateMiningSummary(MiningSummary &out) {
  const auto features = getRuntimeFeatures();
  float totalKh = 0.0f;
  float maxPing = 0.0f;
  uint32_t acc = 0, rej = 0, diff = 0;
  g_anyConnected = false;

  char statusSnap[64];
  char poolDiagSnap[128];
  char nodeNameSnap[48];

  // Enter critical section BEFORE reading any shared state
  portENTER_CRITICAL(&g_statsMux);

  for (int i = 0; i < kDucoMinerThreads; ++i) {
    totalKh += g_thr[i].hashrateKh_;
    acc += g_thr[i].accepted_;
    rej += g_thr[i].rejected_;
    if (g_thr[i].difficulty_ > diff)
      diff = g_thr[i].difficulty_;
    if (g_thr[i].connected_)
      g_anyConnected = true;
    if (g_thr[i].lastPingMs_ > maxPing) {
      maxPing = g_thr[i].lastPingMs_;
    }
  }

  strncpy(statusSnap, g_status, sizeof(statusSnap));
  statusSnap[sizeof(statusSnap) - 1] = '\0';
  strncpy(poolDiagSnap, g_poolDiagText, sizeof(poolDiagSnap));
  poolDiagSnap[sizeof(poolDiagSnap) - 1] = '\0';
  strncpy(nodeNameSnap, g_nodeName, sizeof(nodeNameSnap));
  nodeNameSnap[sizeof(nodeNameSnap) - 1] = '\0';

  portEXIT_CRITICAL(&g_statsMux);

  out.totalKh_ = totalKh;
  out.accepted_ = acc;
  out.rejected_ = rej;
  out.maxDifficulty_ = diff;
  out.anyConnected_ = g_anyConnected;
  out.poolName_ = String(nodeNameSnap);
  out.maxPingMs_ = maxPing;
  out.miningEnabled_ = features.miningEnabled_;
  char logbuf[64];
  const bool isGood = (strncmp(statusSnap, "share GOOD", 10) == 0);
  const bool isBad = (strncmp(statusSnap, "share BAD", 9) == 0);
  snprintf(logbuf, sizeof(logbuf), "%s A%u R%u HR %.1fkH/s d%u",
           isGood           ? "good "
           : isBad          ? "rej  "
           : g_anyConnected ? "alive"
                            : "dead ",
           (unsigned)acc, (unsigned)rej, totalKh, (unsigned)diff);
  out.logLine40_ = String(logbuf);
  out.poolDiag_ = String(poolDiagSnap);
  auto hexDigit = [](uint8_t v) -> char {
    return (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
  };
  int wiConnected = -1;
  int wiAny = -1;
  for (int i = 0; i < kDucoMinerThreads; ++i) {
    if (g_thr[i].workValid_) {
      if (wiAny < 0)
        wiAny = i;
      if (g_thr[i].connected_ && wiConnected < 0)
        wiConnected = i;
    }
  }
  int wi = (wiConnected >= 0) ? wiConnected : wiAny;
  if (wi >= 0) {
    uint8_t out20[20];
    char seed40[41];
    uint32_t nonce = 0, maxNonce = 0, diffv = 0;
    portENTER_CRITICAL(&g_statsMux);
    nonce = g_thr[wi].workNonce_;
    maxNonce = g_thr[wi].workMaxNonce_;
    diffv = g_thr[wi].workDiff_;
    memcpy(out20, g_thr[wi].workOut_, 20);
    strncpy(seed40, g_thr[wi].workSeed_, 40);
    seed40[40] = '\0';
    portEXIT_CRITICAL(&g_statsMux);
    out.workThread_ = (uint8_t)wi;
    out.workNonce_ = nonce;
    out.workMaxNonce_ = maxNonce;
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
  if (activeThreads > kDucoMinerThreads)
    activeThreads = kDucoMinerThreads;
  g_miningActiveThreads = activeThreads;
}
uint8_t getMiningActiveThreads() { return g_miningActiveThreads; }
void setMiningYieldProfile(MiningYieldProfile p) {
  // normalize 'every' to power-of-two (fast bitmask check)
  p.every_ = normalizePow2_(p.every_);
  g_yieldEvery = p.every_;
  g_yieldMs = p.delayMs_;
}
MiningYieldProfile getMiningYieldProfile() {
  MiningYieldProfile p;
  p.every_ = g_yieldEvery;
  p.delayMs_ = g_yieldMs;
  return p;
}
