// Weitere Themes für den Limit-Screen: Battery, Synthwave und Retro.
// Wird von main.cpp eingebunden und nutzt dessen Zustand (st, drawn, tft, spr), Farben und Helfer.
// Battery läuft im normalen Rahmen mit Kopf- und Fußzeile, Synthwave und Retro belegen den ganzen Bildschirm.

#pragma once

uint16_t SY_BG, SY_FRAME, SY_PINK, SY_PINK_DIM, SY_CYAN, SY_CYAN_DIM, SY_PURPLE, SY_GRID, SY_SUN2, SY_STAR;
uint16_t RT_DESK, RT_GRAY, RT_LIGHT, RT_DARK, RT_NAVY, RT_BALLOON, RT_BLACK, RT_WHITE;

void initThemeColors() {
  SY_BG = tft.color565(20, 8, 40);
  SY_FRAME = tft.color565(120, 50, 210);
  SY_PINK = tft.color565(255, 80, 160);
  SY_PINK_DIM = tft.color565(90, 20, 70);
  SY_CYAN = tft.color565(60, 190, 255);
  SY_CYAN_DIM = tft.color565(20, 60, 100);
  SY_PURPLE = tft.color565(150, 90, 255);
  SY_GRID = tft.color565(110, 40, 170);
  SY_SUN2 = tft.color565(255, 140, 90);
  SY_STAR = tft.color565(255, 120, 200);
  RT_DESK = tft.color565(0, 128, 128);
  RT_GRAY = tft.color565(192, 192, 192);
  RT_LIGHT = tft.color565(223, 223, 223);
  RT_DARK = tft.color565(128, 128, 128);
  RT_NAVY = tft.color565(0, 0, 128);
  RT_BALLOON = tft.color565(255, 255, 225);
  RT_BLACK = tft.color565(0, 0, 0);
  RT_WHITE = tft.color565(255, 255, 255);
}

uint16_t lerpColor(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, float f) {
  return tft.color565(r1 + (r2 - r1) * f, g1 + (g2 - g1) * f, b1 + (b2 - b1) * f);
}

// Wie textBox, aber auf beliebigem Hintergrund. font = 1 mit size > 1 ergibt die grobe Pixel-Schrift.
void textOn(int x, int y, int w, int h, const String& s, const GFXfont* f, uint16_t col, uint16_t bg,
            uint8_t datum = MC_DATUM, uint8_t font = 2, uint8_t size = 1) {
  if (!spr.createSprite(w, h)) return;
  spr.fillSprite(bg);
  spr.setTextColor(col, bg);
  if (f) spr.setFreeFont(f);
  else spr.setTextFont(font);
  spr.setTextSize(size);
  spr.setTextDatum(datum);
  int tx = (datum == ML_DATUM) ? 0 : (datum == MR_DATUM) ? w - 1 : w / 2;
  spr.drawString(s, tx, h / 2);
  spr.setTextSize(1);
  spr.pushSprite(x, y);
  spr.deleteSprite();
}

String pctText(const Limit& l) { return l.valid ? String(l.pct) + "%" : "--"; }

String resetText(const Limit& l, uint32_t now) {
  if (!l.valid || !l.resetsAt || !now) return "";
  int32_t left = (int32_t)(l.resetsAt - now);
  return left > 0 ? "in " + fmtDur(left) : fmtDur(left);
}

// Bricht Text für eine Breite (Font 2) in höchstens n Zeilen um.
int wrapText(const String& text, int maxW, String* lines, int n) {
  int count = 0;
  String line, word;
  String rest = text + " ";
  for (unsigned i = 0; i < rest.length() && count < n; i++) {
    char c = rest[i];
    if (c != ' ') { word += c; continue; }
    String test = line.length() ? line + " " + word : word;
    if (tft.textWidth(test, 2) > maxW && line.length()) {
      lines[count++] = line;
      line = word;
    } else {
      line = test;
    }
    word = "";
  }
  if (line.length() && count < n) lines[count++] = line;
  return count;
}

// ---------- Battery: Batterie-Symbole mit 10 Segmenten, das nächste Segment "lädt" ----------

const int BT_X = 12, BT_W = 200;

int batteryRows() { return st.fable.valid ? 3 : 2; }

void batteryGeom(int row, int& y, int& h) {
  int n = batteryRows();
  int rowH = n == 3 ? 42 : 58;
  h = n == 3 ? 22 : 30;
  y = 46 + row * rowH + 18;
}

const Limit& batteryLimit(int row) { return row == 0 ? st.session : row == 1 ? st.weekly : st.fable; }

uint16_t batteryColor(int row, int pct) {
  if (pct >= 90) return C_RED;
  return row == 0 ? C_ORANGE : row == 1 ? C_AMBER : C_SWEAT;
}

void batterySegment(int row, int i, uint16_t col) {
  int y, h;
  batteryGeom(row, y, h);
  const int pad = 4, gap = 3, sw = (BT_W - 2 * pad - 9 * gap) / 10;
  tft.fillRect(BT_X + pad + i * (sw + gap), y + pad, sw, h - 2 * pad, col);
}

int batteryFilled(const Limit& l) { return (!l.valid || l.pct <= 0) ? 0 : min(10, (l.pct + 9) / 10); }

void drawBattery(int row) {
  int y, h;
  batteryGeom(row, y, h);
  const Limit& l = batteryLimit(row);
  tft.fillRect(BT_X, y, BT_W + 8, h, C_BG);
  tft.drawRoundRect(BT_X, y, BT_W, h, 4, C_MUTED);
  tft.drawRoundRect(BT_X + 1, y + 1, BT_W - 2, h - 2, 3, C_MUTED);
  tft.fillRect(BT_X + BT_W, y + h / 4, 5, h - h / 2, C_MUTED);  // Pol
  int filled = batteryFilled(l);
  for (int i = 0; i < 10; i++) batterySegment(row, i, i < filled ? batteryColor(row, l.pct) : C_BG);
}

void renderBattery(uint32_t now) {
  static const char* const LABELS[3] = {"Session genutzt", "Woche genutzt", "Fable genutzt"};
  int n = batteryRows();
  String layout = String(n);
  if (layout != drawn.cl[15]) {  // zwei oder drei Zeilen: alles neu
    tft.fillRect(0, 42, 320, 170, C_BG);
    for (auto& c : drawn.cl) c = "\x01";
    drawn.cl[15] = layout;
  }
  for (int row = 0; row < n; row++) {
    const Limit& l = batteryLimit(row);
    int y, h;
    batteryGeom(row, y, h);
    if (drawn.cl[row * 3] != LABELS[row]) {
      textBox(BT_X, y - 18, 200, 16, LABELS[row], nullptr, C_MUTED, ML_DATUM);
      drawn.cl[row * 3] = LABELS[row];
    }
    String key = pctText(l);
    if (key != drawn.cl[row * 3 + 1]) {
      drawBattery(row);
      textBox(226, y + h / 2 - 14, 86, 28, key, &FreeSansBold12pt7b, C_TEXT, MR_DATUM);
      drawn.cl[row * 3 + 1] = key;
    }
  }
  String rt = st.session.valid ? "Reset " + resetText(st.session, now) : "";
  if (rt != drawn.cl[9]) {
    textBox(BT_X, 178, 300, 30, rt, &FreeSansBold12pt7b, C_TEXT, ML_DATUM);
    drawn.cl[9] = rt;
  }
}

void animateBattery() {
  bool on = (millis() / 600) % 2;
  for (int row = 0; row < batteryRows(); row++) {
    const Limit& l = batteryLimit(row);
    int next = batteryFilled(l);
    if (!l.valid || next >= 10) continue;
    batterySegment(row, next, on ? C_TRACK : C_BG);
  }
}

// ---------- Synthwave: Neon-Pixel-Theme mit Retro-Sonne und laufendem Gitter ----------

const int SY_SX = 8, SY_SY = 44, SY_SW = 304, SY_SH = 82, SY_HORIZON = 52;

void drawSynthStatic() {
  tft.fillScreen(SY_BG);
  tft.drawRoundRect(1, 1, 318, 238, 10, SY_FRAME);
  tft.drawRoundRect(2, 2, 316, 236, 9, SY_FRAME);
  // Titel mit Leucht-Schatten
  if (spr.createSprite(300, 30)) {
    spr.fillSprite(SY_BG);
    spr.setTextFont(1);
    spr.setTextSize(3);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(SY_PINK_DIM);
    spr.drawString("CLAUDE USAGE", 152, 17);
    spr.setTextColor(SY_PINK);
    spr.drawString("CLAUDE USAGE", 150, 15);
    spr.setTextSize(1);
    spr.pushSprite(10, 10);
    spr.deleteSprite();
  }
  textOn(10, 130, 160, 16, "SESSION", nullptr, SY_PURPLE, SY_BG, ML_DATUM, 1, 2);
  textOn(10, 178, 160, 16, "WOCHE", nullptr, SY_CYAN, SY_BG, ML_DATUM, 1, 2);
}

void drawSynthBar(int y, int pct, uint16_t col, uint16_t dim) {
  const int x = 10, w = 222, h = 20, segs = 15, gap = 3;
  tft.drawRect(x, y, w, h, col);
  tft.drawRect(x + 1, y + 1, w - 2, h - 2, col);
  int sw = (w - 8 - (segs - 1) * gap) / segs;
  int filled = pct <= 0 ? 0 : min(segs, (pct * segs + 99) / 100);
  for (int i = 0; i < segs; i++)
    tft.fillRect(x + 4 + i * (sw + gap), y + 4, sw, h - 8, i < filled ? col : dim);
}

void renderSynth(uint32_t now) {
  const Limit* lims[2] = {&st.session, &st.weekly};
  const uint16_t cols[2] = {SY_PINK, SY_CYAN}, dims[2] = {SY_PINK_DIM, SY_CYAN_DIM};
  for (int i = 0; i < 2; i++) {
    const Limit& l = *lims[i];
    String key = pctText(l);
    if (key == drawn.cl[i]) continue;
    int y = 146 + i * 48;
    drawSynthBar(y, l.valid ? l.pct : 0, l.valid && l.pct >= 90 ? C_RED : cols[i], dims[i]);
    textOn(236, y - 4, 78, 26, key, nullptr, cols[i], SY_BG, MR_DATUM, 1, 3);
    textOn(236, y + 23, 78, 10, "GENUTZT", nullptr, SY_PURPLE, SY_BG, MR_DATUM, 1, 1);
    drawn.cl[i] = key;
  }
}

void animateSynth(uint32_t now, bool stale) {
  if (!spr.createSprite(SY_SW, SY_SH)) return;
  uint32_t t = millis();
  spr.fillSprite(SY_BG);
  const int cx = SY_SW / 2, hz = SY_HORIZON;

  // Sterne funkeln versetzt
  static const uint16_t STARS[14][2] = {{14, 8},  {40, 30}, {66, 12}, {92, 40}, {112, 6},  {196, 10}, {214, 36},
                                       {240, 18}, {262, 44}, {286, 8}, {296, 30}, {28, 44}, {130, 24}, {176, 42}};
  for (int i = 0; i < 14; i++) {
    if ((t / 260 + i * 7) % 11 < 3) continue;
    int x = STARS[i][0], y = STARS[i][1];
    spr.drawPixel(x, y, SY_STAR);
    if (i % 3 == 0) {
      spr.drawPixel(x - 1, y, SY_PINK_DIM);
      spr.drawPixel(x + 1, y, SY_PINK_DIM);
      spr.drawPixel(x, y - 1, SY_PINK_DIM);
      spr.drawPixel(x, y + 1, SY_PINK_DIM);
    }
  }

  // Sonne: Verlauf von Pink nach Orange, unten mit Streifen, die langsam nach unten wandern
  const int r = 34;
  int shift = (t / 180) % 6;
  for (int dy = 0; dy <= r; dy++) {
    int y = hz - dy;
    if (dy < 20 && (dy + shift) % 6 < 2) continue;
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    spr.drawFastHLine(cx - dx, y, 2 * dx + 1, lerpColor(255, 140, 90, 255, 70, 170, dy / (float)r));
  }

  // Gitterboden: Längslinien zum Fluchtpunkt, Querlinien laufen auf den Betrachter zu
  spr.drawFastHLine(0, hz, SY_SW, SY_PURPLE);
  for (int j = -9; j <= 9; j++)
    spr.drawLine(cx + j * 10, hz, cx + j * 48, SY_SH - 1, SY_GRID);
  float phase = (t % 900) / 900.0f;
  for (int k = 0; k < 6; k++) {
    float z = (k + phase) / 6.0f;
    int y = hz + (int)(z * z * (SY_SH - hz));
    spr.drawFastHLine(0, y, SY_SW, SY_GRID);
  }

  // Reset links oben, Fable rechts oben, Offline-Hinweis
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);
  String rt = stale ? "OFFLINE" : st.session.valid ? "RESET " + resetText(st.session, now) : "";
  rt.toUpperCase();
  spr.setTextColor(stale ? C_RED : SY_PURPLE);
  spr.drawString(rt, 2, 2);
  if (st.fable.valid) {
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(SY_CYAN);
    spr.drawString("FABLE " + pctText(st.fable), SY_SW - 3, 2);
  }
  spr.pushSprite(SY_SX, SY_SY);
  spr.deleteSprite();
}

// ---------- Retro: Fenster im Stil der 90er, Clawd mit Hilfe-Sprechblase ----------

void bevel(int x, int y, int w, int h, bool raised) {
  uint16_t tl = raised ? RT_WHITE : RT_DARK, br = raised ? RT_DARK : RT_WHITE;
  tft.drawFastHLine(x, y, w, tl);
  tft.drawFastVLine(x, y, h, tl);
  tft.drawFastHLine(x, y + h - 1, w, br);
  tft.drawFastVLine(x + w - 1, y, h, br);
}

void drawRetroTitle(bool stale) {
  uint16_t bar = stale ? RT_DARK : RT_NAVY;
  tft.fillRect(8, 8, 304, 20, bar);
  String title = "Claude Usage";
  if (st.fable.valid) title += "  -  Fable " + pctText(st.fable);
  if (stale) title += "  (offline)";
  textOn(12, 9, 240, 18, title, &FreeSansBold9pt7b, stale ? RT_LIGHT : RT_WHITE, bar, ML_DATUM);
  const int bx[3] = {258, 274, 294};
  for (int i = 0; i < 3; i++) {
    tft.fillRect(bx[i], 11, 15, 14, RT_GRAY);
    bevel(bx[i], 11, 15, 14, true);
  }
  tft.drawFastHLine(261, 21, 6, RT_BLACK);   // _
  tft.drawFastHLine(261, 22, 6, RT_BLACK);
  tft.drawRect(277, 14, 9, 8, RT_BLACK);     // □
  tft.drawFastHLine(277, 15, 9, RT_BLACK);
  tft.drawLine(298, 14, 305, 21, RT_BLACK);  // X
  tft.drawLine(299, 14, 306, 21, RT_BLACK);
  tft.drawLine(305, 14, 298, 21, RT_BLACK);
  tft.drawLine(306, 14, 299, 21, RT_BLACK);
}

void drawRetroStatic() {
  tft.fillScreen(RT_DESK);
  tft.fillRect(4, 4, 312, 232, RT_GRAY);
  tft.drawRect(4, 4, 312, 232, RT_BLACK);
  bevel(5, 5, 310, 230, true);
  tft.drawFastHLine(12, 124, 296, RT_DARK);  // eingeprägte Trennlinie
  tft.drawFastHLine(12, 125, 296, RT_WHITE);
}

String retroTip(Mood m) {
  switch (m) {
    case MOOD_SLEEP: return "Zzz ... weck mich, wenn es was zu tun gibt.";
    case MOOD_BORED: return "Mir ist langweilig. Wollen wir was coden?";
    case MOOD_BUSY: return "Wow, " + String(st.activity) + " Jobs gleichzeitig! Soll ich Kaffee holen?";
    case MOOD_TIGHT: return "Dein Limit wird knapp. Kleine Pause?";
    case MOOD_STRESS: return "Es sieht aus, als waere dein Limit fast weg!";
    default: return "Es sieht aus, als wuerdest du programmieren. Brauchst du Hilfe?";
  }
}

void drawRetroBalloon(const String& text) {
  const int x = 144, y = 34, w = 164, h = 76;
  tft.fillRect(x - 14, y, w + 14, h + 2, RT_GRAY);
  tft.fillRoundRect(x, y, w, h, 7, RT_BALLOON);
  tft.drawRoundRect(x, y, w, h, 7, RT_BLACK);
  tft.fillTriangle(x + 1, y + 44, x - 12, y + 60, x + 1, y + 56, RT_BALLOON);  // Zipfel zu Clawd
  tft.drawLine(x, y + 44, x - 12, y + 60, RT_BLACK);
  tft.drawLine(x - 12, y + 60, x, y + 56, RT_BLACK);
  String lines[4];
  int n = wrapText(text, w - 14, lines, 4);
  for (int i = 0; i < n; i++)
    textOn(x + 7, y + 6 + i * 16, w - 14, 16, lines[i], nullptr, RT_BLACK, RT_BALLOON, ML_DATUM);
}

void drawRetroBar(int y, int pct) {
  const int x = 14, w = 222, h = 20, bw = 8, gap = 2;
  tft.fillRect(x, y, w, h, RT_WHITE);
  bevel(x, y, w, h, false);
  bevel(x + 1, y + 1, w - 2, h - 2, false);
  int blocks = (w - 6) / (bw + gap);
  int filled = pct <= 0 ? 0 : min(blocks, (pct * blocks + 99) / 100);
  for (int i = 0; i < filled; i++) {
    float f = i / (float)(blocks - 1);
    uint16_t c = f < 0.5f ? lerpColor(20, 150, 30, 120, 170, 20, f * 2)
                          : lerpColor(120, 170, 20, 210, 40, 30, (f - 0.5f) * 2);
    tft.fillRect(x + 4 + i * (bw + gap), y + 4, bw, h - 8, c);
  }
}

void renderRetro(uint32_t now, bool stale) {
  String title = String(stale) + pctText(st.fable) + st.fable.valid;
  if (title != drawn.cl[10]) {
    drawRetroTitle(stale);
    drawn.cl[10] = title;
  }
  String tip = retroTip(currentMood(stale));
  if (tip != drawn.cl[11]) {
    drawRetroBalloon(tip);
    drawn.cl[11] = tip;
  }
  const Limit* lims[2] = {&st.session, &st.weekly};
  static const char* const LABELS[2] = {"Session", "Woche"};
  for (int i = 0; i < 2; i++) {
    const Limit& l = *lims[i];
    int y = 130 + i * 50;
    String key = pctText(l);
    if (key != drawn.cl[i]) {
      textOn(14, y, 100, 16, LABELS[i], nullptr, RT_BLACK, RT_GRAY, ML_DATUM);
      drawRetroBar(y + 18, l.valid ? l.pct : 0);
      textOn(242, y + 14, 70, 24, key, &FreeSans12pt7b, RT_BLACK, RT_GRAY, ML_DATUM);
      textOn(242, y + 38, 70, 10, "genutzt", nullptr, RT_BLACK, RT_GRAY, ML_DATUM, 1, 1);
      drawn.cl[i] = key;
    }
    String rt = l.valid ? "Reset " + resetText(l, now) : "";
    if (rt != drawn.cl[i + 2]) {
      textOn(114, y, 122, 16, rt, nullptr, RT_DARK, RT_GRAY, MR_DATUM);
      drawn.cl[i + 2] = rt;
    }
  }
}
