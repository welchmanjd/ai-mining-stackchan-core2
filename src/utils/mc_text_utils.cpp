#include "utils/mc_text_utils.h"

static size_t utf8SeqLen_(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1; // invalid lead -> treat as 1
}

String mcUtf8ClampBytes(const String& s, size_t max_bytes) {
  const size_t n = s.length(); // bytes
  if (n <= max_bytes) return s;
  if (max_bytes == 0) return "";

  size_t i = 0;
  while (i < n && i < max_bytes) {
    const uint8_t c = (uint8_t)s[i];
    const size_t L = utf8SeqLen_(c);
    if (i + L > max_bytes) break;

    bool ok = true;
    for (size_t k = 1; k < L; k++) {
      if (i + k >= n) { ok = false; break; }
      const uint8_t cc = (uint8_t)s[i + k];
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
    }
    if (!ok) break;

    i += L;
  }
  return s.substring(0, (unsigned)i);
}

String mcSanitizeOneLine(const String& s) {
  String out = s;
  out.replace("\r", " ");
  out.replace("\n", " ");
  out.replace("\t", " ");
  out.trim();
  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  return out;
}

String mcLogHead(const String& s, size_t max_bytes) {
  return mcUtf8ClampBytes(mcSanitizeOneLine(s), max_bytes);
}

