// Erzeugt von firmware/tools/cozy_scene.py – nicht von Hand ändern.
// Statischer Teil der Cozy-Coder-Szene: Palette und Rechtecke im 160 x 55-Raster.
#pragma once

const int COZY_W = 160, COZY_H = 55;

enum CozyColor {
  CZ_WALL,
  CZ_WALL_LOW,
  CZ_SHELF,
  CZ_SHELF_BACK,
  CZ_BOARD,
  CZ_BOOK_RED,
  CZ_BOOK_BLUE,
  CZ_BOOK_YELLOW,
  CZ_BOOK_GREEN,
  CZ_BOOK_PURPLE,
  CZ_BOOK_TEAL,
  CZ_LANTERN,
  CZ_LANTERN_GLOW,
  CZ_FRAME_GOLD,
  CZ_PIC_SKY,
  CZ_PIC_MOUNT,
  CZ_POT,
  CZ_LEAF,
  CZ_LEAF_DARK,
  CZ_LEAF_LIGHT,
  CZ_BOX,
  CZ_CORK,
  CZ_NOTE_Y,
  CZ_NOTE_R,
  CZ_WIN_FRAME,
  CZ_SKY,
  CZ_MOON,
  CZ_STAR,
  CZ_CITY,
  CZ_CITY_LIGHT,
  CZ_SILL,
  CZ_CAT,
  CZ_CAT_EYE,
  CZ_DESK,
  CZ_DESK_BODY,
  CZ_DESK_DARK,
  CZ_DRAWER,
  CZ_HANDLE,
  CZ_DESK_LIT,
  CZ_MON,
  CZ_SCREEN,
  CZ_SCREEN_OFF,
  CZ_CODE1,
  CZ_CODE2,
  CZ_CODE3,
  CZ_CODE4,
  CZ_KEYS,
  CZ_LAMP,
  CZ_LAMP_DARK,
  CZ_BULB,
  CZ_MUG,
  CZ_MUG_PRINT,
  CZ_STEAM,
  CZ_CHAIR,
  CZ_CHAIR_DARK,
  CZ_HOOD,
  CZ_HOOD_DARK,
  CZ_HAIR,
  CZ_HAIR_LIGHT,
  CZ_SKIN,
  CZ_SKIN_DARK,
  CZ_JEANS,
  CZ_SWEAT,
  CZ_ZZZ,
  CZ_COUNT
};

const uint8_t COZY_RGB[CZ_COUNT][3] = {
  {38, 30, 62},  // wall
  {32, 25, 52},  // wall_low
  {74, 46, 34},  // shelf
  {50, 34, 40},  // shelf_back
  {100, 64, 42},  // board
  {160, 60, 72},  // book_red
  {64, 104, 166},  // book_blue
  {206, 156, 64},  // book_yellow
  {90, 146, 92},  // book_green
  {124, 84, 166},  // book_purple
  {60, 140, 140},  // book_teal
  {64, 42, 32},  // lantern
  {255, 190, 96},  // lantern_glow
  {150, 112, 62},  // frame_gold
  {64, 84, 146},  // pic_sky
  {170, 180, 210},  // pic_mount
  {164, 92, 60},  // pot
  {58, 140, 76},  // leaf
  {36, 104, 58},  // leaf_dark
  {96, 176, 96},  // leaf_light
  {112, 72, 50},  // box
  {142, 102, 62},  // cork
  {232, 220, 160},  // note_y
  {206, 124, 124},  // note_r
  {72, 60, 94},  // win_frame
  {24, 26, 66},  // sky
  {246, 236, 190},  // moon
  {220, 220, 255},  // star
  {30, 30, 56},  // city
  {255, 200, 104},  // city_light
  {92, 72, 104},  // sill
  {24, 20, 34},  // cat
  {240, 210, 80},  // cat_eye
  {132, 86, 56},  // desk
  {80, 50, 36},  // desk_body
  {60, 37, 27},  // desk_dark
  {98, 62, 42},  // drawer
  {182, 142, 92},  // handle
  {196, 136, 74},  // desk_lit
  {36, 36, 50},  // mon
  {30, 72, 142},  // screen
  {12, 14, 30},  // screen_off
  {150, 226, 255},  // code1
  {124, 240, 172},  // code2
  {255, 150, 206},  // code3
  {232, 232, 124},  // code4
  {62, 62, 84},  // keys
  {206, 146, 64},  // lamp
  {84, 62, 50},  // lamp_dark
  {255, 226, 150},  // bulb
  {222, 216, 232},  // mug
  {124, 92, 166},  // mug_print
  {176, 176, 204},  // steam
  {42, 44, 84},  // chair
  {30, 30, 56},  // chair_dark
  {112, 72, 160},  // hood
  {82, 50, 122},  // hood_dark
  {62, 40, 32},  // hair
  {92, 62, 46},  // hair_light
  {232, 180, 140},  // skin
  {200, 140, 110},  // skin_dark
  {52, 62, 104},  // jeans
  {120, 180, 235},  // sweat
  {200, 200, 230},  // zzz
};

// x, y, Breite, Höhe, Farbe
const uint8_t COZY_RECTS[124][5] = {
  {0, 0, 160, 55, CZ_WALL},
  {0, 41, 160, 14, CZ_WALL_LOW},
  {2, 4, 28, 46, CZ_SHELF},
  {4, 6, 24, 42, CZ_SHELF_BACK},
  {3, 16, 26, 2, CZ_BOARD},
  {3, 28, 26, 2, CZ_BOARD},
  {3, 40, 26, 2, CZ_BOARD},
  {4, 8, 2, 8, CZ_BOOK_RED},
  {6, 9, 2, 7, CZ_BOOK_BLUE},
  {8, 8, 3, 8, CZ_BOOK_YELLOW},
  {11, 10, 2, 6, CZ_BOOK_GREEN},
  {13, 8, 2, 8, CZ_BOOK_PURPLE},
  {15, 9, 1, 7, CZ_BOOK_TEAL},
  {21, 9, 6, 7, CZ_LANTERN},
  {22, 10, 4, 5, CZ_LANTERN_GLOW},
  {4, 21, 2, 7, CZ_BOOK_TEAL},
  {6, 20, 3, 8, CZ_BOOK_RED},
  {9, 22, 2, 6, CZ_BOOK_BLUE},
  {11, 21, 2, 7, CZ_BOOK_YELLOW},
  {16, 21, 9, 7, CZ_FRAME_GOLD},
  {17, 22, 7, 5, CZ_PIC_SKY},
  {18, 25, 2, 2, CZ_PIC_MOUNT},
  {20, 24, 3, 3, CZ_PIC_MOUNT},
  {6, 36, 5, 4, CZ_POT},
  {4, 32, 3, 3, CZ_LEAF},
  {7, 31, 3, 4, CZ_LEAF_LIGHT},
  {10, 33, 3, 3, CZ_LEAF_DARK},
  {15, 34, 12, 6, CZ_BOX},
  {16, 35, 10, 1, CZ_SHELF},
  {20, 1, 4, 3, CZ_LEAF},
  {24, 0, 3, 4, CZ_LEAF_DARK},
  {28, 4, 2, 2, CZ_LEAF},
  {29, 7, 2, 2, CZ_LEAF},
  {28, 10, 2, 2, CZ_LEAF},
  {29, 13, 2, 2, CZ_LEAF},
  {28, 16, 2, 2, CZ_LEAF},
  {29, 19, 2, 2, CZ_LEAF},
  {0, 40, 6, 8, CZ_LEAF_DARK},
  {3, 36, 6, 6, CZ_LEAF},
  {7, 42, 6, 6, CZ_LEAF_LIGHT},
  {0, 47, 9, 8, CZ_LEAF},
  {9, 47, 4, 8, CZ_LEAF_DARK},
  {50, 3, 26, 13, CZ_CORK},
  {53, 5, 5, 5, CZ_NOTE_Y},
  {60, 6, 4, 4, CZ_NOTE_R},
  {56, 11, 6, 3, CZ_NOTE_Y},
  {66, 4, 8, 9, CZ_WIN_FRAME},
  {67, 5, 6, 7, CZ_PIC_SKY},
  {68, 9, 2, 3, CZ_PIC_MOUNT},
  {70, 8, 2, 4, CZ_PIC_MOUNT},
  {112, 2, 38, 28, CZ_WIN_FRAME},
  {114, 4, 34, 24, CZ_SKY},
  {117, 7, 1, 1, CZ_STAR},
  {124, 11, 1, 1, CZ_STAR},
  {135, 8, 1, 1, CZ_STAR},
  {146, 13, 1, 1, CZ_STAR},
  {120, 13, 1, 1, CZ_STAR},
  {128, 6, 1, 1, CZ_STAR},
  {140, 6, 4, 4, CZ_MOON},
  {142, 6, 2, 3, CZ_SKY},
  {114, 22, 5, 6, CZ_CITY},
  {119, 19, 4, 9, CZ_CITY},
  {123, 23, 6, 5, CZ_CITY},
  {132, 20, 5, 8, CZ_CITY},
  {137, 24, 4, 4, CZ_CITY},
  {141, 21, 7, 7, CZ_CITY},
  {116, 24, 1, 1, CZ_CITY_LIGHT},
  {120, 21, 1, 1, CZ_CITY_LIGHT},
  {121, 24, 1, 1, CZ_CITY_LIGHT},
  {134, 22, 1, 1, CZ_CITY_LIGHT},
  {134, 25, 1, 1, CZ_CITY_LIGHT},
  {143, 23, 1, 1, CZ_CITY_LIGHT},
  {145, 25, 1, 1, CZ_CITY_LIGHT},
  {125, 25, 1, 1, CZ_CITY_LIGHT},
  {130, 4, 2, 24, CZ_WIN_FRAME},
  {114, 15, 34, 1, CZ_WIN_FRAME},
  {110, 30, 42, 2, CZ_SILL},
  {118, 26, 7, 4, CZ_CAT},
  {124, 23, 4, 4, CZ_CAT},
  {124, 22, 1, 1, CZ_CAT},
  {127, 22, 1, 1, CZ_CAT},
  {152, 0, 2, 2, CZ_LEAF_DARK},
  {153, 3, 2, 2, CZ_LEAF},
  {152, 6, 2, 2, CZ_LEAF_DARK},
  {153, 9, 2, 2, CZ_LEAF},
  {152, 12, 2, 2, CZ_LEAF_DARK},
  {153, 15, 2, 2, CZ_LEAF},
  {152, 18, 2, 2, CZ_LEAF_DARK},
  {153, 21, 2, 2, CZ_LEAF},
  {152, 24, 2, 2, CZ_LEAF_DARK},
  {153, 27, 2, 2, CZ_LEAF},
  {152, 30, 2, 2, CZ_LEAF_DARK},
  {153, 33, 2, 2, CZ_LEAF},
  {155, 2, 3, 3, CZ_LEAF_LIGHT},
  {150, 46, 9, 9, CZ_POT},
  {149, 38, 5, 6, CZ_LEAF},
  {154, 35, 5, 8, CZ_LEAF_DARK},
  {152, 41, 4, 5, CZ_LEAF_LIGHT},
  {34, 38, 104, 2, CZ_DESK},
  {34, 40, 104, 15, CZ_DESK_BODY},
  {36, 40, 4, 15, CZ_DESK_DARK},
  {112, 42, 22, 5, CZ_DRAWER},
  {112, 48, 22, 5, CZ_DRAWER},
  {122, 44, 3, 1, CZ_HANDLE},
  {122, 50, 3, 1, CZ_HANDLE},
  {78, 17, 30, 19, CZ_MON},
  {91, 36, 4, 2, CZ_MON},
  {88, 37, 10, 1, CZ_MON},
  {75, 37, 18, 1, CZ_KEYS},
  {99, 37, 3, 1, CZ_KEYS},
  {119, 36, 8, 2, CZ_LAMP_DARK},
  {123, 27, 2, 9, CZ_LAMP_DARK},
  {121, 25, 3, 2, CZ_LAMP_DARK},
  {117, 20, 5, 1, CZ_LAMP},
  {115, 21, 8, 1, CZ_LAMP},
  {114, 22, 9, 2, CZ_LAMP},
  {113, 24, 10, 1, CZ_LAMP},
  {128, 33, 5, 5, CZ_MUG},
  {133, 34, 1, 3, CZ_MUG},
  {129, 35, 2, 2, CZ_MUG_PRINT},
  {44, 20, 11, 22, CZ_CHAIR},
  {42, 40, 26, 3, CZ_CHAIR},
  {54, 43, 3, 8, CZ_CHAIR_DARK},
  {47, 51, 18, 2, CZ_CHAIR_DARK},
};
