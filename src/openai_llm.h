#pragma once
#include <Arduino.h>

struct LlmResult {
  bool ok = false;
  String text;
  String err;     // secret禁止。本文/入力の全文も入れない（概要のみ）
  int http = 0;
  uint32_t tookMs = 0;

  String status;           // "completed" / "incomplete" など（短い文字列）
  String incompleteReason; // "max_output_tokens" 等（短い文字列）

  // usage（本文を出さずに数字だけ観測する）
  int inTok = 0;
  int outTok = 0;
  int totalTok = 0;
  int cachedTok = 0;
  int reasoningTok = 0; // output_tokens_details.reasoning_tokens
};


namespace OpenAiLlm {
  // userText は呼び出し側で 200文字(目安)以内に丸めておく想定
  LlmResult generateReply(const String& userText, uint32_t timeoutMs);
}
