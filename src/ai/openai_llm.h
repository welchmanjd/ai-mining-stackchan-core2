// Module implementation.
#pragma once
#include <Arduino.h>
struct LlmResult {
  bool ok_ = false;
  String text_;
  String err_;
  uint32_t tookMs_ = 0;
  int http_ = 0;
  String status_;
  String incompleteReason_;
  int inTok_ = 0;
  int outTok_ = 0;
  int totalTok_ = 0;
  int cachedTok_ = 0;
  int reasoningTok_ = 0; // output_tokens_details.reasoning_tokens
};
struct OpenAiProbeResult {
  bool ok_ = false;
  int http_ = 0;
  String err_;
  uint32_t tookMs_ = 0;
};
namespace openai_llm {
  LlmResult generateReply(const String& userText, uint32_t timeoutMs);
  OpenAiProbeResult probeConnection(uint32_t timeoutMs);
}
