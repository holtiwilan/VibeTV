#!/usr/bin/env python3
"""VibeCYD Studio: Hintergrund-App im Infobereich der Taskleiste.

Enthält den Agent (vibecyd_agent_v3) und steuert ihn: Einstellungsfenster für Theme, Helligkeit, Screens,
Weiterschalten, Tastenkürzel und Autostart, dazu Status und Log. Jede Änderung landet sofort in
vibecyd_config.json und ist nach höchstens einer Sekunde auf dem Display zu sehen.

Start: pythonw vibecyd_studio.pyw (ein zweiter Start öffnet nur das Fenster der laufenden Instanz).
"""
import ctypes
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import pystray
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FROZEN = getattr(sys, "frozen", False)  # als EXE gebaut (tools/build_release.py)
sys.path.insert(0, HERE)
import vibecyd_agent_v3 as core  # noqa: E402

APP_NAME = "VibeCYD Studio"
ICON_PATH = os.path.join(core.DATA_DIR, "vibecyd.ico")
STARTUP_DIR = os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs\Startup")
PROGRAMS_DIR = os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs")
AUTOSTART_LNK = os.path.join(STARTUP_DIR, f"{APP_NAME}.lnk")
OLD_AUTOSTART_LNK = os.path.join(STARTUP_DIR, "Claude TV.lnk")  # startete früher den Agent v1/v2 direkt
CREATE_NO_WINDOW = 0x08000000

THEME_LABELS = {
    "gauges": ("Gauges", "Rundinstrumente, klassisch und ruhig"),
    "clawd": ("Clawd", "Pixel-Maskottchen mit Launen: werkelt, hüpft, schwitzt, schläft"),
    "battery": ("Battery", "Akku-Balken, das nächste Segment lädt"),
    "synthwave": ("Synthwave", "Neon, Retro-Sonne und laufendes Gitter"),
    "retro": ("Retro", "90er-Fenster mit Clawd und Tipps in der Sprechblase"),
    "cozy": ("Cozy Coder", "Pixel-Szene: Coder am Schreibtisch, Regen, Katze, Neon-Zahlen"),
    "clippy": ("Clippy", "Die Büroklammer im 90er-Fenster, schaut umher und zieht die Brauen hoch"),
}


# ---------- Windows-Helfer ----------

def single_instance():
    """True, wenn wir die erste Instanz sind. Sonst wird die laufende gebeten, ihr Fenster zu zeigen."""
    k32 = ctypes.windll.kernel32
    k32.CreateMutexW.restype = ctypes.c_void_p
    k32.CreateEventW.restype = ctypes.c_void_p
    k32.OpenEventW.restype = ctypes.c_void_p
    single_instance.mutex = k32.CreateMutexW(None, False, "Local\\VibeCYDStudio")
    if k32.GetLastError() == 183:  # ERROR_ALREADY_EXISTS
        ev = k32.OpenEventW(0x0002, False, "Local\\VibeCYDStudioShow")  # EVENT_MODIFY_STATE
        if ev:
            k32.SetEvent(ctypes.c_void_p(ev))
        return False
    single_instance.show_event = k32.CreateEventW(None, False, False, "Local\\VibeCYDStudioShow")
    return True


def wait_for_show_requests(on_show):
    k32 = ctypes.windll.kernel32
    while True:
        k32.WaitForSingleObject(ctypes.c_void_p(single_instance.show_event), 0xFFFFFFFF)
        on_show()


def powershell(script, **env):
    """Kurzes PowerShell-Skript ohne Fenster; Werte kommen über Umgebungsvariablen (kein Quoting-Ärger)."""
    return subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                          capture_output=True, text=True, creationflags=CREATE_NO_WINDOW,
                          env={**os.environ, **env})


def stop_standalone_agents():
    """Ein separat laufender Agent (v1/v2/v3 per Kommandozeile) würde den COM-Port blockieren."""
    powershell("Get-CimInstance Win32_Process -Filter \"Name like 'python%'\" | "
               "Where-Object CommandLine -like '*vibecyd_agent*' | "
               "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")


def pythonw():
    return shutil.which("pythonw") or sys.executable.replace("python.exe", "pythonw.exe")


def app_command():
    """(Programm, Argumente, Arbeitsordner) zum Starten dieser App – als EXE oder als Skript."""
    if FROZEN:
        return sys.executable, "", os.path.dirname(sys.executable)
    return pythonw(), f'"{os.path.abspath(__file__)}"', HERE


def make_shortcut(path, show=False):
    target, args, workdir = app_command()
    args = (args + " --show").strip() if show else args
    powershell("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:LNK); "
               "$s.TargetPath = $env:TGT; $s.Arguments = $env:ARGS; $s.WorkingDirectory = $env:WD; "
               "$s.IconLocation = $env:ICO; $s.Description = $env:DESC; $s.Save()",
               LNK=path, TGT=target, ARGS=args, WD=workdir, ICO=ICON_PATH,
               DESC="Claude-Usage auf dem Cheap Yellow Display")


def shortcut_target(path):
    if not os.path.exists(path):
        return ""
    out = powershell("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:LNK); "
                     "$s.TargetPath + ' ' + $s.Arguments", LNK=path)
    return out.stdout.strip()


# ---------- Icon ----------

def make_icon_image(size=64):
    """Clawd in Orange auf dunklem, abgerundetem Quadrat."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=size // 6, fill=(31, 30, 29, 255))
    p = size // 16  # Clawd-Pixel
    x0, y0 = (size - 14 * p) // 2, (size - 8 * p) // 2
    orange, dark = (217, 119, 87, 255), (31, 30, 29, 255)

    def px(c, r, w=1, h=1, col=orange):
        d.rectangle([x0 + c * p, y0 + r * p, x0 + (c + w) * p - 1, y0 + (r + h) * p - 1], fill=col)

    px(2, 0, 10, 6)          # Körper
    px(0, 3, 2, 1)           # Arme
    px(12, 3, 2, 1)
    for c in (3, 5, 8, 10):  # Beine
        px(c, 6, 1, 2)
    px(4, 1, 1, 2, dark)     # Augen
    px(9, 1, 1, 2, dark)
    return img


def ensure_icon_file():
    if not os.path.exists(ICON_PATH):
        make_icon_image(256).save(ICON_PATH, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (256, 256)])


# ---------- Agent im Hintergrund ----------

class AgentHost:
    def __init__(self):
        self.agent, self.thread = None, None

    def start(self):
        self.agent = core.Agent(core.CONFIG_PATH)
        self.thread = threading.Thread(target=self.agent.run, name="vibecyd-agent", daemon=True)
        self.thread.start()

    def stop(self):
        if self.agent:
            self.agent.stop()
            self.thread.join(timeout=8)

    def restart(self):
        self.stop()
        self.start()


# ---------- Firmware flashen ----------

FIRMWARE_DIR = os.path.normpath(os.path.join(HERE, "..", "firmware"))
# Fertige Firmware-Images (von tools/build_release.py): in der EXE eingebettet, sonst unter release/firmware
BUNDLED_FW_DIR = (os.path.join(sys._MEIPASS, "firmware") if FROZEN
                  else os.path.normpath(os.path.join(HERE, "..", "release", "firmware")))
BOARDS = {  # PlatformIO-Umgebung -> Anzeige
    "cyd": "Klassisches CYD (1 USB-Port)",
    "cyd2usb": "CYD mit 2 USB-Ports (Micro-USB + USB-C)",
}


class FlashLog:
    """Sammelt die Ausgabe von PlatformIO/esptool zeilenweise fürs Log; Fortschritt nur in 10-%-Schritten."""

    def __init__(self):
        self.buf, self.last_pct = "", -10

    def write(self, text):
        self.buf += text.replace("\r", "\n")
        *lines, self.buf = self.buf.split("\n")
        for line in lines:
            self.line(line)
        return len(text)

    def flush(self):
        pass

    def line(self, line):
        line = line.strip()
        if not line or line.startswith("*") or "platformio upgrade" in line or "prune" in line:
            return
        if line.startswith("Writing at") and "%" in line:
            pct = int(line.rsplit("(", 1)[-1].split("%")[0].strip() or 0)
            if pct < self.last_pct:  # nächster Abschnitt (Bootloader, Partitionen, Programm) fängt bei 0 an
                self.last_pct = -10
            if pct - self.last_pct < 10:
                return
            self.last_pct = pct
        core.log(f"  {line[:150]}")


def find_pio():
    path = shutil.which("pio") or shutil.which("platformio")
    if path:
        return path
    path = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "pio.exe")
    return path if os.path.exists(path) else None


def find_display_port():
    """Port des Displays: per Handshake, sonst der erste typische USB-Seriell-Adapter (z. B. wenn die
    alte Firmware nicht antwortet)."""
    ser = core.find_device()
    if ser:
        port = ser.port
        ser.close()
        return port
    import serial.tools.list_ports
    ports = [p.device for p in serial.tools.list_ports.comports() if p.vid in core.KNOWN_USB_VIDS]
    return ports[0] if ports else None


def flash_with_pio(pio, env, port):
    """Aus dem Quellcode bauen und flashen (nur im Projektordner, mit PlatformIO)."""
    core.log(f"Firmware: baue und flashe an {port} (PlatformIO) ...")
    proc = subprocess.Popen([pio, "run", "-e", env, "-t", "upload", "--upload-port", port],
                            cwd=FIRMWARE_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", creationflags=CREATE_NO_WINDOW)
    out = FlashLog()
    for line in proc.stdout:
        out.line(line)
    return proc.wait() == 0


def flash_image(image, port):
    """Fertiges Image (Bootloader + Partitionen + Programm, ab Adresse 0) mit esptool flashen."""
    import contextlib
    import esptool
    core.log(f"Firmware: flashe {os.path.basename(image)} an {port} ...")
    out = FlashLog()
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            esptool.main(["--chip", "esp32", "--port", port, "--baud", "921600", "--before", "default_reset",
                          "--after", "hard_reset", "write_flash", "-z", "0x0", image])
        return True
    except SystemExit as e:
        return e.code in (0, None)
    except Exception as e:  # noqa: BLE001 - esptool wirft FatalError & Co.
        core.log(f"Firmware: {e}")
        return False


def flash_firmware(host, env):
    """Agent anhalten (gibt den COM-Port frei), flashen, Agent wieder starten. Im Projektordner wird mit
    PlatformIO frisch gebaut, sonst (EXE) das mitgelieferte Image geflasht."""
    pio = None if FROZEN else find_pio()
    image = os.path.join(BUNDLED_FW_DIR, f"vibecyd-{env}.bin")
    if not pio and not os.path.exists(image):
        core.log("Firmware: weder PlatformIO noch ein fertiges Image gefunden")
        return False
    core.log(f"Firmware: Agent pausiert, suche Display ({env}) ...")
    host.stop()
    ok = False
    try:
        port = find_display_port()
        if not port:
            core.log("Firmware: kein Display gefunden – USB-Kabel prüfen")
            return False
        ok = flash_with_pio(pio, env, port) if pio else flash_image(image, port)
        core.log("Firmware: fertig" if ok else "Firmware: Fehler beim Flashen")
    except OSError as e:
        core.log(f"Firmware: {e}")
    finally:
        host.start()
    return ok


# ---------- Einstellungsfenster ----------

class Studio:
    def __init__(self, host):
        self.host = host
        self.ui_queue = queue.Queue()
        self.cfg = self.read_config()
        self.save_job = None
        self.log_shown = None

        self.root = tk.Tk()
        self.root.title(APP_NAME)
        self.root.geometry("600x800")
        self.root.minsize(520, 600)
        try:
            self.root.iconbitmap(ICON_PATH)
        except tk.TclError:
            pass
        self.root.protocol("WM_DELETE_WINDOW", self.hide)
        try:
            ttk.Style().theme_use("vista")
        except tk.TclError:
            pass
        self.build()
        self.root.withdraw()
        self.root.after(100, self.poll_queue)
        self.root.after(500, self.refresh_status)

    # --- Config ---

    @staticmethod
    def read_config():
        cfg = core.load_config(core.CONFIG_PATH)
        try:  # unbekannte Schlüssel aus der Datei behalten
            with open(core.CONFIG_PATH, encoding="utf-8") as f:
                raw = json.load(f)
            raw.update(cfg)
            cfg = raw
        except (OSError, ValueError):
            pass
        return cfg

    def schedule_save(self, *_):
        if self.save_job:
            self.root.after_cancel(self.save_job)
        self.save_job = self.root.after(250, self.save)

    def save(self):
        self.save_job = None
        c = self.cfg
        c["theme"] = self.v_theme.get()
        c["brightness"] = int(self.v_brightness.get())
        c["sessions"] = self.v_sessions.get()
        c["tokens"] = self.v_tokens.get()
        c["timepunch"] = {**c.get("timepunch", {}), "enabled": self.v_tp.get(), "db": self.v_tp_db.get()}
        try:
            c["rotate_seconds"] = max(int(self.v_rotate.get() or 0), 0)
        except ValueError:
            return
        c["hotkey"] = self.v_hotkey.get().strip()
        c["board"] = next(k for k, v in BOARDS.items() if v == self.v_board.get())  # nur fürs Flashen
        core.save_config(core.CONFIG_PATH, c)
        if self.host.agent:
            self.host.agent.reload()

    # --- Aufbau ---

    def build(self):
        pad = {"padx": 10, "pady": 6}
        main = ttk.Frame(self.root, padding=8)
        main.pack(fill="both", expand=True)
        c = self.cfg

        # Status
        st = ttk.LabelFrame(main, text="Status", padding=8)
        st.pack(fill="x", **pad)
        self.l_display = ttk.Label(st, text="Display: …")
        self.l_display.grid(row=0, column=0, sticky="w")
        self.l_limits = ttk.Label(st, text="Limits: …")
        self.l_limits.grid(row=1, column=0, sticky="w")
        self.l_extra = ttk.Label(st, text="")
        self.l_extra.grid(row=2, column=0, sticky="w")
        btns = ttk.Frame(st)
        btns.grid(row=0, column=1, rowspan=3, sticky="e")
        st.columnconfigure(0, weight=1)
        ttk.Button(btns, text="Nächster Screen", command=lambda: self.agent_cmd("N")).pack(fill="x", pady=2)
        ttk.Button(btns, text="Agent neu starten", command=self.restart_agent).pack(fill="x", pady=2)

        # Theme + Helligkeit
        th = ttk.LabelFrame(main, text="Theme des Limit-Screens", padding=8)
        th.pack(fill="x", **pad)
        self.v_theme = tk.StringVar(value=c["theme"])
        for i, key in enumerate(core.THEMES):
            name, desc = THEME_LABELS[key]
            ttk.Radiobutton(th, text=name, value=key, variable=self.v_theme,
                            command=self.schedule_save).grid(row=i, column=0, sticky="w")
            ttk.Label(th, text=desc, foreground="#666").grid(row=i, column=1, sticky="w", padx=8)
        ttk.Separator(th).grid(row=9, column=0, columnspan=2, sticky="ew", pady=6)
        ttk.Label(th, text="Helligkeit").grid(row=10, column=0, sticky="w")
        bf = ttk.Frame(th)
        bf.grid(row=10, column=1, sticky="ew")
        th.columnconfigure(1, weight=1)
        self.v_brightness = tk.IntVar(value=c["brightness"])
        self.l_brightness = ttk.Label(bf, width=5, text=f"{c['brightness']} %")
        ttk.Scale(bf, from_=5, to=100, variable=self.v_brightness,
                  command=self.on_brightness).pack(side="left", fill="x", expand=True)
        self.l_brightness.pack(side="left", padx=6)

        # Screens
        sc = ttk.LabelFrame(main, text="Screens", padding=8)
        sc.pack(fill="x", **pad)
        ttk.Label(sc, text="Limits (immer)").grid(row=0, column=0, sticky="w", columnspan=3)
        self.v_sessions = tk.BooleanVar(value=c["sessions"])
        self.v_tokens = tk.BooleanVar(value=c["tokens"])
        self.v_tp = tk.BooleanVar(value=c["timepunch"].get("enabled", False))
        self.v_tp_db = tk.StringVar(value=c["timepunch"].get("db", ""))
        ttk.Checkbutton(sc, text="Sessions & Agenten", variable=self.v_sessions,
                        command=self.schedule_save).grid(row=1, column=0, sticky="w", columnspan=3)
        ttk.Checkbutton(sc, text="Token-Verbrauch", variable=self.v_tokens,
                        command=self.schedule_save).grid(row=2, column=0, sticky="w", columnspan=3)
        ttk.Checkbutton(sc, text="Zeiterfassung (TimePunch Helper)", variable=self.v_tp,
                        command=self.schedule_save).grid(row=3, column=0, sticky="w")
        e = ttk.Entry(sc, textvariable=self.v_tp_db)
        e.grid(row=4, column=0, columnspan=2, sticky="ew", padx=(20, 4))
        e.bind("<FocusOut>", self.schedule_save)
        ttk.Button(sc, text="…", width=3, command=self.pick_db).grid(row=4, column=2)
        sc.columnconfigure(0, weight=1)

        # Bedienung
        op = ttk.LabelFrame(main, text="Bedienung", padding=8)
        op.pack(fill="x", **pad)
        self.v_rotate = tk.StringVar(value=str(c["rotate_seconds"]))
        self.v_hotkey = tk.StringVar(value=c["hotkey"])
        self.v_autostart = tk.BooleanVar(value=os.path.exists(AUTOSTART_LNK))
        ttk.Label(op, text="Automatisch weiterschalten alle").grid(row=0, column=0, sticky="w")
        sp = ttk.Spinbox(op, from_=0, to=600, increment=5, width=6, textvariable=self.v_rotate,
                         command=self.schedule_save)
        sp.grid(row=0, column=1, sticky="w", padx=4)
        sp.bind("<KeyRelease>", self.schedule_save)
        ttk.Label(op, text="Sekunden (0 = aus)").grid(row=0, column=2, sticky="w")
        ttk.Label(op, text="Tastenkürzel nächster Screen").grid(row=1, column=0, sticky="w", pady=4)
        hk = ttk.Entry(op, textvariable=self.v_hotkey, width=18)
        hk.grid(row=1, column=1, columnspan=2, sticky="w", padx=4)
        hk.bind("<FocusOut>", self.schedule_save)
        hk.bind("<Return>", self.schedule_save)
        ttk.Checkbutton(op, text="Mit Windows starten", variable=self.v_autostart,
                        command=self.toggle_autostart).grid(row=2, column=0, columnspan=3, sticky="w")

        # Firmware
        fw = ttk.LabelFrame(main, text="Firmware", padding=8)
        fw.pack(fill="x", **pad)
        self.v_board = tk.StringVar(value=BOARDS.get(c.get("board"), BOARDS["cyd"]))
        ttk.Label(fw, text="Board").grid(row=0, column=0, sticky="w")
        cb = ttk.Combobox(fw, textvariable=self.v_board, values=list(BOARDS.values()), state="readonly", width=38)
        cb.grid(row=0, column=1, sticky="w", padx=6)
        cb.bind("<<ComboboxSelected>>", self.schedule_save)
        self.b_flash = ttk.Button(fw, text="Firmware bauen und flashen", command=self.flash)
        self.b_flash.grid(row=0, column=2, sticky="e")
        self.pb_flash = ttk.Progressbar(fw, mode="indeterminate")
        fw.columnconfigure(2, weight=1)

        # Log
        lg = ttk.LabelFrame(main, text="Log", padding=8)
        lg.pack(fill="both", expand=True, **pad)
        self.t_log = tk.Text(lg, height=8, wrap="none", font=("Consolas", 9), state="disabled",
                             background="#1f1e1d", foreground="#f5f4ee")
        self.t_log.pack(fill="both", expand=True)
        ttk.Button(lg, text="Log-Datei öffnen", command=lambda: os.startfile(core.LOG_PATH)).pack(anchor="e", pady=(6, 0))

    # --- Aktionen ---

    def on_brightness(self, value):
        v = int(float(value))
        self.l_brightness.config(text=f"{v} %")
        self.schedule_save()

    def pick_db(self):
        path = filedialog.askopenfilename(title="TimePunch-Datenbank (ledger.db)",
                                          initialdir=os.path.dirname(os.path.expandvars(self.v_tp_db.get())),
                                          filetypes=[("SQLite", "*.db"), ("Alle Dateien", "*.*")])
        if path:
            self.v_tp_db.set(os.path.normpath(path))
            self.schedule_save()

    def agent_cmd(self, cmd):
        if self.host.agent:
            self.host.agent.command(cmd)

    def restart_agent(self):
        threading.Thread(target=self.host.restart, daemon=True).start()

    def toggle_autostart(self):
        set_autostart(self.v_autostart.get())

    def flash(self):
        env = next(k for k, v in BOARDS.items() if v == self.v_board.get())
        if not messagebox.askokcancel(
                APP_NAME, f"Firmware für „{self.v_board.get()}“ bauen und auf das Display flashen?\n\n"
                          "Der Agent pausiert so lange, das Display startet danach neu. Das dauert etwa eine Minute."):
            return
        self.b_flash.config(state="disabled")
        self.pb_flash.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        self.pb_flash.start(12)

        def done(ok):
            self.pb_flash.stop()
            self.pb_flash.grid_remove()
            self.b_flash.config(state="normal")
            if ok:
                messagebox.showinfo(APP_NAME, "Firmware ist drauf. Das Display verbindet sich gleich wieder.")
            else:
                messagebox.showerror(APP_NAME, "Flashen hat nicht geklappt – Details stehen im Log.")

        def work():
            ok = flash_firmware(self.host, env)
            self.ui_queue.put(lambda: done(ok))

        threading.Thread(target=work, name="vibecyd-flash", daemon=True).start()

    def set_theme(self, key):
        self.v_theme.set(key)
        self.save()

    def show(self):
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def hide(self):
        self.root.withdraw()

    # --- Laufende Aktualisierung ---

    def poll_queue(self):
        while True:
            try:
                fn = self.ui_queue.get_nowait()
            except queue.Empty:
                break
            fn()
        self.root.after(100, self.poll_queue)

    def refresh_status(self):
        a = self.host.agent
        if a and a.running:
            d = a.data
            self.l_display.config(text=f"Display: verbunden an {a.connected_port}" if a.connected_port
                                  else "Display: nicht gefunden – USB-Kabel prüfen")
            u = d.get("usage") or {}
            parts = []
            for key, name in (("s", "Session"), ("w", "Woche"), ("f", "Fable")):
                if u.get(key):
                    parts.append(f"{name} {round(u[key]['p'])} %")
            age = int(time.time() - d["fetched_at"]) if d.get("fetched_at") else None
            when = "" if age is None else (" (gerade eben)" if age < 60 else f" (vor {age // 60} min)")
            self.l_limits.config(text="Limits: " + (" · ".join(parts) + when if parts else "noch keine Daten"))
            extra = []
            if d.get("err"):
                extra.append(f"Fehler: {d['err']}")
            ses = d.get("sessions")
            if ses:
                extra.append(f"{ses['n']} {'Session' if ses['n'] == 1 else 'Sessions'}, "
                             f"{ses['na']} {'Agent' if ses['na'] == 1 else 'Agenten'} aktiv")
            self.l_extra.config(text=" · ".join(extra))
        else:
            self.l_display.config(text="Agent läuft nicht")
        lines = list(core.LOG_LINES)[-200:]
        last = lines[-1] if lines else None
        if last != self.log_shown:
            self.t_log.config(state="normal")
            self.t_log.delete("1.0", "end")
            self.t_log.insert("end", "\n".join(lines))
            self.t_log.see("end")
            self.t_log.xview_moveto(0)  # Zeilenanfang zeigen, nicht das Ende der letzten Zeile
            self.t_log.config(state="disabled")
            self.log_shown = last
        self.root.after(1000, self.refresh_status)


def set_autostart(enabled):
    if enabled:
        make_shortcut(AUTOSTART_LNK)
        if "vibecyd_agent" in shortcut_target(OLD_AUTOSTART_LNK):
            os.remove(OLD_AUTOSTART_LNK)  # der alte Eintrag würde einen zweiten Agent starten
    elif os.path.exists(AUTOSTART_LNK):
        os.remove(AUTOSTART_LNK)


# ---------- Tray ----------

def build_tray(studio, host):
    def ui(fn):
        return lambda icon, item: studio.ui_queue.put(fn)

    def theme_item(key):
        return pystray.MenuItem(THEME_LABELS[key][0], ui(lambda: studio.set_theme(key)),
                                checked=lambda item: studio.v_theme.get() == key, radio=True)

    def quit_app():
        icon.stop()
        host.stop()
        studio.root.destroy()

    menu = pystray.Menu(
        pystray.MenuItem("Einstellungen", ui(studio.show), default=True),
        pystray.MenuItem("Nächster Screen", ui(lambda: studio.agent_cmd("N"))),
        pystray.MenuItem("Theme", pystray.Menu(*(theme_item(k) for k in core.THEMES))),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Agent neu starten", ui(studio.restart_agent)),
        pystray.MenuItem("Beenden", ui(quit_app)),
    )
    icon = pystray.Icon("vibecyd", make_icon_image(64), APP_NAME, menu)

    def update_title():
        a = host.agent
        u = (a.data.get("usage") or {}) if a else {}
        parts = [f"{n} {round(u[k]['p'])} %" for k, n in (("s", "Session"), ("w", "Woche")) if u.get(k)]
        icon.title = f"{APP_NAME}\n" + (" · ".join(parts) if parts else "keine Daten")
        studio.root.after(5000, update_title)

    studio.root.after(1000, update_title)
    return icon


def main():
    if sys.platform != "win32":
        sys.exit("VibeCYD Studio läuft nur unter Windows. Auf anderen Systemen den Agent direkt starten.")
    if not single_instance():
        return
    ensure_icon_file()
    stop_standalone_agents()
    programs_lnk = os.path.join(PROGRAMS_DIR, f"{APP_NAME}.lnk")
    me = app_command()[0]
    if me.lower() not in shortcut_target(programs_lnk).lower():
        make_shortcut(programs_lnk, show=True)  # Startmenü-Eintrag öffnet das Fenster
    if os.path.exists(AUTOSTART_LNK) and me.lower() not in shortcut_target(AUTOSTART_LNK).lower():
        make_shortcut(AUTOSTART_LNK)  # z. B. vom Skript auf die EXE umgestiegen
    if "vibecyd_agent" in shortcut_target(OLD_AUTOSTART_LNK):
        set_autostart(True)  # Autostart vom einzelnen Agent auf das Studio umstellen

    host = AgentHost()
    host.start()
    studio = Studio(host)
    threading.Thread(target=wait_for_show_requests, args=(lambda: studio.ui_queue.put(studio.show),),
                     daemon=True).start()
    icon = build_tray(studio, host)
    icon.run_detached()
    if "--show" in sys.argv:
        studio.show()
    studio.root.mainloop()


if __name__ == "__main__":
    main()
