// Module implementation.
#include "ai/openai_llm.h"
#include "core/logging.h"
#include "config/config_private.h"
#include "utils/mc_text_utils.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config/config.h"
#ifndef MC_OPENAI_ENDPOINT
#define MC_OPENAI_ENDPOINT "https://api.openai.com/v1/responses"
#endif
#ifndef MC_OPENAI_MODEL
#define MC_OPENAI_MODEL "gpt-5-nano"
#endif
// ---- small helpers ----
// Step2: moved UTF-8 clamp / one-line sanitize into mc_text_utils.*
static String buildDiag_(JsonVariant root) {
  // Create a compact, log-friendly summary of the response payload.
  String d;
  if (root.isNull()) return "null_root";
  const char* status = nullptr;
  if (root["status"].is<const char*>()) {
    status = root["status"];
    d += "status=";
    d += status;
    d += " ";
  }
  if (root["incomplete_details"].is<JsonObject>() &&
      root["incomplete_details"]["reason"].is<const char*>()) {
    d += "inc=";
    d += (const char*)root["incomplete_details"]["reason"];
    d += " ";
  }
  if (!root["error"].isNull()) {
    d += "has_error ";
    if (root["error"]["message"].is<const char*>()) {
      String msg = (const char*)root["error"]["message"];
      msg = mcLogHead(msg, MC_AI_LOG_HEAD_BYTES_LLM_ERRMSG_SHORT);
      d += "err=" + msg + " ";
    }
  }
  JsonVariant out = root["output"];
  if (!out.is<JsonArray>()) {
    d += "no_output_array";
    return mcLogHead(d, MC_AI_LOG_HEAD_BYTES_LLM_DIAG);
  }
  JsonArray a = out.as<JsonArray>();
  d += "outN=" + String(a.size()) + " ";
  int n = 0;
  for (JsonVariant it : a) {
    if (n >= 3) break;
    const char* t = it["type"].is<const char*>() ? (const char*)it["type"] : "?";
    d += "t" + String(n) + "=" + String(t);
    JsonVariant c = it["content"];
    if (c.is<JsonArray>()) {
      JsonArray ca = c.as<JsonArray>();
      d += "(cN=" + String(ca.size());
      if (ca.size() > 0) {
        const char* ct = ca[0]["type"].is<const char*>() ? (const char*)ca[0]["type"] : "?";
        d += ",c0=" + String(ct);
        if (String(ct) == "output_text") {
          if (!ca[0]["text"].is<const char*>()) d += ",text_not_string";
        }
      }
      d += ")";
    }
    d += " ";
    n++;
  }
  return mcLogHead(d, MC_AI_LOG_HEAD_BYTES_LLM_DIAG);
}
static String extractAnyText_(JsonVariant root) {
  // Accept multiple response shapes (output_text or output[].content[]).
  if (root["output_text"].is<const char*>()) {
    String s = (const char*)root["output_text"];
    s = mcSanitizeOneLine(s);
    if (s.length() > 0) return s;
  }
  String acc;
  JsonVariant out = root["output"];
  if (out.is<JsonArray>()) {
    for (JsonVariant item : out.as<JsonArray>()) {
      if (item["type"].is<const char*>() && String((const char*)item["type"]) == "output_text") {
        if (item["text"].is<const char*>()) {
          String s = (const char*)item["text"];
          s = mcSanitizeOneLine(s);
          if (s.length()) {
            if (acc.length()) acc += "\n";
            acc += s;
          }
        }
      }
      JsonVariant content = item["content"];
      if (!content.is<JsonArray>()) continue;
      for (JsonVariant part : content.as<JsonArray>()) {
        const char* ptype = part["type"] | "";
        if (!ptype || !ptype[0]) continue;
        if (strcmp(ptype, "output_text") == 0) {
          if (part["text"].is<const char*>()) {
            String s = (const char*)part["text"];
            s = mcSanitizeOneLine(s);
            if (s.length()) {
              if (acc.length()) acc += "\n";
              acc += s;
            }
          } else if (part["text"].is<JsonObject>() && part["text"]["value"].is<const char*>()) {
            String s = (const char*)part["text"]["value"];
            s = mcSanitizeOneLine(s);
            if (s.length()) {
              if (acc.length()) acc += "\n";
              acc += s;
            }
          }
        } else if (strcmp(ptype, "refusal") == 0) {
          if (part["refusal"].is<const char*>()) {
            String s = (const char*)part["refusal"];
            s = mcSanitizeOneLine(s);
            if (s.length()) {
              if (acc.length()) acc += "\n";
              acc += s;
            }
          }
        }
      }
    }
  }
  acc = mcSanitizeOneLine(acc);
  return acc;
}
namespace openai_llm {
LlmResult generateReply(const String& userText, uint32_t timeoutMs) {
  LlmResult r;
  const uint32_t t0 = millis();
  if (timeoutMs < 200) timeoutMs = 200;
  MC_EVT_D("LLM", "start timeout=%lums in_len=%u",
           (unsigned long)timeoutMs, (unsigned)userText.length());
  // ---- request build ----
  // Keep instructions short to reduce token use and response latency.
  DynamicJsonDocument req(2048);
  req["model"] = MC_OPENAI_MODEL;
  req["instructions"] =
      "あなたはスタックチャンの会話AIです。日本語で短く答えてください。"
      "返答は120文字以内。箇条書き禁止。1〜2文。"
      "相手が『聞こえる？』等の確認なら、明るく短く返してください。";
  req["input"] = userText;
  req["reasoning"]["effort"] = MC_OPENAI_REASONING_EFFORT;
  req["max_output_tokens"] = (int)MC_OPENAI_MAX_OUTPUT_TOKENS;
  req["text"]["format"]["type"] = "text";
  String payload;
  serializeJson(req, payload);
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(timeoutMs);
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs);
  const char* url = MC_OPENAI_ENDPOINT;
  if (!http.begin(client, url)) {
    r.ok_ = false;
    r.err_ = "http_begin_failed";
    r.tookMs_ = millis() - t0;
    MC_EVT("LLM", "fail stage=begin took=%lums", (unsigned long)r.tookMs_);
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("Authorization", String("Bearer ") + String(MC_OPENAI_API_KEY));
  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  r.http_ = code;
  String body;
  if (code > 0) {
    body = http.getString();
  }
  http.end();
  r.tookMs_ = millis() - t0;
  if (code <= 0) {
    r.ok_ = false;
    r.err_ = "http_post_failed";
    MC_EVT("LLM", "fail stage=http_post code=%d took=%lums",
           code, (unsigned long)r.tookMs_);
    return r;
  }
 if (code < 200 || code >= 300) {
  // HTTP error response: try to extract a short error message for logs.
  DynamicJsonDocument doc(4096);
  DeserializationError e = deserializeJson(doc, body);
  String msg = "";
  if (!e && doc["error"]["message"].is<const char*>()) {
    msg = (const char*)doc["error"]["message"];
    msg = mcLogHead(msg, MC_AI_LOG_HEAD_BYTES_LLM_HTTP_ERRMSG);
  }
  r.err_ = "http_" + String(code);
  r.ok_ = false;
  if (msg.length()) {
    MC_LOGD("LLM", "http=%d err_message=%s", code, msg.c_str());
  } else {
    MC_LOGD("LLM", "http=%d", code);
  }
  return r;
}
  // ---- parse + extract ----
  DynamicJsonDocument doc(24576);
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    r.ok_ = false;
    r.err_ = String("json_parse_failed:") + e.c_str();
    MC_EVT("LLM", "fail stage=json_parse took=%lums body_len=%u",
           (unsigned long)r.tookMs_, (unsigned)body.length());
    return r;
  }
  if (doc["status"].is<const char*>()) {
    r.status_ = (const char*)doc["status"];
  }
  if (doc["incomplete_details"].is<JsonObject>() &&
      doc["incomplete_details"]["reason"].is<const char*>()) {
    r.incompleteReason_ = (const char*)doc["incomplete_details"]["reason"];
  }
  if (doc["usage"].is<JsonObject>()) {
    // Token accounting is optional; log only if present.
    JsonObject u = doc["usage"].as<JsonObject>();
    r.inTok_    = u["input_tokens"] | 0;
    r.outTok_   = u["output_tokens"] | 0;
    r.totalTok_ = u["total_tokens"] | 0;
    if (u["input_tokens_details"].is<JsonObject>()) {
      r.cachedTok_ = u["input_tokens_details"]["cached_tokens"] | 0;
    }
    if (u["output_tokens_details"].is<JsonObject>()) {
      r.reasoningTok_ = u["output_tokens_details"]["reasoning_tokens"] | 0;
    }
#if MC_OPENAI_LOG_USAGE
    int rPct = 0;
    if (r.outTok_ > 0) rPct = (r.reasoningTok_ * 100) / r.outTok_;
    MC_LOGD("LLM", "usage tot=%d in=%d out=%d r=%d(%d%%) cache=%d status=%s inc=%s",
            r.totalTok_, r.inTok_, r.outTok_, r.reasoningTok_, rPct, r.cachedTok_,
            r.status_.length() ? r.status_.c_str() : "-",
            r.incompleteReason_.length() ? r.incompleteReason_.c_str() : "-");
#endif
  }
  String out = extractAnyText_(doc.as<JsonVariant>());
  out = mcSanitizeOneLine(out);
  if (out.length() == 0) {
    String diag = buildDiag_(doc.as<JsonVariant>());
    MC_LOGW("LLM", "empty_output http=%d took=%lums body_len=%u",
            r.http_, (unsigned long)r.tookMs_, (unsigned)body.length());
    MC_LOGD("LLM", "empty_output diag=%s", diag.c_str());
    r.ok_ = false;
    r.err_ = "empty_output";
    return r;
  }
  r.ok_ = true;
  r.text_ = out;
  MC_EVT_D("LLM", "done http=%d took=%lums out_len=%u tok=%d",
           r.http_, (unsigned long)r.tookMs_, (unsigned)r.text_.length(), r.totalTok_);
  return r;
}
} // namespace openai_llm
