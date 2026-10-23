#!/usr/bin/env python3
"""Baut das Release: Firmware-Images für beide Board-Varianten und VibeCYD Studio als einzelne EXE.

    python tools/build_release.py

Ergebnis in release/:
    VibeCYD Studio.exe          – Tray-App mit eingebautem Agent und den Firmware-Images (kein Python nötig)
    firmware/vibecyd-cyd.bin    – komplettes Flash-Image (Bootloader, Partitionen, Programm) ab Adresse 0
    firmware/vibecyd-cyd2usb.bin

Voraussetzungen: PlatformIO (pio), pip install pyinstaller esptool pyserial pystray pillow
"""
import os
import shutil
import subprocess
import sys

import esptool
import PyInstaller.__main__

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FIRMWARE = os.path.join(ROOT, "firmware")
AGENT = os.path.join(ROOT, "agent")
RELEASE = os.path.join(ROOT, "release")
BUILD = os.path.join(ROOT, "build")
ENVS = ["cyd", "cyd2usb"]
PIO_HOME = os.path.join(os.path.expanduser("~"), ".platformio")


def find_pio():
    path = shutil.which("pio") or shutil.which("platformio")
    if path:
        return path
    path = os.path.join(PIO_HOME, "penv", "Scripts", "pio.exe")
    if os.path.exists(path):
        return path
    sys.exit("PlatformIO nicht gefunden (pip install platformio)")


def build_firmware():
    pio = find_pio()
    out_dir = os.path.join(RELEASE, "firmware")
    os.makedirs(out_dir, exist_ok=True)
    boot_app0 = os.path.join(PIO_HOME, "packages", "framework-arduinoespressif32", "tools", "partitions",
                             "boot_app0.bin")
    for env in ENVS:
        print(f"== Firmware {env}")
        subprocess.run([pio, "run", "-e", env], cwd=FIRMWARE, check=True)
        b = os.path.join(FIRMWARE, ".pio", "build", env)
        image = os.path.join(out_dir, f"vibecyd-{env}.bin")
        # Adressen wie beim Upload durch PlatformIO (ESP32, Arduino, 4 MB Flash, DIO)
        esptool.main(["--chip", "esp32", "merge_bin", "-o", image, "--flash_mode", "dio", "--flash_size", "4MB",
                      "0x1000", os.path.join(b, "bootloader.bin"), "0x8000", os.path.join(b, "partitions.bin"),
                      "0xe000", boot_app0, "0x10000", os.path.join(b, "firmware.bin")])


def build_exe():
    print("== VibeCYD Studio.exe")
    icon = os.path.join(BUILD, "vibecyd.ico")
    os.makedirs(BUILD, exist_ok=True)
    sys.path.insert(0, AGENT)
    from importlib.machinery import SourceFileLoader
    studio = SourceFileLoader("studio_for_icon", os.path.join(AGENT, "vibecyd_studio.pyw")).load_module()
    studio.make_icon_image(256).save(icon, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (256, 256)])

    fw = os.path.join(RELEASE, "firmware")
    args = [
        os.path.join(AGENT, "vibecyd_studio.pyw"),
        "--name", "VibeCYD Studio", "--onefile", "--windowed", "--noconfirm", "--clean",
        "--icon", icon,
        "--paths", AGENT,
        "--hidden-import", "pystray._win32",
        "--collect-all", "esptool",  # inkl. Stub-Flasher-Dateien
        "--distpath", RELEASE, "--workpath", os.path.join(BUILD, "pyinstaller"),
        "--specpath", os.path.join(BUILD, "pyinstaller"),
    ]
    for env in ENVS:
        args += ["--add-data", f"{os.path.join(fw, f'vibecyd-{env}.bin')}{os.pathsep}firmware"]
    PyInstaller.__main__.run(args)


if __name__ == "__main__":
    build_firmware()
    build_exe()
    print(f"\nFertig: {os.path.join(RELEASE, 'VibeCYD Studio.exe')}")
