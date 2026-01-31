// Module implementation.
#pragma once
// mc_text_utils: small, side-effect-free String helpers
// - UTF-8 safe byte clamping (do not cut in the middle of a multi-byte sequence)
// - One-line sanitization for logs/UI (preserve Japanese/emoji)
#include <Arduino.h>
// Clamp to max_bytes without breaking UTF-8 sequences.
// If s is already <= max_bytes, returns s as-is.
String mcUtf8ClampBytes(const String& s, size_t max_bytes);
// Sanitize for logs/UI: make it one-line without destroying UTF-8.
// - \r, \n, \t => space
// - trim
// - collapse 2+ spaces => 1 space
String mcSanitizeOneLine(const String& s);
// Convenience for logs: mcUtf8ClampBytes(mcSanitizeOneLine(s), max_bytes)
String mcLogHead(const String& s, size_t max_bytes);
