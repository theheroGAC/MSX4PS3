#ifndef PS3_FRONTEND_H
#define PS3_FRONTEND_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <io/pad.h>
#include "MSX.h"
#include "EMULib.h"

#undef BTN_LEFT
#undef BTN_RIGHT
#undef BTN_UP
#undef BTN_DOWN
#undef BTN_FIREA
#undef BTN_FIREB
#undef BTN_FIREL
#undef BTN_FIRER
#undef BTN_START
#undef BTN_SELECT
#undef BTN_EXIT
#undef BTN_FIREX
#undef BTN_FIREY
#undef BTN_FFWD
#undef BTN_MENU
#undef BTN_ALL
#undef BTN_OK
#undef BTN_FIRE
#undef BTN_ARROWS
#undef BTN_SHIFT
#undef BTN_CONTROL
#undef BTN_ALT
#undef BTN_MODES

#include "Sound.h"
#include "ps3_btn_icons.h"

#define SCREEN_W 1280
#define SCREEN_H 720

#define USRDIR_PATH "/dev_hdd0/game/FMSX00001/USRDIR"
#define BIOS_USRDIR_PATH "/dev_hdd0/game/FMSX00001/USRDIR/bios"
#define CBIOS_USRDIR_PATH "/dev_hdd0/game/FMSX00001/USRDIR/cbios"
#define ROMS_USRDIR_PATH "/dev_hdd0/game/FMSX00001/USRDIR/roms"
#define FMSX_BASE_DIR "/dev_hdd0/fmsx"
#define FMSX_ROMS_DIR "/dev_hdd0/fmsx/roms"
#define FMSX_SAVES_DIR "/dev_hdd0/fmsx/saves"
#define FMSX_STATES_DIR "/dev_hdd0/fmsx/states"
#define FMSX_BIOS_DIR "/dev_hdd0/fmsx/bios"
#define FMSX_CACHE_DIR "/dev_hdd0/fmsx/cache"
#define FMSX_CFG_PATH "/dev_hdd0/fmsx/fmsx_ps3.cfg"

#define PS3_BTN_LEFT      (1U << 0)
#define PS3_BTN_DOWN      (1U << 1)
#define PS3_BTN_RIGHT     (1U << 2)
#define PS3_BTN_UP        (1U << 3)
#define PS3_BTN_START     (1U << 4)
#define PS3_BTN_R3        (1U << 5)
#define PS3_BTN_L3        (1U << 6)
#define PS3_BTN_SELECT    (1U << 7)
#define PS3_BTN_SQUARE    (1U << 8)
#define PS3_BTN_CROSS     (1U << 9)
#define PS3_BTN_CIRCLE    (1U << 10)
#define PS3_BTN_TRIANGLE  (1U << 11)
#define PS3_BTN_R1        (1U << 12)
#define PS3_BTN_L1        (1U << 13)
#define PS3_BTN_R2        (1U << 14)
#define PS3_BTN_L2        (1U << 15)

#define MENU_NONE         0
#define MENU_MAIN         1
#define MENU_QUICK        2
#define MENU_BROWSER      3
#define MENU_STATES       4
#define MENU_MACHINE      5
#define MENU_SETTINGS     6
#define MENU_SLOTS        7
#define MENU_HELP         8
#define MENU_LIBRARY      9

#define TAB_LIBRARY       0
#define TAB_SLOTS         1
#define TAB_FILES         2
#define TAB_STATES        3
#define TAB_MACHINE       4
#define TAB_DISPLAY       5
#define TAB_VKBD          6
#define TAB_CREDITS       7
#define TAB_COUNT         8

#define ASPECT_4_3        0
#define ASPECT_16_9       1
#define ASPECT_PIXEL      2

#define SCANLINES_OFF     0
#define SCANLINES_LIGHT   1
#define SCANLINES_CRT     2

#define PADMODE_HYBRID    0
#define PADMODE_JOYONLY   1
#define PADMODE_MOUSE     2

typedef struct {
    int msx_model;
    int video_region;
    int ram_pages;
    int vram_pages;
    int fmpac_enabled;
    int aspect_ratio;
    int scanlines;
    int palette;
    int blur;
    int sound_volume;
    int audio_volume;
    int pad_mode;
    int autofire_a;
    int autofire_b;
    int vkbd_style;
    int vkbd_transparency;
    int vkbd_position;
    int state_slot;
    int bios_mode;
    int unlimited_sprites;
    int fast_disk;
    int cpu_turbo;
    int vol_psg;
    int vol_scc;
    int vol_fmpac;
    char last_dir[512];
    char carta_path[512];
    char cartb_path[512];
    char dska_path[512];
    char dskb_path[512];
    char cas_path[512];
} PS3Config;

extern PS3Config g_cfg;
extern uint32_t g_framebuf[SCREEN_W * SCREEN_H];
extern volatile int g_app_running;
extern int g_menu_mode;
extern int g_vkbd_mode;
extern int g_pad1_Rx;
extern int g_pad1_Ry;
extern int g_pad1_Lx;
extern int g_pad1_Ly;

void ps3_video_present(void);
void ps3_render_msx_to_framebuf(void);
void ps3_present_framebuf(void);

uint32_t ps3_input_poll(int port);
void ps3_input_update_msx(void);

void ps3_config_defaults(void);
void ps3_config_load(void);
void ps3_config_save(void);
void ps3_apply_machine_config(int reset_now);

void ps3_toast_show(const char *msg, int frames);
void ps3_toast_draw(uint32_t *fb);

void ps3_font_init(void);
void ps3_font_shutdown(void);
void ps3_trigger_hardware_reset(void);

void ps3_draw_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t color);
void ps3_draw_rect_alpha(uint32_t *fb, int x, int y, int w, int h, uint32_t color, int alpha);
void ps3_draw_border(uint32_t *fb, int x, int y, int w, int h, int thickness, uint32_t color);
void ps3_draw_bevel_box(uint32_t *fb, int x, int y, int w, int h, uint32_t face, uint32_t hi, uint32_t sh, uint32_t border, int alpha);
void ps3_draw_panel(uint32_t *fb, int x, int y, int w, int h, uint32_t bg_top, uint32_t bg_bot, uint32_t border_col, int alpha);
void ps3_draw_char(uint32_t *fb, int x, int y, char c, uint32_t color, int scale);
void ps3_draw_text(uint32_t *fb, int x, int y, const char *str, uint32_t color, int scale);
void ps3_draw_text_shadow(uint32_t *fb, int x, int y, const char *str, uint32_t color, uint32_t shadow, int scale);
void ps3_draw_text_centered(uint32_t *fb, int cx, int y, const char *str, uint32_t color, int scale);
void ps3_draw_text_clipped(uint32_t *fb, int x, int y, int max_w, const char *str, uint32_t color, int scale);
int  ps3_text_width(const char *str, int scale);
void ps3_draw_btn_icon(uint32_t *fb, int x, int y, int btn_id);
int  ps3_draw_btn_label(uint32_t *fb, int x, int y, int btn_id, const char *label, uint32_t text_col, int scale);
void ps3_draw_card_row(uint32_t *fb, int x, int y, int w, int h, int focused, int active);
void ps3_draw_selector_row(uint32_t *fb, int x, int y, int w, int h, const char *title, const char *val, int focused);
void ps3_draw_switch_row(uint32_t *fb, int x, int y, int w, int h, const char *title, int enabled, int focused);
void ps3_draw_slider_row(uint32_t *fb, int x, int y, int w, int h, const char *title, int val, int min, int max, const char *suffix, int focused);
void ps3_draw_badge(uint32_t *fb, int x, int y, const char *label, uint32_t bg_col, uint32_t text_col);
void ps3_draw_tab_bar(uint32_t *fb, int current_tab);

void ps3_vkbd_init(void);
void ps3_vkbd_reset_sticky(void);
void ps3_vkbd_update(uint32_t buttons, uint32_t pressed);
void ps3_vkbd_draw(uint32_t *fb);
void ps3_vkbd_apply_to_msx(void);
void ps3_vkbd_queue_macro(const char *str);
int  ps3_vkbd_macro_active(void);

void ps3_menu_init(void);
void ps3_menu_open(int mode);
void ps3_menu_run_loop(void);
int  ps3_state_save_slot(int slot);
int  ps3_state_load_slot(int slot);
int  ps3_state_slot_exists(int slot);
void ps3_get_active_game_name(char *out, size_t out_len);

typedef struct {
    const char *filename;
    const char *desc;
    const char *machine;
    int model_mask;
    int expected_size;
    int found;
    char found_path[256];
    long actual_size;
    int is_cbios;
} Ps3BiosItem;

int  ps3_bios_check_all(Ps3BiosItem *items, int max_items);
int  ps3_bios_is_model_ready(int model);
int  ps3_bios_has_oem_for_model(int model);
int  ps3_bios_copy_from_usb(char *out_msg, size_t max_len);
int  ps3_bios_show_dialog(int allow_cancel);
int  ps3_create_blank_dsk(const char *path);
void ps3_audio_update_mixer(void);

#endif
