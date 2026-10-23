# VibeCYD – Claude-Usage auf dem Cheap Yellow Display

Nachbau der Idee von [VibeTV / Claude TV](https://vibetv.shop/products/claude-tv) auf einem ESP32-2432S028R („Cheap Yellow Display“, CYD).
Das Display zeigt deine Claude-Limits (Session 5 h, Woche, ggf. Fable) mit Reset-Countdown – dieselben Werte wie unter claude.ai → Einstellungen → Nutzung – plus Token-Verbrauch, laufende Claude-Code-Sessions und optional die Zeiterfassung aus dem TimePunch Helper. Sieben Themes, teils animiert, umschaltbar per Touch, Tastenkürzel oder automatisch.

## Schnellstart

1. **App starten:** `release\VibeCYD Studio.exe` (kein Python nötig). Sie legt sich in den Infobereich der Taskleiste, startet ab dann mit Windows und hat einen Startmenü-Eintrag „VibeCYD Studio“.
2. **Display anstecken** (USB). Ist noch keine Firmware drauf: im Fenster unter **Firmware** die Board-Variante wählen und „Firmware bauen und flashen“ klicken.
3. **Einmal in Claude Code anmelden** (`/login`), falls noch nicht geschehen – die App nutzt dessen Anmeldung.

Doppelklick aufs Tray-Symbol (orangefarbenes Pixel-Männchen, ggf. hinter dem **^** neben der Uhr) öffnet die Einstellungen.

## Wie es funktioniert

```
Claude-Server ──Internet──►  VibeCYD Studio (PC)  ──USB, alle 5 s eine JSON-Zeile──►  Display (Firmware zeichnet)
                              liest lokale Dateien
```

Das Display hat selbst kein Internet und kein Konto – es zeichnet nur, was der PC schickt. Kommt 30 s nichts, zeigt es „OFFLINE“.

| Daten | Quelle |
|---|---|
| Limits (Session, Woche, Fable) | `api.anthropic.com/api/oauth/usage`, alle 2 min, mit dem Login-Token von Claude Code (`~/.claude/.credentials.json`, nur gelesen, geht nur an Anthropic) |
| Tokens (heute / 7 / 30 Tage) | Gesprächsverläufe von Claude Code in `~/.claude/projects`, alle 5 min, nur lokal |
| Sessions & Agenten | `~/.claude/sessions` (arbeitet/wartet) und das Ende der Verläufe (aktueller Schritt, Titel, Subagenten) |
| Zeiterfassung | Datenbank des TimePunch Helpers (`%LOCALAPPDATA%\TimePunchHelper\ledger.db`), nur lesend |

Tokens, Sessions und Zeiterfassung beziehen sich nur auf Claude Code **auf diesem PC**; Chats im Browser zählen nur bei den Limits mit.

## Screens

Ein Tipp aufs Display, das Tastenkürzel (Standard **Strg+Alt+F12**) oder das automatische Weiterschalten blättert weiter. Punkte unten zeigen die Seite; Screens ohne Daten werden übersprungen.

- **Limits** – im gewählten Theme (siehe unten). Die RGB-LED auf der Rückseite leuchtet gelb ab 75 %, rot ab 90 %.
- **Sessions** – laufende Claude-Code-Sessions (Projekt, arbeitet/wartet, aktueller Schritt bzw. Titel) und aktive Subagenten.
- **Tokens** – Output, Input, Cache-Read und Anfragen für heute, 7 und 30 Tage. „Input“ enthält die neu in den Cache geschriebenen Tokens.
- **Zeiterfassung** – was gerade läuft (seit wann, wie lange), heute erfasst, Rest bis zur Soll-Arbeitszeit, heute gebucht, Woche erfasst; zu lange Texte laufen als Laufschrift.

## Themes für den Limit-Screen

| Theme | Aussehen |
|---|---|
| `gauges` | Rundinstrumente |
| `clawd` | Pixel-Maskottchen mit Launen |
| `battery` | Akku-Balken, das nächste Segment „lädt“ |
| `synthwave` | Neon, Retro-Sonne, laufendes Gitter, funkelnde Sterne |
| `retro` | 90er-Fenster mit Clawd und Tipps in der Sprechblase |
| `cozy` | Pixel-Szene: Coder am Schreibtisch bei Nacht, Regen, Katze, Lampe, Dampf; darunter Neon-Zahlen |
| `clippy` | Die Büroklammer im 90er-Fenster, schaut umher und zieht die Brauen hoch |

Clawd, Retro, Cozy Coder und Clippy reagieren auf ihre Umgebung: Bei 1–2 arbeitenden Sessions/Agenten werkeln sie, ab 3 ist Vollgas (hüpfen, schneller tippen), ab 75 % Verbrauch schwitzen sie, ab 90 % gibt es Stress, nach 1 Minute ohne Arbeit Langeweile und nach 10 Minuten wird geschlafen.

Clippy ist eine Figur von Microsoft – hier selbst gezeichnete Fan-Art, nur für das eigene Display, nicht zum Weitergeben.

## VibeCYD Studio

Die Tray-App enthält den Agent und steuert alles:

- **Status:** Display-Port, aktuelle Limits, aktive Sessions; Knöpfe „Nächster Screen“ und „Agent neu starten“.
- **Theme** und **Helligkeit** (5–100 %) – nach höchstens einer Sekunde auf dem Display, bei einem Theme-Wechsel springt es zum Limit-Screen.
- **Screens** an/aus (Sessions, Tokens, Zeiterfassung samt Datenbank-Pfad).
- **Bedienung:** automatisch weiterschalten (Sekunden, 0 = aus), Tastenkürzel, Autostart.
- **Firmware:** Board-Variante wählen und flashen. Die App pausiert dafür den Agent, findet das Display selbst und startet den Agent danach wieder.
- **Log** live im Fenster.

Rechtsklick aufs Tray-Symbol: Einstellungen, Nächster Screen, Theme, Agent neu starten, Beenden. Ein zweiter Start öffnet nur das Fenster der laufenden App. Beim Start beendet die App einen separat laufenden Agent (der würde den COM-Port blockieren).

**Ablage:** Die EXE speichert Config, Log und Limit-Cache in `%APPDATA%\VibeCYD`. Als Skript gestartet (`pythonw agent\vibecyd_studio.pyw`) liegen sie in `agent\`.

### Config (`vibecyd_config.json`)

Wird von der App geschrieben; von Hand bearbeiten geht auch – Änderungen wirken ohne Neustart.

```json
{
  "theme": "cozy",
  "brightness": 80,
  "tokens": true,
  "sessions": true,
  "rotate_seconds": 0,
  "hotkey": "Ctrl+Alt+F12",
  "timepunch": { "enabled": true, "db": "%LOCALAPPDATA%\\TimePunchHelper\\ledger.db" },
  "board": "cyd"
}
```

- `hotkey`: Strg/Ctrl, Alt, Shift, Win + F1–F24, Buchstabe oder Ziffer; `""` = aus. Ist es schon belegt, steht das im Log.
- `timepunch`: Soll-Arbeitszeit kommt aus `targetDayHours` in der `settings.json` des TimePunch Helpers.
- `board`: nur fürs Flashen – `cyd` (klassisch, 1 USB-Port) oder `cyd2usb` (2 USB-Ports, invertierte Farben).

## Projektaufbau

```
agent/
  vibecyd_studio.pyw      Tray-App (Einstellungen, Firmware flashen), enthält den Agent v3
  vibecyd_agent_v3.py     Agent als Klasse und Kommandozeilenprogramm
  vibecyd_agent_v2.py     älterer Agent (Kommandozeile, Config-Datei, ohne App) – bleibt erhalten
  vibecyd_agent.py        erster Agent (nur Limits und Tokens) – bleibt erhalten
firmware/
  src/main.cpp            Firmware: Empfang, Screens, Touch, LED
  src/themes.h            Themes Battery, Synthwave, Retro
  src/themes_cozy_clippy.h  Themes Cozy Coder und Clippy
  src/cozy_scene.h        erzeugt von tools/cozy_scene.py
  tools/cozy_scene.py     Pixel-Szene des Cozy Coders, rendert auch eine PNG-Vorschau
tools/build_release.py    baut Firmware-Images und die EXE
release/                  VibeCYD Studio.exe und firmware/vibecyd-*.bin
```

## Selbst bauen

```powershell
pip install -r agent/requirements.txt pyinstaller platformio
python tools/build_release.py
```

Baut die Firmware für beide Board-Varianten, fügt je Bootloader, Partitionen und Programm zu einem Image zusammen (`release/firmware/vibecyd-<board>.bin`, ab Adresse 0) und packt die App samt Images in `release/VibeCYD Studio.exe`.

Als Skript im Projektordner baut die App beim Flashen frisch mit PlatformIO aus `firmware/`; die EXE flasht das mitgelieferte Image mit esptool.

Firmware ohne App flashen:

```powershell
cd firmware
pio run -e cyd -t upload --upload-port COM4        # Variante mit 2 USB-Ports: -e cyd2usb
```

Agent ohne App (Kommandozeile):

```powershell
python agent/vibecyd_agent_v3.py            # echte Daten, Port wird automatisch gefunden
python agent/vibecyd_agent_v3.py --demo     # Testwerte, kein Login nötig
python agent/vibecyd_agent_v3.py --debug    # rohe API-Antwort anzeigen
```

## Hinweise

- Der Limit-Endpoint ist inoffiziell (Claude Codes `/usage` nutzt ihn auch). Ändert Anthropic das Format, hilft `--debug`.
- Der Agent erneuert den Login-Token nicht selbst, damit Claude Code nicht ausgeloggt wird. Ist er abgelaufen, zeigt das Display „Token abgelaufen“; sobald Claude Code einmal läuft, geht es weiter.
- Bei Rate-Limits (429) wartet der Agent zunehmend länger, höchstens 15 min. Die zuletzt geholten Limits werden zwischengespeichert, damit ein Neustart nicht sofort wieder abfragt.
- Der PC muss an sein und die App laufen, sonst zeigt das Display „OFFLINE“.
