#!/usr/bin/env python3
"""Pixel-Szene für das Theme "Cozy Coder": Coder am Schreibtisch, nachts, Regen am Fenster.

Die Szene ist ein Raster aus 160 x 55 "Pixeln" (auf dem Display doppelt so groß). Dieses Skript
- beschreibt den statischen Teil als Liste von Rechtecken und erzeugt daraus firmware/src/cozy_scene.h,
- rendert eine Vorschau (statisch + die animierten Teile wie in der Firmware) als PNG.

    python firmware/tools/cozy_scene.py [vorschau.png] [--t MILLISEKUNDEN] [--mood work|busy|sleep|tight]

Die animierten Teile (Coder, Monitor, Regen, Katze, Lampe, Dampf) stehen in themes.h; drawDynamic() hier
bildet sie für die Vorschau nach. Wer sie ändert, ändert beide Stellen.
"""
import os
import sys

W, H = 160, 55
HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "src", "cozy_scene.h")

PALETTE = {
    "wall": (38, 30, 62), "wall_low": (32, 25, 52),
    "shelf": (74, 46, 34), "shelf_back": (50, 34, 40), "board": (100, 64, 42),
    "book_red": (160, 60, 72), "book_blue": (64, 104, 166), "book_yellow": (206, 156, 64),
    "book_green": (90, 146, 92), "book_purple": (124, 84, 166), "book_teal": (60, 140, 140),
    "lantern": (64, 42, 32), "lantern_glow": (255, 190, 96),
    "frame_gold": (150, 112, 62), "pic_sky": (64, 84, 146), "pic_mount": (170, 180, 210),
    "pot": (164, 92, 60), "leaf": (58, 140, 76), "leaf_dark": (36, 104, 58), "leaf_light": (96, 176, 96),
    "box": (112, 72, 50),
    "cork": (142, 102, 62), "note_y": (232, 220, 160), "note_r": (206, 124, 124),
    "win_frame": (72, 60, 94), "sky": (24, 26, 66), "moon": (246, 236, 190), "star": (220, 220, 255),
    "city": (30, 30, 56), "city_light": (255, 200, 104), "sill": (92, 72, 104),
    "cat": (24, 20, 34), "cat_eye": (240, 210, 80),
    "desk": (132, 86, 56), "desk_body": (80, 50, 36), "desk_dark": (60, 37, 27), "drawer": (98, 62, 42),
    "handle": (182, 142, 92), "desk_lit": (196, 136, 74),
    "mon": (36, 36, 50), "screen": (30, 72, 142), "screen_off": (12, 14, 30),
    "code1": (150, 226, 255), "code2": (124, 240, 172), "code3": (255, 150, 206), "code4": (232, 232, 124),
    "keys": (62, 62, 84),
    "lamp": (206, 146, 64), "lamp_dark": (84, 62, 50), "bulb": (255, 226, 150),
    "mug": (222, 216, 232), "mug_print": (124, 92, 166), "steam": (176, 176, 204),
    "chair": (42, 44, 84), "chair_dark": (30, 30, 56),
    "hood": (112, 72, 160), "hood_dark": (82, 50, 122), "hair": (62, 40, 32), "hair_light": (92, 62, 46),
    "skin": (232, 180, 140), "skin_dark": (200, 140, 110), "jeans": (52, 62, 104),
    "sweat": (120, 180, 235), "zzz": (200, 200, 230),
}
NAMES = list(PALETTE)

STATIC = []


def R(x, y, w, h, c):
    STATIC.append((x, y, w, h, c))


def build_static():
    R(0, 0, W, H, "wall")
    R(0, 41, W, 14, "wall_low")

    # Regal links mit Büchern, Laterne, Bild, Pflanze und Kisten
    R(2, 4, 28, 46, "shelf")
    R(4, 6, 24, 42, "shelf_back")
    for y in (16, 28, 40):
        R(3, y, 26, 2, "board")
    for x, w, h, c in ((4, 2, 8, "book_red"), (6, 2, 7, "book_blue"), (8, 3, 8, "book_yellow"),
                       (11, 2, 6, "book_green"), (13, 2, 8, "book_purple"), (15, 1, 7, "book_teal")):
        R(x, 16 - h, w, h, c)
    R(21, 9, 6, 7, "lantern")
    R(22, 10, 4, 5, "lantern_glow")
    for x, w, h, c in ((4, 2, 7, "book_teal"), (6, 3, 8, "book_red"), (9, 2, 6, "book_blue"),
                       (11, 2, 7, "book_yellow")):
        R(x, 28 - h, w, h, c)
    R(16, 21, 9, 7, "frame_gold")
    R(17, 22, 7, 5, "pic_sky")
    R(18, 25, 2, 2, "pic_mount")
    R(20, 24, 3, 3, "pic_mount")
    R(6, 36, 5, 4, "pot")
    R(4, 32, 3, 3, "leaf")
    R(7, 31, 3, 4, "leaf_light")
    R(10, 33, 3, 3, "leaf_dark")
    R(15, 34, 12, 6, "box")
    R(16, 35, 10, 1, "shelf")
    # Ranke oben auf dem Regal
    R(20, 1, 4, 3, "leaf")
    R(24, 0, 3, 4, "leaf_dark")
    for y in range(4, 22, 3):
        R(28 + (y % 2), y, 2, 2, "leaf" if y % 6 else "leaf_light")

    # Große Pflanze vorne links
    for x, y, w, h, c in ((0, 40, 6, 8, "leaf_dark"), (3, 36, 6, 6, "leaf"), (7, 42, 6, 6, "leaf_light"),
                          (0, 47, 9, 8, "leaf"), (9, 47, 4, 8, "leaf_dark")):
        R(x, y, w, h, c)

    # Pinnwand und Bild an der Wand
    R(50, 3, 26, 13, "cork")
    R(53, 5, 5, 5, "note_y")
    R(60, 6, 4, 4, "note_r")
    R(56, 11, 6, 3, "note_y")
    R(66, 4, 8, 9, "win_frame")
    R(67, 5, 6, 7, "pic_sky")
    R(68, 9, 2, 3, "pic_mount")
    R(70, 8, 2, 4, "pic_mount")

    # Fenster: Nachthimmel, Mond, Sterne, Stadt
    R(112, 2, 38, 28, "win_frame")
    R(114, 4, 34, 24, "sky")
    for x, y in ((117, 7), (124, 11), (135, 8), (146, 13), (120, 13), (128, 6)):
        R(x, y, 1, 1, "star")
    R(140, 6, 4, 4, "moon")
    R(142, 6, 2, 3, "sky")  # Sichel
    for x, y, w in ((114, 22, 5), (119, 19, 4), (123, 23, 6), (132, 20, 5), (137, 24, 4), (141, 21, 7)):
        R(x, y, w, 28 - y, "city")
    for x, y in ((116, 24), (120, 21), (121, 24), (134, 22), (134, 25), (143, 23), (145, 25), (125, 25)):
        R(x, y, 1, 1, "city_light")
    R(130, 4, 2, 24, "win_frame")
    R(114, 15, 34, 1, "win_frame")
    R(110, 30, 42, 2, "sill")
    # Katze auf der Fensterbank (Augen und Schwanz sind animiert)
    R(118, 26, 7, 4, "cat")
    R(124, 23, 4, 4, "cat")
    R(124, 22, 1, 1, "cat")
    R(127, 22, 1, 1, "cat")

    # Ranken rechts
    for y in range(0, 34, 3):
        R(152 + (y % 2), y, 2, 2, "leaf" if y % 6 else "leaf_dark")
    R(155, 2, 3, 3, "leaf_light")
    R(150, 46, 9, 9, "pot")
    R(149, 38, 5, 6, "leaf")
    R(154, 35, 5, 8, "leaf_dark")
    R(152, 41, 4, 5, "leaf_light")

    # Schreibtisch
    R(34, 38, 104, 2, "desk")
    R(34, 40, 104, 15, "desk_body")
    R(36, 40, 4, 15, "desk_dark")
    R(112, 42, 22, 5, "drawer")
    R(112, 48, 22, 5, "drawer")
    R(122, 44, 3, 1, "handle")
    R(122, 50, 3, 1, "handle")

    # Monitor, Tastatur, Maus
    R(78, 17, 30, 19, "mon")
    R(91, 36, 4, 2, "mon")
    R(88, 37, 10, 1, "mon")
    R(75, 37, 18, 1, "keys")
    R(99, 37, 3, 1, "keys")

    # Lampe (Glühbirne und Lichtfleck animiert)
    R(119, 36, 8, 2, "lamp_dark")
    R(123, 27, 2, 9, "lamp_dark")
    R(121, 25, 3, 2, "lamp_dark")
    R(117, 20, 5, 1, "lamp")
    R(115, 21, 8, 1, "lamp")
    R(114, 22, 9, 2, "lamp")
    R(113, 24, 10, 1, "lamp")

    # Tasse
    R(128, 33, 5, 5, "mug")
    R(133, 34, 1, 3, "mug")
    R(129, 35, 2, 2, "mug_print")

    # Stuhl (Coder ist animiert und sitzt davor)
    R(44, 20, 11, 22, "chair")
    R(42, 40, 26, 3, "chair")
    R(54, 43, 3, 8, "chair_dark")
    R(47, 51, 18, 2, "chair_dark")


# ---------- Vorschau der animierten Teile (Nachbildung von themes.h) ----------

def draw_dynamic(px, t, mood):
    sleep, busy = mood == "sleep", mood == "busy"
    # Regen
    for i in range(8):
        x = 114 + (i * 9 + 3) % 34
        y = 4 + (t // 45 + i * 11) % 22
        px(x, y, 1, 2, "steam")
    # Katze: Augen (blinzeln), Schwanz
    if (t % 4200) >= 150:
        px(125, 24, 1, 1, "cat_eye")
        px(127, 24, 1, 1, "cat_eye")
    phase = (t // 500) % 4
    tail = [(117, 28), (116, 29), (116, 30)] if phase in (0, 2) else \
           [(117, 28), (116, 29), (115, 29)] if phase == 1 else [(117, 28), (117, 29), (116, 30)]
    for x, y in tail:
        px(x, y, 1, 1, "cat")
    # Monitor
    if sleep:
        px(80, 19, 26, 15, "screen_off")
        px(81 + (t // 200) % 24, 20 + (t // 330) % 13, 1, 1, "star")
    else:
        px(80, 19, 26, 15, "screen")
        step = t // (120 if busy else 400)
        for row in range(7):
            line = row + step
            indent = (line * 3) % 4
            length = 5 + (line * 7) % 15
            px(81 + indent, 20 + row * 2, min(length, 24 - indent), 1,
               ("code1", "code2", "code3", "code4")[line % 4])
    # Lampe: Glühbirne und Lichtfleck, flackert selten
    lit = not (t % 6000 < 140)
    px(115, 25, 6, 1, "bulb" if lit else "lamp")
    px(108, 38, 26, 1, "desk_lit" if lit else "desk")
    # Dampf über der Tasse
    if not sleep:
        for k in range(3):
            y = 31 - (t // 160 + k * 3) % 9
            px(129 + (y + t // 300) % 3, y, 1, 1, "steam")
    # Coder
    dy = 6 if sleep else (-1 if busy and (t // 140) % 2 else 0)
    px(60, 41, 16, 3, "jeans")
    px(52, 24, 20, 17, "hood")
    px(52, 24, 4, 17, "hood_dark")
    px(56, 22 + max(dy, 0) // 2, 12, 3, "hood_dark")
    hx = 2 if sleep else 0
    # Lockenkopf, leicht zum Monitor gedreht: Haare rund, rechts Wange und Ohr
    for x, y, w, h in ((60, 9, 9, 1), (58, 10, 13, 1), (57, 11, 15, 10), (58, 21, 12, 2)):
        px(x + hx, y + dy, w, h, "hair")
    for x, y in ((59, 10), (63, 9), (67, 10), (57, 14), (61, 13), (65, 12), (69, 13), (59, 17), (63, 16)):
        px(x + hx, y + dy, 2, 1, "hair_light")
    px(69 + hx, 15 + dy, 3, 7, "skin")
    px(71 + hx, 18 + dy, 1, 1, "skin_dark")
    px(64 + hx, 17 + dy, 2, 3, "skin_dark")
    typing = not sleep and mood != "bored"
    tick = (t // (90 if busy else 160)) % 2
    px(66, 30, 11, 4, "hood")
    px(77, 34 - (1 if typing and tick else 0), 3, 2, "skin")
    px(70, 33, 6, 3, "hood_dark")
    px(75, 35 - (1 if typing and not tick else 0), 2, 2, "skin")
    if sleep:
        for i, (x, y) in enumerate(((74, 9), (77, 6), (80, 3))):
            if (t // 400 + i) % 3:
                px(x, y, 2, 1, "zzz")
                px(x + 1, y + 1, 1, 1, "zzz")
                px(x, y + 2, 2, 1, "zzz")
    if mood == "tight":
        px(73, 12 + (t // 120) % 6, 1, 2, "sweat")


def render_preview(path, t, mood):
    from PIL import Image, ImageDraw
    scale = 4
    img = Image.new("RGB", (W * scale, H * scale))
    d = ImageDraw.Draw(img)

    def px(x, y, w, h, c):
        d.rectangle([x * scale, y * scale, (x + w) * scale - 1, (y + h) * scale - 1], fill=PALETTE[c])

    for rect in STATIC:
        px(*rect)
    draw_dynamic(px, t, mood)
    img.save(path)


def write_header():
    lines = [
        "// Erzeugt von firmware/tools/cozy_scene.py – nicht von Hand ändern.",
        "// Statischer Teil der Cozy-Coder-Szene: Palette und Rechtecke im 160 x 55-Raster.",
        "#pragma once",
        "",
        f"const int COZY_W = {W}, COZY_H = {H};",
        "",
        "enum CozyColor {",
        *[f"  CZ_{n.upper()}," for n in NAMES],
        "  CZ_COUNT",
        "};",
        "",
        "const uint8_t COZY_RGB[CZ_COUNT][3] = {",
        *[f"  {{{r}, {g}, {b}}},  // {n}" for n, (r, g, b) in PALETTE.items()],
        "};",
        "",
        "// x, y, Breite, Höhe, Farbe",
        f"const uint8_t COZY_RECTS[{len(STATIC)}][5] = {{",
        *[f"  {{{x}, {y}, {w}, {h}, CZ_{c.upper()}}}," for x, y, w, h, c in STATIC],
        "};",
        "",
    ]
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    build_static()
    write_header()
    args = sys.argv[1:]
    t = int(args[args.index("--t") + 1]) if "--t" in args else 1000
    mood = args[args.index("--mood") + 1] if "--mood" in args else "work"
    out = next((a for a in args if a.endswith(".png")), None)
    if out:
        render_preview(out, t, mood)
    print(f"{len(STATIC)} Rechtecke -> {os.path.normpath(HEADER)}")
