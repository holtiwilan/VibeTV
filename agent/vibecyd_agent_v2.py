#!/usr/bin/env python3
"""VibeCYD-Agent v2: liest die Claude-Nutzungslimits lokal aus und schickt sie per USB ans CYD.

Gegenüber vibecyd_agent.py kommen optional die Daten des TimePunch Helpers dazu (eigener Screen auf
dem Display). Was mitgeschickt wird, steht in vibecyd_config.json neben diesem Skript:

    {"tokens": true, "sessions": true, "theme": "clawd", "rotate_seconds": 0, "hotkey": "Ctrl+Alt+F12",
     "timepunch": {"enabled": true, "db": "%LOCALAPPDATA%\\TimePunchHelper\\ledger.db"}}

rotate_seconds > 0 schaltet die Screens auf dem Display automatisch weiter (0 = nur per Touch).
hotkey ist ein globales Windows-Tastenkürzel für den nächsten Screen ("" = aus).
sessions zeigt einen Screen mit den laufenden Claude-Code-Sessions und Subagenten.
theme wählt die Darstellung des Limit-Screens: "gauges" (Rundinstrumente) oder "clawd" (animiertes Maskottchen).
Änderungen werden nach wenigen Sekunden ohne Neustart übernommen. Fehlt die Datei, sind die Tokens an,
TimePunch und das Weiterschalten aus. Die TimePunch-Datenbank wird nur lesend geöffnet.

Der OAuth-Token wird nur gelesen (aus ~/.claude/.credentials.json bzw. dem macOS-Schlüsselbund),
nie verändert oder irgendwohin außer api.anthropic.com geschickt. Läuft der Token ab, erneuert
Claude Code ihn beim nächsten Start selbst.
"""
import argparse
import ctypes
import glob
import json
import os
import re
import sqlite3
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import date, datetime, timedelta

import serial
import serial.tools.list_ports

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
KNOWN_USB_VIDS = {0x1A86, 0x10C4, 0x303A, 0x0403}  # CH340, CP210x, Espressif, FTDI
SESSION_ACTIVE_SECS = 60  # Subagent gilt als aktiv, wenn sein Verlauf so frisch ist
SEND_EVERY = 5  # Sekunden; das Display gilt nach 30 s ohne Daten als offline
TOKENS_EVERY = 300  # Sekunden zwischen zwei Auswertungen der lokalen Claude-Code-Verläufe
TIMEPUNCH_EVERY = 30  # Sekunden zwischen zwei Abfragen der TimePunch-Datenbank
CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vibecyd_config.json")
USAGE_CACHE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".usage_cache.json")
DEFAULT_CONFIG = {
    "tokens": True,
    "sessions": False,
    "theme": "gauges",
    "rotate_seconds": 0,
    "hotkey": "",
    "timepunch": {"enabled": False, "db": r"%LOCALAPPDATA%\TimePunchHelper\ledger.db"},
}


class UsageError(Exception):
    retry = 0  # Sekunden laut Retry-After bei 429


def log(msg):
    print(f"[{datetime.now():%H:%M:%S}] {msg}", flush=True)


def ascii_only(s, limit=48):
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("Ä", "Ae"), ("Ö", "Oe"), ("Ü", "Ue"), ("ß", "ss")):
        s = s.replace(a, b)
    return s.encode("ascii", "replace").decode()[:limit]


# ---------- Claude ----------

def load_oauth():
    base = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.join(os.path.expanduser("~"), ".claude")
    path = os.path.join(base, ".credentials.json")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    elif sys.platform == "darwin":
        out = subprocess.run(["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
                             capture_output=True, text=True)
        if out.returncode != 0:
            raise UsageError("Keine Claude-Credentials gefunden")
        data = json.loads(out.stdout)
    else:
        raise UsageError("Keine Claude-Credentials gefunden")

    oauth = data.get("claudeAiOauth") or {}
    if not oauth.get("accessToken"):
        raise UsageError("Nicht angemeldet - in Claude Code /login")
    expires = oauth.get("expiresAt") or 0
    if expires and expires / 1000 < time.time():
        raise UsageError("Token abgelaufen - Claude Code starten")
    return oauth


def to_epoch(value):
    if not value:
        return 0
    if isinstance(value, (int, float)):
        return int(value / 1000 if value > 1e12 else value)
    return int(datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp())


def parse_usage(data):
    """Bevorzugt das neuere limits[]-Array, fällt auf five_hour/seven_day zurück.
    Das Fable-Wochenlimit gibt es nur im limits[]-Array (kind weekly_scoped)."""
    session = weekly = fable = None
    for item in data.get("limits") or []:
        entry = {"p": item.get("percent") or 0, "r": to_epoch(item.get("resets_at"))}
        model = ((item.get("scope") or {}).get("model") or {}).get("display_name") or ""
        if item.get("kind") == "session" and session is None:
            session = entry
        elif item.get("kind") == "weekly_all" and weekly is None:
            weekly = entry
        elif item.get("kind") == "weekly_scoped" and model.lower() == "fable" and fable is None:
            fable = entry
    if session is None and data.get("five_hour"):
        fh = data["five_hour"]
        session = {"p": fh.get("utilization") or 0, "r": to_epoch(fh.get("resets_at"))}
    if weekly is None and data.get("seven_day"):
        sd = data["seven_day"]
        weekly = {"p": sd.get("utilization") or 0, "r": to_epoch(sd.get("resets_at"))}
    return session, weekly, fable


def fetch_usage(debug=False):
    oauth = load_oauth()
    req = urllib.request.Request(USAGE_URL, headers={
        "Authorization": f"Bearer {oauth['accessToken']}",
        "anthropic-beta": "oauth-2025-04-20",
        "Accept": "application/json",
        "User-Agent": "vibecyd/0.2",
    })
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as e:
        if e.code == 429:
            retry = int(e.headers.get("Retry-After") or 0)
            limited = UsageError(f"Rate limit (429){f', retry {retry}s' if retry else ''}")
            limited.retry = retry
            raise limited from e
        if e.code in (401, 403):
            raise UsageError("Token ungueltig - Claude Code starten") from e
        raise UsageError(f"HTTP {e.code}") from e
    except urllib.error.URLError as e:
        raise UsageError("Keine Internetverbindung") from e

    if debug:
        log("Antwort: " + json.dumps(data, indent=2))
    session, weekly, fable = parse_usage(data)
    plan = (oauth.get("subscriptionType") or "").capitalize()
    return {"plan": plan, "s": session, "w": weekly, "f": fable}


def demo_usage():
    now = time.time()
    phase = (now / 60) % 100
    return {
        "plan": "Demo",
        "s": {"p": round(phase), "r": int(now + 2 * 3600 + 840)},
        "w": {"p": round(phase / 3 + 10), "r": int(now + 3 * 86400 + 5 * 3600)},
        "f": {"p": round(phase / 2), "r": int(now + 3 * 86400 + 5 * 3600)},
    }


# ---------- Tokens (lokale Claude-Code-Verläufe) ----------

class TokenStats:
    """Zählt die Tokens aus ~/.claude/projects/**/*.jsonl. Dateien werden nur neu gelesen, wenn sie sich
    geändert haben; jede Antwort zählt einmal, auch wenn sie in mehreren Verläufen vorkommt."""

    def __init__(self):
        base = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.join(os.path.expanduser("~"), ".claude")
        self.pattern = os.path.join(base, "projects", "**", "*.jsonl")
        self.files = {}  # Pfad -> ((mtime, size), {Antwort-Key: (Tag, [in, out, cache_write, cache_read])})

    @staticmethod
    def read_file(path):
        entries = {}
        with open(path, encoding="utf-8", errors="ignore") as f:
            for line in f:
                if '"usage"' not in line:
                    continue
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                msg = d.get("message") or {}
                u, ts = msg.get("usage"), d.get("timestamp")
                if not isinstance(u, dict) or not ts:
                    continue
                day = datetime.fromisoformat(ts.replace("Z", "+00:00")).astimezone().date()
                entries[(msg.get("id"), d.get("requestId"))] = (day, [
                    u.get("input_tokens") or 0, u.get("output_tokens") or 0,
                    u.get("cache_creation_input_tokens") or 0, u.get("cache_read_input_tokens") or 0])
        return entries

    def collect(self):
        found = {}
        for path in glob.glob(self.pattern, recursive=True):
            try:
                st = os.stat(path)
            except OSError:
                continue
            sig = (st.st_mtime, st.st_size)
            cached = self.files.get(path)
            found[path] = cached if cached and cached[0] == sig else (sig, self.read_file(path))
        self.files = found

        today = date.today()
        ranges = {"d": today, "w": today - timedelta(days=6), "m": today - timedelta(days=29)}
        totals = {k: [0, 0, 0, 0, 0] for k in ranges}  # in, out, cache_write, cache_read, Anfragen
        seen = set()
        for _, entries in self.files.values():
            for key, (day, vals) in entries.items():
                if key in seen:
                    continue
                seen.add(key)
                for k, since in ranges.items():
                    if day >= since:
                        t = totals[k]
                        for i, v in enumerate(vals):
                            t[i] += v
                        t[4] += 1
        return totals


# ---------- Config ----------

def load_config(path):
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            user = json.load(f)
        cfg["tokens"] = bool(user.get("tokens", cfg["tokens"]))
        cfg["sessions"] = bool(user.get("sessions", cfg["sessions"]))
        cfg["theme"] = "clawd" if str(user.get("theme") or "").lower() == "clawd" else "gauges"
        cfg["rotate_seconds"] = max(int(user.get("rotate_seconds") or 0), 0)
        cfg["hotkey"] = str(user.get("hotkey") or "")
        cfg["timepunch"].update(user.get("timepunch") or {})
    return cfg


def load_usage_cache():
    """Zuletzt abgerufene Limits (höchstens 6 h alt) -> (usage, fetched_at), sonst (None, 0)."""
    try:
        with open(USAGE_CACHE_PATH, encoding="utf-8") as f:
            data = json.load(f)
        if time.time() - data["t"] < 6 * 3600:
            return data["usage"], int(data["t"])
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None, 0


def save_usage_cache(usage, fetched_at):
    try:
        with open(USAGE_CACHE_PATH, "w", encoding="utf-8") as f:
            json.dump({"usage": usage, "t": fetched_at}, f)
    except OSError as e:
        log(f"Cache nicht gespeichert: {e}")


# ---------- Laufende Sessions und Subagenten ----------

def pid_alive(pid):
    if sys.platform == "win32":
        k32 = ctypes.windll.kernel32
        h = k32.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ok = k32.GetExitCodeProcess(h, ctypes.byref(code))
        k32.CloseHandle(h)
        return bool(ok) and code.value == 259  # STILL_ACTIVE
    try:
        os.kill(int(pid), 0)
        return True
    except OSError:
        return False


def tool_activity(item):
    """Kurztext für einen tool_use-Block, z. B. "Edit main.cpp" oder die Beschreibung eines Bash-Befehls."""
    name, inp = item.get("name") or "", item.get("input") or {}
    if inp.get("description"):
        return inp["description"]
    for key in ("file_path", "notebook_path", "path"):
        if inp.get(key):
            return f"{name} {os.path.basename(str(inp[key]).rstrip('/\\'))}"
    if inp.get("pattern"):
        return f"{name} {inp['pattern']}"
    return name


class Sessions:
    """Laufende Claude-Code-Sessions aus ~/.claude/sessions/<pid>.json und ihre gerade aktiven Subagenten.
    Woran gearbeitet wird, steht am Ende des jeweiligen Verlaufs (letzter Werkzeugaufruf, KI-Titel)."""

    TAIL = 256 * 1024

    def __init__(self):
        self.base = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.join(os.path.expanduser("~"), ".claude")
        self.titles = {}  # sessionId -> Titel, einmal komplett gesucht und danach aus dem Dateiende aktualisiert

    def transcript(self, session_id):
        found = glob.glob(os.path.join(self.base, "projects", "*", f"{session_id}.jsonl"))
        return found[0] if found else None

    def read_tail(self, path, whole=False):
        with open(path, "rb") as f:
            if not whole:
                f.seek(max(os.path.getsize(path) - self.TAIL, 0))
            return f.read().decode("utf-8", "ignore").splitlines()

    def inspect(self, session_id):
        """-> (Titel, aktueller Schritt) aus dem Verlauf der Session."""
        path = self.transcript(session_id)
        if not path:
            return self.titles.get(session_id, ""), ""
        lines = self.read_tail(path)
        if session_id not in self.titles:
            self.titles[session_id] = ""
            lines = self.read_tail(path, whole=True)
        activity = None
        for line in reversed(lines):
            if '"ai-title"' in line and '"aiTitle"' in line:
                try:
                    self.titles[session_id] = json.loads(line).get("aiTitle") or self.titles[session_id]
                except ValueError:
                    pass
                break
        for line in reversed(lines):
            if activity is None and '"type":"assistant"' in line:
                try:
                    content = (json.loads(line).get("message") or {}).get("content") or []
                except ValueError:
                    continue
                tools = [c for c in content if isinstance(c, dict) and c.get("type") == "tool_use"]
                if tools:
                    activity = tool_activity(tools[-1])
                elif any(isinstance(c, dict) and c.get("type") == "text" for c in content):
                    activity = "schreibt Antwort"
                elif content:
                    activity = "denkt nach"
                break
        return self.titles[session_id], activity or "denkt nach"

    def subagents(self, session_id, now):
        result = []
        for path in glob.glob(os.path.join(self.base, "projects", "*", session_id, "subagents", "*.jsonl")):
            try:
                if now - os.path.getmtime(path) > SESSION_ACTIVE_SECS:
                    continue
                with open(path[:-len(".jsonl")] + ".meta.json", encoding="utf-8") as f:
                    meta = json.load(f)
            except (OSError, ValueError):
                meta = {}
            result.append([ascii_only(meta.get("agentType") or "Agent", 20),
                           ascii_only(meta.get("description") or "", 60)])
        return result

    def collect(self):
        now = time.time()
        sessions, agents = [], []
        for path in glob.glob(os.path.join(self.base, "sessions", "*.json")):
            try:
                with open(path, encoding="utf-8") as f:
                    d = json.load(f)
            except (OSError, ValueError):
                continue
            if not d.get("pid") or not pid_alive(d["pid"]):
                continue
            sid = d.get("sessionId") or ""
            busy = d.get("status") == "busy"
            title, activity = self.inspect(sid) if sid else ("", "")
            name = os.path.basename(str(d.get("cwd") or "").rstrip("/\\")) or d.get("name") or "Session"
            sessions.append([ascii_only(name, 30), 1 if busy else 0,
                             ascii_only(activity if busy else (title or "wartet"), 60)])
            if sid:
                agents += self.subagents(sid, now)
        sessions.sort(key=lambda x: -x[1])
        busy = sum(x[1] for x in sessions)
        return {"s": sessions[:4], "a": agents[:4], "n": len(sessions), "na": len(agents)}, busy + len(agents)


def demo_sessions():
    return {"s": [["VibeTV", 1, "Edit main.cpp"], ["BEIREFA-UIA-Testautomation", 0, "UIA-Tests automatisieren"]],
            "a": [["Explore", "Umgebungen, DB-Zugang, Reset-Mechanismen"]], "n": 2, "na": 1}, 2


# ---------- Tastenkürzel (nur Windows) ----------

MODIFIERS = {"ctrl": 0x2, "strg": 0x2, "alt": 0x1, "shift": 0x4, "umschalt": 0x4, "win": 0x8}
MOD_NOREPEAT = 0x4000


def parse_hotkey(text):
    """"Ctrl+Alt+F12" -> (Modifier-Bits, Virtual-Key) oder None."""
    mods, vk = 0, None
    for part in (p.strip().lower() for p in text.split("+") if p.strip()):
        if part in MODIFIERS:
            mods |= MODIFIERS[part]
        elif re.fullmatch(r"f([1-9]|1\d|2[0-4])", part):
            vk = 0x6F + int(part[1:])  # VK_F1 = 0x70
        elif re.fullmatch(r"[a-z0-9]", part):
            vk = ord(part.upper())
        else:
            return None
    return (mods, vk) if vk is not None else None


class Hotkey(threading.Thread):
    """Registriert das Tastenkürzel aus der Config und setzt bei jedem Druck `pressed`.
    RegisterHotKey gilt nur für den Thread, der die Nachrichten abholt – deshalb läuft alles hier."""

    def __init__(self, wake):
        super().__init__(daemon=True)
        self.wanted, self.active = "", None
        self.pressed = 0
        self.wake = wake  # weckt die Hauptschleife, damit der Befehl sofort rausgeht

    def run(self):
        user32 = ctypes.windll.user32
        msg = ctypes.create_string_buffer(64)  # MSG-Struktur, Inhalt wird nicht gebraucht
        while True:
            if self.wanted != self.active:
                if self.active:
                    user32.UnregisterHotKey(None, 1)
                self.active = self.wanted
                if self.active:
                    parsed = parse_hotkey(self.active)
                    if not parsed:
                        log(f"Tastenkuerzel nicht erkannt: {self.active}")
                    elif user32.RegisterHotKey(None, 1, parsed[0] | MOD_NOREPEAT, parsed[1]):
                        log(f"Tastenkuerzel aktiv: {self.active}")
                    else:
                        log(f"Tastenkuerzel {self.active} ist schon von einem anderen Programm belegt")
            while user32.PeekMessageW(msg, None, 0x0312, 0x0312, 1):  # WM_HOTKEY, PM_REMOVE
                self.pressed += 1
                self.wake.set()
            time.sleep(0.05)


# ---------- TimePunch Helper ----------

def parse_local(value):
    """ISO-Zeit aus dem TimePunch Helper (bis 7 Nachkommastellen, mit oder ohne Offset) -> lokale datetime."""
    value = re.sub(r"(\.\d{6})\d+", r"\1", value)
    return datetime.fromisoformat(value).astimezone()  # ohne Offset = Ortszeit


def fetch_timepunch(db_path):
    """Laufende Erfassung, heute erfasst/gebucht/Soll, offene/fehlerhafte Einträge, Woche erfasst (Minuten).
    Die Soll-Arbeitszeit kommt aus targetDayHours in der settings.json neben der Datenbank."""
    path = os.path.expandvars(db_path)
    if not os.path.exists(path):
        raise UsageError("TimePunch-Datenbank nicht gefunden")
    now = datetime.now().astimezone()
    today = now.date()
    monday = today - timedelta(days=today.weekday())
    con = sqlite3.connect(f"file:{path}?mode=ro", uri=True, timeout=2)
    try:
        rows = con.execute("SELECT day, status, data FROM segments WHERE day >= ?", (monday.isoformat(),)).fetchall()
        n_open, n_err = con.execute(
            "SELECT SUM(status = 'Offen'), SUM(status = 'Fehler') FROM segments").fetchone()
    finally:
        con.close()

    run, day_min, booked_min, week_min = None, 0.0, 0.0, 0.0
    for day, status, data in rows:
        if status == "Verworfen":
            continue
        seg = json.loads(data)
        start = parse_local(seg["start"])
        end = parse_local(seg["end"]) if seg.get("end") else now
        minutes = max((end - start).total_seconds() / 60, 0)
        week_min += minutes
        if day == today.isoformat():
            day_min += minutes
            if status == "Gebucht" and seg.get("bookedStart") and seg.get("bookedEnd"):
                booked_min += (parse_local(seg["bookedEnd"]) - parse_local(seg["bookedStart"])).total_seconds() / 60
        if status == "Laufend":
            ctx = seg.get("context") or {}
            title = ctx.get("title") or seg.get("description") or ""
            if ctx.get("ticketId"):
                title = f"#{ctx['ticketId']} {title}"
            run = {"t": ascii_only(title, 120), "s": int(start.timestamp())}
    soll_hours = 8
    settings = os.path.join(os.path.dirname(path), "settings.json")
    if os.path.exists(settings):
        with open(settings, encoding="utf-8-sig") as f:
            soll_hours = json.load(f).get("targetDayHours", soll_hours)
    return {"run": run, "d": round(day_min), "b": round(booked_min), "soll": round(soll_hours * 60),
            "wk": round(week_min), "o": n_open or 0, "e": n_err or 0}


def demo_timepunch():
    now = time.time()
    return {"run": {"t": "#206938 Stabilisierung-2.12.0 - FHB - HB-2438 - Pruefung", "s": int(now - 4980)},
            "d": 372, "b": 300, "soll": 480, "wk": 1890, "o": 2, "e": 0}


# ---------- Serial ----------

def open_port(name):
    ser = serial.Serial()
    ser.port = name
    ser.baudrate = 115200
    ser.timeout = 0.3
    ser.dtr = False  # kein Auto-Reset des ESP32 beim Öffnen
    ser.rts = False
    ser.open()
    return ser


def handshake(ser):
    ser.reset_input_buffer()
    ser.write(b"?\n")
    deadline = time.time() + 1.5
    while time.time() < deadline:
        if b"VIBECYD" in ser.readline():
            return True
    return False


def find_device(port=None):
    if port:
        candidates = [port]
    else:
        ports = serial.tools.list_ports.comports()
        candidates = [p.device for p in ports if p.vid in KNOWN_USB_VIDS] or [p.device for p in ports]
    for name in candidates:
        try:
            ser = open_port(name)
        except serial.SerialException:
            continue
        if port or handshake(ser):
            return ser
        ser.close()
    return None


# ---------- Main ----------

def main():
    ap = argparse.ArgumentParser(description="Schickt Claude-Usage ans Cheap Yellow Display")
    ap.add_argument("--port", help="COM-Port, z. B. COM5 (Standard: automatisch)")
    ap.add_argument("--interval", type=int, default=120, help="Abfrage-Intervall in Sekunden (Standard 120)")
    ap.add_argument("--demo", action="store_true", help="Testwerte statt echter Daten")
    ap.add_argument("--debug", action="store_true", help="API-Antwort ausgeben")
    ap.add_argument("--config", default=CONFIG_PATH, help="Pfad zur Config (Standard: vibecyd_config.json)")
    args = ap.parse_args()
    cfg, cfg_mtime = None, None

    ser = None
    usage, fetched_at, err = None, 0, ""
    next_fetch, backoff = 0.0, args.interval
    if not args.demo:
        # Letzte Werte übernehmen: das Display ist nach einem Neustart sofort gefüllt, und solange sie
        # jünger als das Intervall sind, fragt der Agent die API nicht gleich wieder ab (429).
        usage, fetched_at = load_usage_cache()
        if usage:
            next_fetch = fetched_at + args.interval
    token_stats, tokens, next_tokens = TokenStats(), None, 0.0
    session_watch, sessions, activity = Sessions(), None, None
    wake = threading.Event()
    hotkey = Hotkey(wake) if sys.platform == "win32" else None
    if hotkey:
        hotkey.start()
    hotkey_sent = 0
    timepunch, next_timepunch = None, 0.0

    while True:
        now = time.time()

        # Config bei jeder Änderung neu laden – kein Neustart nötig. Ist sie gerade ungültig
        # (z. B. halb gespeichert), bleibt die bisherige aktiv.
        try:
            mtime = os.path.getmtime(args.config)
        except OSError:
            mtime = 0
        if mtime != cfg_mtime:
            try:
                new_cfg = load_config(args.config)
            except (OSError, ValueError, TypeError, AttributeError) as e:
                new_cfg = None
                log(f"Config ungueltig, bleibe bei der bisherigen: {e}")
            if new_cfg is None and cfg is None:
                new_cfg = json.loads(json.dumps(DEFAULT_CONFIG))
            if new_cfg is not None:
                if not new_cfg["tokens"]:
                    tokens = None
                elif not (cfg and cfg["tokens"]):
                    next_tokens = 0.0
                if not new_cfg["timepunch"].get("enabled"):
                    timepunch = None
                elif not (cfg and cfg["timepunch"] == new_cfg["timepunch"]):
                    next_timepunch = 0.0
                cfg = new_cfg
                tp_cfg, rot = cfg["timepunch"], cfg["rotate_seconds"]
                if hotkey:
                    hotkey.wanted = cfg["hotkey"]
                log(f"Config: Tokens {'an' if cfg['tokens'] else 'aus'}, Sessions {'an' if cfg['sessions'] else 'aus'}, "
                    f"Theme {cfg['theme']}, "
                    f"TimePunch {'an' if tp_cfg.get('enabled') else 'aus'}, "
                    f"Weiterschalten {f'alle {rot} s' if rot else 'aus'}")
            cfg_mtime = mtime

        if now >= next_fetch:
            try:
                usage = demo_usage() if args.demo else fetch_usage(args.debug)
                fetched_at, err, backoff = int(now), "", args.interval
                if not args.demo:
                    save_usage_cache(usage, fetched_at)
                s, w, f = usage["s"] or {}, usage["w"] or {}, usage["f"]
                log(f"Session {s.get('p', '?')}% | Woche {w.get('p', '?')}%"
                    + (f" | Fable {f.get('p', '?')}%" if f else ""))
            except UsageError as e:
                err = str(e)
                backoff = max(min(backoff * 2, 900), e.retry) if "429" in err else args.interval
                log(f"Fehler: {err} (naechster Versuch in {backoff}s)")
            except Exception as e:  # noqa: BLE001 - Agent soll weiterlaufen
                err = "Unerwarteter Fehler"
                log(f"Fehler: {e!r}")
            next_fetch = now + (10 if args.demo else backoff)

        if tp_cfg.get("enabled") and now >= next_timepunch:
            try:
                timepunch = demo_timepunch() if args.demo else fetch_timepunch(tp_cfg.get("db") or "")
            except (UsageError, sqlite3.Error, ValueError, KeyError) as e:
                timepunch = None
                log(f"TimePunch: {e}")
            next_timepunch = now + TIMEPUNCH_EVERY

        # Sessions sind billig zu lesen und werden bei jedem Durchlauf aktualisiert. Die Zahl der
        # arbeitenden Sessions/Subagenten bestimmt auch die Laune des Maskottchens.
        if cfg["sessions"] or cfg["theme"] == "clawd":
            try:
                sessions, activity = demo_sessions() if args.demo else session_watch.collect()
            except Exception as e:  # noqa: BLE001 - Agent soll weiterlaufen
                sessions, activity = None, None
                log(f"Fehler beim Lesen der Sessions: {e!r}")
        else:
            sessions, activity = None, None

        if cfg["tokens"] and now >= next_tokens:
            try:
                tokens = token_stats.collect()
                log(f"Tokens heute: {tokens['d'][1]:,} out | 7 Tage: {tokens['w'][1]:,} out")
            except Exception as e:  # noqa: BLE001 - Agent soll weiterlaufen
                log(f"Fehler beim Token-Zaehlen: {e!r}")
            next_tokens = now + TOKENS_EVERY

        if ser is None:
            ser = find_device(args.port)
            if ser:
                log(f"Display verbunden an {ser.port}")

        if ser is not None:
            tz = time.localtime(now).tm_gmtoff  # UTC-Offset inkl. Sommerzeit, für die Uhr auf dem Display
            payload = {"now": int(now), "t": fetched_at, "tz": tz, "rot": rot, "err": ascii_only(err)}
            if usage:
                payload.update(usage)
            if tokens:
                payload["tk"] = tokens
            if timepunch:
                payload["tp"] = timepunch
            if sessions and cfg["sessions"]:
                payload["ag"] = sessions
            if activity is not None:
                payload["act"] = activity
            if cfg["theme"] == "clawd":
                payload["th"] = 1
            # Je Druck auf das Tastenkürzel ein "N" (nächster Screen) vor den Daten
            presses = (hotkey.pressed - hotkey_sent) if hotkey else 0
            try:
                ser.write(("N\n" * presses + json.dumps(payload, separators=(",", ":")) + "\n").encode())
                hotkey_sent += presses
                ser.reset_input_buffer()
            except serial.SerialException:
                log("Display getrennt")
                ser.close()
                ser = None

        wake.wait(SEND_EVERY)
        wake.clear()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
