#include <Arduino.h>
#include <M5Unified.h>

#include "ai_talk_controller.h"

static AiTalkController g_ai;

static bool g_prevTouch = false;
static String g_serialLine;

static bool isTopThirdTap_(int x, int y) {
  (void)x;
  const int h = (int)M5.Display.height();
  return (y >= 0 && y < (h / 3));
}

static void drawOverlay_(const AiUiOverlay& ov) {
  auto& lcd = M5.Display;

  // 画面左上 overlay（常時表示）
  // ざっくり高さ60pxくらいを確保
  const int ox = 0;
  const int oy = 0;
  const int ow = (int)lcd.width();
  const int oh = 64;

  lcd.fillRect(ox, oy, ow, oh, TFT_BLACK);

  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(6, 6);
  lcd.print(ov.line1);

  lcd.setTextSize(1);
  lcd.setCursor(6, 34);
  lcd.print(ov.line2);

  if (ov.hint.length() > 0) {
    lcd.setCursor(6, 48);
    lcd.print(ov.hint);
  }
}

static void drawBaseUiOnce_() {
  auto& lcd = M5.Display;
  lcd.fillScreen(TFT_DARKGREY);

  // 上1/3タップ領域の目印（薄いライン）
  const int h = (int)lcd.height();
  const int y = h / 3;
  lcd.drawFastHLine(0, y, lcd.width(), TFT_BLACK);

  lcd.setTextSize(2);
  lcd.setTextColor(TFT_BLACK, TFT_DARKGREY);
  lcd.setCursor(10, y + 20);
  lcd.print("AI Sandbox");
}

static void handleSerial_() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    // CR/LF 両対応：CRだけ来る環境でも Enter で確定できるようにする
    if (c == '\r') c = '\n';

    if (c == '\n') {
      String line = g_serialLine;
      g_serialLine = "";
      Serial.printf("[ai] rx: %s\n", line.c_str());

      line.trim();
      if (line.length() == 0) continue;

      if (line.startsWith(":say ")) {
        String msg = line.substring(5);
        msg.trim();
        g_ai.injectText(msg);
      }
      continue;
    }

    g_serialLine += c;
    if (g_serialLine.length() > 512) {
      // 暴走防止
      g_serialLine = "";
    }
  }
}

void setup() {
  // M5Unified
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1); // Core2: 横向き
  M5.Display.setBrightness(128);

  Serial.begin(115200);
  Serial.println();
  Serial.println("[ai-sandbox] boot");
  Serial.println("Type ':say こんにちは' + Enter");

  g_ai.begin();
  drawBaseUiOnce_();
}

void loop() {
  M5.update();

  // タップ検出（上1/3）
  bool touchPressed = false;
  int touchX = 0;
  int touchY = 0;

  if (M5.Touch.isEnabled()) {
    auto detail = M5.Touch.getDetail();
    touchPressed = detail.isPressed();
    if (touchPressed) {
      touchX = detail.x;
      touchY = detail.y;
    }
  }

  if (touchPressed && !g_prevTouch) {
    if (isTopThirdTap_(touchX, touchY)) {
      g_ai.onTap();
    }
  }
  g_prevTouch = touchPressed;


  // Controller tick
  g_ai.tick();

  // 描画（overlayのみ毎フレーム更新）
  drawOverlay_(g_ai.getOverlay());

  // Serialコマンド
  handleSerial_();

  delay(16); // 約60fps目安（雑でOK）
}
