#include "ps3_frontend.h"
#include "ps3_btn_icons.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define TTF_FONT_SLOTS 6
#define TTF_CACHE_MAX 384

typedef struct {
    char text[128];
    int px;
    uint32_t col;
    SDL_Surface *surface;
} TtfStringCache;

static char s_ttf_path[256] = "";
static TTF_Font *s_ttf_fonts[TTF_FONT_SLOTS] = { NULL };
static int s_ttf_font_px[TTF_FONT_SLOTS] = { 0 };
static TtfStringCache s_ttf_cache[TTF_CACHE_MAX] = { {{0}} };
static int s_ttf_cache_next = 0;
static int s_ttf_ready = 0;
static int s_ttf_checked = 0;
static SDL_Surface *s_screen_surface = NULL;

static const uint8_t s_font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x7E,0x18,0x3C,0x66,0x3C,0x18,0x7E},
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x34,0x64,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00},
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    {0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00},
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00},
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    {0x1C,0x30,0x78,0x30,0x30,0x30,0x30,0x00},
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C},
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    {0x18,0x7E,0x18,0x18,0x18,0x18,0x0E,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x10,0x38,0x6C,0xC6,0xFE,0x00,0x00}
};

static const uint8_t s_char_widths[96] = {
    4, 2, 4, 7, 6, 7, 7, 2, 4, 4, 6, 6, 2, 6, 2, 6,
    6, 5, 6, 6, 6, 6, 6, 6, 6, 6, 2, 2, 5, 6, 5, 6,
    7, 6, 6, 6, 6, 6, 6, 6, 6, 5, 6, 6, 5, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 4, 6, 4, 5, 6,
    3, 5, 5, 5, 5, 5, 4, 5, 5, 2, 4, 5, 2, 7, 5, 5,
    5, 5, 5, 5, 4, 5, 5, 7, 5, 5, 5, 4, 2, 4, 7, 6
};

static char s_toast_msg[128] = "";
static int  s_toast_timer = 0;

static inline uint32_t blend_argb(uint32_t bg, uint32_t fg, int alpha)
{
    if (alpha <= 0) return bg;
    if (alpha >= 255) return fg;
    uint32_t rb = (bg & 0x00FF00FFU) + (((fg & 0x00FF00FFU) - (bg & 0x00FF00FFU)) * alpha >> 8);
    uint32_t g  = (bg & 0x0000FF00U) + (((fg & 0x0000FF00U) - (bg & 0x0000FF00U)) * alpha >> 8);
    return 0xFF000000U | (rb & 0x00FF00FFU) | (g & 0x0000FF00U);
}

static void ttf_string_cache_clear(void)
{
    for (int i = 0; i < TTF_CACHE_MAX; i++) {
        if (s_ttf_cache[i].surface) {
            SDL_FreeSurface(s_ttf_cache[i].surface);
            s_ttf_cache[i].surface = NULL;
        }
    }
    s_ttf_cache_next = 0;
}

static void ttf_ensure_ready(void)
{
    if (s_ttf_checked) return;
    s_ttf_checked = 1;

    if (TTF_Init() < 0) return;

    static const char *cand_fonts[] = {
        "/dev_hdd0/game/FMSX00001/USRDIR/font.ttf",
        "/dev_hdd0/game/FMSX00001/USRDIR/data/font.ttf",
        "pkg/USRDIR/font.ttf",
        "pkg/USRDIR/data/font.ttf",
        "/dev_hdd0/fmsx/font.ttf",
        "/dev_usb000/font.ttf",
        "/dev_usb001/font.ttf"
    };
    for (int i = 0; i < 7; i++) {
        TTF_Font *probe = TTF_OpenFont(cand_fonts[i], 16);
        if (probe) {
            TTF_CloseFont(probe);
            strncpy(s_ttf_path, cand_fonts[i], sizeof(s_ttf_path) - 1);
            s_ttf_path[sizeof(s_ttf_path) - 1] = '\0';
            s_ttf_ready = 1;
            return;
        }
    }
}

void ps3_font_init(void)
{
    ttf_ensure_ready();
}

void ps3_font_shutdown(void)
{
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        if (s_ttf_fonts[i]) {
            TTF_CloseFont(s_ttf_fonts[i]);
            s_ttf_fonts[i] = NULL;
            s_ttf_font_px[i] = 0;
        }
    }
    ttf_string_cache_clear();
    if (s_screen_surface) {
        SDL_FreeSurface(s_screen_surface);
        s_screen_surface = NULL;
    }
    if (s_ttf_ready) {
        TTF_Quit();
        s_ttf_ready = 0;
    }
    s_ttf_checked = 0;
}

static SDL_Surface *get_screen_surface(void)
{
    if (!s_screen_surface) {
        s_screen_surface = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)g_framebuf, SCREEN_W, SCREEN_H, 32, SCREEN_W * 4, SDL_PIXELFORMAT_ARGB8888);
    }
    return s_screen_surface;
}

static int px_for_scale(int scale)
{
    if (scale <= 0) return 13;
    if (scale == 1) return 17;
    if (scale == 2) return 21;
    return 26;
}

static TTF_Font *ttf_font_for_px(int px)
{
    if (!s_ttf_ready) return NULL;
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        if (s_ttf_font_px[i] == px && s_ttf_fonts[i]) return s_ttf_fonts[i];
    }
    int slot = -1;
    for (int i = 0; i < TTF_FONT_SLOTS; i++) {
        if (!s_ttf_fonts[i]) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        TTF_CloseFont(s_ttf_fonts[0]);
        s_ttf_fonts[0] = NULL;
        s_ttf_font_px[0] = 0;
        ttf_string_cache_clear();
    }
    s_ttf_fonts[slot] = TTF_OpenFont(s_ttf_path, px);
    if (s_ttf_fonts[slot]) {
        s_ttf_font_px[slot] = px;
    }
    return s_ttf_fonts[slot];
}

static SDL_Surface *ttf_render_line(int px, uint32_t col, const char *line, int *out_width)
{
    size_t len = strlen(line);
    if (len == 0 || len >= sizeof(s_ttf_cache[0].text)) return NULL;

    for (int i = 0; i < TTF_CACHE_MAX; i++) {
        TtfStringCache *e = &s_ttf_cache[i];
        if (e->surface && e->px == px && e->col == col && strcmp(e->text, line) == 0) {
            if (out_width) *out_width = e->surface->w;
            return e->surface;
        }
    }

    TTF_Font *font = ttf_font_for_px(px);
    if (!font) return NULL;

    SDL_Color sc;
    sc.r = (uint8_t)((col >> 16) & 0xFF);
    sc.g = (uint8_t)((col >> 8)  & 0xFF);
    sc.b = (uint8_t)(col & 0xFF);
    sc.a = (uint8_t)((col >> 24) & 0xFF);
    if (sc.a == 0) sc.a = 255;

    SDL_Surface *s = TTF_RenderUTF8_Blended(font, line, sc);
    if (!s) return NULL;

    TtfStringCache *e = &s_ttf_cache[s_ttf_cache_next];
    s_ttf_cache_next = (s_ttf_cache_next + 1) % TTF_CACHE_MAX;
    if (e->surface) SDL_FreeSurface(e->surface);
    e->surface = s;
    e->px = px;
    e->col = col;
    strncpy(e->text, line, sizeof(e->text) - 1);
    e->text[sizeof(e->text) - 1] = '\0';

    if (out_width) *out_width = s->w;
    return s;
}

int ps3_text_width(const char *str, int scale)
{
    if (!str || !*str) return 0;
    if (s_ttf_ready) {
        int px = px_for_scale(scale);
        TTF_Font *f = ttf_font_for_px(px);
        if (f) {
            int max_w = 0;
            const char *p = str;
            while (*p) {
                const char *nl = strchr(p, '\n');
                char line[192];
                if (nl) {
                    size_t n = (size_t)(nl - p);
                    if (n >= sizeof(line)) n = sizeof(line) - 1;
                    memcpy(line, p, n);
                    line[n] = '\0';
                } else {
                    if (strlen(p) >= sizeof(line)) break;
                    strcpy(line, p);
                }
                int lw = 0, lh = 0;
                if (TTF_SizeUTF8(f, line, &lw, &lh) == 0 && lw > max_w) {
                    max_w = lw;
                }
                if (!nl) break;
                p = nl + 1;
            }
            if (max_w > 0) return max_w;
        }
    }

    int char_scale = (scale <= 0) ? 1 : (scale == 1 ? 2 : (scale == 2 ? 2 : 3));
    int max_w = 0;
    int cur_w = 0;
    for (int i = 0; str[i]; i++) {
        unsigned char uc = (unsigned char)str[i];
        if (uc == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0;
            continue;
        }
        if (uc >= 32 && uc <= 126) {
            cur_w += (s_char_widths[uc - 32] + 1) * char_scale;
        } else {
            cur_w += 7 * char_scale;
        }
    }
    if (cur_w > max_w) max_w = cur_w;
    return max_w;
}

void ps3_draw_text(uint32_t *fb, int x, int y, const char *str, uint32_t color, int scale)
{
    if (!str || !*str || !fb) return;
    if (s_ttf_ready) {
        int px = px_for_scale(scale);
        TTF_Font *font = ttf_font_for_px(px);
        if (font) {
            SDL_Surface *screen = get_screen_surface();
            int cur_y = y;
            const char *p = str;
            int lskip = TTF_FontLineSkip(font);
            if (lskip < 12) lskip = px + 4;

            while (*p) {
                const char *nl = strchr(p, '\n');
                char line[192];
                if (nl) {
                    size_t n = (size_t)(nl - p);
                    if (n >= sizeof(line)) n = sizeof(line) - 1;
                    memcpy(line, p, n);
                    line[n] = '\0';
                } else {
                    if (strlen(p) >= sizeof(line)) break;
                    strcpy(line, p);
                }
                int lw = 0;
                SDL_Surface *s = ttf_render_line(px, color, line, &lw);
                if (s && screen) {
                    SDL_Rect r = { (Sint16)x, (Sint16)cur_y, (Uint16)s->w, (Uint16)s->h };
                    SDL_BlitSurface(s, NULL, screen, &r);
                }
                if (!nl) break;
                cur_y += lskip;
                p = nl + 1;
            }
            return;
        }
    }

    int char_scale = (scale <= 0) ? 1 : (scale == 1 ? 2 : (scale == 2 ? 2 : 3));
    int cur_x = x;
    int cur_y = y;
    uint32_t col = color | 0xFF000000U;

    for (int i = 0; str[i]; i++) {
        unsigned char uc = (unsigned char)str[i];
        if (uc == '\n') {
            cur_x = x;
            cur_y += 9 * char_scale;
            continue;
        }
        if (uc >= 32 && uc <= 126) {
            const uint8_t *glyph = s_font8x8[uc - 32];
            int char_w = s_char_widths[uc - 32];
            for (int r = 0; r < 8; r++) {
                uint8_t bits = glyph[r];
                if (!bits) continue;
                int py0 = cur_y + r * char_scale;
                for (int c = 0; c < 8; c++) {
                    if (bits & (0x80 >> c)) {
                        int px0 = cur_x + c * char_scale;
                        for (int sy = 0; sy < char_scale; sy++) {
                            int py = py0 + sy;
                            if (py < 0 || py >= SCREEN_H) continue;
                            uint32_t *drow = fb + py * SCREEN_W;
                            for (int sx = 0; sx < char_scale; sx++) {
                                int px = px0 + sx;
                                if (px >= 0 && px < SCREEN_W) {
                                    drow[px] = col;
                                }
                            }
                        }
                    }
                }
            }
            cur_x += (char_w + 1) * char_scale;
        } else {
            cur_x += 7 * char_scale;
        }
    }
}

void ps3_draw_text_shadow(uint32_t *fb, int x, int y, const char *str, uint32_t color, uint32_t shadow, int scale)
{
    int off = (scale >= 2) ? 2 : 1;
    ps3_draw_text(fb, x + off, y + off, str, shadow, scale);
    ps3_draw_text(fb, x, y, str, color, scale);
}

void ps3_draw_text_centered(uint32_t *fb, int cx, int y, const char *str, uint32_t color, int scale)
{
    if (!str) return;
    int w = ps3_text_width(str, scale);
    ps3_draw_text_shadow(fb, cx - w / 2, y, str, color, 0xFF050810U, scale);
}

void ps3_draw_text_clipped(uint32_t *fb, int x, int y, int max_w, const char *str, uint32_t color, int scale)
{
    if (!str || !fb || max_w <= 0) return;
    int tw = ps3_text_width(str, scale);
    if (tw <= max_w) {
        ps3_draw_text_shadow(fb, x, y, str, color, 0xFF050810U, scale);
        return;
    }
    char buf[256];
    int len = (int)strlen(str);
    int p = len > 250 ? 250 : len;
    while (p > 0) {
        memcpy(buf, str, p);
        buf[p] = '\0';
        if (p > 3) {
            buf[p - 1] = '.';
            buf[p - 2] = '.';
            buf[p - 3] = '.';
        }
        if (ps3_text_width(buf, scale) <= max_w) {
            ps3_draw_text_shadow(fb, x, y, buf, color, 0xFF050810U, scale);
            return;
        }
        p--;
    }
}

void ps3_draw_char(uint32_t *fb, int x, int y, char c, uint32_t color, int scale)
{
    char buf[2] = { c, '\0' };
    ps3_draw_text(fb, x, y, buf, color, scale);
}

void ps3_draw_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > SCREEN_W ? SCREEN_W : (x + w);
    int y1 = (y + h) > SCREEN_H ? SCREEN_H : (y + h);
    uint32_t c = color | 0xFF000000U;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = fb + py * SCREEN_W;
        for (int px = x0; px < x1; px++) {
            row[px] = c;
        }
    }
}

void ps3_draw_rect_alpha(uint32_t *fb, int x, int y, int w, int h, uint32_t color, int alpha)
{
    if (alpha >= 255) {
        ps3_draw_rect(fb, x, y, w, h, color);
        return;
    }
    if (alpha <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > SCREEN_W ? SCREEN_W : (x + w);
    int y1 = (y + h) > SCREEN_H ? SCREEN_H : (y + h);
    for (int py = y0; py < y1; py++) {
        uint32_t *row = fb + py * SCREEN_W;
        for (int px = x0; px < x1; px++) {
            row[px] = blend_argb(row[px], color, alpha);
        }
    }
}

void ps3_draw_border(uint32_t *fb, int x, int y, int w, int h, int thickness, uint32_t color)
{
    ps3_draw_rect(fb, x, y, w, thickness, color);
    ps3_draw_rect(fb, x, y + h - thickness, w, thickness, color);
    ps3_draw_rect(fb, x, y, thickness, h, color);
    ps3_draw_rect(fb, x + w - thickness, y, thickness, h, color);
}

void ps3_draw_bevel_box(uint32_t *fb, int x, int y, int w, int h, uint32_t face, uint32_t hi, uint32_t sh, uint32_t border, int alpha)
{
    if (w <= 4 || h <= 4) return;
    ps3_draw_rect_alpha(fb, x, y, w, h, border, alpha);
    ps3_draw_rect_alpha(fb, x + 2, y + 2, w - 4, h - 4, face, alpha);
    ps3_draw_rect_alpha(fb, x + 2, y + 2, w - 4, 2, hi, alpha);
    ps3_draw_rect_alpha(fb, x + 2, y + 2, 2, h - 4, hi, alpha);
    ps3_draw_rect_alpha(fb, x + 2, y + h - 4, w - 4, 2, sh, alpha);
    ps3_draw_rect_alpha(fb, x + w - 4, y + 2, 2, h - 4, sh, alpha);
}

void ps3_draw_panel(uint32_t *fb, int x, int y, int w, int h, uint32_t bg_top, uint32_t bg_bot, uint32_t border_col, int alpha)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > SCREEN_W ? SCREEN_W : (x + w);
    int y1 = (y + h) > SCREEN_H ? SCREEN_H : (y + h);
    if (h <= 0 || w <= 0) return;

    uint32_t tr = (bg_top >> 16) & 0xFF;
    uint32_t tg = (bg_top >> 8)  & 0xFF;
    uint32_t tb =  bg_top        & 0xFF;
    uint32_t br = (bg_bot >> 16) & 0xFF;
    uint32_t bg = (bg_bot >> 8)  & 0xFF;
    uint32_t bb =  bg_bot        & 0xFF;

    for (int py = y0; py < y1; py++) {
        int t = ((py - y) * 256) / (h > 1 ? h : 1);
        if (t < 0) t = 0;
        if (t > 256) t = 256;
        uint32_t r = (tr * (256 - t) + br * t) >> 8;
        uint32_t g = (tg * (256 - t) + bg * t) >> 8;
        uint32_t b = (tb * (256 - t) + bb * t) >> 8;
        uint32_t col = 0xFF000000U | (r << 16) | (g << 8) | b;
        uint32_t *row = fb + py * SCREEN_W;
        if (alpha >= 255) {
            for (int px = x0; px < x1; px++) row[px] = col;
        } else {
            for (int px = x0; px < x1; px++) row[px] = blend_argb(row[px], col, alpha);
        }
    }
    ps3_draw_border(fb, x, y, w, h, 2, border_col);
}

void ps3_draw_btn_icon(uint32_t *fb, int x, int y, int btn_id)
{
    if (btn_id < 0 || btn_id >= BTN_ICON_COUNT) return;
    const uint32_t *src = ps3_btn_icons_data[btn_id];
    for (int iy = 0; iy < PS3_BTN_ICON_SIZE; iy++) {
        int py = y + iy;
        if (py < 0 || py >= SCREEN_H) continue;
        uint32_t *dst_row = fb + py * SCREEN_W;
        for (int ix = 0; ix < PS3_BTN_ICON_SIZE; ix++) {
            int px = x + ix;
            if (px < 0 || px >= SCREEN_W) continue;
            uint32_t p = src[iy * PS3_BTN_ICON_SIZE + ix];
            int a = (int)((p >> 24) & 0xFF);
            if (a > 4) {
                dst_row[px] = blend_argb(dst_row[px], p, a);
            }
        }
    }
}

int ps3_draw_btn_label(uint32_t *fb, int x, int y, int btn_id, const char *label, uint32_t text_col, int scale)
{
    ps3_draw_btn_icon(fb, x, y, btn_id);
    int tx = x + PS3_BTN_ICON_SIZE + 8;
    int th = (scale == 2) ? 21 : (scale == 1 ? 17 : 13);
    int ty = y + (PS3_BTN_ICON_SIZE - th) / 2;
    ps3_draw_text_shadow(fb, tx, ty, label, text_col, 0xFF050810U, scale);
    return PS3_BTN_ICON_SIZE + 8 + ps3_text_width(label, scale);
}

void ps3_toast_show(const char *msg, int frames)
{
    if (!msg) return;
    snprintf(s_toast_msg, sizeof(s_toast_msg), "%s", msg);
    s_toast_timer = frames > 0 ? frames : 150;
}

void ps3_toast_draw(uint32_t *fb)
{
    if (s_toast_timer <= 0 || !s_toast_msg[0]) return;
    s_toast_timer--;

    int tw = ps3_text_width(s_toast_msg, 1);
    int bw = tw + 40;
    int bh = 46;
    int bx = SCREEN_W - bw - 28;
    int by = 22;
    if (bx < 20) bx = 20;

    int alpha = s_toast_timer < 25 ? (s_toast_timer * 230 / 25) : 230;
    ps3_draw_panel(fb, bx, by, bw, bh, 0xFF14284CU, 0xFF0A1428U, 0xFF38A8FFU, alpha);
    ps3_draw_rect(fb, bx + 2, by + 2, 6, bh - 4, 0xFF00E5FFU);
    int ty = by + (bh - 22) / 2;
    ps3_draw_text_shadow(fb, bx + 20, ty, s_toast_msg, 0xFFFFFFFFU, 0xFF000000U, 1);
}

void ps3_draw_card_row(uint32_t *fb, int x, int y, int w, int h, int focused, int active)
{
    if (focused) {
        ps3_draw_bevel_box(fb, x, y, w, h, 0xFF263046, 0xFF384A6E, 0xFF1A2234, 0xFFE52521, 240);
        ps3_draw_border(fb, x, y, w, h, 2, 0xFFE52521);
        if (active) {
            ps3_draw_rect(fb, x + 2, y + 2, 4, h - 4, 0xFF00E5FF);
        }
    } else {
        ps3_draw_bevel_box(fb, x, y, w, h, 0xFF1A202C, 0xFF242C3C, 0xFF121620, 0xFF323C55, 210);
        ps3_draw_border(fb, x, y, w, h, 1, 0xFF323C55);
    }
}

void ps3_draw_badge(uint32_t *fb, int x, int y, const char *label, uint32_t bg_col, uint32_t text_col)
{
    if (!label) return;
    int bw = ps3_text_width(label, 0) + 16;
    int bh = 22;
    ps3_draw_bevel_box(fb, x, y, bw, bh, bg_col, bg_col, bg_col, 0xFF263046, 230);
    int ty = y + (bh - 15) / 2;
    ps3_draw_text_shadow(fb, x + 8, ty, label, text_col, 0xFF000000, 0);
}

void ps3_draw_selector_row(uint32_t *fb, int x, int y, int w, int h, const char *title, const char *val, int focused)
{
    ps3_draw_card_row(fb, x, y, w, h, focused, 0);
    int th = 17;
    int ty = y + (h - th) / 2;

    const char *v = val ? val : "NONE";
    char vbuf[128];
    if (focused) snprintf(vbuf, sizeof(vbuf), "<  %s  >", v);
    else snprintf(vbuf, sizeof(vbuf), "%s", v);

    int vw = ps3_text_width(vbuf, 1);
    int max_val_w = (w * 9) / 20;
    if (vw > max_val_w) {
        ps3_draw_text_clipped(fb, x + w - max_val_w - 18, ty, max_val_w, vbuf, focused ? 0xFF00E5FF : 0xFF94A3B8, 1);
        vw = max_val_w;
    } else {
        ps3_draw_text_shadow(fb, x + w - vw - 18, ty, vbuf, focused ? 0xFF00E5FF : 0xFF94A3B8, 0xFF050810, 1);
    }

    int max_title_w = w - vw - 42;
    if (max_title_w > 0) {
        ps3_draw_text_clipped(fb, x + 16, ty, max_title_w, title, focused ? 0xFFFFFFFF : 0xFFCBD5E1, 1);
    }
}

void ps3_draw_switch_row(uint32_t *fb, int x, int y, int w, int h, const char *title, int enabled, int focused)
{
    ps3_draw_card_row(fb, x, y, w, h, focused, 0);
    int th = 17;
    int ty = y + (h - th) / 2;

    int max_title_w = w - 120;
    if (max_title_w > 0) {
        ps3_draw_text_clipped(fb, x + 16, ty, max_title_w, title, focused ? 0xFFFFFFFF : 0xFFCBD5E1, 1);
    }

    int bgh = 22;
    int bgy = y + (h - bgh) / 2;
    if (enabled) {
        ps3_draw_badge(fb, x + w - 100, bgy, "ENABLED", 0xFF10B981, 0xFFFFFFFF);
    } else {
        ps3_draw_badge(fb, x + w - 100, bgy, "DISABLED", 0xFF333D4F, 0xFF94A3B8);
    }
}

void ps3_draw_slider_row(uint32_t *fb, int x, int y, int w, int h, const char *title, int val, int min, int max, const char *suffix, int focused)
{
    ps3_draw_card_row(fb, x, y, w, h, focused, 0);
    int th = 17;
    int ty = y + (h - th) / 2;

    int max_title_w = w - 240;
    if (max_title_w > 0) {
        ps3_draw_text_clipped(fb, x + 16, ty, max_title_w, title, focused ? 0xFFFFFFFF : 0xFFCBD5E1, 1);
    }

    int bar_x = x + w - 215;
    int bar_y = y + (h - 14) / 2;
    int bar_w = 135;
    int bar_h = 14;

    ps3_draw_rect(fb, bar_x, bar_y, bar_w, bar_h, 0xFF141924);
    ps3_draw_border(fb, bar_x, bar_y, bar_w, bar_h, 1, 0xFF323C55);

    int span = max > min ? (max - min) : 1;
    int cur = val - min;
    if (cur < 0) cur = 0;
    if (cur > span) cur = span;
    int fill_w = ((bar_w - 4) * cur) / span;
    if (fill_w > 0) {
        ps3_draw_rect(fb, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, focused ? 0xFF00D4FF : 0xFF0088CC);
    }

    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "%d%s", val, suffix ? suffix : "");
    ps3_draw_text_shadow(fb, x + w - 68, ty, sbuf, focused ? 0xFFFFFFFF : 0xFF94A3B8, 0xFF050810, 1);
}

void ps3_draw_tab_bar(uint32_t *fb, int current_tab)
{
    static const char *tab_titles[8] = {
        "LIBRARY", "SLOTS", "BROWSER", "STATES",
        "MACHINE", "AUDIO/VIDEO", "KEYBOARD", "SYSTEM"
    };
    int tab_w = SCREEN_W / 8;
    int tab_y = 82;
    int tab_h = 36;

    ps3_draw_rect_alpha(fb, 0, tab_y, SCREEN_W, tab_h + 2, 0xFF10141E, 240);
    ps3_draw_rect(fb, 0, tab_y + tab_h + 1, SCREEN_W, 1, 0xFF2A3448);

    for (int i = 0; i < 8; i++) {
        int tx = i * tab_w;
        int th = 17;
        int ty = tab_y + (tab_h - th) / 2;
        if (i == current_tab) {
            ps3_draw_bevel_box(fb, tx + 3, tab_y + 2, tab_w - 6, tab_h - 2, 0xFF263046, 0xFF3E4F70, 0xFF182030, 0xFF4A5D82, 230);
            ps3_draw_rect(fb, tx + 6, tab_y + tab_h - 2, tab_w - 12, 3, 0xFFE52521);
            ps3_draw_text_centered(fb, tx + (tab_w / 2), ty, tab_titles[i], 0xFFFFFFFF, 1);
        } else {
            ps3_draw_rect_alpha(fb, tx + 3, tab_y + 3, tab_w - 6, tab_h - 4, 0xFF161B26, 140);
            ps3_draw_text_centered(fb, tx + (tab_w / 2), ty, tab_titles[i], 0xFF8A99AD, 1);
        }
    }
}
