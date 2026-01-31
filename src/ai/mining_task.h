// src/mining_task.h
#pragma once
#include <Arduino.h>

// マイニング状態を UI に渡すための要約データ。
struct MiningSummary {
  // 合計ハッシュレート [kH/s]
  float    total_kh;

  // 受理/拒否シェア数
  uint32_t accepted;
  uint32_t rejected;

  // 最大 ping [ms]
  float    maxPingMs = 0.0f;

  // 最大 difficulty
  uint32_t maxDifficulty;

  // どこか1つでも接続済みなら true
  bool     anyConnected;

  // Pool API の name
  String   poolName;

  // 40文字ログ用テキスト（1行）
  String   logLine40;

  // Pool 接続の診断メッセージ
  String   poolDiag;

  // 直近の仕事情報（UI表示/診断用）
  uint8_t  workThread     = 255;  // 0/1.., 255=未使用
  uint32_t workNonce      = 0;    // 現在の nonce
  uint32_t workMaxNonce   = 0;    // difficulty*100
  uint32_t workDifficulty = 0;    // 現在の difficulty
  char     workSeed[41]   = {0};  // prev hash 40文字 + '\0'
  char     workHashHex[41]= {0};  // out[20] の hex 40文字 + '\0'

  bool     miningEnabled  = false;
};

// マイニングタスク開始（FreeRTOS）。
void startMiner();

// マイニング状態を取得して UI 用サマリに詰める。
void updateMiningSummary(MiningSummary& out);

// マイニング停止/再開（PoW自体は捨てない）。
void setMiningPaused(bool paused);
bool isMiningPaused();

// ===== Mining control (for "attention mode" etc.) =====
// activeThreads:
//   0 = stop/pause (all miners idle)
//   1 = half (one miner thread)
//   2 = full (default)
// yieldProfile:
//   every N nonces -> vTaskDelay(delay_ms)
//   every は power-of-two 推奨（1024, 256, 64 など）
struct MiningYieldProfile {
  uint16_t every;
  uint8_t  delay_ms;

  // NOTE: PlatformIO(ESP32/Arduino) の toolchain は gnu++11。
  // C++11 では struct の aggregate 初期化を使った return が効かないため、
  // constexpr ctor を用意している。
  constexpr MiningYieldProfile(uint16_t every_ = 1024, uint8_t delay_ms_ = 1)
    : every(every_), delay_ms(delay_ms_) {}
};

void setMiningActiveThreads(uint8_t activeThreads);
uint8_t getMiningActiveThreads();

void setMiningYieldProfile(MiningYieldProfile p);
MiningYieldProfile getMiningYieldProfile();

// Convenience presets
inline MiningYieldProfile MiningYieldNormal() { return MiningYieldProfile(1024, 1); }
inline MiningYieldProfile MiningYieldStrong() { return MiningYieldProfile(64,  3); }
