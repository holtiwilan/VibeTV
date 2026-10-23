// VibeCYD – zeigt Claude Session-, Wochen- und (falls vorhanden) Fable-Limit auf dem Cheap Yellow Display.
// Die Daten kommen als JSON-Zeilen per USB-Serial vom PC-Agent (agent/vibecyd_agent.py):
//   {"now":1761050000,"plan":"Max","s":{"p":42,"r":1761060000},"w":{"p":17,"r":1761500000},
//    "f":{"p":16,"r":1761500000},"t":1761049990,"tz":7200,"err":""}
// "f" (Fable-Wochenlimit) ist optional; fehlt es, werden nur zwei Gauges gezeigt.
// "tz" ist der UTC-Offset des PCs in Sekunden, für die Uhrzeit in der Kopfzeile.
// "tk" (optional) sind die Claude-Code-Tokens dieses PCs für heute / 7 / 30 Tage, je
//   [input, output, cache_write, cache_read, anfragen]: {"d":[...],"w":[...],"m":[...]}
// "tp" (optional, nur agent v2) ist der TimePunch Helper: laufende Erfassung {"t":Titel,"s":Start} oder null,
//   heute erfasst "d" / gebucht "b" / Soll "soll" und Woche erfasst "wk" in Minuten, offene "o" und fehlerhafte
//   "e" Einträge.
// "rot" (optional): Screens alle n Sekunden automatisch weiterschalten, 0 = aus.
// "ag" (optional, agent v2): laufende Claude-Code-Sessions {"s":[[Name,busy,Text],...],"a":[[Typ,Aufgabe],...],
//   "n":Sessions,"na":Subagenten} – eigener Screen.
// "act" (optional): Zahl der gerade arbeitenden Sessions + Subagenten, steuert die Laune des Maskottchens.
// "th" (optional): Theme des Limit-Screens: 0 Rundinstrumente, 1 Clawd, 2 Battery, 3 Synthwave, 4 Retro,
//   5 Cozy Coder, 6 Clippy.
// "br" (optional): Helligkeit der Hintergrundbeleuchtung in Prozent (5–100).
// Ein Tipp auf den Touchscreen blättert weiter (Limits -> Tokens -> Zeit); Screens ohne Daten werden
// übersprungen. Punkte unten zeigen die Seite.
// Auf eine Zeile "?" antwortet das Gerät mit "VIBECYD 1" (Port-Erkennung).
// Eine Zeile "N" blättert wie ein Tipp zum nächsten Screen, "S<n>" springt zu Screen n (falls verfügbar).

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#ifndef DISPLAY_INVERT
#define DISPLAY_INVERT 0
#endif

// RGB-LED auf der Rückseite (active low)
#define LED_R 4
#define LED_G 16
#define LED_B 17

// Touch-Controller XPT2046 hängt am zweiten SPI-Bus (VSPI)
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25

static const uint32_t STALE_AFTER_MS = 30000;
static const int BL_CHANNEL = 0;  // PWM-Kanal der Hintergrundbeleuchtung
enum Screen { SCREEN_LIMITS, SCREEN_SESSIONS, SCREEN_TOKENS, SCREEN_TIME, SCREEN_COUNT };
enum Theme { THEME_GAUGES, THEME_CLAWD, THEME_BATTERY, THEME_SYNTH, THEME_RETRO, THEME_COZY, THEME_CLIPPY,
             THEME_COUNT };

TFT_eSPI tft;
TFT_eSprite spr(&tft);
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

uint16_t C_BG, C_TRACK, C_ORANGE, C_TEXT, C_MUTED, C_RED, C_GREEN, C_AMBER;

struct Limit {
  bool valid = false;
  int pct = 0;
  uint32_t resetsAt = 0;
};

struct Tokens {
  bool valid = false;
  double v[3][5] = {};  // [heute, 7 Tage, 30 Tage][input, output, cache_write, cache_read, anfragen]
};

struct TimePunch {
  bool valid = false, running = false;
  String title;
  uint32_t runStart = 0;
  int dayMin = 0, bookedMin = 0, sollMin = 0, weekMin = 0, open = 0, errors = 0;
};

// Eine Zeile auf dem Sessions-Screen: Session (busy/idle) oder aktiver Subagent
struct AgentRow {
  String name, text;
  bool busy = false, sub = false;
};

struct Agents {
  bool valid = false;
  int sessions = 0, subagents = 0, rows = 0;
  AgentRow row[4];
};

struct State {
  Limit session, weekly, fable;
  Tokens tokens;
  TimePunch tp;
  Agents ag;
  int theme = THEME_GAUGES;    // Theme des Limit-Screens
  int brightness = 100;        // Prozent
  int activity = -1;           // arbeitende Sessions + Subagenten, -1 = unbekannt
  uint32_t lastActiveMs = 0;   // zuletzt etwas gearbeitet (für Langeweile)
  int screen = SCREEN_LIMITS;
  int rotateSec = 0;
  uint32_t screenSince = 0;  // millis() des letzten Screen-Wechsels, für das automatische Weiterschalten
  String plan, err;
  uint32_t lastMsgMs = 0;
  bool everReceived = false;
  int64_t epochOffset = 0;  // epoch = millis()/1000 + epochOffset
  uint32_t fetchedAt = 0;
  int32_t tzOffset = 0;
} st;

// Was zuletzt gezeichnet wurde – nur Änderungen neu zeichnen.
struct Drawn {
  int screen = -1, dots = -1, brand = -1, theme = -1;
  bool full = false;  // Vollbild-Theme (eigener Rahmen statt Kopf- und Fußzeile)
  bool search = false;  // Suchbildschirm (keine Verbindung zum Agent) ist zu sehen
  String searchHint = "\x01";
  int searchDots = -1;
  int gauges = 0;  // 0 = Gauges müssen komplett neu gezeichnet werden
  int pct[3] = {-2, -2, -2};
  String reset[3] = {"\x01", "\x01", "\x01"};
  String tok[4][3];
  String tp[6];
  String ag[5];   // Kopfzeile + 4 Zeilen des Sessions-Screens
  String cl[16];  // Texte der Limit-Themes
  String plan = "\x01", status = "\x01", clock = "\x01", footer = "\x01";
} drawn;

String lineBuf;

// Laufschrift für den Tätigkeitstext auf dem Zeiterfassungs-Screen
struct Marquee {
  String text;
  uint16_t col = 0;
  uint32_t since = 0;  // millis() seit dem der Text steht – die Bewegung beginnt dort von vorn
  int offset = -1;     // zuletzt gezeichnete Verschiebung in Pixeln
} marquee;

// ---------- Helfer ----------

uint32_t nowEpoch() {
  if (st.epochOffset == 0) return 0;
  return (uint32_t)(millis() / 1000 + st.epochOffset);
}

String fmtDur(int32_t s) {
  if (s <= 0) return "jetzt";
  int d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  if (d > 0) return String(d) + "T " + h + "h";
  if (h > 0) return String(h) + "h " + m + "m";
  if (m > 0) return String(m) + "m";
  return "<1m";
}

// 1234 -> "1,2k", 365726099 -> "366M", 6012021878 -> "6,0Mrd"
String fmtTok(double v) {
  static const char* const UNITS[] = {"", "k", "M", "Mrd"};
  int u = 0;
  while (v >= 1000 && u < 3) { v /= 1000; u++; }
  char buf[16];
  if (u == 0 || v >= 100) snprintf(buf, sizeof(buf), "%.0f%s", v, UNITS[u]);
  else snprintf(buf, sizeof(buf), "%.1f%s", v, UNITS[u]);
  String out = buf;
  out.replace(".", ",");
  return out;
}

uint16_t gaugeColor(int pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 75) return C_AMBER;
  return C_ORANGE;
}

// Text flimmerfrei über ein kleines Sprite zeichnen.
void textBox(int x, int y, int w, int h, const String& s, const GFXfont* f, uint16_t col,
             uint8_t datum = MC_DATUM, uint8_t font = 2) {
  if (!spr.createSprite(w, h)) return;
  spr.fillSprite(C_BG);
  spr.setTextColor(col, C_BG);
  if (f) spr.setFreeFont(f);
  else spr.setTextFont(font);
  spr.setTextDatum(datum);
  int tx = (datum == ML_DATUM) ? 0 : (datum == MR_DATUM) ? w - 1 : w / 2;
  spr.drawString(s, tx, h / 2);
  spr.pushSprite(x, y);
  spr.deleteSprite();
}

void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// ---------- Zeichnen ----------

// Claude-"Sonne": Strahlen unterschiedlicher Länge um einen Mittelpunkt.
void drawLogo(int cx, int cy) {
  const int rays = 12;
  for (int i = 0; i < rays; i++) {
    float a = i * TWO_PI / rays + 0.15f;
    float len = (i % 3 == 0) ? 11.5f : (i % 2 == 0 ? 9.0f : 10.5f);
    tft.drawWideLine(cx + cosf(a) * 2.5f, cy + sinf(a) * 2.5f, cx + cosf(a) * len, cy + sinf(a) * len,
                     2.6f, C_ORANGE, C_BG);
  }
}

const int G_A0 = 40, G_A1 = 320;  // 0° = unten, im Uhrzeigersinn
const int RESET_LABEL_Y = 169, RESET_Y = 186;  // "Reset" über der Restzeit, unter dem Gauge

struct GaugeLayout {
  int cy, r, ir, x[3];
  const GFXfont* font;
  int resetW;
};
const GaugeLayout LAYOUT2 = {116, 64, 53, {80, 240, 0}, &FreeSansBold18pt7b, 152};
const GaugeLayout LAYOUT3 = {120, 50, 42, {55, 160, 265}, &FreeSansBold12pt7b, 104};

void drawGauge(const GaugeLayout& L, int idx, const Limit& l, const char* label) {
  int cx = L.x[idx];
  tft.drawSmoothArc(cx, L.cy, L.r, L.ir, G_A0, G_A1, C_TRACK, C_BG, true);
  if (l.valid && l.pct > 0) {
    int end = G_A0 + (G_A1 - G_A0) * min(l.pct, 100) / 100;
    if (end < G_A0 + 3) end = G_A0 + 3;
    tft.drawSmoothArc(cx, L.cy, L.r, L.ir, G_A0, end, gaugeColor(l.pct), C_BG, true);
  }
  String p = l.valid ? String(l.pct) + "%" : "--";
  int pw = 2 * L.ir - 14, lw = 2 * L.ir - 16;
  textBox(cx - pw / 2, L.cy - L.ir / 2, pw, L.ir * 2 / 3, p, L.font, C_TEXT);
  textBox(cx - lw / 2, L.cy + L.ir / 4, lw, 16, label, nullptr, C_MUTED);
}

// Uhr-Symbol für den Zeiterfassungs-Screen, gleiche Größe wie das Claude-Logo.
void drawClockIcon(int cx, int cy) {
  tft.drawSmoothCircle(cx, cy, 11, C_ORANGE, C_BG);
  tft.drawSmoothCircle(cx, cy, 10, C_ORANGE, C_BG);
  tft.drawWideLine(cx, cy, cx, cy - 7, 2.2f, C_ORANGE, C_BG);
  tft.drawWideLine(cx, cy, cx + 5, cy + 2, 2.2f, C_ORANGE, C_BG);
}

// Kopfzeile links: Claude-Logo + Plan, auf dem Zeiterfassungs-Screen Uhr + "TimePunch".
void drawBrand(int brand) {
  tft.fillRect(0, 0, 184, 40, C_BG);
  if (brand == 1) {
    drawClockIcon(20, 20);
    textBox(38, 6, 140, 28, "TimePunch", &FreeSansBold12pt7b, C_TEXT, ML_DATUM);
  } else {
    drawLogo(20, 20);
    textBox(38, 6, 90, 28, "Claude", &FreeSansBold12pt7b, C_TEXT, ML_DATUM);
  }
  drawn.plan = "\x01";
}

void drawStatic() {
  tft.fillScreen(C_BG);
  tft.drawFastHLine(12, 40, 296, C_TRACK);
}

// Ein Screen wird nur angeboten, wenn der Agent Daten dafür schickt.
bool screenAvailable(int s) {
  if (s == SCREEN_SESSIONS) return st.ag.valid;
  if (s == SCREEN_TOKENS) return st.tokens.valid;
  if (s == SCREEN_TIME) return st.tp.valid;
  return true;
}

// Seitenanzeige ganz unten: ein Punkt pro verfügbarem Screen, der aktive hervorgehoben.
// Vollbild-Themes geben ihre eigenen Farben und die Höhe mit.
void drawPageDots(uint16_t bg, uint16_t on, uint16_t off, int y) {
  int count = 0, active = 0;
  for (int s = 0; s < SCREEN_COUNT; s++) {
    if (!screenAvailable(s)) continue;
    if (s == st.screen) active = count;
    count++;
  }
  int key = count * 10 + active;
  if (key == drawn.dots) return;
  const int gap = 14, x0 = 160 - (count - 1) * gap / 2;
  tft.fillRect(130, y - 5, 60, 11, bg);  // Platz für bis zu vier Punkte
  if (count > 1)
    for (int i = 0; i < count; i++)
      tft.fillSmoothCircle(x0 + i * gap, y, 3, i == active ? on : off, bg);
  drawn.dots = key;
}

void drawPageDots() { drawPageDots(C_BG, C_ORANGE, C_TRACK, 234); }

void renderGauges(uint32_t now);
void renderClawd(uint32_t now);
void drawSessionsHeader();
void renderSessions();
void drawTokenHeader();
void renderTokens();
void drawTimeHeader();
void renderTime(uint32_t now);

void renderBattery(uint32_t now);
void drawSynthStatic();
void renderSynth(uint32_t now);
void drawRetroStatic();
void drawCozyStatic(bool stale);
extern uint16_t SY_BG, SY_PINK, SY_GRID, RT_GRAY, RT_NAVY, RT_DARK, CZ_UI_BG, CZ_UI_YELLOW, CZ_UI_TRACK;
void renderCozy(uint32_t now, bool stale);
void renderRetro(uint32_t now, bool stale);
void renderSearch();

// LED: aus = alles ok, gelb ab 75 %, rot ab 90 % (Session, Woche oder Fable) – auf allen Screens
void updateLed(bool stale) {
  int worst = -1;
  for (const Limit* l : {&st.session, &st.weekly, &st.fable})
    if (l->valid) worst = max(worst, l->pct);
  if (stale) setLed(false, false, false);
  else if (worst >= 90) setLed(true, false, false);
  else if (worst >= 75) setLed(true, true, false);
  else setLed(false, false, false);
}

bool fullScreenTheme() {
  return st.everReceived && st.screen == SCREEN_LIMITS && (st.theme == THEME_SYNTH || st.theme == THEME_RETRO ||
                                                              st.theme == THEME_COZY || st.theme == THEME_CLIPPY);
}

void render() {
  bool stale = !st.everReceived || (millis() - st.lastMsgMs > STALE_AFTER_MS);
  uint32_t now = nowEpoch();

  // Ohne Verbindung zum Agent sucht Clawd den PC – egal welches Theme eingestellt ist.
  if (stale) {
    renderSearch();
    updateLed(true);
    return;
  }

  // Synthwave und Retro zeichnen den ganzen Bildschirm selbst, ohne Kopf- und Fußzeile.
  bool full = fullScreenTheme();
  if (drawn.search || full != drawn.full || (full && (st.theme != drawn.theme || st.screen != drawn.screen))) {
    drawn.search = false;
    if (!full) drawStatic();  // normaler Rahmen: alles neu
    drawn.brand = drawn.dots = drawn.screen = drawn.theme = -1;
    drawn.plan = drawn.status = drawn.clock = drawn.footer = "\x01";
    for (auto& cell : drawn.cl) cell = "\x01";
    drawn.full = full;
    if (full) {
      if (st.theme == THEME_SYNTH) drawSynthStatic();
      else if (st.theme == THEME_COZY) drawCozyStatic(stale);
      else drawRetroStatic();  // Retro und Clippy teilen sich das Fenster
      drawn.theme = st.theme;
      drawn.screen = st.screen;
    }
  }
  if (full) {
    if (st.theme == THEME_SYNTH) {
      renderSynth(now);
      drawPageDots(SY_BG, SY_PINK, SY_GRID, 230);
    } else if (st.theme == THEME_COZY) {
      renderCozy(now, stale);
      drawPageDots(CZ_UI_BG, CZ_UI_YELLOW, CZ_UI_TRACK, 232);
    } else {
      renderRetro(now, stale);
      drawPageDots(RT_GRAY, RT_NAVY, RT_DARK, 228);
    }
    updateLed(stale);
    return;
  }

  // Kopfzeile: Marke (Claude oder TimePunch), Plan, Uhrzeit, Status
  int brand = (st.everReceived && st.screen == SCREEN_TIME) ? 1 : 0;
  if (brand != drawn.brand) {
    drawBrand(brand);
    drawn.brand = brand;
  }
  if (brand == 0 && st.plan != drawn.plan) {
    textBox(128, 10, 56, 22, st.plan, nullptr, C_MUTED, ML_DATUM);
    drawn.plan = st.plan;
  }
  String clock;
  if (now) {
    uint32_t local = now + st.tzOffset;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
    clock = buf;
  }
  if (clock != drawn.clock) {
    textBox(186, 10, 52, 22, clock, nullptr, C_TEXT, MR_DATUM);
    drawn.clock = clock;
  }
  String status = stale ? "OFFLINE" : "LIVE";
  if (status != drawn.status) {
    uint16_t col = stale ? C_RED : C_GREEN;
    textBox(240, 10, 68, 22, status, nullptr, col, MR_DATUM);
    tft.fillSmoothCircle(308 - tft.textWidth(status, 2) - 10, 21, 4, col, C_BG);
    drawn.status = status;
  }

  if (!st.everReceived) return;

  if (!screenAvailable(st.screen)) st.screen = SCREEN_LIMITS;  // Daten weggefallen
  if (st.screen != drawn.screen || st.theme != drawn.theme) {
    tft.fillRect(0, 42, 320, 172, C_BG);  // Warte-Text bzw. anderen Screen/Theme entfernen
    drawn.gauges = 0;
    for (auto& row : drawn.tok)
      for (auto& cell : row) cell = "\x01";
    for (auto& cell : drawn.tp) cell = "\x01";
    for (auto& cell : drawn.ag) cell = "\x01";
    for (auto& cell : drawn.cl) cell = "\x01";
    drawn.footer = "\x01";
    drawn.theme = st.theme;
    if (st.screen == SCREEN_SESSIONS) drawSessionsHeader();
    if (st.screen == SCREEN_TOKENS) drawTokenHeader();
    if (st.screen == SCREEN_TIME) drawTimeHeader();
    drawn.screen = st.screen;
  }
  drawPageDots();
  if (st.screen == SCREEN_LIMITS) {
    if (st.theme == THEME_CLAWD) renderClawd(now);
    else if (st.theme == THEME_BATTERY) renderBattery(now);
    else renderGauges(now);
  }
  else if (st.screen == SCREEN_SESSIONS) renderSessions();
  else if (st.screen == SCREEN_TOKENS) renderTokens();
  else renderTime(now);

  String footer;
  uint16_t footerCol = C_MUTED;
  if (stale) {
    footer = "Keine Verbindung zum PC";
    footerCol = C_RED;
  } else if (st.screen == SCREEN_TOKENS || st.screen == SCREEN_SESSIONS) {
    footer = "Claude Code auf diesem PC";
  } else if (st.screen == SCREEN_TIME) {
    if (st.tp.open || st.tp.errors) {
      footer = String(st.tp.open) + " offen, " + st.tp.errors + " mit Fehler";
      footerCol = st.tp.errors ? C_AMBER : C_MUTED;
    } else {
      footer = "alles gebucht";
    }
  } else if (st.err.length()) {
    footer = st.err;
    footerCol = C_AMBER;
  } else if (st.fetchedAt && now) {
    int32_t age = (int32_t)(now - st.fetchedAt);
    footer = age < 60 ? String("aktualisiert gerade eben") : "aktualisiert vor " + fmtDur(age);
  }
  if (footer != drawn.footer) {
    textBox(4, 212, 312, 16, footer, nullptr, footerCol);
    drawn.footer = footer;
  }
  updateLed(stale);
}

void renderGauges(uint32_t now) {
  // Zwei Gauges (Session, Woche) oder drei, wenn ein Fable-Limit mitkommt.
  int n = st.fable.valid ? 3 : 2;
  const GaugeLayout& L = (n == 3) ? LAYOUT3 : LAYOUT2;
  if (n != drawn.gauges) {
    tft.fillRect(0, 42, 320, 172, C_BG);  // altes Layout entfernen
    for (int i = 0; i < 3; i++) { drawn.pct[i] = -2; drawn.reset[i] = "\x01"; }
    drawn.gauges = n;
  }

  const Limit* limits[3] = {&st.session, &st.weekly, &st.fable};
  static const char* const LABELS[3] = {"5h", "WOCHE", "FABLE"};
  for (int i = 0; i < n; i++) {
    const Limit& l = *limits[i];
    int p = l.valid ? l.pct : -1;
    if (p != drawn.pct[i]) { drawGauge(L, i, l, LABELS[i]); drawn.pct[i] = p; }
    String rt;
    if (l.valid && l.resetsAt && now) {
      int32_t left = (int32_t)(l.resetsAt - now);
      rt = left > 0 ? "in " + fmtDur(left) : fmtDur(left);
    }
    if (rt != drawn.reset[i]) {
      textBox(L.x[i] - L.resetW / 2, RESET_LABEL_Y, L.resetW, 16, rt.length() ? "Reset" : "", nullptr, C_MUTED);
      textBox(L.x[i] - L.resetW / 2, RESET_Y, L.resetW, 20, rt, nullptr, C_TEXT);
      drawn.reset[i] = rt;
    }
  }
}

// Token-Tabelle: Zeilen = Kennzahlen, Spalten = heute / 7 Tage / 30 Tage
const int TOK_COL_R[3] = {176, 244, 312};  // rechte Kante der Wertspalten
const int TOK_COL_W = 66, TOK_ROW_Y0 = 80, TOK_ROW_DY = 32;
const char* const TOK_ROWS[4] = {"Output", "Input", "Cache-Read", "Anfragen"};

void drawTokenHeader() {
  static const char* const COLS[3] = {"Heute", "7 Tage", "30 Tage"};
  textBox(8, 50, 100, 20, "TOKENS", nullptr, C_MUTED, ML_DATUM);
  for (int c = 0; c < 3; c++)
    textBox(TOK_COL_R[c] - TOK_COL_W, 50, TOK_COL_W, 20, COLS[c], nullptr, C_MUTED, MR_DATUM);
  tft.drawFastHLine(8, 72, 304, C_TRACK);
  for (int r = 0; r < 4; r++)
    textBox(8, TOK_ROW_Y0 + r * TOK_ROW_DY, 100, 22, TOK_ROWS[r], nullptr, C_TEXT, ML_DATUM);
}

void renderTokens() {
  for (int c = 0; c < 3; c++) {
    const double* v = st.tokens.v[c];
    // Input zählt den neu geschriebenen Cache mit, Cache-Read steht separat.
    double rows[4] = {v[1], v[0] + v[2], v[3], v[4]};
    for (int r = 0; r < 4; r++) {
      String cell = st.tokens.valid ? (r == 3 ? String((long)rows[r]) : fmtTok(rows[r])) : "--";
      if (cell == drawn.tok[r][c]) continue;
      textBox(TOK_COL_R[c] - TOK_COL_W, TOK_ROW_Y0 + r * TOK_ROW_DY, TOK_COL_W, 22, cell,
              &FreeSans9pt7b, r == 0 ? C_ORANGE : C_TEXT, MR_DATUM);
      drawn.tok[r][c] = cell;
    }
  }
}

// Zeiterfassung (TimePunch Helper): laufende Erfassung oben, darunter Tageswerte
const int TP_ROW_Y0 = 127, TP_ROW_DY = 20;
const char* const TP_ROWS[4] = {"Heute erfasst", "Rest heute", "Heute gebucht", "Woche erfasst"};

// 452 -> "7:32 h"
String fmtHM(int minutes) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d:%02d h", minutes / 60, minutes % 60);
  return buf;
}

// Pause am Anfang und Ende, dazwischen mit konstanter Geschwindigkeit hin bzw. zurück.
void drawMarquee(bool force) {
  const int w = 304, pauseMs = 2000, pxPerSec = 30;
  int overflow = tft.textWidth(marquee.text, 2) - w;
  int offset = 0;
  if (overflow > 0) {
    uint32_t scrollMs = (uint32_t)overflow * 1000 / pxPerSec;
    uint32_t phase = (millis() - marquee.since) % (2 * (pauseMs + scrollMs));
    if (phase < pauseMs) offset = 0;
    else if (phase < pauseMs + scrollMs) offset = (phase - pauseMs) * overflow / scrollMs;
    else if (phase < 2 * pauseMs + scrollMs) offset = overflow;
    else offset = overflow - (phase - 2 * pauseMs - scrollMs) * overflow / scrollMs;
  }
  if (!force && offset == marquee.offset) return;
  if (!spr.createSprite(w, 18)) return;
  spr.fillSprite(C_BG);
  spr.setTextColor(marquee.col, C_BG);
  spr.setTextFont(2);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(marquee.text, -offset, 9);
  spr.pushSprite(8, 103);
  spr.deleteSprite();
  marquee.offset = offset;
}

void drawTimeHeader() {
  textBox(8, 50, 200, 20, "LAEUFT GERADE", nullptr, C_MUTED, ML_DATUM);
  tft.drawFastHLine(8, 72, 304, C_TRACK);
  tft.drawFastHLine(8, 123, 304, C_TRACK);
  for (int r = 0; r < 4; r++)
    textBox(8, TP_ROW_Y0 + r * TP_ROW_DY, 150, 20, TP_ROWS[r], nullptr, C_TEXT, ML_DATUM);
}

void renderTime(uint32_t now) {
  const TimePunch& t = st.tp;
  String cells[6];
  if (t.running && t.runStart && now) {
    uint32_t local = t.runStart + st.tzOffset;
    char since[16];
    snprintf(since, sizeof(since), "seit %02u:%02u", (unsigned)(local / 3600 % 24), (unsigned)(local / 60 % 60));
    cells[0] = fmtHM(now > t.runStart ? (now - t.runStart) / 60 : 0) + "|" + since;
    cells[1] = t.title;
  } else {
    cells[0] = "|";
    cells[1] = "Keine laufende Erfassung";
  }
  // Die laufende Erfassung zählt bis jetzt mit; der Agent liefert alle 30 s neue Tageswerte.
  int rest = t.sollMin - t.dayMin;
  cells[2] = fmtHM(t.dayMin);
  cells[3] = !t.sollMin ? String("--") : rest > 0 ? fmtHM(rest) : "+" + fmtHM(-rest);
  cells[4] = fmtHM(t.bookedMin);
  cells[5] = fmtHM(t.weekMin);

  if (cells[0] != drawn.tp[0]) {
    int bar = cells[0].indexOf('|');
    textBox(8, 78, 150, 26, cells[0].substring(0, bar), &FreeSans12pt7b, C_ORANGE, ML_DATUM);
    textBox(162, 78, 150, 26, cells[0].substring(bar + 1), nullptr, C_MUTED, MR_DATUM);
    drawn.tp[0] = cells[0];
  }
  if (cells[1] != drawn.tp[1]) {
    marquee.text = cells[1];
    marquee.col = t.running ? C_TEXT : C_MUTED;
    marquee.since = millis();
    drawMarquee(true);
    drawn.tp[1] = cells[1];
  }
  for (int r = 0; r < 4; r++) {
    if (cells[r + 2] == drawn.tp[r + 2]) continue;
    uint16_t col = (r == 1 && t.sollMin && rest <= 0) ? C_GREEN : C_TEXT;  // Soll erreicht
    textBox(212, TP_ROW_Y0 + r * TP_ROW_DY, 100, 20, cells[r + 2], &FreeSans9pt7b, col, MR_DATUM);
    drawn.tp[r + 2] = cells[r + 2];
  }
}

// ---------- Sessions-Screen ----------

const int AG_ROW_Y0 = 78, AG_ROW_DY = 33;

void drawSessionsHeader() {
  textBox(8, 50, 120, 20, "SESSIONS", nullptr, C_MUTED, ML_DATUM);
  tft.drawFastHLine(8, 72, 304, C_TRACK);
}

void renderSessions() {
  const Agents& a = st.ag;
  String head = String(a.sessions) + (a.sessions == 1 ? " Session, " : " Sessions, ") + a.subagents +
                (a.subagents == 1 ? " Agent" : " Agenten");
  if (head != drawn.ag[0]) {
    textBox(130, 50, 182, 20, head, nullptr, C_MUTED, MR_DATUM);
    drawn.ag[0] = head;
  }
  for (int i = 0; i < 4; i++) {
    String key = i < a.rows ? a.row[i].name + (a.row[i].busy ? "|1" : "|0") + (a.row[i].sub ? "1|" : "0|") + a.row[i].text
                            : (i == 0 ? "leer" : "");
    if (key == drawn.ag[i + 1]) continue;
    drawn.ag[i + 1] = key;
    int y = AG_ROW_Y0 + i * AG_ROW_DY;
    tft.fillRect(0, y, 320, AG_ROW_DY, C_BG);
    if (i >= a.rows) {
      if (i == 0) textBox(8, y + 8, 304, 18, "Gerade laeuft keine Session", nullptr, C_MUTED, ML_DATUM);
      continue;
    }
    const AgentRow& r = a.row[i];
    int x = r.sub ? 30 : 8;  // Subagenten eingerückt unter den Sessions
    uint16_t col = r.busy ? C_ORANGE : C_TRACK;
    if (r.sub) tft.drawSmoothCircle(x + 5, y + 8, 4, C_ORANGE, C_BG);
    else tft.fillSmoothCircle(x + 5, y + 8, 4, col, C_BG);
    textBox(x + 16, y, 220 - x, 16, r.sub ? "Agent: " + r.name : r.name, nullptr, C_TEXT, ML_DATUM);
    textBox(240, y, 72, 16, r.busy ? "arbeitet" : "wartet", nullptr, r.busy ? C_ORANGE : C_MUTED, MR_DATUM);
    textBox(x + 16, y + 15, 296 - x, 16, r.text, nullptr, C_MUTED, ML_DATUM);
  }
}

// ---------- Theme "clawd": Limit-Screen mit animiertem Maskottchen ----------
//
// Clawd ist ein Pixel-Krabbler (14 x 8 Pixel, Pixelgröße CL_P). Seine Laune hängt davon ab, wie viel
// gerade los ist und wie knapp die Limits werden.

enum Mood { MOOD_SLEEP, MOOD_BORED, MOOD_WORK, MOOD_BUSY, MOOD_TIGHT, MOOD_STRESS, MOOD_SEARCH };

// Suchen: Clawd pendelt hin und her (-1..1) und schaut zur Seite, wenn er außen steht.
float searchSwing(uint32_t t) { return sinf(t / 1100.0f); }
int searchDir(uint32_t t) {
  float s = searchSwing(t);
  return s > 0.45f ? 1 : s < -0.45f ? -1 : 0;
}

const int CL_P = 6;                          // Größe eines Clawd-Pixels
const int CL_SX = 100, CL_SY = 96, CL_SW = 120, CL_SH = 90;  // Sprite-Bereich auf dem Display
uint16_t C_SWEAT;

int worstPct() {
  int worst = -1;
  for (const Limit* l : {&st.session, &st.weekly, &st.fable})
    if (l->valid) worst = max(worst, l->pct);
  return worst;
}

Mood currentMood(bool stale) {
  if (stale) return MOOD_SLEEP;
  int worst = worstPct();
  if (worst >= 90) return MOOD_STRESS;
  if (st.activity >= 3) return MOOD_BUSY;
  if (worst >= 75) return MOOD_TIGHT;
  if (st.activity > 0) return MOOD_WORK;
  uint32_t idle = millis() - st.lastActiveMs;
  if (st.activity == 0 && idle > 10UL * 60 * 1000) return MOOD_SLEEP;
  if (st.activity == 0 && idle > 60UL * 1000) return MOOD_BORED;
  return MOOD_WORK;
}

String moodText(Mood m, bool stale) {
  switch (m) {
    case MOOD_SLEEP: return stale ? "offline ... zzz" : "Zzz ... nix los";
    case MOOD_BORED: return "langweilt sich";
    case MOOD_BUSY: return "Vollgas: " + String(st.activity) + " Jobs!";
    case MOOD_TIGHT: return "wird langsam knapp ...";
    case MOOD_STRESS: return "Hilfe, Limit fast weg!";
    default:
      if (st.activity == 1) return "werkelt fleissig";
      if (st.activity == 2) return "jongliert 2 Jobs";
      return "wartet auf Arbeit";
  }
}

// Zeichnet Clawd ins Sprite. (x0, y0) = linke obere Ecke des 14 x 8-Rasters.
void drawClawdBody(int x0, int y0, Mood m, uint32_t t) {
  const int P = CL_P;
  bool busy = m == MOOD_BUSY || m == MOOD_STRESS;

  // Beine: im Laufschritt abwechselnd angehoben
  int dir = m == MOOD_SEARCH ? searchDir(t) : 0;
  bool walking = busy || m == MOOD_WORK || m == MOOD_TIGHT || (m == MOOD_SEARCH && dir == 0);
  int legPhase = (t / (busy ? 120 : m == MOOD_SEARCH ? 220 : 450)) % 2;
  const int legCols[4] = {3, 5, 8, 10};
  for (int i = 0; i < 4; i++) {
    int h = (walking && i % 2 == legPhase) ? P : 2 * P;
    spr.fillRect(x0 + legCols[i] * P, y0 + 6 * P, P, h, C_ORANGE);
  }

  // Körper und Arme (bei Vollgas winken die Arme abwechselnd)
  spr.fillRect(x0 + 2 * P, y0, 10 * P, 6 * P, C_ORANGE);
  // Beim Suchen hält er die Hand auf der Blickseite über die Augen.
  int wave = busy ? (t / 160) % 2 : -1;
  int rowL = (wave == 0 || dir == -1) ? (dir == -1 ? 0 : 1) : 3;
  int rowR = (wave == 1 || dir == 1) ? (dir == 1 ? 0 : 1) : 3;
  spr.fillRect(x0, y0 + rowL * P, 2 * P, P, C_ORANGE);
  spr.fillRect(x0 + 12 * P, y0 + rowR * P, 2 * P, P, C_ORANGE);

  // Augen: offen, blinzelnd, halb zu (gelangweilt), zu (schläft) oder "> <" (Stress)
  int look = 0;
  if (m == MOOD_WORK || m == MOOD_TIGHT) {
    int phase = (t / 2600) % 4;  // schaut ab und zu nach links und rechts
    look = phase == 1 ? -P / 2 : phase == 3 ? P / 2 : 0;
  } else if (m == MOOD_SEARCH) {
    look = dir * (P * 3 / 4);
  }
  bool blink = (t % 3700) < 140;
  bool yawn = m == MOOD_BORED && (t % 9000) > 7600;
  const int eyeCols[2] = {4, 9};
  for (int e = 0; e < 2; e++) {
    int ex = x0 + eyeCols[e] * P + look, ey = y0 + P;
    if (m == MOOD_STRESS) {
      int dir = e == 0 ? 1 : -1, xa = e == 0 ? ex : ex + P - 1;
      spr.drawWideLine(xa, ey, xa + dir * (P - 1), ey + P, 2, C_BG, C_ORANGE);
      spr.drawWideLine(xa + dir * (P - 1), ey + P, xa, ey + 2 * P, 2, C_BG, C_ORANGE);
    } else if (m == MOOD_SLEEP || yawn) {
      spr.fillRect(ex - 1, ey + P + 2, P + 2, 2, C_BG);
    } else if (blink) {
      spr.fillRect(ex, ey + 2 * P - 3, P, 2, C_BG);
    } else if (m == MOOD_BORED) {
      spr.fillRect(ex, ey + P, P, P, C_BG);
    } else {
      spr.fillRect(ex, ey, P, 2 * P, C_BG);
    }
  }
  if (yawn) spr.fillRect(x0 + 6 * P, y0 + 4 * P - 2, 2 * P, P + 2, C_BG);  // Gähnen
}

void drawClawdMood(int px, int py, int w, int h, uint16_t bg, Mood m) {
  if (!spr.createSprite(w, h)) return;
  spr.fillSprite(bg);
  uint32_t t = millis();
  int dx = 0, dy = 0;
  switch (m) {
    case MOOD_SEARCH: dx = (int)roundf(18 * searchSwing(t)); break;            // läuft hin und her
    case MOOD_BUSY: dy = -(int)(fabsf(sinf(t / 140.0f)) * 7); break;          // hüpft
    case MOOD_STRESS: dx = (int)(t / 50 % 5) - 2; dy = -(int)(t / 70 % 3); break;  // zittert
    case MOOD_BORED: dx = (int)roundf(4 * sinf(t / 1300.0f)); break;         // schaukelt
    case MOOD_SLEEP: dy = (t / 1500) % 2; break;                               // atmet
    default: dy = (int)roundf(2 * sinf(t / 420.0f)); break;                    // wippt
  }
  int x0 = (w - 14 * CL_P) / 2 + dx, y0 = h - 60 + dy;
  if (m == MOOD_SEARCH) spr.fillEllipse(x0 + 7 * CL_P, y0 + 8 * CL_P + 3, 5 * CL_P, 3, C_TRACK);  // Schatten
  drawClawdBody(x0, y0, m, t);

  // Fragezeichen beim Suchen, Zzz beim Schlafen, Schweißtropfen wenn es knapp wird
  if (m == MOOD_SEARCH) {
    if (searchDir(t) == 0) {
      spr.setTextFont(4);
      spr.setTextColor(C_TEXT, bg);
      spr.setTextDatum(BC_DATUM);
      spr.drawString("?", x0 + 7 * CL_P, y0 - 4 - (int)(t / 120 % 4));
    }
  } else if (m == MOOD_SLEEP) {
    spr.setTextFont(2);
    spr.setTextColor(C_MUTED, bg);
    spr.setTextDatum(MC_DATUM);
    for (int i = 0; i < 3; i++) {
      uint32_t ph = (t + i * 1000) % 3000;
      spr.drawString(i == 2 ? "Z" : "z", x0 + 12 * CL_P + ph / 200, y0 + 4 - ph / 110);
    }
  } else if (m == MOOD_TIGHT || m == MOOD_STRESS) {
    int drop = (t / 25) % 40;
    int sx = x0 + 13 * CL_P, sy = y0 + drop / 2;
    spr.fillTriangle(sx, sy - 6, sx - 3, sy, sx + 3, sy, C_SWEAT);
    spr.fillSmoothCircle(sx, sy + 1, 3, C_SWEAT, bg);
  }
  spr.pushSprite(px, py);
  spr.deleteSprite();
}

void drawClawdAt(int px, int py, int w, int h, uint16_t bg, bool stale) {
  drawClawdMood(px, py, w, h, bg, currentMood(stale));
}

void drawClawd(bool stale) { drawClawdAt(CL_SX, CL_SY, CL_SW, CL_SH, C_BG, stale); }

// ---------- Keine Verbindung: Clawd sucht den PC ----------

const int SR_X = 70, SR_Y = 50, SR_W = 180, SR_H = 112;  // Sprite-Bereich für den suchenden Clawd

void drawSearchClawd() { drawClawdMood(SR_X, SR_Y, SR_W, SR_H, C_BG, MOOD_SEARCH); }

void renderSearch() {
  if (!drawn.search) {
    drawStatic();
    drawLogo(20, 20);
    textBox(38, 6, 90, 28, "Claude", &FreeSansBold12pt7b, C_TEXT, ML_DATUM);
    textBox(240, 10, 68, 22, "OFFLINE", nullptr, C_RED, MR_DATUM);
    tft.fillSmoothCircle(308 - tft.textWidth("OFFLINE", 2) - 10, 21, 4, C_RED, C_BG);
    drawSearchClawd();
    drawn.search = true;
    drawn.searchHint = "\x01";
    drawn.searchDots = -1;
    drawn.screen = drawn.theme = -1;  // beim Wiederverbinden alles neu zeichnen
  }

  // "Warte auf PC-Agent" mit 0–3 Punkten; linksbündig ab fester Stelle, damit der Text nicht springt
  int dots = millis() / 1000 % 4;
  if (dots != drawn.searchDots) {
    tft.setFreeFont(&FreeSans9pt7b);
    int x = 160 - tft.textWidth("Warte auf PC-Agent ...") / 2;
    textBox(x, 168, 320 - 2 * x + 8, 24, String("Warte auf PC-Agent ") + String("...").substring(0, dots),
            &FreeSans9pt7b, C_TEXT, ML_DATUM);
    drawn.searchDots = dots;
  }

  String hint = st.everReceived
                    ? "Verbindung weg seit " + fmtDur((millis() - st.lastMsgMs) / 1000)
                    : String("VibeCYD Studio am PC starten");
  if (hint != drawn.searchHint) {
    textBox(4, 198, 312, 18, hint, nullptr, C_MUTED);
    drawn.searchHint = hint;
  }
}

#include "themes.h"
#include "themes_cozy_clippy.h"

void renderClawd(uint32_t now) {
  auto resetIn = [&](const Limit& l) -> String {
    if (!l.valid || !l.resetsAt || !now) return "";
    int32_t left = (int32_t)(l.resetsAt - now);
    return left > 0 ? "in " + fmtDur(left) : fmtDur(left);
  };
  auto pct = [](const Limit& l) -> String { return l.valid ? String(l.pct) + "%" : "--"; };
  bool stale = millis() - st.lastMsgMs > STALE_AFTER_MS;
  String cells[11] = {
      "Session", pct(st.session), st.session.valid ? "Reset" : "", resetIn(st.session),
      "Woche", pct(st.weekly), st.weekly.valid ? "Reset" : "", resetIn(st.weekly),
      st.fable.valid ? "Fable " + pct(st.fable) : "", st.fable.valid ? resetIn(st.fable) : "",
      moodText(currentMood(stale), stale)};
  for (int i = 0; i < 11; i++) {
    if (cells[i] == drawn.cl[i]) continue;
    drawn.cl[i] = cells[i];
    int col = i / 4;  // 0 = links (Session), 1 = rechts (Woche), 2 = Fable + Laune
    int x = col == 0 ? 8 : 214;
    uint8_t datum = col == 0 ? ML_DATUM : MR_DATUM;
    const Limit& l = col == 0 ? st.session : st.weekly;
    switch (i) {
      case 0: case 4: textBox(x, 48, 98, 16, cells[i], nullptr, C_MUTED, datum); break;
      case 1: case 5:
        textBox(x, 64, 98, 34, cells[i], &FreeSansBold18pt7b, l.valid ? gaugeColor(l.pct) : C_MUTED, datum);
        break;
      case 2: case 6: textBox(x, 100, 98, 16, cells[i], nullptr, C_MUTED, datum); break;
      case 3: case 7: textBox(x, 116, 98, 16, cells[i], nullptr, C_TEXT, datum); break;
      case 8:
        textBox(214, 142, 98, 16, cells[i], nullptr, st.fable.valid ? gaugeColor(st.fable.pct) : C_MUTED, MR_DATUM);
        break;
      case 9: textBox(214, 158, 98, 16, cells[i], nullptr, C_TEXT, MR_DATUM); break;
      case 10: textBox(40, 190, 240, 18, cells[i], nullptr, C_ORANGE, MC_DATUM); break;
    }
  }
  drawClawd(stale);
}

// ---------- Serial ----------

void parseLimit(JsonVariantConst v, Limit& l) {
  if (v.isNull()) { l.valid = false; return; }
  l.valid = true;
  l.pct = (int)lroundf(v["p"] | 0.0f);
  l.resetsAt = v["r"] | 0u;
}

void parseTokens(JsonVariantConst v, Tokens& t) {
  static const char* const KEYS[3] = {"d", "w", "m"};
  t.valid = !v.isNull();
  for (int c = 0; c < 3; c++)
    for (int i = 0; i < 5; i++) t.v[c][i] = v[KEYS[c]][i] | 0.0;
}

void parseTimePunch(JsonVariantConst v, TimePunch& t) {
  t.valid = !v.isNull();
  if (!t.valid) return;
  JsonVariantConst run = v["run"];
  t.running = !run.isNull();
  t.title = run["t"] | "";
  t.runStart = run["s"] | 0u;
  t.dayMin = v["d"] | 0;
  t.bookedMin = v["b"] | 0;
  t.sollMin = v["soll"] | 0;
  t.weekMin = v["wk"] | 0;
  t.open = v["o"] | 0;
  t.errors = v["e"] | 0;
}

// Zum nächsten Screen mit Daten blättern (Touch oder automatisch).
void nextScreen() {
  do st.screen = (st.screen + 1) % SCREEN_COUNT;
  while (!screenAvailable(st.screen));
  st.screenSince = millis();
}

void parseAgents(JsonVariantConst v, Agents& a) {
  a.valid = !v.isNull();
  a.rows = 0;
  if (!a.valid) return;
  a.sessions = v["n"] | 0;
  a.subagents = v["na"] | 0;
  for (JsonVariantConst s : v["s"].as<JsonArrayConst>()) {
    if (a.rows >= 4) break;
    AgentRow& r = a.row[a.rows++];
    r.name = s[0] | "";
    r.busy = (s[1] | 0) != 0;
    r.text = s[2] | "";
    r.sub = false;
  }
  for (JsonVariantConst s : v["a"].as<JsonArrayConst>()) {
    if (a.rows >= 4) break;
    AgentRow& r = a.row[a.rows++];
    r.name = s[0] | "";
    r.text = s[1] | "";
    r.busy = r.sub = true;
  }
}

void handleLine(const String& line) {
  if (line == "?") { Serial.println("VIBECYD 1"); return; }
  if (line == "N") {  // nächster Screen, z. B. per Tastenkürzel am PC
    if (st.everReceived) { nextScreen(); render(); }
    return;
  }
  if (line.length() == 2 && line[0] == 'S' && isDigit(line[1])) {  // Screen direkt wählen
    int s = line[1] - '0';
    if (st.everReceived && s < SCREEN_COUNT && screenAvailable(s)) {
      st.screen = s;
      st.screenSince = millis();
      render();
    }
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, line)) return;

  uint32_t now = doc["now"] | 0u;
  if (now) st.epochOffset = (int64_t)now - (int64_t)(millis() / 1000);
  parseLimit(doc["s"], st.session);
  parseLimit(doc["w"], st.weekly);
  parseLimit(doc["f"], st.fable);
  parseTokens(doc["tk"], st.tokens);
  parseTimePunch(doc["tp"], st.tp);
  st.rotateSec = doc["rot"] | 0;
  parseAgents(doc["ag"], st.ag);
  st.theme = constrain((int)(doc["th"] | 0), 0, THEME_COUNT - 1);
  int br = constrain((int)(doc["br"] | 100), 5, 100);
  if (br != st.brightness) {
    st.brightness = br;
    ledcWrite(BL_CHANNEL, br * 255 / 100);
  }
  st.activity = doc["act"] | -1;
  if (st.activity > 0 || !st.lastActiveMs) st.lastActiveMs = millis();
  st.plan = doc["plan"] | "";
  st.err = doc["err"] | "";
  st.fetchedAt = doc["t"] | 0u;
  st.tzOffset = doc["tz"] | 0;
  st.lastMsgMs = millis();
  st.everReceived = true;
  render();
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      lineBuf.trim();
      if (lineBuf.length()) handleLine(lineBuf);
      lineBuf = "";
    } else if (c != '\r' && lineBuf.length() < 4096) {
      lineBuf += c;
    }
  }
}

// ---------- Setup / Loop ----------

void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, false);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(DISPLAY_INVERT);

  C_BG = tft.color565(31, 30, 29);
  C_TRACK = tft.color565(58, 57, 54);
  C_ORANGE = tft.color565(217, 119, 87);
  C_TEXT = tft.color565(245, 244, 238);
  C_MUTED = tft.color565(156, 154, 146);
  C_RED = tft.color565(224, 87, 79);
  C_GREEN = tft.color565(127, 176, 105);
  C_AMBER = tft.color565(229, 180, 90);
  C_SWEAT = tft.color565(120, 180, 235);
  initThemeColors();
  initCozyColors();
  initClippyColors();

  // Hintergrundbeleuchtung per PWM, damit die Helligkeit einstellbar ist
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  ledcWrite(BL_CHANNEL, 255);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);

  spr.setColorDepth(16);
  render();
  Serial.println("VIBECYD 1");
}

// Bei jedem neuen Antippen (nach Loslassen) zwischen den Screens wechseln.
void pollTouch() {
  static bool wasTouched = false;
  static uint32_t lastSwitch = 0;
  bool touched = ts.touched();
  if (touched && !wasTouched && millis() - lastSwitch > 300) {
    nextScreen();
    lastSwitch = millis();
    render();
  }
  wasTouched = touched;
}

void loop() {
  pollSerial();
  pollTouch();
  static uint32_t lastMarquee = 0;
  if (drawn.screen == SCREEN_TIME && millis() - lastMarquee >= 40) {
    lastMarquee = millis();
    drawMarquee(false);
  }
  static uint32_t lastAnim = 0;
  if ((drawn.search || drawn.screen == SCREEN_LIMITS) && millis() - lastAnim >= 70) {
    lastAnim = millis();
    bool stale = millis() - st.lastMsgMs > STALE_AFTER_MS;
    if (drawn.search) drawSearchClawd();
    else if (drawn.theme == THEME_CLAWD) drawClawd(stale);
    else if (drawn.theme == THEME_BATTERY) animateBattery();
    else if (drawn.theme == THEME_SYNTH) animateSynth(nowEpoch(), stale);
    else if (drawn.theme == THEME_RETRO) drawClawdAt(14, 32, 116, 88, RT_GRAY, stale);
    else if (drawn.theme == THEME_CLIPPY) drawClippyAt(14, 32, 116, 88, stale);
    else if (drawn.theme == THEME_COZY) animateCozy(stale);
  }
  if (st.rotateSec > 0 && st.everReceived && millis() - st.screenSince >= (uint32_t)st.rotateSec * 1000) {
    nextScreen();
    render();
  }
  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    render();
  }
  delay(5);
}
