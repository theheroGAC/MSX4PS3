#include "ps3_frontend.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <SDL2/SDL.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>
#include <sys/process.h>
#include <sys/thread.h>
#include <sys/event_queue.h>
#include <audio/audio.h>
#include <rsx/gcm_sys.h>

SYS_PROCESS_PARAM(1001, 0x200000);

#define WIDTH 272
#define HEIGHT 228

#define RGB2PIXEL(R,G,B) (0xFF000000U | (((unsigned int)(R) & 0xFFU) << 16) | (((unsigned int)(G) & 0xFFU) << 8) | ((unsigned int)(B) & 0xFFU))

#define AUDIO_RING_SAMPLES 16384
#define AUDIO_RING_MASK    (AUDIO_RING_SAMPLES - 1)

PS3Config g_cfg;
uint32_t g_framebuf[SCREEN_W * SCREEN_H];
volatile int g_app_running = 1;
int g_menu_mode = MENU_NONE;
int g_vkbd_mode = 0;
int g_pad1_Rx = 0;
int g_pad1_Ry = 0;
int g_pad1_Lx = 0;
int g_pad1_Ly = 0;
extern int HPeriod, VPeriod;

static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;
static int           s_out_w    = SCREEN_W;
static int           s_out_h    = SCREEN_H;

static unsigned int BPal[256], XPal[80], XPal0;
static Image NormScreen;
static Image WideScreen;
static pixel *XBuf = NULL;
static pixel *WBuf = NULL;

static float             s_audio_ring[AUDIO_RING_SAMPLES];
static volatile uint32_t s_audio_wpos = 0;
static volatile uint32_t s_audio_rpos = 0;
static volatile int      s_audio_running = 0;
static u32               s_audio_port = 0;
static audioPortConfig   s_audio_cfg;
static sys_event_queue_t s_audio_queue = 0;
static sys_ipc_key_t     s_audio_key = 0;
static sys_ppu_thread_t  s_audio_tid  = 0;
static unsigned int      s_emu_rate   = 48000;

static volatile int s_xmb_menu_open = 0;
static uint32_t s_prev_pad = 0;
static word     s_cur_joystate = 0;
static int      s_mouse_x = 128;
static int      s_mouse_y = 96;
static int      s_boot_frame_cnt = 0;
static int      s_autofire_tick = 0;

void ps3_sdl_render_cp(const char *tag)
{
    (void)tag;
}

void ps3_sdl_flip_timeout(unsigned int ms)
{
    (void)ms;
}

void ps3_sdl_fifo_blocked(unsigned int ms)
{
    (void)ms;
}

static void sysutil_cb(u64 status, u64 param, void *userdata)
{
    (void)param;
    (void)userdata;
    switch (status) {
        case SYSUTIL_EXIT_GAME:
            g_app_running = 0;
            g_menu_mode = MENU_NONE;
            g_vkbd_mode = 0;
            ExitNow = 1;
            break;
        case SYSUTIL_MENU_OPEN:
        case SYSUTIL_DRAW_BEGIN:
            s_xmb_menu_open = 1;
            break;
        case SYSUTIL_MENU_CLOSE:
        case SYSUTIL_DRAW_END:
            s_xmb_menu_open = 0;
            break;
        default:
            break;
    }
}

static void audio_thread_entry(void *arg)
{
    (void)arg;
    sys_event_t event;
    float *base = (float *)(uintptr_t)s_audio_cfg.audioDataStart;
    u32 nblocks = (u32)s_audio_cfg.numBlocks;
    u32 block = 0;

    if (!base || nblocks == 0) {
        sysThreadExit(0);
        return;
    }

    while (s_audio_running && g_app_running) {
        if (sysEventQueueReceive(s_audio_queue, &event, 20000) != 0) continue;

        float *dst = base + (block * AUDIO_BLOCK_SAMPLES * 2);
        block = (block + 1) % nblocks;

        float vol = (float)g_cfg.audio_volume / 100.0f;
        if (g_menu_mode != MENU_NONE || s_xmb_menu_open) vol = 0.0f;

        uint32_t rp = s_audio_rpos;
        uint32_t wp = s_audio_wpos;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            float sample_val = 0.0f;
            if (rp != wp) {
                sample_val = s_audio_ring[rp & AUDIO_RING_MASK] * vol;
                rp++;
            }
            dst[i * 2 + 0] = sample_val;
            dst[i * 2 + 1] = sample_val;
        }
        s_audio_rpos = rp;
    }

    sysThreadExit(0);
}

static void ps3_audio_start(void)
{
    if (s_audio_running) return;
    if (audioInit() != 0) return;

    audioPortParam aparam;
    memset(&aparam, 0, sizeof(aparam));
    aparam.numChannels = AUDIO_PORT_2CH;
    aparam.numBlocks   = 8;
    aparam.attrib      = AUDIO_PORT_INITLEVEL;
    aparam.level       = 1.0f;

    if (audioPortOpen(&aparam, &s_audio_port) != 0) {
        audioQuit();
        return;
    }
    if (audioGetPortConfig(s_audio_port, &s_audio_cfg) != 0) {
        audioPortClose(s_audio_port);
        audioQuit();
        return;
    }
    if (audioCreateNotifyEventQueue(&s_audio_queue, &s_audio_key) != 0) {
        audioPortClose(s_audio_port);
        audioQuit();
        return;
    }
    if (audioSetNotifyEventQueue(s_audio_key) != 0) {
        audioRemoveNotifyEventQueue(s_audio_key);
        audioPortClose(s_audio_port);
        sysEventQueueDestroy(s_audio_queue, 0);
        audioQuit();
        return;
    }

    sysEventQueueDrain(s_audio_queue);
    s_audio_wpos = 0;
    s_audio_rpos = 0;
    memset(s_audio_ring, 0, sizeof(s_audio_ring));
    s_audio_running = 1;

    if (sysThreadCreate(&s_audio_tid, audio_thread_entry, NULL, 1000, 0x10000, THREAD_JOINABLE, "fmsx_snd") != 0) {
        s_audio_running = 0;
        audioRemoveNotifyEventQueue(s_audio_key);
        audioPortClose(s_audio_port);
        sysEventQueueDestroy(s_audio_queue, 0);
        audioQuit();
        return;
    }

    audioPortStart(s_audio_port);
}

static void ps3_audio_stop(void)
{
    if (!s_audio_running) return;
    s_audio_running = 0;
    u64 retval = 0;
    sysThreadJoin(s_audio_tid, &retval);
    audioPortStop(s_audio_port);
    audioRemoveNotifyEventQueue(s_audio_key);
    audioPortClose(s_audio_port);
    sysEventQueueDestroy(s_audio_queue, 0);
    audioQuit();
}

unsigned int InitAudio(unsigned int Rate, unsigned int Latency)
{
    (void)Latency;
    if (!Rate) return 0;
    s_emu_rate = Rate;
    return s_emu_rate;
}

void TrashAudio(void)
{
}

unsigned int GetFreeAudio(void)
{
    uint32_t used = s_audio_wpos - s_audio_rpos;
    if (used >= AUDIO_RING_SAMPLES - 512) return 0;
    return (AUDIO_RING_SAMPLES - 512) - used;
}

unsigned int WriteAudio(sample *Data, unsigned int Length)
{
    if (!Data || !Length) return 0;
    uint32_t wp = s_audio_wpos;
    uint32_t rp = s_audio_rpos;
    unsigned int written = 0;

    for (unsigned int i = 0; i < Length; i++) {
        if ((wp - rp) >= (AUDIO_RING_SAMPLES - 16)) break;
        int16_t pcm = (int16_t)((uint16_t)Data[i] ^ 0x8000U);
        float f = (float)pcm * (1.0f / 32768.0f);
        s_audio_ring[wp & AUDIO_RING_MASK] = f;
        wp++;
        written++;
    }
    s_audio_wpos = wp;
    return written;
}

int UseSound = 48000;

void PlayAllSound(int uSec)
{
    if (UseSound > 0) {
        RenderAndPlayAudio((unsigned int)(2 * uSec * UseSound / 1000000));
    }
}

unsigned int GetJoystick(void)
{
    return ps3_input_poll(0);
}

static const char *get_cbios_fallback(const char *name)
{
    if (!name) return NULL;
    if (strcasecmp(name, "MSX.ROM") == 0) return "cbios_main_msx1.rom";
    if (strcasecmp(name, "MSX2.ROM") == 0) return "cbios_main_msx2.rom";
    if (strcasecmp(name, "MSX2EXT.ROM") == 0) return "cbios_sub.rom";
    if (strcasecmp(name, "MSX2P.ROM") == 0) return "cbios_main_msx2+.rom";
    if (strcasecmp(name, "MSX2PEXT.ROM") == 0) return "cbios_sub.rom";
    if (strcasecmp(name, "DISK.ROM") == 0) return "cbios_disk.rom";
    if (strcasecmp(name, "FMPAC.ROM") == 0) return "cbios_music.rom";
    return NULL;
}

static const char * const s_bios_search_dirs[] = {
    FMSX_BIOS_DIR,
    "/dev_usb000/bios", "/dev_usb000",
    "/dev_usb001/bios", "/dev_usb001",
    "/dev_usb002/bios", "/dev_usb002",
    "/dev_usb003/bios", "/dev_usb003",
    "/dev_usb004/bios", "/dev_usb004",
    "/dev_usb005/bios", "/dev_usb005",
    "/dev_usb006/bios", "/dev_usb006",
    "/dev_usb007/bios", "/dev_usb007",
    BIOS_USRDIR_PATH,
    CBIOS_USRDIR_PATH,
    USRDIR_PATH,
    FMSX_BASE_DIR
};
static const int s_num_bios_dirs = (int)(sizeof(s_bios_search_dirs) / sizeof(s_bios_search_dirs[0]));

static FILE *try_find_and_open_bios(const char *name, const char *mode)
{
    if (!name || !*name || !mode) return NULL;
    for (int i = 0; i < s_num_bios_dirs; i++) {
        if (!s_bios_search_dirs[i]) continue;
        char cand[512];
        snprintf(cand, sizeof(cand), "%s/%s", s_bios_search_dirs[i], name);
        FILE *f = fopen(cand, mode);
        if (f) return f;
        char lcase[64];
        snprintf(lcase, sizeof(lcase), "%s", name);
        for (int c = 0; lcase[c]; c++) {
            if (lcase[c] >= 'A' && lcase[c] <= 'Z') lcase[c] += 32;
        }
        snprintf(cand, sizeof(cand), "%s/%s", s_bios_search_dirs[i], lcase);
        f = fopen(cand, mode);
        if (f) return f;
    }
    return NULL;
}

FILE *ps3_msx_fopen(const char *path, const char *mode)
{
    if (!path || !*path || !mode) return NULL;

    if (strcasecmp(path, "CARTA.ROM") == 0 ||
        strcasecmp(path, "CARTB.ROM") == 0 ||
        strcasecmp(path, "DRIVEA.DSK") == 0 ||
        strcasecmp(path, "DRIVEB.DSK") == 0 ||
        strcasecmp(path, "DEFAULT.CAS") == 0 ||
        strcasecmp(path, "DEFAULT.STA") == 0 ||
        strcasecmp(path, "LOG.MID") == 0 ||
        strcasecmp(path, "PAINTER.ROM") == 0) {
        return NULL;
    }

    if (!g_cfg.fmpac_enabled && strcasecmp(path, "FMPAC.ROM") == 0) {
        return NULL;
    }

    if (strcasecmp(path, "CMOS.ROM") == 0) {
        char cmos_save[512];
        snprintf(cmos_save, sizeof(cmos_save), "%s/CMOS.ROM", FMSX_SAVES_DIR);
        FILE *f = fopen(cmos_save, mode);
        if (f) return f;
        if (strchr(mode, 'r')) {
            snprintf(cmos_save, sizeof(cmos_save), "%s/CMOS.ROM", BIOS_USRDIR_PATH);
            return fopen(cmos_save, mode);
        }
        return NULL;
    }

    size_t len = strlen(path);
    if (len > 4 && strcasecmp(path + len - 4, ".sav") == 0) {
        const char *base = strrchr(path, '/');
        base = base ? (base + 1) : path;
        char sav_path[512];
        snprintf(sav_path, sizeof(sav_path), "%s/%s", FMSX_SAVES_DIR, base);
        return fopen(sav_path, mode);
    }

    if (len > 4 && strcasecmp(path + len - 4, ".sta") == 0 && strstr(path, FMSX_STATES_DIR) == NULL) {
        if (strchr(mode, 'r') && strstr(path, "_slot") == NULL) {
            return NULL;
        }
    }

    if (path[0] == '/') {
        return fopen(path, mode);
    }

    FILE *bf = try_find_and_open_bios(path, mode);
    if (bf) {
        if (strchr(mode, 'r')) {
            fseek(bf, 0, SEEK_END);
            long sz = ftell(bf);
            if (sz == 32768 + 16 || sz == 16384 + 16) {
                fseek(bf, 16, SEEK_SET);
            } else if (sz == 32768 + 512 || sz == 16384 + 512) {
                fseek(bf, 512, SEEK_SET);
            } else {
                fseek(bf, 0, SEEK_SET);
            }
        }
        return bf;
    }

    if (strcasecmp(path, "MSX2EXT.ROM") == 0) {
        FILE *f_main = try_find_and_open_bios("MSX2.ROM", "rb");
        if (f_main) {
            fseek(f_main, 0, SEEK_END);
            long sz = ftell(f_main);
            if (sz >= 49152) {
                fseek(f_main, 0x8000, SEEK_SET);
                return f_main;
            }
            fclose(f_main);
        }
    }

    if (strcasecmp(path, "MSX2PEXT.ROM") == 0) {
        FILE *f_main = try_find_and_open_bios("MSX2P.ROM", "rb");
        if (f_main) {
            fseek(f_main, 0, SEEK_END);
            long sz = ftell(f_main);
            if (sz >= 49152) {
                fseek(f_main, 0x8000, SEEK_SET);
                return f_main;
            }
            fclose(f_main);
        }
    }

    const char *cbios_name = get_cbios_fallback(path);
    if (cbios_name) {
        FILE *cbf = try_find_and_open_bios(cbios_name, mode);
        if (cbf) return cbf;
    }

    return fopen(path, mode);
}

static gzFile try_find_and_open_bios_gz(const char *name, const char *mode)
{
    if (!name || !*name || !mode) return NULL;
    for (int i = 0; i < s_num_bios_dirs; i++) {
        if (!s_bios_search_dirs[i]) continue;
        char cand[512];
        snprintf(cand, sizeof(cand), "%s/%s", s_bios_search_dirs[i], name);
        gzFile f = gzopen(cand, mode);
        if (f) return f;
        char lcase[64];
        snprintf(lcase, sizeof(lcase), "%s", name);
        for (int c = 0; lcase[c]; c++) {
            if (lcase[c] >= 'A' && lcase[c] <= 'Z') lcase[c] += 32;
        }
        snprintf(cand, sizeof(cand), "%s/%s", s_bios_search_dirs[i], lcase);
        f = gzopen(cand, mode);
        if (f) return f;
    }
    return NULL;
}

gzFile ps3_msx_gzopen(const char *path, const char *mode)
{
    if (!path || !*path || !mode) return NULL;

    if (strcasecmp(path, "CARTA.ROM") == 0 ||
        strcasecmp(path, "CARTB.ROM") == 0 ||
        strcasecmp(path, "DRIVEA.DSK") == 0 ||
        strcasecmp(path, "DRIVEB.DSK") == 0 ||
        strcasecmp(path, "DEFAULT.CAS") == 0 ||
        strcasecmp(path, "DEFAULT.STA") == 0 ||
        strcasecmp(path, "LOG.MID") == 0 ||
        strcasecmp(path, "PAINTER.ROM") == 0) {
        return NULL;
    }

    if (!g_cfg.fmpac_enabled && strcasecmp(path, "FMPAC.ROM") == 0) {
        return NULL;
    }

    if (path[0] == '/') {
        return gzopen(path, mode);
    }

    gzFile bf = try_find_and_open_bios_gz(path, mode);
    if (bf) {
        if (strchr(mode, 'r')) {
            gzseek(bf, 0, SEEK_END);
            z_off_t sz = gztell(bf);
            if (sz == 32768 + 16 || sz == 16384 + 16) {
                gzseek(bf, 16, SEEK_SET);
            } else if (sz == 32768 + 512 || sz == 16384 + 512) {
                gzseek(bf, 512, SEEK_SET);
            } else {
                gzseek(bf, 0, SEEK_SET);
            }
        }
        return bf;
    }

    if (strcasecmp(path, "MSX2EXT.ROM") == 0) {
        gzFile f_main = try_find_and_open_bios_gz("MSX2.ROM", "rb");
        if (f_main) {
            gzseek(f_main, 0, SEEK_END);
            z_off_t sz = gztell(f_main);
            if (sz >= 49152) {
                gzseek(f_main, 0x8000, SEEK_SET);
                return f_main;
            }
            gzclose(f_main);
        }
    }

    if (strcasecmp(path, "MSX2PEXT.ROM") == 0) {
        gzFile f_main = try_find_and_open_bios_gz("MSX2P.ROM", "rb");
        if (f_main) {
            gzseek(f_main, 0, SEEK_END);
            z_off_t sz = gztell(f_main);
            if (sz >= 49152) {
                gzseek(f_main, 0x8000, SEEK_SET);
                return f_main;
            }
            gzclose(f_main);
        }
    }

    const char *cbios_name = get_cbios_fallback(path);
    if (cbios_name) {
        gzFile cbf = try_find_and_open_bios_gz(cbios_name, mode);
        if (cbf) return cbf;
    }

    return gzopen(path, mode);
}

void ps3_audio_update_mixer(void)
{
    SoundMixerPSG   = g_cfg.vol_psg;
    SoundMixerSCC   = g_cfg.vol_scc;
    SoundMixerFMPAC = g_cfg.vol_fmpac;
}

int ps3_create_blank_dsk(const char *path)
{
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    static uint8_t sector[512];
    memset(sector, 0, sizeof(sector));

    sector[0x00] = 0xEB;
    sector[0x01] = 0xFE;
    sector[0x02] = 0x90;
    memcpy(sector + 3, "MSX_2.0 ", 8);
    sector[0x0B] = 0x00;
    sector[0x0C] = 0x02;
    sector[0x0D] = 0x02;
    sector[0x0E] = 0x01;
    sector[0x0F] = 0x00;
    sector[0x10] = 0x02;
    sector[0x11] = 0x70;
    sector[0x12] = 0x00;
    sector[0x13] = 0xA0;
    sector[0x14] = 0x05;
    sector[0x15] = 0xF9;
    sector[0x16] = 0x03;
    sector[0x17] = 0x00;
    sector[0x18] = 0x09;
    sector[0x19] = 0x00;
    sector[0x1A] = 0x02;
    sector[0x1B] = 0x00;
    sector[0x1FE] = 0x55;
    sector[0x1FF] = 0xAA;

    if (fwrite(sector, 1, 512, f) != 512) {
        fclose(f);
        return 0;
    }

    memset(sector, 0, sizeof(sector));
    sector[0] = 0xF9;
    sector[1] = 0xFF;
    sector[2] = 0xFF;
    fwrite(sector, 1, 512, f);
    memset(sector, 0, 3);
    fwrite(sector, 1, 512, f);
    fwrite(sector, 1, 512, f);

    sector[0] = 0xF9;
    sector[1] = 0xFF;
    sector[2] = 0xFF;
    fwrite(sector, 1, 512, f);
    memset(sector, 0, 3);
    fwrite(sector, 1, 512, f);
    fwrite(sector, 1, 512, f);

    for (int i = 0; i < 1433; i++) {
        fwrite(sector, 1, 512, f);
    }

    fclose(f);
    return 1;
}

void ps3_config_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.msx_model = 2;
    g_cfg.video_region = 0;
    g_cfg.ram_pages = 8;
    g_cfg.vram_pages = 8;
    g_cfg.fmpac_enabled = 1;
    g_cfg.aspect_ratio = ASPECT_4_3;
    g_cfg.scanlines = SCANLINES_OFF;
    g_cfg.audio_volume = 90;
    g_cfg.sound_volume = 90;
    g_cfg.palette = 0;
    g_cfg.blur = 1;
    g_cfg.pad_mode = PADMODE_HYBRID;
    g_cfg.autofire_a = 0;
    g_cfg.autofire_b = 0;
    g_cfg.vkbd_style = 0;
    g_cfg.vkbd_transparency = 1;
    g_cfg.vkbd_position = 0;
    g_cfg.state_slot = 0;
    g_cfg.unlimited_sprites = 1;
    g_cfg.fast_disk = 1;
    g_cfg.cpu_turbo = 0;
    g_cfg.vol_psg = 100;
    g_cfg.vol_scc = 100;
    g_cfg.vol_fmpac = 100;
    ps3_audio_update_mixer();
    snprintf(g_cfg.last_dir, sizeof(g_cfg.last_dir), "%s", ROMS_USRDIR_PATH);
}

void ps3_config_load(void)
{
    ps3_config_defaults();
    FILE *f = fopen(FMSX_CFG_PATH, "rb");
    if (!f) return;
    PS3Config tmp;
    if (fread(&tmp, 1, sizeof(PS3Config), f) == sizeof(PS3Config)) {
        if (tmp.msx_model >= 0 && tmp.msx_model <= 2 &&
            tmp.audio_volume >= 0 && tmp.audio_volume <= 100) {
            g_cfg = tmp;
            if (g_cfg.vol_psg < 0 || g_cfg.vol_psg > 100) g_cfg.vol_psg = 100;
            if (g_cfg.vol_scc < 0 || g_cfg.vol_scc > 100) g_cfg.vol_scc = 100;
            if (g_cfg.vol_fmpac < 0 || g_cfg.vol_fmpac > 100) g_cfg.vol_fmpac = 100;
            ps3_audio_update_mixer();
        }
    }
    fclose(f);
}

void ps3_config_save(void)
{
    FILE *f = fopen(FMSX_CFG_PATH, "wb");
    if (!f) return;
    fwrite(&g_cfg, 1, sizeof(PS3Config), f);
    fclose(f);
}

void ps3_apply_machine_config(int reset_now)
{
    int m = MSX_GUESSA | MSX_GUESSB | MSX_MSXDOS2 | MSX_PATCHBDOS | MSX_DRUMS;
    if (g_cfg.unlimited_sprites) m |= MSX_ALLSPRITE;
    if (g_cfg.msx_model == 0)      m |= MSX_MSX1;
    else if (g_cfg.msx_model == 1) m |= MSX_MSX2;
    else                           m |= MSX_MSX2P;

    if (g_cfg.video_region == 0) m |= MSX_NTSC;
    else                         m |= MSX_PAL;

    if (g_cfg.pad_mode == PADMODE_MOUSE) {
        m |= MSX_MOUSE1 | MSX_JOY2;
    } else {
        m |= MSX_JOY1 | MSX_JOY2;
    }

    RAMPages  = g_cfg.ram_pages;
    VRAMPages = g_cfg.vram_pages;

    if (reset_now) {
        ResetMSX(m, RAMPages, VRAMPages);
    } else {
        Mode = m;
    }

    if (g_cfg.cpu_turbo) {
        HPeriod = (HPERIOD / 6) * 2;
        VPeriod = ((g_cfg.video_region == 1 ? VPERIOD_PAL : VPERIOD_NTSC) / 6) * 2;
    } else {
        HPeriod = HPERIOD / 6;
        VPeriod = (g_cfg.video_region == 1 ? VPERIOD_PAL : VPERIOD_NTSC) / 6;
    }
}

void ps3_trigger_hardware_reset(void)
{
    ps3_apply_machine_config(1);
}

static int s_dpad_hold_up[7]    = {0};
static int s_dpad_hold_down[7]  = {0};
static int s_dpad_hold_left[7]  = {0};
static int s_dpad_hold_right[7] = {0};

uint32_t ps3_input_poll(int port)
{
    sysUtilCheckCallback();

    padInfo padinfo;
    padData paddata;
    uint32_t mask = 0;

    if (ioPadGetInfo(&padinfo) != 0) return 0;
    if (port < 0 || port >= 7 || !padinfo.status[port]) return 0;

    if (ioPadGetData(port, &paddata) == 0 && paddata.len >= 8) {
        uint16_t dig1 = paddata.button[PAD_BUTTON_OFFSET_DIGITAL1];
        uint16_t dig2 = paddata.button[PAD_BUTTON_OFFSET_DIGITAL2];

        int raw_left  = paddata.BTN_LEFT  || (dig1 & PAD_CTRL_LEFT)  || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_LEFT)  && paddata.button[PAD_BUTTON_OFFSET_PRESS_LEFT]  > 15);
        int raw_down  = paddata.BTN_DOWN  || (dig1 & PAD_CTRL_DOWN)  || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_DOWN)  && paddata.button[PAD_BUTTON_OFFSET_PRESS_DOWN]  > 15);
        int raw_right = paddata.BTN_RIGHT || (dig1 & PAD_CTRL_RIGHT) || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_RIGHT) && paddata.button[PAD_BUTTON_OFFSET_PRESS_RIGHT] > 15);
        int raw_up    = paddata.BTN_UP    || (dig1 & PAD_CTRL_UP)    || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_UP)    && paddata.button[PAD_BUTTON_OFFSET_PRESS_UP]    > 15);

        if (raw_left)  s_dpad_hold_left[port]  = 2;
        else if (s_dpad_hold_left[port] > 0)  s_dpad_hold_left[port]--;

        if (raw_down)  s_dpad_hold_down[port]  = 2;
        else if (s_dpad_hold_down[port] > 0)  s_dpad_hold_down[port]--;

        if (raw_right) s_dpad_hold_right[port] = 2;
        else if (s_dpad_hold_right[port] > 0) s_dpad_hold_right[port]--;

        if (raw_up)    s_dpad_hold_up[port]    = 2;
        else if (s_dpad_hold_up[port] > 0)    s_dpad_hold_up[port]--;

        if (s_dpad_hold_left[port] > 0)  mask |= PS3_BTN_LEFT;
        if (s_dpad_hold_down[port] > 0)  mask |= PS3_BTN_DOWN;
        if (s_dpad_hold_right[port] > 0) mask |= PS3_BTN_RIGHT;
        if (s_dpad_hold_up[port] > 0)    mask |= PS3_BTN_UP;

        if (paddata.BTN_START    || (dig1 & PAD_CTRL_START))    mask |= PS3_BTN_START;
        if (paddata.BTN_R3       || (dig1 & PAD_CTRL_R3))       mask |= PS3_BTN_R3;
        if (paddata.BTN_L3       || (dig1 & PAD_CTRL_L3))       mask |= PS3_BTN_L3;
        if (paddata.BTN_SELECT   || (dig1 & PAD_CTRL_SELECT))   mask |= PS3_BTN_SELECT;

        if (paddata.BTN_SQUARE   || (dig2 & PAD_CTRL_SQUARE)   || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_SQUARE)   && paddata.button[PAD_BUTTON_OFFSET_PRESS_SQUARE]   > 15)) mask |= PS3_BTN_SQUARE;
        if (paddata.BTN_CROSS    || (dig2 & PAD_CTRL_CROSS)    || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_CROSS)    && paddata.button[PAD_BUTTON_OFFSET_PRESS_CROSS]    > 15)) mask |= PS3_BTN_CROSS;
        if (paddata.BTN_CIRCLE   || (dig2 & PAD_CTRL_CIRCLE)   || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_CIRCLE)   && paddata.button[PAD_BUTTON_OFFSET_PRESS_CIRCLE]   > 15)) mask |= PS3_BTN_CIRCLE;
        if (paddata.BTN_TRIANGLE || (dig2 & PAD_CTRL_TRIANGLE) || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_TRIANGLE) && paddata.button[PAD_BUTTON_OFFSET_PRESS_TRIANGLE] > 15)) mask |= PS3_BTN_TRIANGLE;

        if (paddata.BTN_R1       || (dig2 & PAD_CTRL_R1))       mask |= PS3_BTN_R1;
        if (paddata.BTN_L1       || (dig2 & PAD_CTRL_L1))       mask |= PS3_BTN_L1;
        if (paddata.BTN_R2       || (dig2 & PAD_CTRL_R2)       || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_R2) && paddata.button[PAD_BUTTON_OFFSET_PRESS_R2] > 25)) mask |= PS3_BTN_R2;
        if (paddata.BTN_L2       || (dig2 & PAD_CTRL_L2)       || ((paddata.len > PAD_BUTTON_OFFSET_PRESS_L2) && paddata.button[PAD_BUTTON_OFFSET_PRESS_L2] > 25)) mask |= PS3_BTN_L2;

        if (port == 0) {
            g_pad1_Lx = (int)paddata.ANA_L_H - 128;
            g_pad1_Ly = (int)paddata.ANA_L_V - 128;
            g_pad1_Rx = (int)paddata.ANA_R_H - 128;
            g_pad1_Ry = (int)paddata.ANA_R_V - 128;
        }
    }
    return mask;
}

void ps3_present_framebuf(void)
{
    sysUtilCheckCallback();
    if (!g_app_running || !s_renderer || !s_texture) return;

    SDL_Rect dr = { 0, 0, s_out_w, s_out_h };
    if (s_xmb_menu_open) {
        SDL_RenderCopy(s_renderer, s_texture, NULL, &dr);
        SDL_RenderPresent(s_renderer);
        usleep(16000);
        return;
    }

    SDL_UpdateTexture(s_texture, NULL, g_framebuf, SCREEN_W * sizeof(uint32_t));
    SDL_RenderCopy(s_renderer, s_texture, NULL, &dr);
    SDL_RenderPresent(s_renderer);
}

static void draw_msx_logo_graphic(uint32_t *fb, int x, int y)
{
    uint32_t cyan = 0xFF00E5FFU;
    uint32_t white = 0xFFFFFFFFU;
    uint32_t shadow = 0xFF051832U;

    for (int pass = 0; pass < 2; pass++) {
        int ox = (pass == 0) ? 3 : 0;
        int oy = (pass == 0) ? 3 : 0;
        uint32_t col = (pass == 0) ? shadow : cyan;

        int mx = x + ox;
        int my = y + oy;
        ps3_draw_rect(fb, mx, my, 18, 54, col);
        ps3_draw_rect(fb, mx + 68, my, 18, 54, col);
        for (int d = 0; d < 36; d++) {
            int cx1 = mx + 18 + (d * 24) / 36;
            int cx2 = mx + 68 - (d * 24) / 36 - 12;
            int cy_row = my + (d * 42) / 36;
            ps3_draw_rect(fb, cx1, cy_row, 12, 4, col);
            ps3_draw_rect(fb, cx2, cy_row, 12, 4, col);
        }

        int sx = x + 104 + ox;
        int sy = y + oy;
        ps3_draw_rect(fb, sx, sy, 76, 14, col);
        ps3_draw_rect(fb, sx, sy + 14, 18, 14, col);
        ps3_draw_rect(fb, sx, sy + 20, 76, 14, col);
        ps3_draw_rect(fb, sx + 58, sy + 28, 18, 14, col);
        ps3_draw_rect(fb, sx, sy + 40, 76, 14, col);

        int xx = x + 198 + ox;
        int xy = y + oy;
        for (int r = 0; r < 54; r++) {
            int x1 = xx + (r * 60) / 54;
            int x2 = xx + 60 - (r * 60) / 54;
            ps3_draw_rect(fb, x1, xy + r, 16, 1, col);
            ps3_draw_rect(fb, x2, xy + r, 16, 1, col);
        }
        ps3_draw_rect(fb, xx, xy, 20, 5, col);
        ps3_draw_rect(fb, xx + 56, xy, 20, 5, col);
        ps3_draw_rect(fb, xx, xy + 49, 20, 5, col);
        ps3_draw_rect(fb, xx + 56, xy + 49, 20, 5, col);
    }

    int lx = x;
    int ly = y;
    ps3_draw_rect(fb, lx, ly, 18, 3, white);
    ps3_draw_rect(fb, lx, ly, 3, 54, white);
    ps3_draw_rect(fb, lx + 68, ly, 18, 3, white);
    ps3_draw_rect(fb, lx + 68, ly, 3, 54, white);

    int lsx = x + 104;
    ps3_draw_rect(fb, lsx, ly, 76, 3, white);
    ps3_draw_rect(fb, lsx, ly, 3, 24, white);
    ps3_draw_rect(fb, lsx, ly + 20, 76, 3, white);

    int lxx = x + 198;
    ps3_draw_rect(fb, lxx, ly, 20, 3, white);
    ps3_draw_rect(fb, lxx + 56, ly, 20, 3, white);

    int bx = x + 284;
    int by = y + 4;
    ps3_draw_bevel_box(fb, bx, by, 66, 46, 0xFF0055AAU, 0xFF0099FFU, 0xFF002255U, 0xFFFFFFFFU, 255);
    ps3_draw_text_centered(fb, bx + 33, by + 12, "4PS3", white, 2);

    int hx = bx + 74;
    ps3_draw_bevel_box(fb, hx, by, 72, 46, 0xFFD97706U, 0xFFFBBF24U, 0xFF78350FU, 0xFFFFFFFFU, 255);
    ps3_draw_text_centered(fb, hx + 36, by + 14, "v1.00", white, 1);
}

static SDL_Surface *s_splash_surf = NULL;

static void draw_boot_splash(const char *step_msg, int pct)
{
    if (!s_splash_surf) {
        static const char *spaths[4] = {
            "./data/splash.bmp",
            "app0:/data/splash.bmp",
            "/dev_hdd0/game/FMSX00001/USRDIR/data/splash.bmp",
            "/dev_hdd0/fmsx/data/splash.bmp"
        };
        for (int i = 0; i < 4; i++) {
            s_splash_surf = SDL_LoadBMP(spaths[i]);
            if (s_splash_surf) break;
        }
    }

    if (s_splash_surf) {
        SDL_Surface *screen = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)g_framebuf, SCREEN_W, SCREEN_H, 32, SCREEN_W * 4, SDL_PIXELFORMAT_ARGB8888);
        if (screen) {
            SDL_BlitSurface(s_splash_surf, NULL, screen, NULL);
            SDL_FreeSurface(screen);
        }
    } else {
        ps3_draw_panel(g_framebuf, 0, 0, SCREEN_W, SCREEN_H, 0xFF0B172CU, 0xFF040914U, 0xFF1E3A68U, 255);
        for (int y = 0; y < SCREEN_H; y += 40) {
            ps3_draw_rect_alpha(g_framebuf, 0, y, SCREEN_W, 1, 0xFF142440U, 60);
        }
        for (int x = 0; x < SCREEN_W; x += 40) {
            ps3_draw_rect_alpha(g_framebuf, x, 0, 1, SCREEN_H, 0xFF142440U, 60);
        }
        draw_msx_logo_graphic(g_framebuf, 430, 60);
        ps3_draw_text_centered(g_framebuf, SCREEN_W / 2, 132, "MICROSOFT & ASCII STANDARD", 0xFFFFE040U, 2);
        ps3_draw_text_centered(g_framebuf, SCREEN_W / 2, 168, "MSX4PS3 v1.00 - PS3 PORT BY theheroGAC", 0xFF90D0FFU, 1);
    }

    int stat_x = 240, stat_y = 540, stat_w = 800, stat_h = 76;
    ps3_draw_bevel_box(g_framebuf, stat_x, stat_y, stat_w, stat_h, 0xFF0E1A30U, 0xFF1A3058U, 0xFF060C18U, 0xFF00E5FFU, 240);
    ps3_draw_text_centered(g_framebuf, SCREEN_W / 2, stat_y + 12, step_msg ? step_msg : "INITIALIZING...", 0xFFFFFFFFU, 1);

    int bar_x = stat_x + 30, bar_y = stat_y + 40, bar_w = stat_w - 60, bar_h = 20;
    ps3_draw_bevel_box(g_framebuf, bar_x, bar_y, bar_w, bar_h, 0xFF081020U, 0xFF183058U, 0xFF040810U, 0xFF3898FFU, 255);
    int p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    int fill_w = ((bar_w - 6) * p) / 100;
    if (fill_w > 0) {
        ps3_draw_rect(g_framebuf, bar_x + 3, bar_y + 3, fill_w, bar_h - 6, 0xFF00E5FFU);
        ps3_draw_rect(g_framebuf, bar_x + 3, bar_y + 3, fill_w, 2, 0xFFFFFFFFU);
    }

    if (s_splash_surf && pct >= 100) {
        SDL_FreeSurface(s_splash_surf);
        s_splash_surf = NULL;
    }

    ps3_present_framebuf();
}

void ps3_render_msx_to_framebuf(void)
{
    int is_wide = ((ScrMode == 6) || ((ScrMode == 7) && !ModeYJK) || (ScrMode == MAXSCREEN + 1)) ? 1 : 0;
    const uint32_t *src = is_wide ? (const uint32_t *)WBuf : (const uint32_t *)XBuf;
    int src_w = is_wide ? (WIDTH * 2) : WIDTH;
    int src_h = HEIGHT;

    if (!src) {
        ps3_draw_rect(g_framebuf, 0, 0, SCREEN_W, SCREEN_H, 0xFF080E1CU);
        return;
    }

    int dst_x = 0, dst_y = 0, dst_w = SCREEN_W, dst_h = SCREEN_H;
    if (g_cfg.aspect_ratio == ASPECT_4_3) {
        dst_w = 960;
        dst_h = 720;
        dst_x = (SCREEN_W - dst_w) / 2;
        dst_y = 0;
    } else if (g_cfg.aspect_ratio == ASPECT_PIXEL) {
        dst_w = WIDTH * 3;
        dst_h = HEIGHT * 3;
        dst_x = (SCREEN_W - dst_w) / 2;
        dst_y = (SCREEN_H - dst_h) / 2;
    }

    if (dst_x > 0 || dst_y > 0) {
        ps3_draw_rect(g_framebuf, 0, 0, SCREEN_W, SCREEN_H, 0xFF050811U);
        if (dst_x > 8) {
            ps3_draw_rect(g_framebuf, dst_x - 3, dst_y, 2, dst_h, 0xFF1C3C6CU);
            ps3_draw_rect(g_framebuf, dst_x + dst_w + 1, dst_y, 2, dst_h, 0xFF1C3C6CU);
        }
    }

    int scan_mode = g_cfg.scanlines % 3;

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        const uint32_t *srow = src + sy * src_w;
        uint32_t *drow = g_framebuf + (dst_y + y) * SCREEN_W + dst_x;

        if (scan_mode != SCANLINES_OFF && (y & 1)) {
            uint32_t mask = 0xFFFEFEFEU;
            for (int x = 0; x < dst_w; x++) {
                int sx = (x * src_w) / dst_w;
                uint32_t p = srow[sx] | 0xFF000000U;
                if (scan_mode == SCANLINES_LIGHT) {
                    uint32_t half = ((p & mask) >> 1);
                    uint32_t qtr  = ((half & mask) >> 1);
                    drow[x] = 0xFF000000U | (half + qtr);
                } else {
                    drow[x] = 0xFF000000U | ((p & mask) >> 1);
                }
            }
        } else {
            for (int x = 0; x < dst_w; x++) {
                int sx = (x * src_w) / dst_w;
                drow[x] = srow[sx] | 0xFF000000U;
            }
        }
    }
}

static void PutImage(void)
{
    if (!g_app_running) return;
    ps3_render_msx_to_framebuf();
    if (g_vkbd_mode) {
        ps3_vkbd_draw(g_framebuf);
    }
    ps3_toast_draw(g_framebuf);
    ps3_present_framebuf();
}

int InitMachine(void)
{
    if (!NewImage(&NormScreen, WIDTH, HEIGHT)) return 0;
    XBuf = NormScreen.Data;
    if (!NewImage(&WideScreen, WIDTH * 2, HEIGHT)) return 0;
    WBuf = WideScreen.Data;

    for (int j = 0; j < 256; j++) {
        BPal[j] = RGB2PIXEL(((j >> 2) & 0x07) * 255 / 7,
                            ((j >> 5) & 0x07) * 255 / 7,
                            (j & 0x03) * 255 / 3);
    }
    memset(XPal, 0, sizeof(XPal));
    XPal0 = 0xFF000000U;

    InitSound(48000, 150);
    SetChannels(255, 0xFFFF);
    return 1;
}

void TrashMachine(void)
{
    TrashSound();
    FreeImage(&NormScreen);
    FreeImage(&WideScreen);
}

void SetColor(byte N, byte R, byte G, byte B)
{
    if (N) XPal[N] = RGB2PIXEL(R, G, B);
    else   XPal0   = RGB2PIXEL(R, G, B);
}

unsigned int Joystick(void)
{
    return s_cur_joystate;
}

void Keyboard(void)
{
    s_boot_frame_cnt++;
    if (s_boot_frame_cnt == 15) {
        ps3_menu_open(MENU_MAIN);
    }

    if (g_menu_mode != MENU_NONE) {
        memset((void *)KeyState, 0xFF, 16);
        s_cur_joystate = 0;
        ps3_menu_run_loop();
        s_prev_pad = ps3_input_poll(0);
        return;
    }

    uint32_t buttons = ps3_input_poll(0);
    uint32_t pad2    = ps3_input_poll(1);
    uint32_t pressed = buttons & ~s_prev_pad;
    s_prev_pad = buttons;

    if (!g_app_running) {
        ExitNow = 1;
        return;
    }

    if (((buttons & PS3_BTN_L3) && (buttons & PS3_BTN_R3)) ||
        ((buttons & PS3_BTN_L2) && (buttons & PS3_BTN_R2) && (buttons & PS3_BTN_START))) {
        ps3_trigger_hardware_reset();
        ps3_toast_show("MSX HARDWARE RESET EXECUTED", 90);
        return;
    }

    if (pressed & (PS3_BTN_START | PS3_BTN_R3)) {
        g_vkbd_mode = 0;
        ps3_menu_open(MENU_QUICK);
        return;
    }

    if ((pressed & PS3_BTN_R2) && !g_vkbd_mode) {
        ps3_menu_open(MENU_QUICK);
        return;
    }

    if (pressed & (PS3_BTN_SELECT | PS3_BTN_L2 | PS3_BTN_L3)) {
        g_vkbd_mode = !g_vkbd_mode;
        memset((void *)KeyState, 0xFF, 16);
        s_cur_joystate = 0;
        return;
    }

    memset((void *)KeyState, 0xFF, 16);
    s_cur_joystate = 0;

    if (g_vkbd_mode) {
        ps3_vkbd_update(buttons, pressed);
        ps3_vkbd_apply_to_msx();
        return;
    }

    ps3_vkbd_apply_to_msx();

    int up    = (buttons & PS3_BTN_UP)    || (g_pad1_Ly < -55);
    int down  = (buttons & PS3_BTN_DOWN)  || (g_pad1_Ly >  55);
    int left  = (buttons & PS3_BTN_LEFT)  || (g_pad1_Lx < -55);
    int right = (buttons & PS3_BTN_RIGHT) || (g_pad1_Lx >  55);

    if (up)    s_cur_joystate |= JST_UP;
    if (down)  s_cur_joystate |= JST_DOWN;
    if (left)  s_cur_joystate |= JST_LEFT;
    if (right) s_cur_joystate |= JST_RIGHT;

    s_autofire_tick++;
    int fire_a = (buttons & PS3_BTN_CROSS) ? 1 : 0;
    if (fire_a && g_cfg.autofire_a && (s_autofire_tick & 2)) fire_a = 0;
    int fire_b = (buttons & PS3_BTN_CIRCLE) ? 1 : 0;

    if (fire_a) s_cur_joystate |= JST_FIREA;
    if (fire_b) s_cur_joystate |= JST_FIREB;

    if (g_cfg.pad_mode == PADMODE_HYBRID) {
        if (up)     KBD_SET(KBD_UP);
        if (down)   KBD_SET(KBD_DOWN);
        if (left)   KBD_SET(KBD_LEFT);
        if (right)  KBD_SET(KBD_RIGHT);
        if (fire_a) KBD_SET(KBD_SPACE);
        if (fire_b) KBD_SET('M');
    }

    if (buttons & PS3_BTN_SQUARE)   KBD_SET(KBD_F1);
    if (buttons & PS3_BTN_TRIANGLE) KBD_SET(KBD_F2);
    if (buttons & PS3_BTN_L1)       KBD_SET(KBD_ESCAPE);
    if (buttons & PS3_BTN_R1)       KBD_SET(KBD_ENTER);

    if (pad2 & PS3_BTN_UP)     s_cur_joystate |= (JST_UP    << 8);
    if (pad2 & PS3_BTN_DOWN)   s_cur_joystate |= (JST_DOWN  << 8);
    if (pad2 & PS3_BTN_LEFT)   s_cur_joystate |= (JST_LEFT  << 8);
    if (pad2 & PS3_BTN_RIGHT)  s_cur_joystate |= (JST_RIGHT << 8);
    if (pad2 & PS3_BTN_CROSS)  s_cur_joystate |= (JST_FIREA << 8);
    if (pad2 & PS3_BTN_CIRCLE) s_cur_joystate |= (JST_FIREB << 8);
}

unsigned int Mouse(byte N)
{
    if (N != 0 || g_cfg.pad_mode != PADMODE_MOUSE) return 0;
    if (abs(g_pad1_Lx) > 20) s_mouse_x += g_pad1_Lx / 16;
    if (abs(g_pad1_Ly) > 20) s_mouse_y += g_pad1_Ly / 16;
    if (s_mouse_x < 0) s_mouse_x = 0;
    if (s_mouse_x > 255) s_mouse_x = 255;
    if (s_mouse_y < 0) s_mouse_y = 0;
    if (s_mouse_y > 211) s_mouse_y = 211;
    unsigned int btns = 0;
    if (s_prev_pad & PS3_BTN_CROSS)  btns |= 0x10000U;
    if (s_prev_pad & PS3_BTN_CIRCLE) btns |= 0x20000U;
    return btns | ((unsigned int)(s_mouse_y & 0xFF) << 8) | (unsigned int)(s_mouse_x & 0xFF);
}

#include "Common.h"
#include "Wide.h"

static int ps3_video_init(void)
{
    static const Uint32 wflags[] = { 0, SDL_WINDOW_SHOWN };
    static const Uint32 rflags[] = {
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE,
        SDL_RENDERER_ACCELERATED,
        SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE,
        SDL_RENDERER_SOFTWARE,
        0
    };

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 0;
    }

    SDL_DisplayMode dm;
    int ww = SCREEN_W;
    int wh = SCREEN_H;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w >= 640 && dm.h >= 480) {
        ww = dm.w;
        wh = dm.h;
    }
    s_out_w = ww;
    s_out_h = wh;

    for (int i = 0; i < 2 && !s_window; i++) {
        s_window = SDL_CreateWindow("MSX4PS3", 0, 0, ww, wh, wflags[i]);
    }
    if (!s_window) {
        s_window = SDL_CreateWindow("MSX4PS3", 0, 0, SCREEN_W, SCREEN_H, 0);
    }
    if (s_window) {
        int actual_w = 0, actual_h = 0;
        SDL_GetWindowSize(s_window, &actual_w, &actual_h);
        if (actual_w >= 640 && actual_h >= 480) {
            s_out_w = actual_w;
            s_out_h = actual_h;
        }
        for (int i = 0; i < 5 && !s_renderer; i++) {
            s_renderer = SDL_CreateRenderer(s_window, -1, rflags[i]);
        }
    }
    if (!s_window || !s_renderer) {
        return 0;
    }

    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);

    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!s_texture) {
        return 0;
    }
    SDL_SetTextureBlendMode(s_texture, SDL_BLENDMODE_NONE);

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    return 1;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ioPadInit(7);

    if (!ps3_video_init()) {
        sysProcessExit(1);
        return 0;
    }

    ps3_font_init();

    draw_boot_splash("INITIALIZING RSX HD VIDEO & PS3 CONTROLLERS...", 25);

    mkdir(FMSX_BASE_DIR, 0777);
    mkdir(FMSX_ROMS_DIR, 0777);
    mkdir(FMSX_SAVES_DIR, 0777);
    mkdir(FMSX_STATES_DIR, 0777);
    mkdir(FMSX_BIOS_DIR, 0777);
    mkdir(FMSX_CACHE_DIR, 0777);

    draw_boot_splash("LOADING CONFIGURATION, GAME LIBRARY & 48KHZ AUDIO...", 65);
    ps3_config_load();
    ps3_vkbd_init();
    ps3_menu_init();
    ps3_audio_start();

    draw_boot_splash("MSX SYSTEM CORE READY - STARTING EMULATION...", 100);
    for (int sf = 0; sf < 100; sf++) {
        uint32_t b = ps3_input_poll(0);
        if (b & (PS3_BTN_CROSS | PS3_BTN_START | PS3_BTN_CIRCLE | PS3_BTN_SQUARE)) break;
        ps3_present_framebuf();
        usleep(16000);
    }

    Verbose = 0;
    UPeriod = 100;
    ProgDir = BIOS_USRDIR_PATH;

    ps3_apply_machine_config(0);

    int has_oem = ps3_bios_has_oem_for_model(g_cfg.msx_model);
    if (!has_oem) {
        if (!ps3_bios_show_dialog(1)) {
            ps3_audio_stop();
            if (s_texture)  SDL_DestroyTexture(s_texture);
            if (s_renderer) SDL_DestroyRenderer(s_renderer);
            if (s_window)   SDL_DestroyWindow(s_window);
            ps3_font_shutdown();
            SDL_Quit();
            ioPadEnd();
            sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
            sysProcessExit(0);
            return 0;
        }
    } else {
        ps3_toast_show("AUTHENTIC OEM COMMERCIAL BIOS ACTIVE", 120);
    }

    if (g_cfg.carta_path[0]) ROMName[0] = g_cfg.carta_path;
    else                     ROMName[0] = NULL;
    if (g_cfg.cartb_path[0]) ROMName[1] = g_cfg.cartb_path;
    else                     ROMName[1] = NULL;
    if (g_cfg.dska_path[0])  DSKName[0] = g_cfg.dska_path;
    else                     DSKName[0] = NULL;
    if (g_cfg.dskb_path[0])  DSKName[1] = g_cfg.dskb_path;
    else                     DSKName[1] = NULL;
    if (g_cfg.cas_path[0])   CasName    = g_cfg.cas_path;
    else                     CasName    = NULL;

    if (InitMachine()) {
        if (!StartMSX(Mode, RAMPages, VRAMPages)) {
            if (g_cfg.msx_model != 0) {
                g_cfg.msx_model = 0;
                ps3_apply_machine_config(0);
                ps3_toast_show("FALLBACK TO MSX1 CORE", 120);
                StartMSX(Mode, RAMPages, VRAMPages);
            } else {
                ps3_bios_show_dialog(1);
            }
        }
        TrashMSX();
        TrashMachine();
    }

    ps3_audio_stop();
    if (s_texture)  SDL_DestroyTexture(s_texture);
    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window)   SDL_DestroyWindow(s_window);
    ps3_font_shutdown();
    SDL_Quit();
    ioPadEnd();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysProcessExit(0);
    return 0;
}
