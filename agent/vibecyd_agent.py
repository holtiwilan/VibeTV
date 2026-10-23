#!/usr/bin/env python3
"""VibeCYD-Agent: liest die Claude-Nutzungslimits lokal aus und schickt sie per USB ans CYD.

Der OAuth-Token wird nur gelesen (aus ~/.claude/.credentials.json bzw. dem macOS-Schlüsselbund),
nie verändert oder irgendwohin außer api.anthropic.com geschickt. Läuft der Token ab, erneuert
Claude Code ihn beim nächsten Start selbst.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import date, datetime, timedelta

import serial
import serial.tools.list_ports

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
KNOWN_USB_VIDS = {0x1A86, 0x10C4, 0x303A, 0x0403}  # CH340, CP210x, Espressif, FTDI
SEND_EVERY = 5  # Sekunden; das Display gilt nach 30 s ohne Daten als offline
TOKENS_EVERY = 300  # Sekunden zwischen zwei Auswertungen der lokalen Claude-Code-Verläufe


class UsageError(Exception):
    pass


def log(msg):
    print(f"[{datetime.now():%H:%M:%S}] {msg}", flush=True)


def ascii_only(s):
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("Ä", "Ae"), ("Ö", "Oe"), ("Ü", "Ue"), ("ß", "ss")):
        s = s.replace(a, b)
    return s.encode("ascii", "replace").decode()[:48]


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
        "User-Agent": "vibecyd/0.1",
    })
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = json.load(resp)
    except urllib.error.HTTPError as e:
        if e.code == 429:
            retry = int(e.headers.get("Retry-After") or 0)
            raise UsageError(f"Rate limit (429){f', retry {retry}s' if retry else ''}") from e
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
    args = ap.parse_args()

    ser = None
    usage, fetched_at, err = None, 0, ""
    next_fetch, backoff = 0.0, args.interval
    token_stats, tokens, next_tokens = TokenStats(), None, 0.0

    while True:
        now = time.time()

        if now >= next_fetch:
            try:
                usage = demo_usage() if args.demo else fetch_usage(args.debug)
                fetched_at, err, backoff = int(now), "", args.interval
                s, w, f = usage["s"] or {}, usage["w"] or {}, usage["f"]
                log(f"Session {s.get('p', '?')}% | Woche {w.get('p', '?')}%"
                    + (f" | Fable {f.get('p', '?')}%" if f else ""))
            except UsageError as e:
                err = str(e)
                backoff = min(backoff * 2, 900) if "429" in err else args.interval
                log(f"Fehler: {err} (naechster Versuch in {backoff}s)")
            except Exception as e:  # noqa: BLE001 - Agent soll weiterlaufen
                err = "Unerwarteter Fehler"
                log(f"Fehler: {e!r}")
            next_fetch = now + (10 if args.demo else backoff)

        if now >= next_tokens:
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
            payload = {"now": int(now), "t": fetched_at, "tz": tz, "err": ascii_only(err)}
            if usage:
                payload.update(usage)
            if tokens:
                payload["tk"] = tokens
            try:
                ser.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
                ser.reset_input_buffer()
            except serial.SerialException:
                log("Display getrennt")
                ser.close()
                ser = None

        time.sleep(SEND_EVERY)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
