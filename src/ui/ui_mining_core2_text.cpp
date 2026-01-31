// UI for Mining stackchan avatar ticker.
// Module implementation.
// Uses M5Stack-Avatar library for Stack-chan style face rendering.
// https://github.com/meganetaaan/m5stack-avatar
#include "ui/ui_mining_core2.h"

#include <WiFi.h>
// ===== Font helpers =====
void UIMining::prepInfoFont() {
  info_.setFont(&fonts::Font0);
  info_.setTextSize(2);
}
void UIMining::prepBodyFont() {
  info_.setFont(&fonts::Font0);
  info_.setTextSize(2);
}
void UIMining::prepHeaderFont() {
  info_.setFont(&fonts::Font0);
  info_.setTextSize(1);
}
// ===== Header + dots =====
void UIMining::drawDots(const TextLayoutY& ly) {
  uint16_t active   = TFT_CYAN;
  uint16_t inactive = kColDark;
  const int xs[3] = { kIndX1, kIndX2, kIndX3 };
  for (int i = 0; i < 3; ++i) {
    if (i == infoPage_) info_.fillCircle(xs[i], ly.indY, kIndR, active);
    else                 info_.drawCircle(xs[i], ly.indY, kIndR, inactive);
  }
}
void UIMining::drawHeader(const char* title, const TextLayoutY& ly) {
  info_.fillRect(0, ly.header, INF_W, 8, BLACK);
  prepHeaderFont();
  info_.setTextColor(TFT_CYAN, BLACK);
  int safeW = INF_W - 30;
  String t = title;
  while (t.length() && info_.textWidth(t) > safeW) {
    t.remove(t.length() - 1);
  }
  int tw = info_.textWidth(t);
  int x  = (safeW - tw) / 2;
  if (x < kPadLr) x = kPadLr;
  info_.setCursor(x, ly.header);
  info_.print(t);
  drawDots(ly);
}
// ===== Line primitive =====
void UIMining::drawLine(int y, const char* label4, const String& value,
                        uint16_t colLabel, uint16_t colValue) {
  info_.fillRect(0, y, INF_W, kCharH, BLACK);
  prepBodyFont();
  info_.setTextColor(colLabel, BLACK);
  info_.setCursor(kXLabel, y);
  String lab = String(label4);
  while (lab.length() < 4) lab += ' ';
  if (lab.length() > 4) lab = lab.substring(0, 4);
  info_.print(lab);
  info_.setTextColor(colValue, BLACK);
  info_.setCursor(kXValue, y);
  String v = value;
  if (v.length() > 9) v = v.substring(0, 9);
  info_.print(v);
}
// ===== Value formatters =====
String UIMining::vHash(float kh) const {
  char b[16];
  if (kh < 10.0f)       snprintf(b, sizeof(b), " %.2fkH/s", kh);
  else if (kh < 100.0f) snprintf(b, sizeof(b), " %.1fkH/s", kh);
  else                  snprintf(b, sizeof(b), " %.0fkH/s", kh);
  return String(b);
}
String UIMining::vShare(uint32_t acc, uint32_t rej, uint8_t& successOut) {
  uint32_t total   = acc + rej;
  uint8_t  success = 0;
  if (total) {
    success = static_cast<uint8_t>((acc * 100 + total / 2) / total);
  }
  successOut = success;
  char b[16];
  int n = snprintf(b, sizeof(b), "%u/%u %u%%",
                   static_cast<unsigned>(acc),
                   static_cast<unsigned>(rej),
                   static_cast<unsigned>(success));
  if (n > 9) {
    n = snprintf(b, sizeof(b), "%u/%u %u",
                 static_cast<unsigned>(acc),
                 static_cast<unsigned>(rej),
                 static_cast<unsigned>(success));
  }
  if (n > 9) {
    snprintf(b, sizeof(b), "%u/%u",
             static_cast<unsigned>(acc),
             static_cast<unsigned>(rej));
  }
  return String(b);
}
String UIMining::vDiff(float diff) const {
  // simple K notation
  double v = diff;
  const char* sfx = "";
  if (v >= 1000.0) { v /= 1000.0; sfx = "K"; }
  char b[16];
  if (sfx[0]) snprintf(b, sizeof(b), " %.1f%s", v, sfx);
  else        snprintf(b, sizeof(b), " %.0f",  v);
  return String(b);
}
String UIMining::vLast(uint32_t age) const {
  uint32_t mm = age / 60;
  uint32_t ss = age % 60;
  if (mm > 99) mm = 99;
  char b[16];
  snprintf(b, sizeof(b), " %02u:%02uago",
           static_cast<unsigned>(mm),
           static_cast<unsigned>(ss));
  return String(b);
}
String UIMining::vUp(uint32_t s) const {
  uint32_t hh = s / 3600;
  s %= 3600;
  uint32_t mm = s / 60;
  uint32_t ss = s % 60;
  char b[16];
  snprintf(b, sizeof(b), " %02u:%02u:%02u",
           static_cast<unsigned>(hh),
           static_cast<unsigned>(mm),
           static_cast<unsigned>(ss));
  return String(b);
}
String UIMining::vTemp(float c) const {
  int t = static_cast<int>(roundf(c));
  char b[16];
  snprintf(b, sizeof(b), " %d C", t);
  return String(b);
}
String UIMining::vHeap() const {
  uint32_t kb = ESP.getFreeHeap() / 1024;
  char b[16];
  snprintf(b, sizeof(b), " %uKB", static_cast<unsigned>(kb));
  return String(b);
}
String UIMining::vNet(const PanelData& p) const {
  if (WiFi.status() != WL_CONNECTED) return " OFFLINE";
  if (p.poolAlive_)                   return " ONLINE";
  return " CONN...";
}
String UIMining::vRssi() const {
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  char b[16];
  snprintf(b, sizeof(b), " %ddBm", rssi);
  return String(b);
}
String UIMining::vPool(const String& name) const {
  if (!name.length()) return " --";
  String s = name;
  if (s.length() > 8) s = s.substring(0, 8);
  return " " + s;
}
// ===== Color helpers =====
uint16_t UIMining::cHash(const PanelData& p) const {
  if (p.hrKh_ <= 0.05f) return 0xF800;
  if (hrRefKh_ > 0.1f) {
    float r = p.hrKh_ / hrRefKh_;
    if (r >= 0.90f && r <= 1.10f) return TFT_CYAN;
    if (r >= 0.70f) return WHITE;
    return 0xFD20;
  }
  return TFT_CYAN;
}
uint16_t UIMining::cShare(uint8_t s) const {
  if (s == 0)        return TFT_RED;
  if (s >= 95)       return 0x07E0;  // good
  if (s >= 90)       return WHITE;
  return 0xFD20;
}
uint16_t UIMining::cSharePct(float s) const {
  if (s >= 95.0f) return 0x07E0;   // green
  if (s >= 90.0f) return WHITE;    // white
  if (s >= 70.0f) return 0xFD20;   // orange/yellow
  return 0xF800;                   // red
}
uint16_t UIMining::cLast(uint32_t age) const {
  if (age <= 30)  return 0x07E0;
  if (age <= 120) return WHITE;
  if (age <= 300) return 0xFD20;
  return 0xF800;
}
uint16_t UIMining::cTemp(float c) const {
  if (c < 55.0f) return WHITE;
  if (c < 65.0f) return 0xFD20;
  return 0xF800;
}
uint16_t UIMining::cHeap(uint32_t kb) const {
  if (kb >= 50) return WHITE;
  if (kb >= 30) return 0xFD20;
  return 0xF800;
}
uint16_t UIMining::cNet(const String& v) const {
  if (v.indexOf("ONLINE") >= 0)  return 0x07E0;
  if (v.indexOf("CONN")   >= 0)  return 0xFFE0;
  return 0xF800;
}
uint16_t UIMining::cRssi(int rssi) const {
  if (rssi >= -60) return 0x07E0;
  if (rssi >= -75) return WHITE;
  return 0xFD20;
}
uint16_t UIMining::cBatt(int pct) const {
  if (pct >= 50) return 0x07E0;
  if (pct >= 20) return 0xFFE0;
  return 0xF800;
}
// ===== Temperature / Power =====
float UIMining::readTempC() {
  float t = NAN;
  M5.Imu.getTemp(&t);
#if defined(ARDUINO_ARCH_ESP32)
  if (isnan(t) || t < -40.0f || t > 125.0f) {
    t = temperatureRead();
  }
#endif
  if (isnan(t)) t = 0.0f;
  return t;
}
int UIMining::batteryPct() {
  static int s_lastPct = -1;
  int raw = static_cast<int>(M5.Power.getBatteryLevel());
  if (raw < 0 || raw > 100) {
    return (s_lastPct < 0) ? 0 : s_lastPct;
  }
  if (s_lastPct < 0) {
    s_lastPct = raw;
    return s_lastPct;
  }
  if (abs(raw - s_lastPct) > 20) {
    return s_lastPct;
  }
  s_lastPct = raw;
  return s_lastPct;
}
bool UIMining::isExternalPower() {
  if (M5.Power.isCharging()) return true;
  if (batteryPct() >= 100) return true;
  return false;
}
String UIMining::vBatt() {
  int  pct = batteryPct();
  bool ext = isExternalPower();
  char b[16];
  snprintf(b, sizeof(b), " %d%% %s", pct, ext ? "AC" : "BAT");
  return String(b);
}
// ===== Pages =====
void UIMining::drawPage0(const PanelData& p) {
  // Mining summary page.
  auto ly = computeTextLayoutY();
  drawHeader("MINING STATUS", ly);
  drawLine(ly.y1, "HASH", vHash(p.hrKh_), kColLabel, cHash(p));
  uint8_t success = 0;
  String shrVal = vShare(p.accepted_, p.rejected_, success);
  drawLine(ly.y2, "SHR ", shrVal, kColLabel, cShare(success));
  drawLine(ly.y3, "DIFF", vDiff(p.diff_), kColLabel, WHITE);
  uint32_t age = lastShareAgeSec();
  drawLine(ly.y4, "LAST", vLast(age), kColLabel, cLast(age));
}
void UIMining::drawPage1(const PanelData& p) {
  // Device health/status page.
  auto ly = computeTextLayoutY();
  drawHeader("DEVICE STATUS", ly);
  drawLine(ly.y1, "UP  ", vUp(p.elapsedS_), kColLabel, WHITE);
  float tc = readTempC();
  drawLine(ly.y2, "TEMP", vTemp(tc), kColLabel, cTemp(tc));
  int pct = batteryPct();
  drawLine(ly.y3, "BATT", vBatt(), kColLabel, cBatt(pct));
  uint32_t freeKb = ESP.getFreeHeap() / 1024;
  drawLine(ly.y4, "HEAP", vHeap(), kColLabel, cHeap(freeKb));
}
void UIMining::drawPage2(const PanelData& p) {
  // Network status page.
  auto ly = computeTextLayoutY();
  drawHeader("NETWORK", ly);
  String nv = vNet(p);
  drawLine(ly.y1, "NET ", nv, kColLabel, cNet(nv));
  String pv;
  if (p.pingMs_ < 0) {
    pv = " ---- ms";
  } else {
    char b[16];
    snprintf(b, sizeof(b), " %d ms", static_cast<int>(roundf(p.pingMs_)));
    pv = String(b);
  }
  drawLine(ly.y2, "PING", pv, kColLabel, WHITE);
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  drawLine(ly.y3, "WIFI", vRssi(), kColLabel, cRssi(rssi));
  drawLine(ly.y4, "POOL", "", kColLabel, WHITE);
  drawPoolNameSmall(ly, p.poolName_);
}
void UIMining::drawPoolNameSmall(const TextLayoutY& ly, const String& name) {
  int y = ly.y4 + kCharH + 6;
  info_.fillRect(0, y, INF_W, 10, BLACK);
  info_.setFont(&fonts::Font0);
  info_.setTextSize(1);
  info_.setTextColor(WHITE, BLACK);
  String s = name.length() ? name : String("--");
  int maxW = INF_W - kPadLr * 2;
  while (s.length() && info_.textWidth(s) > maxW) {
    s.remove(s.length() - 1);
  }
  info_.setCursor(kPadLr, y);
  info_.print(s);
}
// ===== Right panel draw =====
void UIMining::drawInfo(const PanelData& p) {
  // Clear only the text block area (header + 4 rows)
  info_.fillScreen(BLACK);
  switch (infoPage_) {
    case 0: drawPage0(p); break;
    case 1: drawPage1(p); break;
    default: drawPage2(p); break;
  }
  info_.pushSprite(X_INF, 0);
}

