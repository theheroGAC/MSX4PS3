#include "ps3_frontend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VKBD_KEYS_COUNT 88
#define VKBD_NUM_STICKY 5

#define VKBD_ACT_NONE      0
#define VKBD_ACT_MACRO_RUN 1
#define VKBD_ACT_MACRO_BLD 2
#define VKBD_ACT_MACRO_CLD 3
#define VKBD_ACT_MACRO_FIL 4

typedef struct {
    int x, y, w, h;
    const char *label_norm;
    const char *label_shift;
    uint8_t msx_row;
    uint8_t msx_mask;
    uint8_t is_mod;
    uint8_t sticky_id;
    uint8_t special_action;
} VkbdKey;

typedef struct {
    uint32_t chassis_bg;
    uint32_t chassis_border;
    uint32_t key_face;
    uint32_t key_hi;
    uint32_t key_sh;
    uint32_t key_border;
    uint32_t key_text;
    uint32_t mod_face;
    uint32_t mod_hi;
    uint32_t mod_sh;
    uint32_t mod_text;
    uint32_t cur_face;
    uint32_t cur_hi;
    uint32_t cur_border;
    uint32_t cur_text;
} VkbdTheme;

static const VkbdTheme s_themes[4] = {
    {
        0xFF282C34U, 0xFF586070U,
        0xFFC6C6C6U, 0xFFFFFFFFU, 0xFF888888U, 0xFF101010U, 0xFF0A0A0EU,
        0xFF989CA6U, 0xFFE0E4ECU, 0xFF646870U, 0xFF0A0A0EU,
        0xFF3898FFU, 0xFF90D0FFU, 0xFFFFFFFFU, 0xFFFFFFFFU
    },
    {
        0xFF2E2A24U, 0xFF786C5CU,
        0xFFE3E0D6U, 0xFFFFFFFAU, 0xFFA49C8CU, 0xFF24201CU, 0xFF282A30U,
        0xFFD4C2A4U, 0xFFF5EBD7U, 0xFF94846AU, 0xFF282A30U,
        0xFFE88820U, 0xFFFFC870U, 0xFFFFFFFFU, 0xFF181208U
    },
    {
        0xFF1C2636U, 0xFF4C688CU,
        0xFFE4EAF2U, 0xFFFFFFFFU, 0xFF919EB0U, 0xFF141C28U, 0xFF1C2430U,
        0xFFB6C6DAU, 0xFFE4EEFAU, 0xFF76869CU, 0xFF141C28U,
        0xFF00B8E8U, 0xFF80F0FFU, 0xFFFFFFFFU, 0xFF06101CU
    },
    {
        0xFF12151AU, 0xFF3C4654U,
        0xFF2C3036U, 0xFF525862U, 0xFF16181CU, 0xFF06080AU, 0xFFF5FCFFU,
        0xFF4C545EU, 0xFF747E8AU, 0xFF262A30U, 0xFFFFE68CU,
        0xFF00C878U, 0xFF70FFB8U, 0xFFFFFFFFU, 0xFF06140CU
    }
};

static const VkbdKey s_keys[VKBD_KEYS_COUNT] = {
    {  10,   8,  66, 34, "F1",    "F6",    6, 0x20, 1, 0, 0 },
    {  80,   8,  66, 34, "F2",    "F7",    6, 0x40, 1, 0, 0 },
    { 150,   8,  66, 34, "F3",    "F8",    6, 0x80, 1, 0, 0 },
    { 220,   8,  66, 34, "F4",    "F9",    7, 0x01, 1, 0, 0 },
    { 290,   8,  66, 34, "F5",    "F10",   7, 0x02, 1, 0, 0 },
    { 366,   8,  72, 34, "SEL",   "SEL",   7, 0x40, 1, 0, 0 },
    { 442,   8,  72, 34, "STOP",  "STOP",  7, 0x10, 1, 0, 0 },
    { 518,   8,  72, 34, "HOME",  "CLS",   8, 0x02, 1, 0, 0 },
    { 594,   8,  68, 34, "INS",   "INS",   8, 0x04, 1, 0, 0 },
    { 666,   8,  68, 34, "DEL",   "DEL",   8, 0x08, 1, 0, 0 },
    { 750,   8,  48, 34, "NP7",   "NP7",  10, 0x04, 1, 0, 0 },
    { 802,   8,  48, 34, "NP8",   "NP8",  10, 0x08, 1, 0, 0 },
    { 854,   8,  48, 34, "NP9",   "NP9",  10, 0x10, 1, 0, 0 },
    { 906,   8,  48, 34, "NP/",   "NP/",   9, 0x04, 1, 0, 0 },

    {  10,  46,  54, 34, "ESC",   "ESC",   7, 0x04, 1, 0, 0 },
    {  68,  46,  42, 34, "1",     "!",     0, 0x02, 0, 0, 0 },
    { 114,  46,  42, 34, "2",     "\"",    0, 0x04, 0, 0, 0 },
    { 160,  46,  42, 34, "3",     "#",     0, 0x08, 0, 0, 0 },
    { 206,  46,  42, 34, "4",     "$",     0, 0x10, 0, 0, 0 },
    { 252,  46,  42, 34, "5",     "%",     0, 0x20, 0, 0, 0 },
    { 298,  46,  42, 34, "6",     "&",     0, 0x40, 0, 0, 0 },
    { 344,  46,  42, 34, "7",     "'",     0, 0x80, 0, 0, 0 },
    { 390,  46,  42, 34, "8",     "(",     1, 0x01, 0, 0, 0 },
    { 436,  46,  42, 34, "9",     ")",     1, 0x02, 0, 0, 0 },
    { 482,  46,  42, 34, "0",     "0",     0, 0x01, 0, 0, 0 },
    { 528,  46,  42, 34, "-",     "=",     1, 0x04, 0, 0, 0 },
    { 574,  46,  42, 34, "=",     "+",     1, 0x08, 0, 0, 0 },
    { 620,  46,  42, 34, "\\",    "|",     1, 0x10, 0, 0, 0 },
    { 666,  46,  68, 34, "BS",    "BS",    7, 0x20, 1, 0, 0 },
    { 750,  46,  48, 34, "NP4",   "NP4",   9, 0x80, 1, 0, 0 },
    { 802,  46,  48, 34, "NP5",   "NP5",  10, 0x01, 1, 0, 0 },
    { 854,  46,  48, 34, "NP6",   "NP6",  10, 0x02, 1, 0, 0 },
    { 906,  46,  48, 34, "NP*",   "NP*",   9, 0x01, 1, 0, 0 },

    {  10,  84,  66, 34, "TAB",   "TAB",   7, 0x08, 1, 0, 0 },
    {  80,  84,  44, 34, "q",     "Q",     4, 0x40, 0, 0, 0 },
    { 128,  84,  44, 34, "w",     "W",     5, 0x10, 0, 0, 0 },
    { 176,  84,  44, 34, "e",     "E",     3, 0x04, 0, 0, 0 },
    { 224,  84,  44, 34, "r",     "R",     4, 0x80, 0, 0, 0 },
    { 272,  84,  44, 34, "t",     "T",     5, 0x02, 0, 0, 0 },
    { 320,  84,  44, 34, "y",     "Y",     5, 0x40, 0, 0, 0 },
    { 368,  84,  44, 34, "u",     "U",     5, 0x04, 0, 0, 0 },
    { 416,  84,  44, 34, "i",     "I",     3, 0x40, 0, 0, 0 },
    { 464,  84,  44, 34, "o",     "O",     4, 0x10, 0, 0, 0 },
    { 512,  84,  44, 34, "p",     "P",     4, 0x20, 0, 0, 0 },
    { 560,  84,  44, 34, "[",     "{",     1, 0x20, 0, 0, 0 },
    { 608,  84,  44, 34, "]",     "}",     1, 0x40, 0, 0, 0 },
    { 656,  84,  78, 72, "RET",   "RET",   7, 0x80, 1, 0, 0 },
    { 750,  84,  48, 34, "NP1",   "NP1",   9, 0x10, 1, 0, 0 },
    { 802,  84,  48, 34, "NP2",   "NP2",   9, 0x20, 1, 0, 0 },
    { 854,  84,  48, 34, "NP3",   "NP3",   9, 0x40, 1, 0, 0 },
    { 906,  84,  48, 34, "NP-",   "NP-",  10, 0x20, 1, 0, 0 },

    {  10, 122,  72, 34, "CTRL",  "CTRL",  6, 0x02, 1, 2, 0 },
    {  86, 122,  44, 34, "a",     "A",     2, 0x40, 0, 0, 0 },
    { 134, 122,  44, 34, "s",     "S",     5, 0x01, 0, 0, 0 },
    { 182, 122,  44, 34, "d",     "D",     3, 0x02, 0, 0, 0 },
    { 230, 122,  44, 34, "f",     "F",     3, 0x08, 0, 0, 0 },
    { 278, 122,  44, 34, "g",     "G",     3, 0x10, 0, 0, 0 },
    { 326, 122,  44, 34, "h",     "H",     3, 0x20, 0, 0, 0 },
    { 374, 122,  44, 34, "j",     "J",     3, 0x80, 0, 0, 0 },
    { 422, 122,  44, 34, "k",     "K",     4, 0x01, 0, 0, 0 },
    { 470, 122,  44, 34, "l",     "L",     4, 0x02, 0, 0, 0 },
    { 518, 122,  44, 34, ";",     ":",     1, 0x80, 0, 0, 0 },
    { 566, 122,  44, 34, "'",     "\"",    2, 0x01, 0, 0, 0 },
    { 614, 122,  38, 34, "`",     "~",     2, 0x02, 0, 0, 0 },
    { 750, 122,  48, 34, "NP0",   "NP0",   9, 0x08, 1, 0, 0 },
    { 802, 122,  48, 34, "NP.",   "NP.",  10, 0x80, 1, 0, 0 },
    { 854, 122,  48, 34, "NP,",   "NP,",  10, 0x40, 1, 0, 0 },
    { 906, 122,  48, 34, "NP+",   "NP+",   9, 0x02, 1, 0, 0 },

    {  10, 160,  86, 34, "SHIFT", "SHIFT", 6, 0x01, 1, 1, 0 },
    { 100, 160,  44, 34, "z",     "Z",     5, 0x80, 0, 0, 0 },
    { 148, 160,  44, 34, "x",     "X",     5, 0x20, 0, 0, 0 },
    { 196, 160,  44, 34, "c",     "C",     3, 0x01, 0, 0, 0 },
    { 244, 160,  44, 34, "v",     "V",     5, 0x08, 0, 0, 0 },
    { 292, 160,  44, 34, "b",     "B",     2, 0x80, 0, 0, 0 },
    { 340, 160,  44, 34, "n",     "N",     4, 0x08, 0, 0, 0 },
    { 388, 160,  44, 34, "m",     "M",     4, 0x04, 0, 0, 0 },
    { 436, 160,  44, 34, ",",     "<",     2, 0x04, 0, 0, 0 },
    { 484, 160,  44, 34, ".",     ">",     2, 0x08, 0, 0, 0 },
    { 532, 160,  44, 34, "/",     "?",     2, 0x10, 0, 0, 0 },
    { 580, 160,  94, 34, "R-SHF", "R-SHF", 6, 0x01, 1, 1, 0 },
    { 678, 160,  56, 34, "UP",    "UP",    8, 0x20, 1, 0, 0 },
    { 750, 160,  98, 34, "CAS:RUN","CAS:RUN",0,0x00, 1, 0, VKBD_ACT_MACRO_RUN },
    { 856, 160,  98, 34, "BLOAD", "BLOAD", 0, 0x00, 1, 0, VKBD_ACT_MACRO_BLD },

    {  10, 198,  72, 34, "CAPS",  "CAPS",  6, 0x08, 1, 4, 0 },
    {  86, 198,  76, 34, "GRAPH", "GRAPH", 6, 0x04, 1, 3, 0 },
    { 166, 198, 314, 34, "SPACE", "SPACE", 8, 0x01, 0, 0, 0 },
    { 484, 198,  72, 34, "KANA",  "KANA",  6, 0x10, 1, 5, 0 },
    { 560, 198,  56, 34, "LEFT",  "LEFT",  8, 0x10, 1, 0, 0 }
};

#define EXTRA_KEYS_COUNT 4
static const VkbdKey s_extra_keys[EXTRA_KEYS_COUNT] = {
    { 620, 198,  56, 34, "DOWN",  "DOWN",  8, 0x40, 1, 0, 0 },
    { 680, 198,  54, 34, "RGHT",  "RGHT",  8, 0x80, 1, 0, 0 },
    { 750, 198,  98, 34, "CLOAD", "CLOAD", 0, 0x00, 1, 0, VKBD_ACT_MACRO_CLD },
    { 856, 198,  98, 34, "FILES", "FILES", 0, 0x00, 1, 0, VKBD_ACT_MACRO_FIL }
};

#define TOTAL_VKBD_KEYS (VKBD_KEYS_COUNT + EXTRA_KEYS_COUNT)

static int  s_vkbd_sel = 85;
static int  s_vkbd_y = 436;
static int  s_sticky_state[VKBD_NUM_STICKY + 1] = {0};
static int  s_active_row = -1;
static int  s_active_mask = 0;
static int  s_hold_bs_frames = 0;
static int  s_nav_repeat_timer = 0;
static int  s_blink_counter = 0;
static int  s_analog_cool = 0;
static char s_macro_buf[128] = {0};
static int  s_macro_pos = 0;
static int  s_macro_delay = 0;

static const VkbdKey *get_vkbd_key(int idx)
{
    if (idx < 0 || idx >= TOTAL_VKBD_KEYS) return &s_keys[0];
    if (idx < VKBD_KEYS_COUNT) return &s_keys[idx];
    return &s_extra_keys[idx - VKBD_KEYS_COUNT];
}

void ps3_vkbd_init(void)
{
    s_vkbd_sel = 85;
    if (g_cfg.vkbd_position == 1) s_vkbd_y = 26;
    else if (g_cfg.vkbd_position == 2) s_vkbd_y = (SCREEN_H - 266) / 2;
    else s_vkbd_y = SCREEN_H - 278;
    ps3_vkbd_reset_sticky();
}

void ps3_vkbd_reset_sticky(void)
{
    for (int i = 0; i <= VKBD_NUM_STICKY; i++) {
        s_sticky_state[i] = 0;
    }
    s_active_row = -1;
    s_active_mask = 0;
    s_hold_bs_frames = 0;
}

static void inject_msx_basic_keybuf(const char *str)
{
    if (!str || !*str) return;
    word putpnt = (word)RdZ80(0xF3F8) | ((word)RdZ80(0xF3F9) << 8);
    word getpnt = (word)RdZ80(0xF3FA) | ((word)RdZ80(0xF3FB) << 8);
    if (putpnt < 0xFBF0 || putpnt >= 0xFC18 || getpnt < 0xFBF0 || getpnt >= 0xFC18) {
        s_macro_pos = 0;
        snprintf(s_macro_buf, sizeof(s_macro_buf), "%s", str);
        s_macro_delay = 4;
        return;
    }
    while (*str) {
        WrZ80(putpnt, (byte)*str);
        putpnt++;
        if (putpnt >= 0xFC18) putpnt = 0xFBF0;
        if (putpnt == getpnt) break;
        str++;
    }
    WrZ80(0xF3F8, putpnt & 0xFF);
    WrZ80(0xF3F9, (putpnt >> 8) & 0xFF);
}

void ps3_vkbd_queue_macro(const char *str)
{
    inject_msx_basic_keybuf(str);
}

int ps3_vkbd_macro_active(void)
{
    return (s_macro_buf[s_macro_pos] != '\0');
}

static int find_nearest_key(int cur, int dx, int dy)
{
    const VkbdKey *ck = get_vkbd_key(cur);
    int cx = ck->x + ck->w / 2;
    int cy = ck->y + ck->h / 2;

    int best = cur;
    int best_score = 99999999;

    for (int i = 0; i < TOTAL_VKBD_KEYS; i++) {
        if (i == cur) continue;
        const VkbdKey *tk = get_vkbd_key(i);
        int tx = tk->x + tk->w / 2;
        int ty = tk->y + tk->h / 2;
        int ddx = tx - cx;
        int ddy = ty - cy;

        if (dx < 0 && (ddx >= -4 || abs(ddy) > 20)) continue;
        if (dx > 0 && (ddx <=  4 || abs(ddy) > 20)) continue;
        if (dy < 0 && ddy >= -10) continue;
        if (dy > 0 && ddy <=  10) continue;

        int score;
        if (dx != 0) {
            score = abs(ddx) + abs(ddy) * 6;
        } else {
            score = abs(ddy) * 3 + abs(ddx);
        }
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

void ps3_vkbd_update(uint32_t buttons, uint32_t pressed)
{
    s_blink_counter++;
    s_active_row = -1;
    s_active_mask = 0;

    if (g_pad1_Ry < -48) {
        s_vkbd_y -= 6;
        if (s_vkbd_y < 16) s_vkbd_y = 16;
    } else if (g_pad1_Ry > 48) {
        s_vkbd_y += 6;
        if (s_vkbd_y > SCREEN_H - 270) s_vkbd_y = SCREEN_H - 270;
    }

    if (s_analog_cool > 0) s_analog_cool--;
    if (s_analog_cool == 0) {
        if (g_pad1_Rx < -64) {
            g_cfg.vkbd_transparency = (g_cfg.vkbd_transparency + 3) % 4;
            s_analog_cool = 18;
        } else if (g_pad1_Rx > 64) {
            g_cfg.vkbd_transparency = (g_cfg.vkbd_transparency + 1) % 4;
            s_analog_cool = 18;
        }
    }

    if (pressed & PS3_BTN_R2) {
        g_cfg.vkbd_style = (g_cfg.vkbd_style + 1) % 4;
        const char *tnames[4] = {
            "KEYBOARD THEME: ORIGINAL",
            "KEYBOARD THEME: WARM",
            "KEYBOARD THEME: COOL",
            "KEYBOARD THEME: DARK"
        };
        ps3_toast_show(tnames[g_cfg.vkbd_style & 3], 75);
    }

    if (pressed & PS3_BTN_TRIANGLE) {
        s_sticky_state[1] = !s_sticky_state[1];
    }

    if (pressed & PS3_BTN_SQUARE) {
        s_hold_bs_frames = 4;
    }

    if (pressed & PS3_BTN_CIRCLE) {
        int any_stuck = 0;
        for (int i = 1; i <= VKBD_NUM_STICKY; i++) {
            if (s_sticky_state[i]) any_stuck = 1;
        }
        if (any_stuck) {
            ps3_vkbd_reset_sticky();
            ps3_toast_show("STICKY KEYS RESET", 60);
        } else {
            g_vkbd_mode = 0;
        }
        return;
    }

    int dx = 0, dy = 0;
    if ((buttons & PS3_BTN_LEFT)  || g_pad1_Lx < -55) dx = -1;
    else if ((buttons & PS3_BTN_RIGHT) || g_pad1_Lx > 55) dx = 1;
    if ((buttons & PS3_BTN_UP)    || g_pad1_Ly < -55) dy = -1;
    else if ((buttons & PS3_BTN_DOWN)  || g_pad1_Ly > 55) dy = 1;

    if (dx != 0 || dy != 0) {
        if (s_nav_repeat_timer == 0 || (s_nav_repeat_timer >= 14 && (s_nav_repeat_timer % 4) == 0)) {
            if (dx != 0) s_vkbd_sel = find_nearest_key(s_vkbd_sel, dx, 0);
            if (dy != 0) s_vkbd_sel = find_nearest_key(s_vkbd_sel, 0, dy);
        }
        s_nav_repeat_timer++;
    } else {
        s_nav_repeat_timer = 0;
    }

    const VkbdKey *k = get_vkbd_key(s_vkbd_sel);
    if (pressed & PS3_BTN_CROSS) {
        if (k->sticky_id > 0 && k->sticky_id <= VKBD_NUM_STICKY) {
            s_sticky_state[k->sticky_id] = !s_sticky_state[k->sticky_id];
        } else if (k->special_action != VKBD_ACT_NONE) {
            if (k->special_action == VKBD_ACT_MACRO_RUN) {
                RewindTape();
                inject_msx_basic_keybuf("RUN\"CAS:\"\r");
                ps3_toast_show("COMMAND: RUN\"CAS:\"", 90);
            } else if (k->special_action == VKBD_ACT_MACRO_BLD) {
                RewindTape();
                inject_msx_basic_keybuf("BLOAD\"CAS:\",R\r");
                ps3_toast_show("COMMAND: BLOAD\"CAS:\",R", 90);
            } else if (k->special_action == VKBD_ACT_MACRO_CLD) {
                RewindTape();
                inject_msx_basic_keybuf("CLOAD\rRUN\r");
                ps3_toast_show("COMMAND: CLOAD + RUN", 90);
            } else if (k->special_action == VKBD_ACT_MACRO_FIL) {
                inject_msx_basic_keybuf("FILES\r");
                ps3_toast_show("COMMAND: FILES", 90);
            }
        }
    }

    if ((buttons & PS3_BTN_CROSS) && k->sticky_id == 0 && k->special_action == VKBD_ACT_NONE) {
        s_active_row = k->msx_row;
        s_active_mask = k->msx_mask;
    }
}

void ps3_vkbd_apply_to_msx(void)
{
    if (s_sticky_state[1]) KeyState[6] &= ~0x01;
    if (s_sticky_state[2]) KeyState[6] &= ~0x02;
    if (s_sticky_state[3]) KeyState[6] &= ~0x04;
    if (s_sticky_state[4]) KeyState[6] &= ~0x08;
    if (s_sticky_state[5]) KeyState[6] &= ~0x10;

    if (s_active_row >= 0 && s_active_row < 16 && s_active_mask != 0) {
        KeyState[s_active_row] &= (byte)~s_active_mask;
    }

    if (s_hold_bs_frames > 0) {
        KeyState[7] &= ~0x20;
        s_hold_bs_frames--;
    }

    if (s_macro_buf[s_macro_pos] != '\0') {
        if (s_macro_delay > 0) {
            s_macro_delay--;
            if (s_macro_delay >= 2) {
                unsigned char ch = (unsigned char)s_macro_buf[s_macro_pos];
                if (ch == '\r') {
                    KBD_SET(KBD_ENTER);
                } else if (ch < 128) {
                    if (ch == '"' || ch == ':' || (ch >= 'A' && ch <= 'Z')) {
                        KBD_SET(KBD_SHIFT);
                    }
                    KBD_SET(ch);
                }
            }
        } else {
            s_macro_pos++;
            if (s_macro_buf[s_macro_pos] != '\0') {
                s_macro_delay = 4;
            }
        }
    }
}

void ps3_vkbd_draw(uint32_t *fb)
{
    static const int alpha_lut[4] = { 255, 208, 160, 110 };
    int alpha = alpha_lut[g_cfg.vkbd_transparency & 3];
    const VkbdTheme *th = &s_themes[g_cfg.vkbd_style & 3];

    int kb_w = 964;
    int kb_h = 272;
    int kb_x = (SCREEN_W - kb_w) / 2;
    int kb_y = s_vkbd_y;
    if (kb_y < 12) kb_y = 12;
    if (kb_y > SCREEN_H - kb_h - 8) kb_y = SCREEN_H - kb_h - 8;

    ps3_draw_bevel_box(fb, kb_x, kb_y, kb_w, kb_h,
                       th->chassis_bg, th->chassis_border, 0xFF080A0EU, 0xFF000000U, alpha);

    ps3_draw_rect_alpha(fb, kb_x + 741, kb_y + 6, 2, kb_h - 44, th->chassis_border, alpha);

    int shift_on = s_sticky_state[1];

    for (int i = 0; i < TOTAL_VKBD_KEYS; i++) {
        const VkbdKey *k = get_vkbd_key(i);
        int kx = kb_x + k->x;
        int ky = kb_y + k->y;
        int kw = k->w;
        int kh = k->h;

        int is_sel = (i == s_vkbd_sel);
        int is_stuck = (k->sticky_id > 0 && k->sticky_id <= VKBD_NUM_STICKY && s_sticky_state[k->sticky_id]);

        uint32_t face = k->is_mod ? th->mod_face : th->key_face;
        uint32_t hi   = k->is_mod ? th->mod_hi   : th->key_hi;
        uint32_t sh   = k->is_mod ? th->mod_sh   : th->key_sh;
        uint32_t brd  = th->key_border;
        uint32_t txt  = k->is_mod ? th->mod_text : th->key_text;

        if (k->special_action != VKBD_ACT_NONE) {
            face = 0xFF1E4878U;
            hi   = 0xFF4888C8U;
            sh   = 0xFF0E2440U;
            txt  = 0xFF90F0FFU;
        }

        if (is_sel) {
            face = th->cur_face;
            hi   = th->cur_hi;
            sh   = th->chassis_bg;
            brd  = (s_blink_counter & 8) ? th->cur_border : 0xFFFFE040U;
            txt  = th->cur_text;
        }

        ps3_draw_bevel_box(fb, kx, ky, kw, kh, face, hi, sh, brd, alpha);

        if (k->sticky_id > 0) {
            uint32_t led_col = is_stuck ? 0xFF00FF40U : 0xFF283028U;
            ps3_draw_rect(fb, kx + 4, ky + 4, 7, 4, 0xFF080808U);
            ps3_draw_rect(fb, kx + 5, ky + 5, 5, 2, led_col);
        }

        const char *norm = k->label_norm;
        const char *shft = k->label_shift;
        int has_dual = (norm && shft && strcmp(norm, shft) != 0 && strlen(norm) == 1 && strlen(shft) == 1 && (norm[0] < 'a' || norm[0] > 'z'));

        if (has_dual) {
            uint32_t top_col = shift_on ? txt : 0xFF88A8C8U;
            uint32_t bot_col = shift_on ? 0xFF88A8C8U : txt;
            ps3_draw_text_centered(fb, kx + kw / 2, ky + 4, shft, top_col, 0);
            ps3_draw_text_centered(fb, kx + kw / 2, ky + 17, norm, bot_col, 0);
        } else if (norm && strlen(norm) == 1 && norm[0] >= 'a' && norm[0] <= 'z') {
            int ty = ky + (kh - 13) / 2 - 1;
            ps3_draw_text_centered(fb, kx + kw / 2, ty, shft, txt, 0);
        } else {
            const char *lbl = shift_on ? shft : norm;
            int ty = ky + (kh - 13) / 2 - 1;
            ps3_draw_text_centered(fb, kx + kw / 2, ty, lbl, txt, 0);
        }
    }

    int bar_y = kb_y + kb_h - 32;
    ps3_draw_rect_alpha(fb, kb_x + 6, bar_y, kb_w - 12, 26, 0xFF0E131CU, alpha);
    int hx = kb_x + 18;
    int hy = bar_y + (26 - 17) / 2;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_CROSS,    "Press",         0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_TRIANGLE, "Shift",         0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_SQUARE,   "BS",            0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_CIRCLE,   "Clear/Close",   0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_L1,       "Enter",         0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_R1,       "Space",         0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_R3,       "Move/Alpha",    0xFFD8E8FFU, 1) + 18;
    hx += ps3_draw_btn_label(fb, hx, hy, BTN_ICON_R2,       "Theme",         0xFFD8E8FFU, 1) + 18;
    ps3_draw_btn_label(fb, hx, hy, BTN_ICON_L2,             "Exit VKBD",     0xFFD8E8FFU, 1);
}
