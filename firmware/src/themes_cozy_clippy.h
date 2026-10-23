// Themes "Cozy Coder" (Pixel-Szene, Neon-Zahlen) und "Clippy" (90er-Fenster mit Büroklammer).
// Wird nach themes.h von main.cpp eingebunden. Clippy ist eine Figur von Microsoft – hier nur als
// selbst gezeichnete Fan-Art für das eigene Display.

#pragma once
#include "cozy_scene.h"

// ---------- Cozy Coder ----------
//
// Die Szene ist ein 160 x 55-Raster, auf dem Display doppelt so groß (320 x 110 ab y = COZY_Y).
// Statischer Teil: cozy_scene.h (erzeugt von tools/cozy_scene.py). Animiert: Regen, Katze, Monitor,
// Lampe, Dampf, Coder. Für die Animation werden nur drei Ausschnitte neu gezeichnet, jeweils komplett
// (statisch + animiert) in ein Sprite.

const int COZY_Y = 24, COZY_S = 2;
uint16_t czCol[CZ_COUNT];
uint16_t CZ_UI_BG, CZ_UI_LINE, CZ_UI_YELLOW, CZ_UI_CYAN, CZ_UI_PINK, CZ_UI_LILAC, CZ_UI_TRACK;

void initCozyColors() {
  for (int i = 0; i < CZ_COUNT; i++) czCol[i] = tft.color565(COZY_RGB[i][0], COZY_RGB[i][1], COZY_RGB[i][2]);
  CZ_UI_BG = tft.color565(16, 14, 34);
  CZ_UI_LINE = tft.color565(72, 52, 124);
  CZ_UI_YELLOW = tft.color565(250, 222, 120);
  CZ_UI_CYAN = tft.color565(80, 232, 240);
  CZ_UI_PINK = tft.color565(250, 112, 204);
  CZ_UI_LILAC = tft.color565(184, 164, 232);
  CZ_UI_TRACK = tft.color565(44, 38, 76);
}

// Ein Szenen-Pixel(-Rechteck) zeichnen; (ox, oy) = Lage des Rasters im Ziel (Display oder Sprite).
inline void czPx(TFT_eSPI& g, int ox, int oy, int x, int y, int w, int h, uint8_t c) {
  g.fillRect(ox + x * COZY_S, oy + y * COZY_S, w * COZY_S, h * COZY_S, czCol[c]);
}

// Animierter Teil – Nachbildung in tools/cozy_scene.py (draw_dynamic) für die Vorschau.
void drawCozyDynamic(TFT_eSPI& g, int ox, int oy, uint32_t t, Mood mood) {
  bool sleep = mood == MOOD_SLEEP, busy = mood == MOOD_BUSY || mood == MOOD_STRESS;
  // Regen
  for (int i = 0; i < 8; i++)
    czPx(g, ox, oy, 114 + (i * 9 + 3) % 34, 4 + (t / 45 + i * 11) % 22, 1, 2, CZ_STEAM);
  // Katze: Augen blinzeln, Schwanz schwingt
  if (t % 4200 >= 150) {
    czPx(g, ox, oy, 125, 24, 1, 1, CZ_CAT_EYE);
    czPx(g, ox, oy, 127, 24, 1, 1, CZ_CAT_EYE);
  }
  int phase = (t / 500) % 4;
  static const int8_t TAIL[3][3][2] = {{{117, 28}, {116, 29}, {116, 30}},
                                       {{117, 28}, {116, 29}, {115, 29}},
                                       {{117, 28}, {117, 29}, {116, 30}}};
  const int8_t(*tail)[2] = TAIL[phase == 1 ? 1 : phase == 3 ? 2 : 0];
  for (int i = 0; i < 3; i++) czPx(g, ox, oy, tail[i][0], tail[i][1], 1, 1, CZ_CAT);
  // Monitor: Code scrollt (bei Vollgas schneller), im Schlaf Bildschirmschoner
  if (sleep) {
    czPx(g, ox, oy, 80, 19, 26, 15, CZ_SCREEN_OFF);
    czPx(g, ox, oy, 81 + (t / 200) % 24, 20 + (t / 330) % 13, 1, 1, CZ_STAR);
  } else {
    czPx(g, ox, oy, 80, 19, 26, 15, CZ_SCREEN);
    uint32_t step = t / (busy ? 120 : 400);
    static const uint8_t CODE[4] = {CZ_CODE1, CZ_CODE2, CZ_CODE3, CZ_CODE4};
    for (int row = 0; row < 7; row++) {
      uint32_t line = row + step;
      int indent = (line * 3) % 4, len = 5 + (line * 7) % 15;
      czPx(g, ox, oy, 81 + indent, 20 + row * 2, min(len, 24 - indent), 1, CODE[line % 4]);
    }
  }
  // Lampe flackert selten
  bool lit = !(t % 6000 < 140);
  czPx(g, ox, oy, 115, 25, 6, 1, lit ? CZ_BULB : CZ_LAMP);
  czPx(g, ox, oy, 108, 38, 26, 1, lit ? CZ_DESK_LIT : CZ_DESK);
  // Dampf über der Tasse
  if (!sleep)
    for (int k = 0; k < 3; k++) {
      int y = 31 - (t / 160 + k * 3) % 9;
      czPx(g, ox, oy, 129 + (y + t / 300) % 3, y, 1, 1, CZ_STEAM);
    }
  // Coder: tippt (bei Vollgas schneller und wippt), langweilt sich (Hände still), schläft (Kopf sinkt)
  int dy = sleep ? 6 : (busy && (t / 140) % 2 ? -1 : 0);
  int hx = sleep ? 2 : 0;
  czPx(g, ox, oy, 60, 41, 16, 3, CZ_JEANS);
  czPx(g, ox, oy, 52, 24, 20, 17, CZ_HOOD);
  czPx(g, ox, oy, 52, 24, 4, 17, CZ_HOOD_DARK);
  czPx(g, ox, oy, 56, 22 + max(dy, 0) / 2, 12, 3, CZ_HOOD_DARK);
  static const uint8_t HAIR[4][4] = {{60, 9, 9, 1}, {58, 10, 13, 1}, {57, 11, 15, 10}, {58, 21, 12, 2}};
  for (auto& r : HAIR) czPx(g, ox, oy, r[0] + hx, r[1] + dy, r[2], r[3], CZ_HAIR);
  static const uint8_t CURLS[9][2] = {{59, 10}, {63, 9}, {67, 10}, {57, 14}, {61, 13},
                                      {65, 12}, {69, 13}, {59, 17}, {63, 16}};
  for (auto& c : CURLS) czPx(g, ox, oy, c[0] + hx, c[1] + dy, 2, 1, CZ_HAIR_LIGHT);
  czPx(g, ox, oy, 69 + hx, 15 + dy, 3, 7, CZ_SKIN);
  czPx(g, ox, oy, 71 + hx, 18 + dy, 1, 1, CZ_SKIN_DARK);
  czPx(g, ox, oy, 64 + hx, 17 + dy, 2, 3, CZ_SKIN_DARK);
  bool typing = !sleep && mood != MOOD_BORED;
  bool tick = (t / (busy ? 90 : 160)) % 2;
  czPx(g, ox, oy, 66, 30, 11, 4, CZ_HOOD);
  czPx(g, ox, oy, 77, 34 - (typing && tick ? 1 : 0), 3, 2, CZ_SKIN);
  czPx(g, ox, oy, 70, 33, 6, 3, CZ_HOOD_DARK);
  czPx(g, ox, oy, 75, 35 - (typing && !tick ? 1 : 0), 2, 2, CZ_SKIN);
  if (sleep) {
    static const uint8_t ZS[3][2] = {{74, 9}, {77, 6}, {80, 3}};
    for (int i = 0; i < 3; i++) {
      if ((t / 400 + i) % 3 == 0) continue;
      czPx(g, ox, oy, ZS[i][0], ZS[i][1], 2, 1, CZ_ZZZ);
      czPx(g, ox, oy, ZS[i][0] + 1, ZS[i][1] + 1, 1, 1, CZ_ZZZ);
      czPx(g, ox, oy, ZS[i][0], ZS[i][1] + 2, 2, 1, CZ_ZZZ);
    }
  }
  if (mood == MOOD_TIGHT || mood == MOOD_STRESS) czPx(g, ox, oy, 73, 12 + (t / 120) % 6, 1, 2, CZ_SWEAT);
}

void drawCozyScene(TFT_eSPI& g, int ox, int oy, uint32_t t, Mood mood) {
  for (auto& r : COZY_RECTS) czPx(g, ox, oy, r[0], r[1], r[2], r[3], r[4]);
  drawCozyDynamic(g, ox, oy, t, mood);
}

void drawCozyStatic(bool stale) {
  tft.fillScreen(CZ_UI_BG);
  textOn(6, 2, 120, 20, "Claude", &FreeSansBold9pt7b, CZ_UI_YELLOW, CZ_UI_BG, ML_DATUM);
  tft.drawFastHLine(0, 22, 320, CZ_UI_LINE);
  tft.drawFastHLine(0, 23, 320, CZ_UI_LINE);
  drawCozyScene(tft, 0, COZY_Y, millis(), currentMood(stale));
  tft.drawFastHLine(0, COZY_Y + COZY_H * COZY_S, 320, CZ_UI_LINE);
  tft.drawFastHLine(0, COZY_Y + COZY_H * COZY_S + 1, 320, CZ_UI_LINE);
}

// Gestrichelter Balken unter Session/Woche: Striche bis zum Prozentwert, dahinter eine dunkle Spur
void drawCozyDashes(int y, int pct, uint16_t col) {
  const int x = 8, w = 304, dash = 7, gap = 4;
  tft.fillRect(x, y, w, 3, CZ_UI_TRACK);
  int filled = w * constrain(pct, 0, 100) / 100;
  for (int dx = 0; dx < filled; dx += dash + gap) tft.fillRect(x + dx, y, min(dash, filled - dx), 3, col);
}

void renderCozy(uint32_t now, bool stale) {
  String head = stale ? "offline" : st.fable.valid ? "Fable " + pctText(st.fable) + "  genutzt" : "genutzt";
  if (head != drawn.cl[10]) {
    textOn(130, 2, 184, 20, head, nullptr, stale ? C_RED : CZ_UI_LILAC, CZ_UI_BG, MR_DATUM);
    drawn.cl[10] = head;
  }
  const Limit* lims[2] = {&st.session, &st.weekly};
  static const char* const LABELS[2] = {"Session", "Woche"};
  const uint16_t cols[2] = {CZ_UI_CYAN, CZ_UI_PINK};
  for (int i = 0; i < 2; i++) {
    const Limit& l = *lims[i];
    String key = pctText(l);
    if (key == drawn.cl[i]) continue;
    int y = 138 + i * 38;
    textOn(6, y, 180, 30, LABELS[i], &FreeSansBold18pt7b, cols[i], CZ_UI_BG, ML_DATUM);
    textOn(190, y, 124, 30, key, &FreeSansBold18pt7b, l.valid && l.pct >= 90 ? C_RED : C_TEXT, CZ_UI_BG,
           MR_DATUM);
    drawCozyDashes(y + 32, l.valid ? l.pct : 0, cols[i]);
    drawn.cl[i] = key;
  }
  String rt = st.session.valid ? resetText(st.session, now) : "";
  if (rt.startsWith("in ")) rt = rt.substring(3);
  if (rt != drawn.cl[2]) {
    textOn(6, 214, 100, 24, "Reset", &FreeSans9pt7b, CZ_UI_LILAC, CZ_UI_BG, ML_DATUM);
    textOn(196, 212, 118, 28, rt, &FreeSansBold12pt7b, CZ_UI_YELLOW, CZ_UI_BG, MR_DATUM);  // Mitte frei für die Seitenpunkte
    drawn.cl[2] = rt;
  }
}

// Drei animierte Ausschnitte (im 160 x 55-Raster): Fenster mit Katze, Coder mit Monitor, Lampe mit Tasse
void animateCozy(bool stale) {
  static const uint8_t REGIONS[3][4] = {{112, 2, 40, 30}, {50, 2, 60, 43}, {106, 18, 30, 22}};
  uint32_t t = millis();
  Mood mood = currentMood(stale);
  for (auto& r : REGIONS) {
    int w = r[2] * COZY_S, h = r[3] * COZY_S;
    if (!spr.createSprite(w, h)) continue;
    drawCozyScene(spr, -r[0] * COZY_S, -r[1] * COZY_S, t, mood);
    spr.pushSprite(r[0] * COZY_S, COZY_Y + r[1] * COZY_S);
    spr.deleteSprite();
  }
}

// ---------- Clippy ----------
//
// Gleicher Fensterrahmen wie "Retro", statt Clawd sitzt eine Büroklammer auf einem gelben Blatt:
// Augen schauen umher und blinzeln, Augenbrauen gehen hoch, bei Vollgas hüpft sie.

uint16_t CP_WIRE, CP_WIRE_DARK, CP_WIRE_LIGHT, CP_PAPER, CP_PAPER_LINE, CP_PAPER_DARK;

void initClippyColors() {
  CP_WIRE = tft.color565(178, 184, 196);
  CP_WIRE_DARK = tft.color565(70, 72, 84);
  CP_WIRE_LIGHT = tft.color565(240, 242, 248);
  CP_PAPER = tft.color565(246, 232, 140);
  CP_PAPER_LINE = tft.color565(206, 184, 96);
  CP_PAPER_DARK = tft.color565(196, 176, 92);
}

// Bogen als Polylinie (Winkel in Grad, 0 = rechts, gegen den Uhrzeigersinn, y nach unten)
void wireArc(TFT_eSprite& g, float cx, float cy, float r, float a0, float a1, float w, uint16_t col) {
  const int steps = 10;
  float px = cx + r * cosf(a0 * DEG_TO_RAD), py = cy - r * sinf(a0 * DEG_TO_RAD);
  for (int i = 1; i <= steps; i++) {
    float a = (a0 + (a1 - a0) * i / steps) * DEG_TO_RAD;
    float x = cx + r * cosf(a), y = cy - r * sinf(a);
    g.drawWideLine(px, py, x, y, w, col, RT_GRAY);
    px = x;
    py = y;
  }
}

// Die Büroklammer: äußere Schlaufe oben, innere Schlaufe unten, wie das Original-Clippy
void wirePath(TFT_eSprite& g, int cx, int top, float w, uint16_t col) {
  g.drawWideLine(cx - 9, top + 58, cx - 9, top + 10, w, col, RT_GRAY);  // links hoch
  wireArc(g, cx - 1, top + 10, 8, 180, 0, w, col);                      // oben rund
  g.drawWideLine(cx + 7, top + 10, cx + 7, top + 52, w, col, RT_GRAY);  // rechts runter
  wireArc(g, cx + 1, top + 52, 6, 0, -180, w, col);                     // unten rund
  g.drawWideLine(cx - 5, top + 52, cx - 5, top + 22, w, col, RT_GRAY);  // innen hoch
  wireArc(g, cx - 1, top + 22, 4, 180, 0, w, col);                      // innen oben
  g.drawWideLine(cx + 3, top + 22, cx + 3, top + 44, w, col, RT_GRAY);  // innen runter
}

void drawClippyAt(int px, int py, int w, int h, bool stale) {
  if (!spr.createSprite(w, h)) return;
  spr.fillSprite(RT_GRAY);
  uint32_t t = millis();
  Mood m = currentMood(stale);

  // Blatt Papier, leicht schräg, mit Linien und umgeknickter Ecke
  spr.fillTriangle(12, 70, 100, 62, 106, 82, CP_PAPER);
  spr.fillTriangle(12, 70, 106, 82, 18, 88, CP_PAPER);
  for (int i = 1; i < 4; i++) spr.drawLine(16 + i, 72 + i * 4, 100 + i, 65 + i * 4, CP_PAPER_LINE);
  spr.fillTriangle(12, 70, 22, 69, 15, 76, CP_PAPER_DARK);

  // Hüpfen bei Vollgas, sonst leichtes Wippen
  int dy = m == MOOD_BUSY ? -(int)(fabsf(sinf(t / 140.0f)) * 6) : (int)roundf(1.5f * sinf(t / 500.0f));
  if (m == MOOD_STRESS) dy = (int)(t / 60 % 3) - 1;
  int cx = 58, top = 6 + dy;
  wirePath(spr, cx, top, 6, CP_WIRE_DARK);
  wirePath(spr, cx, top, 4, CP_WIRE);
  spr.drawLine(cx - 10, top + 14, cx - 10, top + 50, CP_WIRE_LIGHT);  // Glanzlicht

  // Augen: weiß mit schwarzem Rand, Pupillen schauen umher; zu beim Schlafen und Blinzeln
  bool blink = (t % 3900) < 130 || m == MOOD_SLEEP;
  int look = 0, lookY = 0;
  uint32_t ph = (t / 1800) % 6;
  if (m == MOOD_BORED) look = (ph % 2) ? -2 : 2, lookY = -1;
  else if (ph == 1) look = -2;
  else if (ph == 3) look = 2;
  else if (ph == 5) lookY = 2;  // schaut aufs Blatt
  const int ey = top + 22, ex[2] = {cx - 8, cx + 8};
  for (int i = 0; i < 2; i++) {
    if (blink) {
      spr.drawWideLine(ex[i] - 5, ey + 1, ex[i] + 5, ey + 1, 2, RT_BLACK, RT_GRAY);
      continue;
    }
    int r = m == MOOD_STRESS ? 8 : 7;
    spr.fillSmoothCircle(ex[i], ey, r, RT_BLACK, RT_GRAY);
    spr.fillSmoothCircle(ex[i], ey, r - 1, RT_WHITE, RT_BLACK);
    spr.fillSmoothCircle(ex[i] + look, ey + lookY + 1, m == MOOD_STRESS ? 2 : 3, RT_BLACK, RT_WHITE);
  }
  // Augenbrauen: ziehen sich regelmäßig hoch, bei Stress dauerhaft, beim Schlafen entspannt
  bool raise = m == MOOD_STRESS || m == MOOD_TIGHT || (t % 5200) < 700 || (m == MOOD_BUSY && (t / 300) % 2);
  int by = ey - (raise ? 14 : 11);
  spr.drawWideLine(ex[0] - 6, by + 2, ex[0] + 4, by - 1, 3, RT_BLACK, RT_GRAY);
  spr.drawWideLine(ex[1] - 4, by - 1, ex[1] + 6, by + 2, 3, RT_BLACK, RT_GRAY);

  if (m == MOOD_SLEEP) {
    spr.setTextFont(2);
    spr.setTextColor(RT_DARK, RT_GRAY);
    spr.setTextDatum(MC_DATUM);
    for (int i = 0; i < 3; i++) {
      uint32_t p = (t + i * 1000) % 3000;
      spr.drawString(i == 2 ? "Z" : "z", cx + 18 + p / 200, top + 10 - p / 140);
    }
  } else if (m == MOOD_TIGHT || m == MOOD_STRESS) {
    int drop = (t / 25) % 30;
    spr.fillSmoothCircle(cx + 20, top + 12 + drop / 2, 3, C_SWEAT, RT_GRAY);
  }
  spr.pushSprite(px, py);
  spr.deleteSprite();
}
