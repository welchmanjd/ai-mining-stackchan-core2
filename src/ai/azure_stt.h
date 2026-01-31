#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace AzureStt {

// STT結果（AiTalkController側が扱うのに必要な最小情報）
struct SttResult {
  bool ok = false;     // true: 認識成功（textが有効）
  String text;         // 認識テキスト（呼び出し側で200文字に丸める想定）
  String err;          // ユーザー向け短文エラー（ログやUI向け）
  int status = 0;      // HTTP status（200など）。内部エラーは負数にしてよい。
};

// 制約（指示書のルール）
static constexpr uint32_t kDefaultTimeoutMs = 8000;
static constexpr size_t   kMaxKeepChars     = 200;  // 呼び出し側で丸める
static constexpr size_t   kLogHeadChars     = 60;   // ログに全文を出さない用（.cpp側で遵守）

// 16kHz/16bit/mono PCM を Azure STT に投げて会話文として認識する（同期）
//
// 前提:
//  - pcm: int16 little-endian, mono
//  - sampleRate: 16000 推奨（録音と一致させる）
//  - timeoutMs: HTTP全体の上限（8秒枠）
//
// 成功:
//  - ok=true, textに認識文
//  - status=200
//
// 失敗:
//  - ok=false, errに短文
//  - status=HTTP status または負数
//
SttResult transcribePcm16Mono(
    const int16_t* pcm,
    size_t samples,
    int sampleRate = 16000,
    uint32_t timeoutMs = kDefaultTimeoutMs);

} // namespace AzureStt
