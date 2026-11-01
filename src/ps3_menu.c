#include "ps3_frontend.h"
#include "unzip.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_BROWSER_ENTRIES 512
#define MAX_LIBRARY_GAMES   512
#define THUMB_W 136
#define THUMB_H 114

#define LIB_MEDIA_CART 0
#define LIB_MEDIA_DISK 1
#define LIB_MEDIA_TAPE 2

typedef struct {
    char name[256];
    int  is_dir;
    long size;
} BrowserEntry;

typedef struct {
    char title[128];
    char filename[256];
    char full_path[512];
    char source_label[32];
    int  media_type;
    long size;
} LibraryGame;

extern int HPeriod, VPeriod;

static BrowserEntry s_entries[MAX_BROWSER_ENTRIES];
static int s_entry_count = 0;
static int s_browser_sel = 0;
static int s_browser_scroll = 0;
static int s_browser_target_slot = 0;
static int s_root_idx = 0;
static char s_cur_dir[512] = FMSX_ROMS_DIR;

static LibraryGame s_lib_games[MAX_LIBRARY_GAMES];
static int s_lib_total = 0;
static int s_lib_filtered[MAX_LIBRARY_GAMES];
static int s_lib_filtered_count = 0;
static int s_lib_filter_cat = 0;
static int s_lib_sel = 0;
static int s_lib_scroll = 0;

static int s_main_sel = 0;
static int s_quick_sel = 0;
static int s_states_sel = 0;
static int s_states_action = 0;
static int s_machine_sel = 0;
static int s_settings_sel = 0;
static int s_slots_sel = 0;
static int s_vkbd_sel = 0;
static int s_credits_sel = 0;
static int s_active_tab = TAB_LIBRARY;
static int s_prev_menu = MENU_MAIN;

static uint32_t s_thumb_buf[THUMB_W * THUMB_H];
static uint32_t s_save_tmp_buf[THUMB_W * THUMB_H];
static int      s_thumb_loaded_slot = -1;
static int      s_thumb_valid = 0;
static char     s_thumb_game[128] = {0};

static const char *s_quick_roots[] = {
    "/dev_hdd0/fmsx/roms",
    "/dev_hdd0/game/FMSX00001/USRDIR/roms",
    "/dev_hdd0/ROMS/msx",
    "/dev_hdd0/roms",
    "/dev_usb000/roms/msx",
    "/dev_usb000/roms",
    "/dev_usb000/msx",
    "/dev_usb000",
    "/dev_usb001/roms/msx",
    "/dev_usb001/roms",
    "/dev_usb001/msx",
    "/dev_usb001",
    "/dev_usb002",
    "/dev_usb003",
    "/dev_usb006",
    "/dev_usb007",
    "/dev_hdd0",
    "/"
};
#define NUM_QUICK_ROOTS 18

static int has_ext_ci(const char *name, const char *ext)
{
    size_t nl = strlen(name);
    size_t el = strlen(ext);
    if (nl < el) return 0;
    const char *p = name + nl - el;
    for (size_t i = 0; i < el; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)ext[i])) return 0;
    }
    return 1;
}

static int is_raw_msx_media(const char *name)
{
    return has_ext_ci(name, ".rom") ||
           has_ext_ci(name, ".mx1") ||
           has_ext_ci(name, ".mx2") ||
           has_ext_ci(name, ".dsk") ||
           has_ext_ci(name, ".fdi") ||
           has_ext_ci(name, ".cas");
}

static int is_supported_file(const char *name)
{
    return is_raw_msx_media(name) ||
           has_ext_ci(name, ".zip") ||
           has_ext_ci(name, ".sta");
}

static int is_game_media_file(const char *name)
{
    return is_raw_msx_media(name) ||
           has_ext_ci(name, ".zip");
}

static int inspect_zip_archive(const char *zip_path, int *out_media_type, long *out_uncomp_size)
{
    unzFile uf = unzOpen(zip_path);
    if (!uf) return 0;

    int found = 0;
    if (unzGoToFirstFile(uf) == UNZ_OK) {
        do {
            unz_file_info fi;
            char entry_name[256];
            if (unzGetCurrentFileInfo(uf, &fi, entry_name, sizeof(entry_name), NULL, 0, NULL, 0) == UNZ_OK) {
                if (is_raw_msx_media(entry_name)) {
                    if (out_uncomp_size) *out_uncomp_size = (long)fi.uncompressed_size;
                    if (out_media_type) {
                        if (has_ext_ci(entry_name, ".dsk") || has_ext_ci(entry_name, ".fdi")) *out_media_type = LIB_MEDIA_DISK;
                        else if (has_ext_ci(entry_name, ".cas")) *out_media_type = LIB_MEDIA_TAPE;
                        else *out_media_type = LIB_MEDIA_CART;
                    }
                    found = 1;
                    break;
                }
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }
    unzClose(uf);
    return found;
}

static int extract_zip_to_cache(const char *zip_path, char *out_extracted_path, size_t out_len)
{
    unzFile uf = unzOpen(zip_path);
    if (!uf) return 0;

    int ok = 0;
    if (unzGoToFirstFile(uf) == UNZ_OK) {
        do {
            unz_file_info fi;
            char entry_name[256];
            if (unzGetCurrentFileInfo(uf, &fi, entry_name, sizeof(entry_name), NULL, 0, NULL, 0) == UNZ_OK) {
                if (is_raw_msx_media(entry_name) || has_ext_ci(entry_name, ".sta")) {
                    if (unzOpenCurrentFile(uf) == UNZ_OK) {
                        mkdir(FMSX_CACHE_DIR, 0777);
                        const char *base = strrchr(entry_name, '/');
                        if (!base) base = strrchr(entry_name, '\\');
                        base = base ? (base + 1) : entry_name;
                        snprintf(out_extracted_path, out_len, "%s/%s", FMSX_CACHE_DIR, base);

                        FILE *outf = fopen(out_extracted_path, "wb");
                        if (outf) {
                            unsigned char zbuf[16384];
                            int rd = 0;
                            ok = 1;
                            while ((rd = unzReadCurrentFile(uf, zbuf, sizeof(zbuf))) > 0) {
                                if (fwrite(zbuf, 1, (size_t)rd, outf) != (size_t)rd) {
                                    ok = 0;
                                    break;
                                }
                            }
                            fclose(outf);
                        }
                        unzCloseCurrentFile(uf);
                        break;
                    }
                }
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }
    unzClose(uf);
    return ok;
}

static void clean_game_title(const char *filename, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", filename);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    for (size_t i = 0; out[i]; i++) {
        if (out[i] == '_') out[i] = ' ';
    }
}

static void extract_base_stem(const char *path_or_name, char *out, size_t out_len)
{
    const char *base = strrchr(path_or_name, '/');
    base = base ? (base + 1) : path_or_name;
    snprintf(out, out_len, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static int cmp_entries(const void *a, const void *b)
{
    const BrowserEntry *ea = (const BrowserEntry *)a;
    const BrowserEntry *eb = (const BrowserEntry *)b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return strcasecmp(ea->name, eb->name);
}

static int cmp_lib_games(const void *a, const void *b)
{
    const LibraryGame *ga = (const LibraryGame *)a;
    const LibraryGame *gb = (const LibraryGame *)b;
    return strcasecmp(ga->title, gb->title);
}

static void rebuild_library_filter(void)
{
    s_lib_filtered_count = 0;
    for (int i = 0; i < s_lib_total; i++) {
        int match = 0;
        if (s_lib_filter_cat == 0) match = 1;
        else if (s_lib_filter_cat == 1 && s_lib_games[i].media_type == LIB_MEDIA_CART) match = 1;
        else if (s_lib_filter_cat == 2 && s_lib_games[i].media_type == LIB_MEDIA_DISK) match = 1;
        else if (s_lib_filter_cat == 3 && s_lib_games[i].media_type == LIB_MEDIA_TAPE) match = 1;
        if (match) {
            s_lib_filtered[s_lib_filtered_count++] = i;
        }
    }
    if (s_lib_sel >= s_lib_filtered_count) {
        s_lib_sel = s_lib_filtered_count > 0 ? s_lib_filtered_count - 1 : 0;
    }
    if (s_lib_sel < 0) s_lib_sel = 0;
    s_lib_scroll = 0;
    s_thumb_loaded_slot = -1;
}

static void scan_library_dir(const char *dir_path, const char *source_label, int depth)
{
    if (depth > 1 || s_lib_total >= MAX_LIBRARY_GAMES) return;
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && s_lib_total < MAX_LIBRARY_GAMES) {
        if (de->d_name[0] == '.') continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);

        struct stat st;
        int is_dir = 0;
        long fsize = 0;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            fsize = (long)st.st_size;
        } else if (de->d_type == DT_DIR) {
            is_dir = 1;
        }

        if (is_dir) {
            if (depth < 1 && strcasecmp(de->d_name, "bios") != 0 && strcasecmp(de->d_name, "saves") != 0 && strcasecmp(de->d_name, "states") != 0) {
                scan_library_dir(full, source_label, depth + 1);
            }
            continue;
        }

        if (!is_game_media_file(de->d_name)) continue;

        int dup = 0;
        for (int i = 0; i < s_lib_total; i++) {
            if (strcasecmp(s_lib_games[i].filename, de->d_name) == 0 && s_lib_games[i].size == fsize) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        LibraryGame *g = &s_lib_games[s_lib_total++];
        snprintf(g->filename, sizeof(g->filename), "%s", de->d_name);
        snprintf(g->full_path, sizeof(g->full_path), "%s", full);
        snprintf(g->source_label, sizeof(g->source_label), "%s", source_label);
        clean_game_title(de->d_name, g->title, sizeof(g->title));
        if (has_ext_ci(de->d_name, ".zip")) {
            int ztype = LIB_MEDIA_CART;
            long zsize = fsize;
            if (!inspect_zip_archive(full, &ztype, &zsize)) continue;
            g->media_type = ztype;
            g->size = zsize;
        } else {
            g->size = fsize;
            if (has_ext_ci(de->d_name, ".dsk") || has_ext_ci(de->d_name, ".fdi")) g->media_type = LIB_MEDIA_DISK;
            else if (has_ext_ci(de->d_name, ".cas")) g->media_type = LIB_MEDIA_TAPE;
            else g->media_type = LIB_MEDIA_CART;
        }
    }
    closedir(d);
}

static void scan_game_library(void)
{
    s_lib_total = 0;
    scan_library_dir(FMSX_ROMS_DIR,        "HDD0",   0);
    scan_library_dir(ROMS_USRDIR_PATH,     "USRDIR", 0);
    scan_library_dir("/dev_hdd0/ROMS/msx", "HDD0",   0);
    scan_library_dir("/dev_hdd0/roms",     "HDD0",   0);

    for (int u = 0; u < 8; u++) {
        char root_cand[32];
        snprintf(root_cand, sizeof(root_cand), "/dev_usb00%d", u);
        struct stat st_usb;
        if (stat(root_cand, &st_usb) != 0) continue;

        char path[64];
        char label[32];
        snprintf(label, sizeof(label), "USB%d", u);

        snprintf(path, sizeof(path), "/dev_usb00%d/roms/msx", u);
        scan_library_dir(path, label, 0);

        snprintf(path, sizeof(path), "/dev_usb00%d/ROMS/msx", u);
        scan_library_dir(path, label, 0);

        snprintf(path, sizeof(path), "/dev_usb00%d/roms", u);
        scan_library_dir(path, label, 0);

        snprintf(path, sizeof(path), "/dev_usb00%d/msx", u);
        scan_library_dir(path, label, 0);

        snprintf(path, sizeof(path), "/dev_usb00%d/MSX", u);
        scan_library_dir(path, label, 0);
        scan_library_dir(path, label, 0);

        snprintf(path, sizeof(path), "/dev_usb00%d", u);
        snprintf(label, sizeof(label), "USB%d Root", u);
        scan_library_dir(path, label, 0);
    }

    if (s_lib_total > 1) {
        qsort(s_lib_games, s_lib_total, sizeof(LibraryGame), cmp_lib_games);
    }
    rebuild_library_filter();
}

static void scan_directory(const char *dir_path)
{
    s_entry_count = 0;
    s_browser_sel = 0;
    s_browser_scroll = 0;

    if (strcmp(dir_path, "/") != 0) {
        snprintf(s_entries[s_entry_count].name, sizeof(s_entries[0].name), "..");
        s_entries[s_entry_count].is_dir = 1;
        s_entries[s_entry_count].size = 0;
        s_entry_count++;
    }

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && s_entry_count < MAX_BROWSER_ENTRIES) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);

        struct stat st;
        int is_dir = 0;
        long fsize = 0;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            fsize = (long)st.st_size;
        } else if (de->d_type == DT_DIR) {
            is_dir = 1;
        }

        if (!is_dir && !is_supported_file(de->d_name)) continue;

        snprintf(s_entries[s_entry_count].name, sizeof(s_entries[0].name), "%s", de->d_name);
        s_entries[s_entry_count].is_dir = is_dir;
        s_entries[s_entry_count].size = fsize;
        s_entry_count++;
    }
    closedir(d);

    int start_sort = (s_entry_count > 0 && strcmp(s_entries[0].name, "..") == 0) ? 1 : 0;
    if (s_entry_count - start_sort > 1) {
        qsort(&s_entries[start_sort], s_entry_count - start_sort, sizeof(BrowserEntry), cmp_entries);
    }
}

void ps3_get_active_game_name(char *out, size_t out_len)
{
    const char *src = NULL;
    if (g_cfg.carta_path[0]) src = g_cfg.carta_path;
    else if (g_cfg.cartb_path[0]) src = g_cfg.cartb_path;
    else if (g_cfg.dska_path[0]) src = g_cfg.dska_path;
    else if (g_cfg.cas_path[0]) src = g_cfg.cas_path;

    if (!src || !*src) {
        snprintf(out, out_len, "MSX_SYSTEM");
        return;
    }

    extract_base_stem(src, out, out_len);
}

static void build_state_path_for_game(const char *gname, int slot, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s_slot%d.sta", FMSX_STATES_DIR, gname, slot % 10);
}

static void build_thumb_path_for_game(const char *gname, int slot, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s_slot%d.thm", FMSX_STATES_DIR, gname, slot % 10);
}

static void build_state_path(int slot, char *out, size_t out_len)
{
    char gname[128];
    ps3_get_active_game_name(gname, sizeof(gname));
    build_state_path_for_game(gname, slot, out, out_len);
}

static void build_thumb_path(int slot, char *out, size_t out_len)
{
    char gname[128];
    ps3_get_active_game_name(gname, sizeof(gname));
    build_thumb_path_for_game(gname, slot, out, out_len);
}

static int count_game_states(const char *gname, int *first_valid_slot)
{
    int cnt = 0;
    if (first_valid_slot) *first_valid_slot = -1;
    for (int s = 0; s < 10; s++) {
        char path[512];
        build_state_path_for_game(gname, s, path, sizeof(path));
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 16) {
            if (cnt == 0 && first_valid_slot) *first_valid_slot = s;
            cnt++;
        }
    }
    return cnt;
}

static void save_state_thumbnail(int slot)
{
    char tpath[512];
    build_thumb_path(slot, tpath, sizeof(tpath));
    ps3_render_msx_to_framebuf();

    for (int y = 0; y < THUMB_H; y++) {
        int sy = (y * SCREEN_H) / THUMB_H;
        const uint32_t *srow = g_framebuf + sy * SCREEN_W;
        for (int x = 0; x < THUMB_W; x++) {
            int sx = (x * SCREEN_W) / THUMB_W;
            s_save_tmp_buf[y * THUMB_W + x] = srow[sx] | 0xFF000000U;
        }
    }

    FILE *f = fopen(tpath, "wb");
    if (f) {
        fwrite(s_save_tmp_buf, sizeof(uint32_t), THUMB_W * THUMB_H, f);
        fclose(f);
    }
    s_thumb_loaded_slot = -1;
}

static void ensure_thumb_loaded_for_game(const char *gname, int slot)
{
    if (slot < 0) {
        s_thumb_valid = 0;
        s_thumb_loaded_slot = -1;
        return;
    }
    if (s_thumb_loaded_slot == slot && strcmp(s_thumb_game, gname) == 0) return;

    s_thumb_loaded_slot = slot;
    snprintf(s_thumb_game, sizeof(s_thumb_game), "%s", gname);
    s_thumb_valid = 0;

    char tpath[512];
    build_thumb_path_for_game(gname, slot, tpath, sizeof(tpath));
    FILE *f = fopen(tpath, "rb");
    if (f) {
        if (fread(s_thumb_buf, sizeof(uint32_t), THUMB_W * THUMB_H, f) == (size_t)(THUMB_W * THUMB_H)) {
            s_thumb_valid = 1;
        }
        fclose(f);
    }
}

static void ensure_thumb_loaded(int slot)
{
    char gname[128];
    ps3_get_active_game_name(gname, sizeof(gname));
    ensure_thumb_loaded_for_game(gname, slot);
}

static void draw_stylized_media_card(uint32_t *fb, int x, int y, int w, int h, const LibraryGame *g)
{
    ps3_draw_panel(fb, x, y, w, h, 0xFF0E1B34U, 0xFF060C18U, 0xFF3890E8U, 255);

    if (!g) {
        ps3_draw_text_centered(fb, x + w / 2, y + h / 2 - 8, "NO PREVIEW", 0xFF6888B0U, 2);
        return;
    }

    int cx = x + w / 2;
    if (g->media_type == LIB_MEDIA_CART) {
        int cw = w - 44;
        int ch = h - 38;
        int kx = x + 22;
        int ky = y + 14;
        ps3_draw_bevel_box(fb, kx, ky, cw, ch, 0xFF1E2430U, 0xFF485468U, 0xFF0C1018U, 0xFF05080CU, 255);
        ps3_draw_rect(fb, kx + 12, ky + 10, cw - 24, 6, 0xFF10141CU);
        ps3_draw_rect(fb, kx + 12, ky + 20, cw - 24, 6, 0xFF10141CU);

        ps3_draw_panel(fb, kx + 14, ky + 34, cw - 28, ch - 68, 0xFF1A4C96U, 0xFF0D2652U, 0xFF60B8FFU, 255);
        ps3_draw_rect(fb, kx + 16, ky + 36, cw - 32, 24, 0xFFE63244U);
        ps3_draw_text_centered(fb, cx, ky + 41, "MSX ROM CARTRIDGE", 0xFFFFFFFFU, 1);
        ps3_draw_text_clipped(fb, kx + 24, ky + 76, cw - 48, g->title, 0xFFFFE850U, 2);
        ps3_draw_text_centered(fb, cx, ky + 108, has_ext_ci(g->filename, ".mx2") ? "MSX2 MEGAROM" : "MSX / MSX2 GAME", 0xFF90E8FFU, 1);

        for (int p = 0; p < 14; p++) {
            ps3_draw_rect(fb, kx + 24 + p * ((cw - 48) / 14), ky + ch - 22, 8, 16, 0xFFE8B830U);
        }
    } else if (g->media_type == LIB_MEDIA_DISK) {
        int dw = w - 52;
        int dh = h - 36;
        int dx = x + 26;
        int dy = y + 14;
        ps3_draw_bevel_box(fb, dx, dy, dw, dh, 0xFF1C4488U, 0xFF4C80D8U, 0xFF0E244CU, 0xFF060E1CU, 255);
        ps3_draw_rect(fb, dx + 38, dy + 4, dw - 76, 52, 0xFFC8D4E4U);
        ps3_draw_rect(fb, dx + dw - 72, dy + 12, 22, 36, 0xFF182438U);
        ps3_draw_panel(fb, dx + 20, dy + 68, dw - 40, dh - 84, 0xFFF0F4F8U, 0xFFD0DCE8U, 0xFF406088U, 255);
        ps3_draw_rect(fb, dx + 22, dy + 70, dw - 44, 22, 0xFF009874U);
        ps3_draw_text_centered(fb, cx, dy + 77, "3.5\" MSX-DOS FLOPPY DISK", 0xFFFFFFFFU, 1);
        ps3_draw_text_clipped(fb, dx + 28, dy + 106, dw - 56, g->title, 0xFF102038U, 2);
    } else {
        int tw = w - 36;
        int th = h - 56;
        int tx = x + 18;
        int ty = y + 24;
        ps3_draw_bevel_box(fb, tx, ty, tw, th, 0xFF282C38U, 0xFF50586CU, 0xFF12161CU, 0xFF080A0EU, 255);
        ps3_draw_panel(fb, tx + 16, ty + 14, tw - 32, 68, 0xFFD87820U, 0xFF984808U, 0xFFFFB860U, 255);
        ps3_draw_text_centered(fb, cx, ty + 22, "MSX DATA CASSETTE", 0xFFFFFFFFU, 1);
        ps3_draw_text_clipped(fb, tx + 24, ty + 44, tw - 48, g->title, 0xFFFFF0A0U, 2);
        ps3_draw_rect(fb, tx + 44, ty + 96, tw - 88, 42, 0xFF10141CU);
        ps3_draw_bevel_box(fb, tx + 60, ty + 103, 28, 28, 0xFFE0E8F0U, 0xFFFFFFFFU, 0xFF90A0B0U, 0xFF000000U, 255);
        ps3_draw_bevel_box(fb, tx + tw - 88, ty + 103, 28, 28, 0xFFE0E8F0U, 0xFFFFFFFFU, 0xFF90A0B0U, 0xFF000000U, 255);
    }
}

static void draw_thumb_preview(uint32_t *fb, int slot, int x, int y, int scale)
{
    ensure_thumb_loaded(slot);
    int w = THUMB_W * scale;
    int h = THUMB_H * scale;
    ps3_draw_bevel_box(fb, x - 4, y - 4, w + 8, h + 8, 0xFF081020U, 0xFF3898FFU, 0xFF040810U, 0xFFFFFFFFU, 255);

    if (!s_thumb_valid) {
        ps3_draw_rect(fb, x, y, w, h, 0xFF0A1428U);
        ps3_draw_text_centered(fb, x + w / 2, y + h / 2 - 8, "NO PREVIEW", 0xFF6080A8U, scale);
        return;
    }

    for (int py = 0; py < h; py++) {
        int sy = py / scale;
        const uint32_t *srow = s_thumb_buf + sy * THUMB_W;
        uint32_t *drow = fb + (y + py) * SCREEN_W + x;
        for (int px = 0; px < w; px++) {
            drow[px] = srow[px / scale] | 0xFF000000U;
        }
    }
}

int ps3_state_slot_exists(int slot)
{
    char path[512];
    build_state_path(slot, path, sizeof(path));
    struct stat st;
    return (stat(path, &st) == 0 && st.st_size > 16) ? 1 : 0;
}

static long get_state_slot_size(int slot)
{
    char path[512];
    build_state_path(slot, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 16) return (long)st.st_size;
    return 0;
}

int ps3_state_save_slot(int slot)
{
    char path[512];
    build_state_path(slot, path, sizeof(path));
    int ok = SaveSTA(path);
    char msg[96];
    if (ok) {
        save_state_thumbnail(slot);
        snprintf(msg, sizeof(msg), "STATE SAVED TO SLOT %d", slot % 10);
    } else {
        snprintf(msg, sizeof(msg), "SAVE FAILED ON SLOT %d", slot % 10);
    }
    ps3_toast_show(msg, 110);
    return ok;
}

static int smart_load_sta_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char hdr[16];
    size_t rd = fread(hdr, 1, 16, f);
    fclose(f);
    if (rd == 16 && memcmp(hdr, "STE\032\003", 5) == 0) {
        int req_ram  = hdr[5];
        int req_vram = hdr[6];
        if (req_ram > 0 && req_vram > 0 && (req_ram != RAMPages || req_vram != VRAMPages)) {
            ResetMSX(Mode, req_ram, req_vram);
        }
    }
    return LoadSTA(path);
}

int ps3_state_load_slot(int slot)
{
    char path[512];
    build_state_path(slot, path, sizeof(path));
    int ok = smart_load_sta_file(path);
    char msg[96];
    if (ok) snprintf(msg, sizeof(msg), "STATE LOADED FROM SLOT %d", slot % 10);
    else    snprintf(msg, sizeof(msg), "SLOT %d EMPTY OR INCOMPATIBLE", slot % 10);
    ps3_toast_show(msg, 110);
    return ok;
}

static void delete_state_slot(int slot)
{
    char path[512], tpath[512];
    build_state_path(slot, path, sizeof(path));
    build_thumb_path(slot, tpath, sizeof(tpath));
    unlink(path);
    unlink(tpath);
    s_thumb_loaded_slot = -1;
    char msg[96];
    snprintf(msg, sizeof(msg), "SLOT %d DELETED", slot % 10);
    ps3_toast_show(msg, 90);
}

static int load_media_file(const char *full_path, int target_slot)
{
    char extracted_zip_path[512];
    if (has_ext_ci(full_path, ".zip")) {
        if (!extract_zip_to_cache(full_path, extracted_zip_path, sizeof(extracted_zip_path))) {
            ps3_toast_show("ZIP ARCHIVE HAS NO VALID MSX MEDIA", 110);
            return 0;
        }
        full_path = extracted_zip_path;
    }

    if (has_ext_ci(full_path, ".sta")) {
        int ok = smart_load_sta_file(full_path);
        ps3_toast_show(ok ? "SAVE STATE LOADED" : "FAILED TO LOAD STATE FILE", 110);
        return ok;
    }

    if (has_ext_ci(full_path, ".dsk") || has_ext_ci(full_path, ".fdi")) {
        int drv = (target_slot == 3) ? 1 : 0;
        if (ChangeDisk((byte)drv, full_path)) {
            if (drv == 0) {
                snprintf(g_cfg.dska_path, sizeof(g_cfg.dska_path), "%s", full_path);
                DSKName[0] = g_cfg.dska_path;
                for (int j = 0; j < MAXCARTS; j++) LoadCart(0, j, ROMTYPE(j));
                g_cfg.carta_path[0] = '\0';
                ResetMSX(Mode, RAMPages, VRAMPages);
            } else {
                snprintf(g_cfg.dskb_path, sizeof(g_cfg.dskb_path), "%s", full_path);
                DSKName[1] = g_cfg.dskb_path;
            }
            ps3_config_save();
            ps3_toast_show(drv == 0 ? "DISK A: INSERTED & BOOTED" : "DISK B: INSERTED", 110);
            return 1;
        }
        ps3_toast_show("FAILED TO OPEN DSK IMAGE", 110);
        return 0;
    }

    if (has_ext_ci(full_path, ".cas")) {
        if (ChangeTape(full_path)) {
            snprintf(g_cfg.cas_path, sizeof(g_cfg.cas_path), "%s", full_path);
            CasName = g_cfg.cas_path;
            RewindTape();
            ps3_config_save();
            ps3_toast_show("TAPE INSERTED (USE RUN\"CAS:\" OR BLOAD)", 130);
            return 1;
        }
        ps3_toast_show("FAILED TO OPEN CASSETTE FILE", 110);
        return 0;
    }

    int cslot = (target_slot == 1) ? 1 : 0;
    if (LoadCart(full_path, cslot, ROMGUESS(cslot) | ROMTYPE(cslot))) {
        if (cslot == 0) {
            snprintf(g_cfg.carta_path, sizeof(g_cfg.carta_path), "%s", full_path);
            ROMName[0] = g_cfg.carta_path;
        } else {
            snprintf(g_cfg.cartb_path, sizeof(g_cfg.cartb_path), "%s", full_path);
            ROMName[1] = g_cfg.cartb_path;
        }
        ResetMSX(Mode, RAMPages, VRAMPages);
        ps3_config_save();
        ps3_toast_show(cslot == 0 ? "CARTRIDGE SLOT A LOADED" : "CARTRIDGE SLOT B LOADED", 110);
        return 1;
    }

    ps3_toast_show("INVALID ROM FILE OR READ ERROR", 110);
    return 0;
}

void ps3_menu_init(void)
{
    if (g_cfg.last_dir[0]) {
        snprintf(s_cur_dir, sizeof(s_cur_dir), "%s", g_cfg.last_dir);
    } else {
        struct stat st;
        if (stat(FMSX_ROMS_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(s_cur_dir, sizeof(s_cur_dir), "%s", FMSX_ROMS_DIR);
        } else {
            snprintf(s_cur_dir, sizeof(s_cur_dir), "%s", ROMS_USRDIR_PATH);
        }
    }
    scan_directory(s_cur_dir);
    if (s_entry_count <= 1 && strcmp(s_cur_dir, ROMS_USRDIR_PATH) != 0) {
        snprintf(s_cur_dir, sizeof(s_cur_dir), "%s", ROMS_USRDIR_PATH);
        scan_directory(s_cur_dir);
    }
    scan_game_library();
}

void ps3_menu_open(int mode)
{
    if (mode == MENU_BROWSER) {
        scan_directory(s_cur_dir);
    } else if (mode == MENU_LIBRARY) {
        if (s_lib_total == 0) scan_game_library();
    }
    s_thumb_loaded_slot = -1;
    g_menu_mode = mode;
}

static const Ps3BiosItem s_bios_catalog[8] = {
    { "MSX.ROM",      "MSX1 Base System BIOS",        "MSX1 Required", 1, 32768, 0, "", 0, 0 },
    { "MSX2.ROM",     "MSX2 Main System BIOS",        "MSX2 Required", 2, 32768, 0, "", 0, 0 },
    { "MSX2EXT.ROM",  "MSX2 Sub / Extended BIOS",     "MSX2 Required", 2, 16384, 0, "", 0, 0 },
    { "MSX2P.ROM",    "MSX2+ Main System BIOS",       "MSX2+ Required", 4, 32768, 0, "", 0, 0 },
    { "MSX2PEXT.ROM", "MSX2+ Sub / Extended BIOS",    "MSX2+ Required", 4, 16384, 0, "", 0, 0 },
    { "DISK.ROM",     "Floppy Disk Interface FDC",    "Disk Drives",   8, 16384, 0, "", 0, 0 },
    { "FMPAC.ROM",    "MSX-MUSIC / FM Sound BIOS",   "FM-PAC Audio",  16, 65536, 0, "", 0, 0 },
    { "KANJI.ROM",    "Japanese Font Hardware ROM",  "Kanji Support", 32, 131072, 0, "", 0, 0 }
};

static int is_file_cbios(const char *path)
{
    if (!path) return 0;
    if (strstr(path, "cbios") != NULL || strstr(path, "CBIOS") != NULL) return 1;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n >= 32) {
        for (size_t i = 0; i + 5 < n; i++) {
            if ((buf[i] == 'C' || buf[i] == 'c') &&
                buf[i+1] == '-' &&
                (buf[i+2] == 'B' || buf[i+2] == 'b') &&
                (buf[i+3] == 'I' || buf[i+3] == 'i') &&
                (buf[i+4] == 'O' || buf[i+4] == 'o') &&
                (buf[i+5] == 'S' || buf[i+5] == 's')) {
                return 1;
            }
        }
    }
    return 0;
}

static const char *get_menu_cbios_fallback(const char *name)
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

int ps3_bios_check_all(Ps3BiosItem *items, int max_items)
{
    static const char * const search_dirs[] = {
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
    int s_num_search_dirs = (int)(sizeof(search_dirs) / sizeof(search_dirs[0]));

    int found_count = 0;
    int count = (max_items < 8) ? max_items : 8;
    for (int i = 0; i < count; i++) {
        items[i] = s_bios_catalog[i];
        items[i].found = 0;
        items[i].found_path[0] = '\0';
        items[i].actual_size = 0;
        items[i].is_cbios = 0;

        for (int d = 0; d < s_num_search_dirs; d++) {
            char cand[512];
            snprintf(cand, sizeof(cand), "%s/%s", search_dirs[d], items[i].filename);
            struct stat st;
            if (stat(cand, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                items[i].found = 1;
                items[i].actual_size = (long)st.st_size;
                items[i].is_cbios = is_file_cbios(cand);
                snprintf(items[i].found_path, sizeof(items[i].found_path), "%s", cand);
                found_count++;
                break;
            }
            char lcase[64];
            snprintf(lcase, sizeof(lcase), "%s", items[i].filename);
            for (int c = 0; lcase[c]; c++) {
                if (lcase[c] >= 'A' && lcase[c] <= 'Z') lcase[c] += 32;
            }
            snprintf(cand, sizeof(cand), "%s/%s", search_dirs[d], lcase);
            if (stat(cand, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                items[i].found = 1;
                items[i].actual_size = (long)st.st_size;
                items[i].is_cbios = is_file_cbios(cand);
                snprintf(items[i].found_path, sizeof(items[i].found_path), "%s", cand);
                found_count++;
                break;
            }
        }

        if (!items[i].found) {
            const char *cb_name = get_menu_cbios_fallback(items[i].filename);
            if (cb_name) {
                for (int d = 0; d < s_num_search_dirs; d++) {
                    char cand[512];
                    snprintf(cand, sizeof(cand), "%s/%s", search_dirs[d], cb_name);
                    struct stat st;
                    if (stat(cand, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                        items[i].found = 1;
                        items[i].actual_size = (long)st.st_size;
                        items[i].is_cbios = 1;
                        snprintf(items[i].found_path, sizeof(items[i].found_path), "%s", cand);
                        found_count++;
                        break;
                    }
                }
            }
        }
    }
    return found_count;
}

int ps3_bios_is_model_ready(int model)
{
    Ps3BiosItem items[8];
    ps3_bios_check_all(items, 8);
    if (model == 0) {
        return items[0].found;
    } else if (model == 1) {
        return items[1].found && (items[2].found || items[1].actual_size >= 49152);
    } else if (model == 2) {
        return items[3].found && (items[4].found || items[3].actual_size >= 49152);
    }
    return 0;
}

int ps3_bios_has_oem_for_model(int model)
{
    Ps3BiosItem items[8];
    ps3_bios_check_all(items, 8);
    if (model == 0) {
        return items[0].found && !items[0].is_cbios;
    } else if (model == 1) {
        if (!items[1].found || items[1].is_cbios) return 0;
        if (items[1].actual_size >= 49152) return 1;
        return items[2].found && !items[2].is_cbios;
    } else if (model == 2) {
        if (!items[3].found || items[3].is_cbios) return 0;
        if (items[3].actual_size >= 49152) return 1;
        return items[4].found && !items[4].is_cbios;
    }
    return 0;
}

static int ps3_copy_file_simple(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

int ps3_bios_copy_from_usb(char *out_msg, size_t max_len)
{
    mkdir(FMSX_BASE_DIR, 0777);
    mkdir(FMSX_BIOS_DIR, 0777);
    mkdir(BIOS_USRDIR_PATH, 0777);

    int copied = 0;
    for (int i = 0; i < 8; i++) {
        const char *fn = s_bios_catalog[i].filename;
        char lcase[64];
        snprintf(lcase, sizeof(lcase), "%s", fn);
        for (int c = 0; lcase[c]; c++) {
            if (lcase[c] >= 'A' && lcase[c] <= 'Z') lcase[c] += 32;
        }

        int found_on_usb = 0;
        char usb_src[512];
        for (int u = 0; u < 8; u++) {
            char p1[512], p2[512], p3[512], p4[512];
            snprintf(p1, sizeof(p1), "/dev_usb%03d/bios/%s", u, fn);
            snprintf(p2, sizeof(p2), "/dev_usb%03d/%s", u, fn);
            snprintf(p3, sizeof(p3), "/dev_usb%03d/bios/%s", u, lcase);
            snprintf(p4, sizeof(p4), "/dev_usb%03d/%s", u, lcase);
            struct stat st;
            if (stat(p1, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                snprintf(usb_src, sizeof(usb_src), "%s", p1);
                found_on_usb = 1;
                break;
            }
            if (stat(p2, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                snprintf(usb_src, sizeof(usb_src), "%s", p2);
                found_on_usb = 1;
                break;
            }
            if (stat(p3, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                snprintf(usb_src, sizeof(usb_src), "%s", p3);
                found_on_usb = 1;
                break;
            }
            if (stat(p4, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
                snprintf(usb_src, sizeof(usb_src), "%s", p4);
                found_on_usb = 1;
                break;
            }
        }

        if (found_on_usb) {
            char dst1[512];
            snprintf(dst1, sizeof(dst1), "%s/%s", FMSX_BIOS_DIR, fn);
            if (ps3_copy_file_simple(usb_src, dst1)) {
                copied++;
                char dst2[512];
                snprintf(dst2, sizeof(dst2), "%s/%s", BIOS_USRDIR_PATH, fn);
                ps3_copy_file_simple(usb_src, dst2);
            }
        }
    }

    if (out_msg && max_len > 0) {
        if (copied > 0) {
            snprintf(out_msg, max_len, "COPIED %d BIOS ROMS FROM USB TO /dev_hdd0/fmsx/bios/", copied);
        } else {
            snprintf(out_msg, max_len, "NO BIOS ROMS DETECTED ON ANY USB DRIVE (/dev_usb000..007)");
        }
    }
    return copied;
}

int ps3_bios_show_dialog(int allow_cancel)
{
    Ps3BiosItem items[8];
    uint32_t last_buttons = 0;

    for (;;) {
        ps3_bios_check_all(items, 8);
        int ready = ps3_bios_is_model_ready(g_cfg.msx_model);

        uint32_t buttons = ps3_input_poll(0);
        uint32_t pressed = buttons & ~last_buttons;
        last_buttons = buttons;

        if (pressed & PS3_BTN_TRIANGLE) {
            char msg[128];
            int n = ps3_bios_copy_from_usb(msg, sizeof(msg));
            ps3_toast_show(msg, 120);
            ps3_bios_check_all(items, 8);
        }

        if (pressed & PS3_BTN_SQUARE) {
            g_cfg.msx_model = (g_cfg.msx_model + 1) % 3;
            ps3_apply_machine_config(0);
            ps3_config_save();
            char mmsg[64];
            snprintf(mmsg, sizeof(mmsg), "SWITCHED ACTIVE MODEL: %s",
                     (g_cfg.msx_model == 0) ? "MSX1" : (g_cfg.msx_model == 1) ? "MSX2" : "MSX2+");
            ps3_toast_show(mmsg, 90);
        }

        if (pressed & (PS3_BTN_CROSS | PS3_BTN_START)) {
            if (ready) {
                ps3_toast_show("BIOS VERIFIED - STARTING EMULATION", 60);
                return 1;
            } else {
                ps3_toast_show("CANNOT START: REQUIRED BIOS FILES ARE MISSING", 100);
            }
        }

        if (pressed & PS3_BTN_SELECT) {
            ps3_menu_open(MENU_BROWSER);
            ps3_menu_run_loop();
        }

        if (pressed & PS3_BTN_CIRCLE) {
            if (allow_cancel) {
                return 0;
            } else {
                g_app_running = 0;
                return 0;
            }
        }

        memset(g_framebuf, 0, SCREEN_W * SCREEN_H * sizeof(uint32_t));
        ps3_draw_panel(g_framebuf, 0, 0, SCREEN_W, SCREEN_H, 0xFF0B1424U, 0xFF050A14U, 0xFF14243CU, 255);

        int has_oem = ps3_bios_has_oem_for_model(g_cfg.msx_model);
        ps3_draw_panel(g_framebuf, 24, 18, SCREEN_W - 48, 68, 0xFF2A1014U, 0xFF18080CU, has_oem ? 0xFF285084U : 0xFFFF3B30U, 245);
        ps3_draw_rect(g_framebuf, 28, 22, 10, 60, has_oem ? 0xFF3080D8U : 0xFFFF3B30U);
        ps3_draw_text_shadow(g_framebuf, 52, 28, has_oem ? "MSX SYSTEM FIRMWARE STATUS" : "MSX OEM BIOS MISSING (C-BIOS ACTIVE)", has_oem ? 0xFF00E5FFU : 0xFFFF453AU, 0xFF000000U, 3);
        ps3_draw_text_shadow(g_framebuf, 52, 58, has_oem ? "ALL REQUIRED OEM FIRMWARE ROMS DETECTED" : "GENUINE OEM ROMS ARE MISSING - RUNNING COMPATIBLE OPEN SOURCE C-BIOS", 0xFFE0E0E0U, 0xFF000000U, 1);

        const char *mname = (g_cfg.msx_model == 0) ? "MSX1" : (g_cfg.msx_model == 1) ? "MSX2" : "MSX2+";
        char mbadge[64];
        snprintf(mbadge, sizeof(mbadge), "ACTIVE MODEL: %s", mname);
        ps3_draw_badge(g_framebuf, SCREEN_W - 240, 36, mbadge, 0xFF1E3A68U, 0xFFFFD840U);

        int left_x = 24, left_y = 96, left_w = 600, left_h = 556;
        ps3_draw_panel(g_framebuf, left_x, left_y, left_w, left_h, 0xFF101B2EU, 0xFF080E18U, 0xFF285084U, 240);
        ps3_draw_text_shadow(g_framebuf, left_x + 16, left_y + 14, "SYSTEM BIOS CHECKLIST (8 STANDARD ROMS)", 0xFF00E5FFU, 0xFF000000U, 2);
        ps3_draw_rect(g_framebuf, left_x + 16, left_y + 36, left_w - 32, 2, 0xFF244874U);

        int row_y = left_y + 46;
        for (int i = 0; i < 8; i++) {
            int is_req = 0;
            if (g_cfg.msx_model == 0 && (items[i].model_mask & 1)) is_req = 1;
            if (g_cfg.msx_model == 1 && (items[i].model_mask & 2)) is_req = 1;
            if (g_cfg.msx_model == 2 && (items[i].model_mask & 4)) is_req = 1;

            if (is_req) {
                ps3_draw_rect_alpha(g_framebuf, left_x + 12, row_y, left_w - 24, 56, 0xFF1E3C64U, 210);
                ps3_draw_border(g_framebuf, left_x + 12, row_y, left_w - 24, 56, 1, 0xFF4A90E2U);
                ps3_draw_rect(g_framebuf, left_x + 14, row_y + 4, 4, 48, 0xFFFFD840U);
            } else {
                ps3_draw_rect_alpha(g_framebuf, left_x + 12, row_y, left_w - 24, 56, 0xFF0D1728U, 170);
                ps3_draw_border(g_framebuf, left_x + 12, row_y, left_w - 24, 56, 1, 0xFF1C3454U);
            }

            uint32_t title_col = items[i].found ? 0xFFFFFFFFU : (is_req ? 0xFFFF6050U : 0xFFB0B0B0U);
            ps3_draw_text_shadow(g_framebuf, left_x + 24, row_y + 8, items[i].filename, title_col, 0xFF000000U, 2);

            char sub[128];
            snprintf(sub, sizeof(sub), "%s (%d KB) - %s", items[i].desc, items[i].expected_size / 1024, items[i].machine);
            ps3_draw_text_clipped(g_framebuf, left_x + 24, row_y + 32, left_w - 170, sub, 0xFF90B0D0U, 1);

            if (items[i].found) {
                if (items[i].is_cbios) {
                    ps3_draw_badge(g_framebuf, left_x + left_w - 130, row_y + 14, "C-BIOS BSD", 0xFFE65100U, 0xFFFFD54FU);
                } else {
                    ps3_draw_badge(g_framebuf, left_x + left_w - 130, row_y + 14, "OEM ORIGINAL", 0xFF1B5E20U, 0xFF76FF03U);
                }
            } else if (is_req) {
                ps3_draw_badge(g_framebuf, left_x + left_w - 130, row_y + 14, "MISSING (REQ)", 0xFFB71C1CU, 0xFFFF8A80U);
            } else {
                ps3_draw_badge(g_framebuf, left_x + left_w - 130, row_y + 14, "OPTIONAL", 0xFF37474FU, 0xFFCFD8DCU);
            }

            row_y += 62;
        }

        int right_x = 636, right_y = 96, right_w = SCREEN_W - right_x - 24, right_h = 556;
        ps3_draw_panel(g_framebuf, right_x, right_y, right_w, right_h, 0xFF101B2EU, 0xFF080E18U, 0xFF285084U, 240);
        ps3_draw_text_shadow(g_framebuf, right_x + 16, right_y + 14, "WHERE TO PLACE BIOS FILES MANUALLY", 0xFFFFD840U, 0xFF000000U, 2);
        ps3_draw_rect(g_framebuf, right_x + 16, right_y + 36, right_w - 32, 2, 0xFF244874U);

        int cy = right_y + 46;

        ps3_draw_panel(g_framebuf, right_x + 14, cy, right_w - 28, 114, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 10, "1. PS3 INTERNAL HARD DRIVE PATHS (FTP / MULTIMAN)", 0xFF00E5FFU, 0xFF000000U, 2);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 34, "Primary Path:", 0xFF80D8FFU, 0xFF000000U, 1);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 48, right_w - 52, "/dev_hdd0/game/FMSX00001/USRDIR/bios/", 0xFFFFFFFFU, 2);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 74, "Secondary Path:", 0xFF80D8FFU, 0xFF000000U, 1);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 88, right_w - 52, "/dev_hdd0/fmsx/bios/", 0xFF80FFAAU, 2);
        cy += 124;

        ps3_draw_panel(g_framebuf, right_x + 14, cy, right_w - 28, 114, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 10, "2. EXTERNAL USB DRIVE PATHS (FAT32)", 0xFF00E5FFU, 0xFF000000U, 2);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 34, "USB Directory Path:", 0xFF80D8FFU, 0xFF000000U, 1);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 48, right_w - 52, "/dev_usb000/bios/  (or root /dev_usb000/)", 0xFFFFFFFFU, 2);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 74, "USB Compatibility:", 0xFF80D8FFU, 0xFF000000U, 1);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 88, right_w - 52, "Supports all USB ports from /dev_usb000 to /dev_usb007", 0xFFFFD840U, 1);
        cy += 124;

        ps3_draw_panel(g_framebuf, right_x + 14, cy, right_w - 28, 126, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 10, "3. ONE-CLICK USB AUTO-IMPORT FEATURE", 0xFF00E5FFU, 0xFF000000U, 2);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 34, right_w - 52, "Place your BIOS files on any FAT32 USB flash drive", 0xFFD0E4FFU, 1);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 50, right_w - 52, "in either a /bios/ folder or directly in the root directory.", 0xFFD0E4FFU, 1);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 66, right_w - 52, "Plug the USB drive into your PS3 and press [TRIANGLE].", 0xFF80FFAAU, 2);
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 92, right_w - 52, "The emulator will automatically import all BIOS ROMs to HDD!", 0xFFFFE040U, 1);
        cy += 136;

        ps3_draw_panel(g_framebuf, right_x + 14, cy, right_w - 28, 114, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
        ps3_draw_text_shadow(g_framebuf, right_x + 26, cy + 10, "4. ACTIVE MACHINE STATUS", 0xFF00E5FFU, 0xFF000000U, 2);

        char stat_line[128];
        snprintf(stat_line, sizeof(stat_line), "Current Machine: %s",
                 (g_cfg.msx_model == 0) ? "MSX1 (requires MSX.ROM)" :
                 (g_cfg.msx_model == 1) ? "MSX2 (requires MSX2.ROM + MSX2EXT.ROM)" :
                 "MSX2+ (requires MSX2P.ROM + MSX2PEXT.ROM)");
        ps3_draw_text_clipped(g_framebuf, right_x + 26, cy + 36, right_w - 52, stat_line, 0xFFFFFFFFU, 2);

        if (has_oem) {
            ps3_draw_badge(g_framebuf, right_x + 26, cy + 68, "OEM BIOS ACTIVE - PRESS [CROSS] TO LAUNCH", 0xFF1B5E20U, 0xFF76FF03U);
        } else if (ready) {
            ps3_draw_badge(g_framebuf, right_x + 26, cy + 68, "OEM BIOS MISSING - C-BIOS ACTIVE - [CROSS] TO CONTINUE", 0xFFE65100U, 0xFFFFD54FU);
        } else {
            ps3_draw_badge(g_framebuf, right_x + 26, cy + 68, "BLOCKED: PLEASE INSTALL REQUIRED BIOS OR SWITCH MODEL WITH [SQUARE]", 0xFFB71C1CU, 0xFFFF8A80U);
        }

        ps3_draw_panel(g_framebuf, 24, 644, SCREEN_W - 48, 38, 0xFF10203EU, 0xFF081020U, 0xFF2C78C8U, 238);
        int fx = 42, fy = 651;
        fx += ps3_draw_btn_label(g_framebuf, fx, fy, BTN_ICON_TRIANGLE, "Copy from USB", 0xFFFFD840U, 1) + 24;
        const char *cross_msg = !ready ? "Retry Check" : (has_oem ? "Launch MSX" : "Continue with C-BIOS");
        fx += ps3_draw_btn_label(g_framebuf, fx, fy, BTN_ICON_CROSS,    cross_msg, ready ? 0xFF80FFAAU : 0xFFFFFFFFU, 1) + 24;
        fx += ps3_draw_btn_label(g_framebuf, fx, fy, BTN_ICON_SQUARE,   "Switch Model", 0xFF80D8FFU, 1) + 24;
        fx += ps3_draw_btn_label(g_framebuf, fx, fy, BTN_ICON_SELECT,   "File Browser", 0xFFE0F0FFU, 1) + 24;
        ps3_draw_btn_label(g_framebuf, fx, fy, BTN_ICON_CIRCLE,         allow_cancel ? "Back" : "Exit to XMB", 0xFFE0F0FFU, 1);

        ps3_toast_draw(g_framebuf);
        ps3_present_framebuf();
        usleep(16000);
    }
}

static void draw_top_header(uint32_t *fb, const char *subtitle)
{
    ps3_draw_panel(fb, 24, 24, SCREEN_W - 48, 54, 0xFF142A52U, 0xFF0B162CU, 0xFF3CA0FFU, 240);
    ps3_draw_rect(fb, 28, 28, 8, 46, 0xFF00E5FFU);
    ps3_draw_text_shadow(fb, 48, 29, "MSX4PS3", 0xFF00E5FFU, 0xFF000000U, 3);
    ps3_draw_text_shadow(fb, 200, 34, "v1.00", 0xFFFFD84CU, 0xFF000000U, 2);
    ps3_draw_text_shadow(fb, 276, 36, "PS3 PORT by theheroGAC", 0xFF90E0FFU, 0xFF000000U, 1);

    const char *mname = (g_cfg.msx_model == 0) ? "MSX1" : (g_cfg.msx_model == 1) ? "MSX2" : "MSX2+";
    const char *vname = (g_cfg.video_region == 0) ? "NTSC 60Hz" : "PAL 50Hz";
    char mach_str[64];
    snprintf(mach_str, sizeof(mach_str), "[%s | %s | %dKB RAM]", mname, vname, g_cfg.ram_pages * 16);
    int mw = ps3_text_width(mach_str, 1);
    ps3_draw_text_shadow(fb, SCREEN_W - mw - 44, 34, mach_str, 0xFF90E0FFU, 0xFF000000U, 1);

    ps3_draw_text_clipped(fb, 48, 54, 520, subtitle, 0xFFD0E4FFU, 1);

    char gname[64];
    ps3_get_active_game_name(gname, sizeof(gname));
    char act_str[128];
    snprintf(act_str, sizeof(act_str), "ACTIVE: %s", gname);
    int max_aw = 620;
    int aw = ps3_text_width(act_str, 1);
    if (aw > max_aw) aw = max_aw;
    ps3_draw_text_clipped(fb, SCREEN_W - aw - 44, 54, aw, act_str, 0xFF80FFAAU, 1);

    ps3_draw_tab_bar(fb, s_active_tab);
}

static void draw_footer_bar(uint32_t *fb)
{
    ps3_draw_panel(fb, 24, 644, SCREEN_W - 48, 38, 0xFF10203EU, 0xFF081020U, 0xFF2C78C8U, 238);
    int fx = 38, fy = 651;
    fx += ps3_draw_btn_label(fb, fx, fy, BTN_ICON_L1,       "Tab",        0xFFFFD840U, 1) + 12;
    fx += ps3_draw_btn_label(fb, fx, fy, BTN_ICON_R1,       "Switch",     0xFFFFD840U, 1) + 24;
    fx += ps3_draw_btn_label(fb, fx, fy, BTN_ICON_DPAD,     "Navigate",   0xFFE0F0FFU, 1) + 24;

    const char *cross_lbl = "Select";
    const char *sqr_lbl   = "Action";
    const char *tri_lbl   = "Option";

    if (s_active_tab == TAB_LIBRARY) {
        cross_lbl = "Launch";
        sqr_lbl   = "Filter";
        tri_lbl   = "Rescan";
    } else if (s_active_tab == TAB_SLOTS) {
        cross_lbl = "Insert";
        sqr_lbl   = "Eject";
        tri_lbl   = "Browse";
    } else if (s_active_tab == TAB_FILES) {
        cross_lbl = "Open";
        sqr_lbl   = "Parent";
        tri_lbl   = "Refresh";
    } else if (s_active_tab == TAB_STATES) {
        cross_lbl = "Load";
        sqr_lbl   = "Save";
        tri_lbl   = "Delete";
    } else if (s_active_tab == TAB_MACHINE || s_active_tab == TAB_DISPLAY) {
        cross_lbl = "Toggle";
        sqr_lbl   = "Default";
        tri_lbl   = "Reset";
    }

    fx += ps3_draw_btn_label(fb, fx, fy, BTN_ICON_CROSS,    cross_lbl,    0xFF80FFAAU, 1) + 24;
    fx += ps3_draw_btn_label(fb, fx, fy, BTN_ICON_SQUARE,   sqr_lbl,      0xFFFFD840U, 1) + 24;
    fx += ps3_draw_btn_label(fb, fx, fy, BTN_ICON_TRIANGLE, tri_lbl,      0xFF80D8FFU, 1) + 24;
    ps3_draw_btn_label(fb, fx, fy, BTN_ICON_CIRCLE,         "Resume MSX", 0xFFE0F0FFU, 1);
}

static void draw_tab_library(uint32_t *fb)
{
    draw_top_header(fb, "GAME LIBRARY - BROWSE ROMS, DISKS, CASSETTES ON HDD AND USB0..7");

    int list_x = 24, list_y = 124, list_w = 584, list_h = 532;
    ps3_draw_panel(fb, list_x, list_y, list_w, list_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int img_x = 618, img_y = 124, img_w = 310, img_h = 290;
    ps3_draw_panel(fb, img_x, img_y, img_w, img_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int inf_x = 938, inf_y = 124, inf_w = 318, img_h2 = 290;
    ps3_draw_panel(fb, inf_x, inf_y, inf_w, img_h2, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int syn_x = 618, syn_y = 422, syn_w = 638, syn_h = 234;
    ps3_draw_panel(fb, syn_x, syn_y, syn_w, syn_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    static const char *cat_names[4] = { "ALL GAMES", "CARTRIDGES (.ROM)", "FLOPPY DISKS (.DSK)", "CASSETTES (.CAS)" };
    char cat_str[128];
    snprintf(cat_str, sizeof(cat_str), "CATEGORY: %s  (%d Games)", cat_names[s_lib_filter_cat % 4], s_lib_filtered_count);
    ps3_draw_text_shadow(fb, list_x + 14, list_y + 12, cat_str, 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, list_x + 14, list_y + 34, list_w - 28, 2, 0xFF2C78C8U);

    int visible_rows = 12;
    if (s_lib_sel < s_lib_scroll) s_lib_scroll = s_lib_sel;
    if (s_lib_sel >= s_lib_scroll + visible_rows) s_lib_scroll = s_lib_sel - visible_rows + 1;

    if (s_lib_filtered_count == 0) {
        ps3_draw_text_centered(fb, list_x + list_w / 2, list_y + 190, "NO GAMES FOUND IN THIS CATEGORY", 0xFFFFD84CU, 2);
        ps3_draw_text_centered(fb, list_x + list_w / 2, list_y + 224, "Copy MSX ROMs/DSK/CAS to /dev_hdd0/fmsx/roms", 0xFF80FFAAU, 2);
        ps3_draw_text_centered(fb, list_x + list_w / 2, list_y + 254, "or to any FAT32 USB drive (/dev_usb000..007)", 0xFFFFFFFFU, 2);
        ps3_draw_text_centered(fb, list_x + list_w / 2, list_y + 286, "Press [TRIANGLE] to re-scan HDD and USB", 0xFF00E5FFU, 2);
        ps3_draw_text_centered(fb, list_x + list_w / 2, list_y + 316, "Press [SQUARE] to change category filter", 0xFFD0E4FFU, 2);
    } else {
        for (int row = 0; row < visible_rows; row++) {
            int fidx = s_lib_scroll + row;
            if (fidx >= s_lib_filtered_count) break;
            int gidx = s_lib_filtered[fidx];
            LibraryGame *g = &s_lib_games[gidx];
            int ry = list_y + 44 + row * 40;
            int sel = (fidx == s_lib_sel);
            if (sel) {
                ps3_draw_bevel_box(fb, list_x + 8, ry, list_w - 16, 36, 0xFF1E58A8U, 0xFF60B8FFU, 0xFF0E2C58U, 0xFFFFFFFFU, 245);
                ps3_draw_rect(fb, list_x + 12, ry + 4, 6, 28, 0xFFFFD840U);
            } else {
                ps3_draw_rect_alpha(fb, list_x + 8, ry, list_w - 16, 36, 0xFF10203CU, 175);
                ps3_draw_border(fb, list_x + 8, ry, list_w - 16, 36, 1, 0xFF1E3A68U);
            }

            const char *badge = (g->media_type == LIB_MEDIA_DISK) ? "DSK" :
                                (g->media_type == LIB_MEDIA_TAPE) ? "CAS" : "ROM";
            uint32_t bcol = (g->media_type == LIB_MEDIA_DISK) ? 0xFF00A2E8U :
                            (g->media_type == LIB_MEDIA_TAPE) ? 0xFF22B14CU : 0xFFFF7F27U;
            ps3_draw_badge(fb, list_x + 22, ry + 8, badge, bcol, 0xFFFFFFFFU);

            int src_bw = ps3_text_width(g->source_label, 0) + 16;
            int src_bx = list_x + list_w - src_bw - 18;
            ps3_draw_badge(fb, src_bx, ry + 8, g->source_label, 0xFF1A3554U, 0xFF80FFAAU);

            int title_max_w = src_bx - (list_x + 72) - 10;
            if (title_max_w < 50) title_max_w = 50;
            ps3_draw_text_clipped(fb, list_x + 72, ry + 9, title_max_w, g->title, sel ? 0xFFFFFFFFU : 0xFFC8DCF4U, 1);
        }
    }

    if (s_lib_filtered_count > 0 && s_lib_sel >= 0 && s_lib_sel < s_lib_filtered_count) {
        int cgidx = s_lib_filtered[s_lib_sel];
        LibraryGame *sg = &s_lib_games[cgidx];
        draw_stylized_media_card(fb, img_x + 18, img_y + 36, img_w - 36, img_h - 48, sg);

        ps3_draw_text_shadow(fb, inf_x + 16, inf_y + 12, "GAME INFORMATION", 0xFFFFE040U, 0xFF000000U, 2);
        ps3_draw_rect(fb, inf_x + 16, inf_y + 34, inf_w - 32, 2, 0xFF2C78C8U);

        const char *sys_str = has_ext_ci(sg->filename, ".mx2") ? "MSX2 / MSX2+" : "MSX1 / MSX2 / MSX2+";
        const char *med_str = (sg->media_type == LIB_MEDIA_DISK) ? "3.5\" Floppy Disk" :
                              (sg->media_type == LIB_MEDIA_TAPE) ? "Cassette Tape" : "ROM Cartridge";
        const char *map_str = (sg->media_type == LIB_MEDIA_DISK) ? "WD1793 Disk FDC" :
                              (sg->media_type == LIB_MEDIA_TAPE) ? "BIOS Tape Loader" :
                              (sg->size > 32768) ? "Auto MegaROM / SCC" : "Standard 16K/32K";

        char iline[96];
        ps3_draw_text_shadow(fb, inf_x + 16, inf_y + 44, "SYSTEM:", 0xFF80D8FFU, 0xFF000000U, 0);
        ps3_draw_text_clipped(fb, inf_x + 16, inf_y + 59, inf_w - 32, sys_str, 0xFFFFFFFFU, 1);

        ps3_draw_text_shadow(fb, inf_x + 16, inf_y + 86, "MEDIA FORMAT:", 0xFF80D8FFU, 0xFF000000U, 0);
        ps3_draw_text_clipped(fb, inf_x + 16, inf_y + 101, inf_w - 32, med_str, 0xFFFFFFFFU, 1);

        ps3_draw_text_shadow(fb, inf_x + 16, inf_y + 128, "MAPPER / CONTROLLER:", 0xFF80D8FFU, 0xFF000000U, 0);
        ps3_draw_text_clipped(fb, inf_x + 16, inf_y + 143, inf_w - 32, map_str, 0xFF80FFAAU, 1);

        snprintf(iline, sizeof(iline), "%ld KB (%ld Bytes)", (sg->size + 1023) / 1024, sg->size);
        ps3_draw_text_shadow(fb, inf_x + 16, inf_y + 170, "FILE SIZE:", 0xFF80D8FFU, 0xFF000000U, 0);
        ps3_draw_text_clipped(fb, inf_x + 16, inf_y + 185, inf_w - 32, iline, 0xFFFFFFFFU, 1);

        int n_states = 0;
        for (int st = 0; st < 10; st++) if (ps3_state_slot_exists(st)) n_states++;
        snprintf(iline, sizeof(iline), "%d / 10 Slots Saved", n_states);
        ps3_draw_text_shadow(fb, inf_x + 16, inf_y + 212, "SAVED STATES:", 0xFF80D8FFU, 0xFF000000U, 0);
        ps3_draw_text_clipped(fb, inf_x + 16, inf_y + 227, inf_w - 32, iline, n_states > 0 ? 0xFFFFD840U : 0xFFA0B8D0U, 1);

        ps3_draw_badge(fb, inf_x + 16, inf_y + 254, sg->source_label, 0xFF142C4EU, 0xFF90E0FFU);

        ps3_draw_text_clipped(fb, syn_x + 18, syn_y + 12, syn_w - 36, sg->title, 0xFFFFE040U, 2);
        ps3_draw_rect(fb, syn_x + 18, syn_y + 34, syn_w - 36, 2, 0xFF2C78C8U);
        ps3_draw_text_clipped(fb, syn_x + 18, syn_y + 44, syn_w - 36, sg->full_path, 0xFF98C0E8U, 1);

        ps3_draw_btn_label(fb, syn_x + 18,  syn_y + 68,  BTN_ICON_CROSS,    "Launch Game (Auto-Boot)",       0xFFFFFFFFU, 1);
        ps3_draw_btn_label(fb, syn_x + 18,  syn_y + 100, BTN_ICON_SQUARE,   "Switch Category Filter",        0xFFFFD840U, 1);
        ps3_draw_btn_label(fb, syn_x + 18,  syn_y + 132, BTN_ICON_TRIANGLE, "Rescan HDD & USB Storage",      0xFF80FFAAU, 1);
        ps3_draw_btn_label(fb, syn_x + 18,  syn_y + 164, BTN_ICON_SELECT,   "Open Directory File Browser",   0xFF90E0FFU, 1);
        ps3_draw_btn_label(fb, syn_x + 360, syn_y + 164, BTN_ICON_CIRCLE,   "Resume Current MSX Session",    0xFFD8E8FFU, 1);
    }

    draw_footer_bar(fb);
}

static int get_dsk_swap_list(char dsk_paths[32][512], char dsk_names[32][64], int *cur_idx)
{
    *cur_idx = -1;
    if (!g_cfg.dska_path[0]) return 0;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", g_cfg.dska_path);
    char *slash = strrchr(dir, '/');
    if (!slash) return 0;
    *slash = '\0';
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    int count = 0;
    while ((de = readdir(d)) != NULL && count < 32) {
        if (de->d_name[0] == '.') continue;
        int len = strlen(de->d_name);
        if (len > 4 && (strcasecmp(de->d_name + len - 4, ".dsk") == 0 ||
                        strcasecmp(de->d_name + len - 4, ".fdi") == 0)) {
            snprintf(dsk_names[count], 64, "%s", de->d_name);
            snprintf(dsk_paths[count], 512, "%s/%s", dir, de->d_name);
            count++;
        }
    }
    closedir(d);
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcasecmp(dsk_names[i], dsk_names[j]) > 0) {
                char tn[64], tp[512];
                strcpy(tn, dsk_names[i]); strcpy(dsk_names[i], dsk_names[j]); strcpy(dsk_names[j], tn);
                strcpy(tp, dsk_paths[i]); strcpy(dsk_paths[i], dsk_paths[j]); strcpy(dsk_paths[j], tp);
            }
        }
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(dsk_paths[i], g_cfg.dska_path) == 0) {
            *cur_idx = i;
            break;
        }
    }
    return count;
}

static void draw_tab_slots(uint32_t *fb)
{
    draw_top_header(fb, "SLOTS & MEDIA MANAGER - CARTRIDGES, FLOPPY DISKS & CASSETTES");

    int left_x = 24, left_y = 124, left_w = 750, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 790, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    ps3_draw_text_shadow(fb, left_x + 16, left_y + 12, "MSX MEDIA DRIVE & CARTRIDGE SLOTS", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    static const char *slot_titles[10] = {
        "Cartridge Slot A (Primary ROM)",
        "Cartridge Slot B (Secondary / FM-PAC)",
        "Floppy Disk Drive A: (3.5\" DSK)",
        "Floppy Disk Drive B: (3.5\" DSK)",
        "Quick Multi-Disk Swap (Drive A:)",
        "Cassette Tape Player (CAS)",
        "Rewind Cassette Tape to Beginning",
        "Auto-Type BASIC Command: RUN\"CAS:\"",
        "Auto-Type Binary Command: BLOAD\"CAS:\",R",
        "Create Formatted 720KB Save Disk in B:"
    };

    const char *ca = g_cfg.carta_path[0] ? (strrchr(g_cfg.carta_path, '/') ? strrchr(g_cfg.carta_path, '/') + 1 : g_cfg.carta_path) : "[EMPTY]";
    const char *cb = g_cfg.cartb_path[0] ? (strrchr(g_cfg.cartb_path, '/') ? strrchr(g_cfg.cartb_path, '/') + 1 : g_cfg.cartb_path) : "[EMPTY]";
    const char *da = g_cfg.dska_path[0]  ? (strrchr(g_cfg.dska_path, '/')  ? strrchr(g_cfg.dska_path, '/') + 1  : g_cfg.dska_path)  : "[EMPTY]";
    const char *db = g_cfg.dskb_path[0]  ? (strrchr(g_cfg.dskb_path, '/')  ? strrchr(g_cfg.dskb_path, '/') + 1  : g_cfg.dskb_path)  : "[EMPTY]";
    const char *cs = g_cfg.cas_path[0]   ? (strrchr(g_cfg.cas_path, '/')   ? strrchr(g_cfg.cas_path, '/') + 1   : g_cfg.cas_path)   : "[EMPTY]";

    char dsk_paths[32][512];
    char dsk_names[32][64];
    int cur_dsk_idx = -1;
    int dsk_count = get_dsk_swap_list(dsk_paths, dsk_names, &cur_dsk_idx);
    char swap_val[96];
    if (dsk_count > 0 && cur_dsk_idx >= 0) {
        snprintf(swap_val, sizeof(swap_val), "< %d/%d: %s >", cur_dsk_idx + 1, dsk_count, dsk_names[cur_dsk_idx]);
    } else if (dsk_count > 0) {
        snprintf(swap_val, sizeof(swap_val), "< 1/%d: %s >", dsk_count, dsk_names[0]);
    } else {
        snprintf(swap_val, sizeof(swap_val), "[No DSK In Dir]");
    }

    const char *slot_vals[10] = { ca, cb, da, db, swap_val, cs, "Action", "Command", "Command", "Create" };

    for (int i = 0; i < 10; i++) {
        int ry = left_y + 40 + i * 48;
        int sel = (i == s_slots_sel);
        ps3_draw_selector_row(fb, left_x + 14, ry, left_w - 28, 44, slot_titles[i], slot_vals[i], sel);
    }

    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, "MEDIA CONTROLS GUIDE", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    ps3_draw_btn_label(fb, right_x + 16, right_y + 48,  BTN_ICON_CROSS,    "Insert Media / Execute Action", 0xFFFFFFFFU, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 78,  BTN_ICON_LEFT,     "Previous Multi-Disk (Drive A:)", 0xFF00E5FFU, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 108, BTN_ICON_RIGHT,    "Next Multi-Disk (Drive A:)", 0xFF00E5FFU, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 138, BTN_ICON_SQUARE,   "Eject Media from Selected Slot", 0xFFFF7F27U, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 168, BTN_ICON_TRIANGLE, "Reset MSX Machine Core", 0xFFFFD840U, 2);

    ps3_draw_panel(fb, right_x + 16, right_y + 204, right_w - 32, 300, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, right_x + 28, right_y + 216, "MULTI-DISK & SAVE DISK SUPPORT", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 28, right_y + 238, right_w - 56, 1, 0xFF2C78C8U);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 248, "1. Quick Multi-Disk Swapper:", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 270, right_w - 56, "Press LEFT/RIGHT to swap disk on-the-fly without reboot", 0xFFD8E8FFU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 296, "2. Formatted 720KB Save Disk:", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 318, right_w - 56, "Auto-creates standard FAT12 MSX-DOS 720KB disk in B:", 0xFF80FFAAU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 344, "3. In-Game Save Persistence:", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 366, right_w - 56, "Saves from games like Snatcher/Metal Gear persist to HDD", 0xFFD8E8FFU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 392, "4. Fast Disk Emulation:", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 414, right_w - 56, "Instant sector access eliminates floppy wait times", 0xFF00E5FFU, 1);

    draw_footer_bar(fb);
}

static void draw_tab_files(uint32_t *fb)
{
    draw_top_header(fb, "FILE BROWSER - DIRECTORY NAVIGATION ON HDD AND USB STICKS");

    int left_x = 24, left_y = 124, left_w = 760, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 800, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    char dir_hdr[512];
    snprintf(dir_hdr, sizeof(dir_hdr), "CURRENT DIRECTORY: %s", s_cur_dir);
    ps3_draw_text_clipped(fb, left_x + 16, left_y + 12, left_w - 32, dir_hdr, 0xFFFFD840U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    int visible_rows = 12;
    if (s_browser_sel < s_browser_scroll) s_browser_scroll = s_browser_sel;
    if (s_browser_sel >= s_browser_scroll + visible_rows) s_browser_scroll = s_browser_sel - visible_rows + 1;

    for (int row = 0; row < visible_rows; row++) {
        int idx = s_browser_scroll + row;
        if (idx >= s_entry_count) break;
        BrowserEntry *e = &s_entries[idx];
        int ry = left_y + 44 + row * 40;
        int sel = (idx == s_browser_sel);

        if (sel) {
            ps3_draw_bevel_box(fb, left_x + 8, ry, left_w - 16, 36, 0xFF1E58A8U, 0xFF60B8FFU, 0xFF0E2C58U, 0xFFFFFFFFU, 245);
            ps3_draw_rect(fb, left_x + 12, ry + 4, 6, 28, 0xFFFFD840U);
        } else {
            ps3_draw_rect_alpha(fb, left_x + 8, ry, left_w - 16, 36, 0xFF10203CU, 175);
            ps3_draw_border(fb, left_x + 8, ry, left_w - 16, 36, 1, 0xFF1E3A68U);
        }

        if (e->is_dir) {
            ps3_draw_badge(fb, left_x + 22, ry + 8, "DIR", 0xFF007ACCU, 0xFFFFFFFFU);
        } else {
            const char *ext = strrchr(e->name, '.');
            const char *bdg = ext ? (ext + 1) : "FILE";
            ps3_draw_badge(fb, left_x + 22, ry + 8, bdg, 0xFF3A5A82U, 0xFFFFFFFFU);
        }

        ps3_draw_text_clipped(fb, left_x + 80, ry + 9, left_w - 220, e->name, sel ? 0xFFFFFFFFU : 0xFFC8DCF4U, 1);

        if (!e->is_dir) {
            char sz_str[32];
            snprintf(sz_str, sizeof(sz_str), "%ld KB", (e->size + 1023) / 1024);
            ps3_draw_text_shadow(fb, left_x + left_w - 110, ry + 9, sz_str, 0xFF80FFAAU, 0xFF000000U, 1);
        }
    }

    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, "QUICK ROOT BOOKMARKS", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    ps3_draw_btn_label(fb, right_x + 16, right_y + 50,  BTN_ICON_SELECT,   "Cycle Quick Roots (HDD / USB)", 0xFFFFD840U, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 82,  BTN_ICON_TRIANGLE, "Go to Parent Directory (..)", 0xFF80FFAAU, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 114, BTN_ICON_CROSS,    "Open Folder / Load Selected File", 0xFFFFFFFFU, 2);
    ps3_draw_btn_label(fb, right_x + 16, right_y + 146, BTN_ICON_SQUARE,   "Swap Drive A: (On-the-fly DSK)", 0xFFFF7F27U, 2);

    ps3_draw_panel(fb, right_x + 16, right_y + 186, right_w - 32, 314, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, right_x + 28, right_y + 198, "POPULAR MSX DIRECTORIES", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 28, right_y + 220, right_w - 56, 1, 0xFF2C78C8U);

    static const char *pop_roots[6] = {
        "/dev_hdd0/fmsx/roms",
        "/dev_hdd0/game/FMSX00001/USRDIR/roms",
        "/dev_usb000/roms/msx",
        "/dev_usb000/roms",
        "/dev_usb000",
        "/dev_usb001"
    };

    for (int p = 0; p < 6; p++) {
        int py = right_y + 230 + p * 42;
        ps3_draw_rect_alpha(fb, right_x + 24, py, right_w - 48, 36, 0xFF0C1628U, 180);
        ps3_draw_border(fb, right_x + 24, py, right_w - 48, 36, 1, 0xFF1C3454U);
        ps3_draw_text_clipped(fb, right_x + 36, py + 10, right_w - 72, pop_roots[p], 0xFFD0E4FFU, 1);
    }

    draw_footer_bar(fb);
}

static void draw_tab_states(uint32_t *fb)
{
    draw_top_header(fb, "SAVE STATES - 10 SLOTS WITH REAL-TIME PREVIEW THUMBNAILS");

    int left_x = 24, left_y = 124, left_w = 580, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 620, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    ps3_draw_text_shadow(fb, left_x + 16, left_y + 12, "SAVE / LOAD STATE SLOTS (0 - 9)", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    for (int i = 0; i < 10; i++) {
        int ry = left_y + 44 + i * 47;
        int sel = (i == s_states_sel);
        int exists = ps3_state_slot_exists(i);

        if (sel) {
            ps3_draw_bevel_box(fb, left_x + 8, ry, left_w - 16, 42, 0xFF1E58A8U, 0xFF60B8FFU, 0xFF0E2C58U, 0xFFFFFFFFU, 245);
            ps3_draw_rect(fb, left_x + 12, ry + 4, 6, 34, 0xFFFFD840U);
        } else {
            ps3_draw_rect_alpha(fb, left_x + 8, ry, left_w - 16, 42, 0xFF10203CU, 175);
            ps3_draw_border(fb, left_x + 8, ry, left_w - 16, 42, 1, 0xFF1E3A68U);
        }

        char slot_txt[128];
        char cur_gname[64];
        ps3_get_active_game_name(cur_gname, sizeof(cur_gname));
        if (exists) {
            snprintf(slot_txt, sizeof(slot_txt), "Slot #%d - %s", i, cur_gname);
        } else {
            snprintf(slot_txt, sizeof(slot_txt), "Slot #%d - Empty", i);
        }
        int badge_bw = ps3_text_width(exists ? "SAVED OK" : "EMPTY", 0) + 16;
        int badge_bx = left_x + left_w - badge_bw - 18;
        int max_tw = badge_bx - (left_x + 26) - 8;
        if (max_tw < 40) max_tw = 40;
        ps3_draw_text_clipped(fb, left_x + 26, ry + 12, max_tw, slot_txt, sel ? 0xFFFFFFFFU : 0xFFC8DCF4U, 1);

        if (exists) {
            ps3_draw_badge(fb, badge_bx, ry + 10, "SAVED OK", 0xFF1B5E20U, 0xFF76FF03U);
        } else {
            ps3_draw_badge(fb, badge_bx, ry + 10, "EMPTY", 0xFF37474FU, 0xFFCFD8DCU);
        }
    }

    char ptitle[64];
    snprintf(ptitle, sizeof(ptitle), "PREVIEW THUMBNAIL - SLOT #%d", s_states_sel);
    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, ptitle, 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    int th_w = 272, th_h = 228;
    int th_x = right_x + (right_w - th_w) / 2;
    int th_y = right_y + 54;
    draw_thumb_preview(fb, s_states_sel, th_x, th_y, 2);

    int ex = ps3_state_slot_exists(s_states_sel);
    char inf_line[256];
    char game_name[64];
    ps3_get_active_game_name(game_name, sizeof(game_name));
    snprintf(inf_line, sizeof(inf_line), "Associated Game: %s", game_name);
    ps3_draw_text_clipped(fb, right_x + 24, right_y + 304, right_w - 48, inf_line, 0xFFFFFFFFU, 2);

    char path_line[512];
    snprintf(path_line, sizeof(path_line), "Storage Path: /dev_hdd0/fmsx/states/%s_slot%d.sta", game_name, s_states_sel);
    ps3_draw_text_clipped(fb, right_x + 24, right_y + 334, right_w - 48, path_line, 0xFF98C0E8U, 1);

    ps3_draw_panel(fb, right_x + 20, right_y + 368, right_w - 40, 140, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_btn_label(fb, right_x + 36, right_y + 384, BTN_ICON_CROSS,    ex ? "Load State from Selected Slot" : "Slot is Empty (Cannot Load)", ex ? 0xFF80FFAAU : 0xFFB0B0B0U, 2);
    ps3_draw_btn_label(fb, right_x + 36, right_y + 420, BTN_ICON_SQUARE,   "Save Current State to Selected Slot", 0xFFFFD840U, 2);
    ps3_draw_btn_label(fb, right_x + 36, right_y + 456, BTN_ICON_TRIANGLE, ex ? "Delete State File from Slot" : "Cannot Delete Empty Slot", ex ? 0xFFFF7F27U : 0xFFB0B0B0U, 2);

    draw_footer_bar(fb);
}

static void draw_tab_machine(uint32_t *fb)
{
    draw_top_header(fb, "HARDWARE SETUP & BIOS ROM STATUS - MSX1 / MSX2 / MSX2+ CONFIGURATION");

    int left_x = 24, left_y = 124, left_w = 580, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 620, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    ps3_draw_text_shadow(fb, left_x + 16, left_y + 12, "MSX HARDWARE ARCHITECTURE", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    static const char *model_names[3] = { "MSX1 (TMS9918 Video)", "MSX2 (V9938 Video)", "MSX2+ (V9958 Enhanced)" };
    static const char *region_names[2] = { "NTSC 60Hz (Standard)", "PAL 50Hz (Europe)" };
    static const char *ram_names[4]    = { "64 KB (4 Pages)", "128 KB (8 Pages)", "256 KB (16 Pages)", "512 KB (32 Pages)" };
    static const char *vram_names[2]   = { "64 KB (MSX1/2)", "128 KB (MSX2/2+)" };
    static const char *turbo_names[2]  = { "3.58 MHz (Authentic Standard)", "7.16 MHz (Turbo 2x Overclock)" };

    int ram_idx = (g_cfg.ram_pages <= 4) ? 0 : (g_cfg.ram_pages <= 8) ? 1 : (g_cfg.ram_pages <= 16) ? 2 : 3;
    int vram_idx = (g_cfg.vram_pages <= 4) ? 0 : 1;

    ps3_draw_selector_row(fb, left_x + 14, left_y + 40,  left_w - 28, 44, "MSX Machine Model", model_names[g_cfg.msx_model % 3], s_machine_sel == 0);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 88,  left_w - 28, 44, "Video Standard / Refresh", region_names[g_cfg.video_region % 2], s_machine_sel == 1);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 136, left_w - 28, 44, "CPU Clock Speed", turbo_names[g_cfg.cpu_turbo ? 1 : 0], s_machine_sel == 2);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 184, left_w - 28, 44, "Main System RAM", ram_names[ram_idx], s_machine_sel == 3);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 232, left_w - 28, 44, "Video VRAM Size", vram_names[vram_idx], s_machine_sel == 4);
    ps3_draw_switch_row(fb,   left_x + 14, left_y + 280, left_w - 28, 44, "FM-PAC Audio (YM2413 / OPLL)", g_cfg.fmpac_enabled, s_machine_sel == 5);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 328, left_w - 28, 44, "Reset MSX Machine Core", "Execute", s_machine_sel == 6);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 376, left_w - 28, 44, "Open Missing BIOS Notice Dialog", "Display", s_machine_sel == 7);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 424, left_w - 28, 44, "Copy BIOS ROMs from USB to HDD", "Import", s_machine_sel == 8);

    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, "SYSTEM BIOS ROM STATUS (8 STANDARD FILES)", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    Ps3BiosItem bitems[8];
    ps3_bios_check_all(bitems, 8);

    int by = right_y + 44;
    for (int b = 0; b < 8; b++) {
        int is_req = 0;
        if (g_cfg.msx_model == 0 && (bitems[b].model_mask & 1)) is_req = 1;
        if (g_cfg.msx_model == 1 && (bitems[b].model_mask & 2)) is_req = 1;
        if (g_cfg.msx_model == 2 && (bitems[b].model_mask & 4)) is_req = 1;

        ps3_draw_rect_alpha(fb, right_x + 12, by, right_w - 24, 38, 0xFF0F1E34U, 180);
        ps3_draw_border(fb, right_x + 12, by, right_w - 24, 38, 1, is_req ? 0xFF3C78B4U : 0xFF1A3250U);

        uint32_t name_col = bitems[b].found ? 0xFFFFFFFFU : (is_req ? 0xFFFF6050U : 0xFFB0B0B0U);
        ps3_draw_text_shadow(fb, right_x + 22, by + 10, bitems[b].filename, name_col, 0xFF000000U, 1);

        char bdesc[96];
        snprintf(bdesc, sizeof(bdesc), "%s (%dKB)", bitems[b].desc, bitems[b].expected_size / 1024);
        ps3_draw_text_clipped(fb, right_x + 160, by + 12, right_w - 300, bdesc, 0xFF90B0D0U, 0);

        if (bitems[b].found) {
            if (bitems[b].is_cbios) {
                ps3_draw_badge(fb, right_x + right_w - 130, by + 8, "C-BIOS BSD", 0xFFE65100U, 0xFFFFD54FU);
            } else {
                ps3_draw_badge(fb, right_x + right_w - 130, by + 8, "OEM ORIGINAL", 0xFF1B5E20U, 0xFF76FF03U);
            }
        } else if (is_req) {
            ps3_draw_badge(fb, right_x + right_w - 130, by + 8, "MISSING (REQ)", 0xFFB71C1CU, 0xFFFF8A80U);
        } else {
            ps3_draw_badge(fb, right_x + right_w - 130, by + 8, "OPTIONAL", 0xFF37474FU, 0xFFCFD8DCU);
        }
        by += 44;
    }

    ps3_draw_panel(fb, right_x + 12, by + 4, right_w - 24, 126, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, right_x + 24, by + 14, "C-BIOS INTEGRATED & OPTIONAL MANUAL OEM BIOS:", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 24, by + 40, right_w - 48, "Legal C-BIOS (BSD) active. Optional original OEM BIOS paths:", 0xFFCBD5E1U, 1);
    ps3_draw_text_clipped(fb, right_x + 24, by + 64, right_w - 48, "1. /dev_hdd0/fmsx/bios/ (Prioritized on boot)", 0xFF80FFAAU, 1);
    ps3_draw_text_clipped(fb, right_x + 24, by + 88, right_w - 48, "2. /dev_usb000/bios/ (Press TRIANGLE to import to HDD)", 0xFF00E5FFU, 1);

    draw_footer_bar(fb);
}

static void draw_tab_display(uint32_t *fb)
{
    draw_top_header(fb, "VIDEO, AUDIO & VISUAL OPTIONS - RSX HD SCALING, SCANLINES & THEMES");

    int left_x = 24, left_y = 124, left_w = 680, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 720, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    ps3_draw_text_shadow(fb, left_x + 16, left_y + 12, "DISPLAY & SOUND SETTINGS", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    static const char *aspect_names[3] = { "4:3 Original MSX Aspect", "16:9 Widescreen Stretch", "Pixel Perfect 1:1" };
    static const char *scan_names[3]   = { "Off (Solid Pixels)", "Light CRT (25%)", "Classic CRT (50%)" };
    static const char *sprite_names[2] = { "Authentic (4/8 Flicker)", "Unlimited (No Flicker)" };
    static const char *pal_names[3]    = { "Standard MSX Official", "V9938 Vibrant Studio", "Cool Monochrome" };
    static const char *blur_names[2]   = { "Bilinear Smooth Filtering", "Crisp Nearest Neighbor" };
    static const char *vkb_names[4]    = { "Original Grey", "Warm Beige", "Cool Blue", "Dark Carbon" };

    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 0 * 44,  left_w - 28, 40, "Screen Aspect Ratio", aspect_names[g_cfg.aspect_ratio % 3], s_settings_sel == 0);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 1 * 44,  left_w - 28, 40, "CRT Scanline Simulation", scan_names[g_cfg.scanlines % 3], s_settings_sel == 1);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 2 * 44,  left_w - 28, 40, "Sprite Limit (Flicker)", sprite_names[g_cfg.unlimited_sprites ? 1 : 0], s_settings_sel == 2);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 3 * 44,  left_w - 28, 40, "Color Palette Profile", pal_names[g_cfg.palette % 3], s_settings_sel == 3);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 4 * 44,  left_w - 28, 40, "Hardware Texture Filtering", blur_names[g_cfg.blur ? 1 : 0], s_settings_sel == 4);
    ps3_draw_slider_row(fb,   left_x + 14, left_y + 40 + 5 * 44,  left_w - 28, 40, "Master Audio Volume", g_cfg.sound_volume, 0, 100, "%", s_settings_sel == 5);
    ps3_draw_slider_row(fb,   left_x + 14, left_y + 40 + 6 * 44,  left_w - 28, 40, "PSG Sound Balance", g_cfg.vol_psg, 0, 100, "%", s_settings_sel == 6);
    ps3_draw_slider_row(fb,   left_x + 14, left_y + 40 + 7 * 44,  left_w - 28, 40, "SCC Sound Balance", g_cfg.vol_scc, 0, 100, "%", s_settings_sel == 7);
    ps3_draw_slider_row(fb,   left_x + 14, left_y + 40 + 8 * 44,  left_w - 28, 40, "FM-PAC Sound Balance", g_cfg.vol_fmpac, 0, 100, "%", s_settings_sel == 8);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 9 * 44,  left_w - 28, 40, "Virtual MSX Keyboard Theme", vkb_names[g_cfg.vkbd_style & 3], s_settings_sel == 9);
    ps3_draw_selector_row(fb, left_x + 14, left_y + 40 + 10 * 44, left_w - 28, 40, "Save Current Settings to HDD", "Save", s_settings_sel == 10);

    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, "AUDIO MIXER & VIDEO PIPELINE", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    ps3_draw_panel(fb, right_x + 16, right_y + 54, right_w - 32, 450, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, right_x + 28, right_y + 70, "INDEPENDENT AUDIO CHANNELS", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 28, right_y + 94, right_w - 56, 1, 0xFF2C78C8U);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 110, "PSG (AY-3-8910):", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 134, right_w - 56, "3 square wave channels + 3 noise channels", 0xFFFFFFFFU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 164, "SCC / SCC+ (Konami Waveform):", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 188, right_w - 56, "5 custom 32-byte wavetable synth channels", 0xFF80FFAAU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 218, "FM-PAC (Yamaha YM2413 OPLL):", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 242, right_w - 56, "9 channels 2-operator FM synthesis + rhythm", 0xFFFFFFFFU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 272, "Video Resolution & Refresh:", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 296, right_w - 56, "1280x720 HD 60Hz HDMI Progressive", 0xFFD0E4FFU, 1);

    ps3_draw_text_shadow(fb, right_x + 28, right_y + 326, "Configuration File:", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 350, right_w - 56, "/dev_hdd0/fmsx/fmsx_ps3.cfg", 0xFFCBD5E1U, 1);

    draw_footer_bar(fb);
}

static void draw_tab_vkbd(uint32_t *fb)
{
    draw_top_header(fb, "VIRTUAL MSX KEYBOARD & MACROS - UAE4ALL2-HD HIGH RESOLUTION KEYBOARD");

    int left_x = 24, left_y = 124, left_w = 720, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 760, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    ps3_draw_text_shadow(fb, left_x + 16, left_y + 12, "QUICK BASIC & SYSTEM COMMAND MACROS", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    static const char *macro_labels[8] = {
        "Auto-Run Cassette Tape: RUN\"CAS:\"",
        "Load Binary from Tape: BLOAD\"CAS:\",R",
        "Directory File List: FILES",
        "Set Text Screen Mode: SCREEN 1",
        "Set High-Res Graphic Mode: SCREEN 2",
        "Classic MSX Colors: COLOR 15,4,7",
        "Return to MSX-DOS: _SYSTEM",
        "Open Full Virtual MSX Keyboard"
    };

    for (int m = 0; m < 8; m++) {
        int ry = left_y + 44 + m * 56;
        int sel = (m == s_vkbd_sel);
        ps3_draw_selector_row(fb, left_x + 14, ry, left_w - 28, 48, macro_labels[m], "Send", sel);
    }

    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, "IN-GAME KEYBOARD CONTROLS", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    ps3_draw_panel(fb, right_x + 16, right_y + 54, right_w - 32, 450, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 70,  BTN_ICON_SELECT,   "Toggle Keyboard during Emulation", 0xFFFFFFFFU, 2);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 104, BTN_ICON_DPAD,     "Navigate Keyboard Cursor", 0xFF80D8FFU, 2);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 138, BTN_ICON_CROSS,    "Press & Release Key", 0xFF80FFAAU, 2);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 172, BTN_ICON_SQUARE,   "Sticky Modifier (SHIFT/CTRL/GRAPH)", 0xFFFFD840U, 2);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 206, BTN_ICON_TRIANGLE, "Backspace Key", 0xFFFF7F27U, 2);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 240, BTN_ICON_R1,       "Return / Enter Key", 0xFFE0F0FFU, 2);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 274, BTN_ICON_CIRCLE,   "Close Virtual Keyboard", 0xFFE0F0FFU, 2);

    ps3_draw_rect(fb, right_x + 28, right_y + 314, right_w - 56, 1, 0xFF2C78C8U);
    ps3_draw_text_shadow(fb, right_x + 28, right_y + 328, "Physical USB Keyboards:", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 352, right_w - 56, "Any standard USB keyboard connected to PS3", 0xFFFFFFFFU, 1);
    ps3_draw_text_clipped(fb, right_x + 28, right_y + 376, right_w - 56, "is automatically recognized for direct typing!", 0xFF80FFAAU, 1);

    draw_footer_bar(fb);
}

static void draw_tab_credits(uint32_t *fb)
{
    draw_top_header(fb, "DEVELOPER CREDITS & SYSTEM INFORMATION - ACKNOWLEDGMENTS & HARDWARE");

    int left_x = 24, left_y = 124, left_w = 600, left_h = 532;
    ps3_draw_panel(fb, left_x, left_y, left_w, left_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    int right_x = 640, right_y = 124, right_w = SCREEN_W - right_x - 24, right_h = 532;
    ps3_draw_panel(fb, right_x, right_y, right_w, right_h, 0xFF0E1B32U, 0xFF070E1AU, 0xFF3080D8U, 236);

    ps3_draw_text_shadow(fb, left_x + 16, left_y + 12, "DEVELOPER ACKNOWLEDGMENTS", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_rect(fb, left_x + 16, left_y + 34, left_w - 32, 2, 0xFF2C78C8U);

    ps3_draw_panel(fb, left_x + 16, left_y + 42, left_w - 32, 112, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 52, "ORIGINAL fMSX EMULATOR ENGINE", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 76, "Author: Marat Fayzullin", 0xFFFFFFFFU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 100, "Official Engine: fMSX 6.0 Portable Core (EMULib & Z80)", 0xFF80FFAAU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 122, "Official Website: fms.komkon.org/fMSX", 0xFFD0E4FFU, 0xFF000000U, 0);

    ps3_draw_panel(fb, left_x + 16, left_y + 160, left_w - 32, 112, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 170, "openMSX PROJECT & C-BIOS ARCHITECTURE", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 194, "openMSX Authors: Maarten ter Huurne & Manuel Bilderbeek", 0xFFFFFFFFU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 218, "Developers: Wouter Vermaelen, Joost Y. Damad & Team", 0xFF80FFAAU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 240, "C-BIOS: BouKiCHi, M. ter Huurne, M. Bilderbeek (BSD)", 0xFFD0E4FFU, 0xFF000000U, 0);

    ps3_draw_panel(fb, left_x + 16, left_y + 278, left_w - 32, 112, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 288, "PLAYSTATION 3 PORT & ARCHITECTURE", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 312, "Developer: theheroGAC", 0xFFFFFFFFU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 336, "Architecture: PSL1GHT Open Source SDK + RSX Blit", 0xFF80FFAAU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 358, "Platform: PS3 CFW & PS3HEN Custom Firmware", 0xFFD0E4FFU, 0xFF000000U, 0);

    ps3_draw_panel(fb, left_x + 16, left_y + 396, left_w - 32, 116, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 406, "GUI & GRAPHICS SUBSYSTEM", 0xFFF472B6U, 0xFF000000U, 2);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 430, "Font Subsystem: UAE4ALL2-HD Typography & font.ttf", 0xFFFFFFFFU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 452, "Audio Subsystem: 48000Hz Low-Latency Cell Stream", 0xFF80FFAAU, 0xFF000000U, 1);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 474, "Package: Signed Retail NPDRM & Standard PS3 PKG", 0xFFFFE040U, 0xFF000000U, 0);
    ps3_draw_text_shadow(fb, left_x + 28, left_y + 492, "BIOS License: C-BIOS BSD-2-Clause (No Copyrighted ROMs)", 0xFF00E5FFU, 0xFF000000U, 0);

    ps3_draw_text_shadow(fb, right_x + 16, right_y + 12, "CONTROLLER MAPPINGS QUICK REFERENCE", 0xFF00E5FFU, 0xFF000000U, 2);
    ps3_draw_rect(fb, right_x + 16, right_y + 34, right_w - 32, 2, 0xFF2C78C8U);

    ps3_draw_panel(fb, right_x + 16, right_y + 46, right_w - 32, 464, 0xFF14243EU, 0xFF0A1424U, 0xFF3C78B4U, 230);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 58,  BTN_ICON_START,    "In-Game Quick Menu (or R2)", 0xFFFFFFFFU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 92,  BTN_ICON_SELECT,   "Toggle Virtual Keyboard (or L2)", 0xFFFFFFFFU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 126, BTN_ICON_CROSS,    "MSX Joystick 1 Fire A / Spacebar", 0xFF80FFAAU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 160, BTN_ICON_CIRCLE,   "MSX Joystick 1 Fire B / M key", 0xFF80FFAAU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 194, BTN_ICON_SQUARE,   "MSX Function Key F1", 0xFFFFD840U, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 228, BTN_ICON_TRIANGLE, "MSX Function Key F2", 0xFFFFD840U, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 262, BTN_ICON_R1,       "MSX Return / Enter Key", 0xFFE0F0FFU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 296, BTN_ICON_L1,       "MSX Escape / Stop Key", 0xFFE0F0FFU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 330, BTN_ICON_L3,       "Fast Forward Speed Boost (3x)", 0xFF00E5FFU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 364, BTN_ICON_R3,       "Instant Quick State Save", 0xFF80FFAAU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 398, BTN_ICON_DPAD,     "MSX 8-Way Directional Pad", 0xFFFFFFFFU, 1);
    ps3_draw_btn_label(fb, right_x + 28, right_y + 432, BTN_ICON_DPAD,     "Analog Stick & D-Pad Emulation", 0xFFFFFFFFU, 1);

    draw_footer_bar(fb);
}

static void draw_menu_quick(uint32_t *fb)
{
    int pw = 980;
    int ph = 560;
    int px = (SCREEN_W - pw) / 2;
    int py = (SCREEN_H - ph) / 2;

    ps3_draw_panel(fb, px, py, pw, ph, 0xFF14284CU, 0xFF091224U, 0xFF00E5FFU, 238);
    ps3_draw_rect(fb, px + 4, py + 4, pw - 8, 44, 0xFF1C3C70U);
    ps3_draw_text_centered(fb, SCREEN_W / 2, py + 16, "IN-GAME QUICK MENU - MSX4PS3", 0xFFFFE040U, 2);

    const char *asp_names[3] = { "4:3 Original", "16:9 Widescreen", "Pixel Perfect" };
    const char *scn_names[3] = { "Off", "Light (25%)", "CRT Classic (50%)" };
    const char *vkb_names[4] = { "Original Grey", "Warm Beige", "Cool Blue", "Dark Carbon" };

    char items[12][96];
    snprintf(items[0], sizeof(items[0]), "Resume Game");
    snprintf(items[1], sizeof(items[1]), "Reset MSX Machine (Hardware Reboot)");
    snprintf(items[2], sizeof(items[2]), "Quick Save State        < Slot %d %s >",
             g_cfg.state_slot, ps3_state_slot_exists(g_cfg.state_slot) ? "[USED]" : "[EMPTY]");
    snprintf(items[3], sizeof(items[3]), "Quick Load State        < Slot %d %s >",
             g_cfg.state_slot, ps3_state_slot_exists(g_cfg.state_slot) ? "[READY]" : "[EMPTY]");
    snprintf(items[4], sizeof(items[4]), "Open Save / Load State Manager (All 10 Slots)");
    snprintf(items[5], sizeof(items[5]), "Open Virtual MSX Keyboard (UAE4ALL2-HD)");
    snprintf(items[6], sizeof(items[6]), "Open Game Library (ROM / DSK / CAS)");
    snprintf(items[7], sizeof(items[7]), "Auto-Run Tape Cassette (RUN\"CAS:\")");
    snprintf(items[8], sizeof(items[8]), "Aspect Ratio            < %s >", asp_names[g_cfg.aspect_ratio % 3]);
    snprintf(items[9], sizeof(items[9]), "CRT Scanlines           < %s >", scn_names[g_cfg.scanlines % 3]);
    snprintf(items[10], sizeof(items[10]), "Keyboard Theme          < %s >", vkb_names[g_cfg.vkbd_style & 3]);
    snprintf(items[11], sizeof(items[11]), "Open Tabbed Frontend Menu (All Tabs)");

    for (int i = 0; i < 12; i++) {
        int iy = py + 50 + i * 38;
        int sel = (i == s_quick_sel);
        if (sel) {
            ps3_draw_bevel_box(fb, px + 20, iy, 630, 34, 0xFF1E58A8U, 0xFF60B8FFU, 0xFF0E2C58U, 0xFFFFFFFFU, 245);
        } else {
            ps3_draw_rect_alpha(fb, px + 20, iy, 630, 34, 0xFF10203CU, 185);
        }
        ps3_draw_text_shadow(fb, px + 34, iy + 8, items[i], sel ? 0xFFFFFFFFU : 0xFFC8DCF4U, 0xFF000000U, 1);
    }

    int rx = px + 666;
    int ry = py + 58;
    ps3_draw_panel(fb, rx, ry, 294, 428, 0xFF0D1A32U, 0xFF070E1CU, 0xFF2C78C8U, 230);
    char slot_hdr[64];
    snprintf(slot_hdr, sizeof(slot_hdr), "STATE SLOT #%d", g_cfg.state_slot);
    ps3_draw_text_centered(fb, rx + 147, ry + 16, slot_hdr, 0xFF00E5FFU, 2);
    draw_thumb_preview(fb, g_cfg.state_slot, rx + 79, ry + 54, 1);

    int ex = ps3_state_slot_exists(g_cfg.state_slot);
    ps3_draw_text_centered(fb, rx + 147, ry + 180, ex ? "[SLOT OCCUPIED]" : "[SLOT EMPTY]",
                           ex ? 0xFF76FF03U : 0xFFCFD8DCU, 2);

    ps3_draw_text_shadow(fb, rx + 20, ry + 220, "QUICK SHORTCUTS:", 0xFFFFD840U, 0xFF000000U, 2);
    ps3_draw_btn_label(fb, rx + 20, ry + 250, BTN_ICON_CROSS,  "Activate Selected Item", 0xFFFFFFFFU, 2);
    ps3_draw_btn_label(fb, rx + 20, ry + 286, BTN_ICON_CIRCLE, "Resume Game", 0xFF80FFAAU, 2);
    ps3_draw_btn_label(fb, rx + 20, ry + 322, BTN_ICON_START,  "Resume Game", 0xFF80FFAAU, 2);
}

void ps3_menu_run_loop(void)
{
    uint32_t last_buttons = 0;

    while (g_app_running && g_menu_mode != MENU_NONE) {
        uint32_t buttons = ps3_input_poll(0);
        uint32_t pressed = buttons & ~last_buttons;
        last_buttons = buttons;

        if (g_menu_mode == MENU_QUICK) {
            if (pressed & PS3_BTN_UP)   s_quick_sel = (s_quick_sel + 11) % 12;
            if (pressed & PS3_BTN_DOWN) s_quick_sel = (s_quick_sel + 1) % 12;
            if (pressed & (PS3_BTN_CIRCLE | PS3_BTN_START)) {
                g_menu_mode = MENU_NONE;
                break;
            }

            if (pressed & (PS3_BTN_LEFT | PS3_BTN_RIGHT)) {
                int dir = (pressed & PS3_BTN_RIGHT) ? 1 : -1;
                if (s_quick_sel == 2 || s_quick_sel == 3) {
                    g_cfg.state_slot = (g_cfg.state_slot + dir + 10) % 10;
                } else if (s_quick_sel == 8) {
                    g_cfg.aspect_ratio = (g_cfg.aspect_ratio + dir + 3) % 3;
                    ps3_config_save();
                } else if (s_quick_sel == 9) {
                    g_cfg.scanlines = (g_cfg.scanlines + dir + 3) % 3;
                    ps3_config_save();
                } else if (s_quick_sel == 10) {
                    g_cfg.vkbd_style = (g_cfg.vkbd_style + dir + 4) % 4;
                    ps3_config_save();
                }
            }

            if (pressed & PS3_BTN_CROSS) {
                if (s_quick_sel == 0) {
                    g_menu_mode = MENU_NONE;
                    break;
                } else if (s_quick_sel == 1) {
                    ps3_trigger_hardware_reset();
                    ps3_toast_show("MSX HARDWARE RESET EXECUTED", 90);
                    g_menu_mode = MENU_NONE;
                    break;
                } else if (s_quick_sel == 2) {
                    ps3_state_save_slot(g_cfg.state_slot);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "QUICK SAVED STATE TO SLOT #%d", g_cfg.state_slot);
                    ps3_toast_show(msg, 90);
                } else if (s_quick_sel == 3) {
                    if (ps3_state_load_slot(g_cfg.state_slot)) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "QUICK LOADED STATE FROM SLOT #%d", g_cfg.state_slot);
                        ps3_toast_show(msg, 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    }
                } else if (s_quick_sel == 4) {
                    s_active_tab = TAB_STATES;
                    g_menu_mode = MENU_MAIN;
                } else if (s_quick_sel == 5) {
                    g_menu_mode = MENU_NONE;
                    g_vkbd_mode = 1;
                    break;
                } else if (s_quick_sel == 6) {
                    s_active_tab = TAB_LIBRARY;
                    g_menu_mode = MENU_MAIN;
                } else if (s_quick_sel == 7) {
                    RewindTape();
                    ps3_vkbd_queue_macro("RUN\"CAS:\"\r");
                    ps3_toast_show("SENT COMMAND: RUN\"CAS:\"", 90);
                    g_menu_mode = MENU_NONE;
                    break;
                } else if (s_quick_sel == 8) {
                    g_cfg.aspect_ratio = (g_cfg.aspect_ratio + 1) % 3;
                    ps3_config_save();
                } else if (s_quick_sel == 9) {
                    g_cfg.scanlines = (g_cfg.scanlines + 1) % 3;
                    ps3_config_save();
                } else if (s_quick_sel == 10) {
                    g_cfg.vkbd_style = (g_cfg.vkbd_style + 1) % 4;
                    ps3_config_save();
                } else if (s_quick_sel == 11) {
                    g_menu_mode = MENU_MAIN;
                }
            }
        } else {
            if (pressed & PS3_BTN_L1) {
                s_active_tab = (s_active_tab + TAB_COUNT - 1) % TAB_COUNT;
            }
            if (pressed & PS3_BTN_R1) {
                s_active_tab = (s_active_tab + 1) % TAB_COUNT;
            }

            if (pressed & (PS3_BTN_CIRCLE | PS3_BTN_START)) {
                if (ps3_bios_is_model_ready(g_cfg.msx_model)) {
                    g_menu_mode = MENU_NONE;
                    break;
                } else {
                    ps3_bios_show_dialog(1);
                }
            }

            if (s_active_tab == TAB_LIBRARY) {
                if (pressed & PS3_BTN_UP) {
                    if (s_lib_sel > 0) s_lib_sel--;
                }
                if (pressed & PS3_BTN_DOWN) {
                    if (s_lib_sel + 1 < s_lib_filtered_count) s_lib_sel++;
                }
                if (pressed & PS3_BTN_LEFT) {
                    s_lib_sel -= 12;
                    if (s_lib_sel < 0) s_lib_sel = 0;
                }
                if (pressed & PS3_BTN_RIGHT) {
                    s_lib_sel += 12;
                    if (s_lib_sel >= s_lib_filtered_count) s_lib_sel = s_lib_filtered_count - 1;
                    if (s_lib_sel < 0) s_lib_sel = 0;
                }
                if (pressed & PS3_BTN_SQUARE) {
                    s_lib_filter_cat = (s_lib_filter_cat + 1) % 4;
                    s_lib_filtered_count = 0;
                    for (int i = 0; i < s_lib_total; i++) {
                        int match = 0;
                        if (s_lib_filter_cat == 0) match = 1;
                        else if (s_lib_filter_cat == 1 && s_lib_games[i].media_type == LIB_MEDIA_CART) match = 1;
                        else if (s_lib_filter_cat == 2 && s_lib_games[i].media_type == LIB_MEDIA_DISK) match = 1;
                        else if (s_lib_filter_cat == 3 && s_lib_games[i].media_type == LIB_MEDIA_TAPE) match = 1;
                        if (match) s_lib_filtered[s_lib_filtered_count++] = i;
                    }
                    s_lib_sel = 0;
                    s_lib_scroll = 0;
                }
                if (pressed & PS3_BTN_TRIANGLE) {
                    scan_game_library();
                    ps3_toast_show("SCANNED HDD & ALL USB DRIVES", 90);
                }
                if (pressed & PS3_BTN_SELECT) {
                    s_active_tab = TAB_FILES;
                }
                if (pressed & PS3_BTN_CROSS) {
                    if (s_lib_filtered_count > 0 && s_lib_sel >= 0 && s_lib_sel < s_lib_filtered_count) {
                        int gidx = s_lib_filtered[s_lib_sel];
                        LibraryGame *g = &s_lib_games[gidx];
                        if (load_media_file(g->full_path, 0)) {
                            g_menu_mode = MENU_NONE;
                            break;
                        }
                    }
                }
            } else if (s_active_tab == TAB_SLOTS) {
                if (pressed & PS3_BTN_UP)   s_slots_sel = (s_slots_sel + 9) % 10;
                if (pressed & PS3_BTN_DOWN) s_slots_sel = (s_slots_sel + 1) % 10;

                if (pressed & (PS3_BTN_LEFT | PS3_BTN_RIGHT)) {
                    int dir = (pressed & PS3_BTN_RIGHT) ? 1 : -1;
                    if (s_slots_sel == 4) {
                        char dsk_paths[32][512];
                        char dsk_names[32][64];
                        int cur_dsk_idx = -1;
                        int dsk_count = get_dsk_swap_list(dsk_paths, dsk_names, &cur_dsk_idx);
                        if (dsk_count > 0) {
                            int next_idx = cur_dsk_idx + dir;
                            if (next_idx < 0) next_idx = dsk_count - 1;
                            if (next_idx >= dsk_count) next_idx = 0;
                            if (ChangeDisk(0, dsk_paths[next_idx])) {
                                strncpy(g_cfg.dska_path, dsk_paths[next_idx], sizeof(g_cfg.dska_path));
                                char msg[96];
                                snprintf(msg, sizeof(msg), "SWAPPED DRIVE A -> %s", dsk_names[next_idx]);
                                ps3_toast_show(msg, 100);
                                ps3_config_save();
                            }
                        } else {
                            ps3_toast_show("NO DSK FILES IN DRIVE A DIRECTORY", 90);
                        }
                    }
                }

                if (pressed & PS3_BTN_SQUARE) {
                    if (s_slots_sel == 0) {
                        LoadCart(0, 0, ROMTYPE(0));
                        g_cfg.carta_path[0] = '\0';
                        ps3_toast_show("CARTRIDGE SLOT A EJECTED", 90);
                    } else if (s_slots_sel == 1) {
                        LoadCart(0, 1, ROMTYPE(1));
                        g_cfg.cartb_path[0] = '\0';
                        ps3_toast_show("CARTRIDGE SLOT B EJECTED", 90);
                    } else if (s_slots_sel == 2 || s_slots_sel == 4) {
                        ChangeDisk(0, 0);
                        g_cfg.dska_path[0] = '\0';
                        ps3_toast_show("DISK DRIVE A: EJECTED", 90);
                    } else if (s_slots_sel == 3) {
                        ChangeDisk(1, 0);
                        g_cfg.dskb_path[0] = '\0';
                        ps3_toast_show("DISK DRIVE B: EJECTED", 90);
                    } else if (s_slots_sel == 5) {
                        ChangeTape(0);
                        g_cfg.cas_path[0] = '\0';
                        ps3_toast_show("CASSETTE TAPE EJECTED", 90);
                    }
                    ps3_config_save();
                }

                if (pressed & PS3_BTN_CROSS) {
                    if (s_slots_sel >= 0 && s_slots_sel <= 3) {
                        s_browser_target_slot = s_slots_sel;
                        s_active_tab = TAB_FILES;
                    } else if (s_slots_sel == 4) {
                        char dsk_paths[32][512];
                        char dsk_names[32][64];
                        int cur_dsk_idx = -1;
                        int dsk_count = get_dsk_swap_list(dsk_paths, dsk_names, &cur_dsk_idx);
                        if (dsk_count > 0) {
                            int next_idx = cur_dsk_idx + 1;
                            if (next_idx >= dsk_count) next_idx = 0;
                            if (ChangeDisk(0, dsk_paths[next_idx])) {
                                strncpy(g_cfg.dska_path, dsk_paths[next_idx], sizeof(g_cfg.dska_path));
                                char msg[96];
                                snprintf(msg, sizeof(msg), "SWAPPED DRIVE A -> %s", dsk_names[next_idx]);
                                ps3_toast_show(msg, 100);
                                ps3_config_save();
                            }
                        } else {
                            s_browser_target_slot = 2;
                            s_active_tab = TAB_FILES;
                        }
                    } else if (s_slots_sel == 5) {
                        s_browser_target_slot = 4;
                        s_active_tab = TAB_FILES;
                    } else if (s_slots_sel == 6) {
                        RewindTape();
                        ps3_toast_show("CASSETTE TAPE REWOUND", 90);
                    } else if (s_slots_sel == 7) {
                        RewindTape();
                        ps3_vkbd_queue_macro("RUN\"CAS:\"\r");
                        ps3_toast_show("COMMAND SENT: RUN\"CAS:\"", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_slots_sel == 8) {
                        RewindTape();
                        ps3_vkbd_queue_macro("BLOAD\"CAS:\",R\r");
                        ps3_toast_show("COMMAND SENT: BLOAD\"CAS:\",R", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_slots_sel == 9) {
                        mkdir(FMSX_BASE_DIR, 0777);
                        mkdir(FMSX_SAVES_DIR, 0777);
                        char game_name[64];
                        ps3_get_active_game_name(game_name, sizeof(game_name));
                        char new_path[512];
                        int found_slot = 1;
                        for (int s = 1; s <= 99; s++) {
                            snprintf(new_path, sizeof(new_path), "%s/%s_save_%02d.dsk", FMSX_SAVES_DIR, game_name, s);
                            FILE *chk = fopen(new_path, "rb");
                            if (!chk) { found_slot = s; break; }
                            fclose(chk);
                        }
                        if (ps3_create_blank_dsk(new_path)) {
                            ChangeDisk(1, new_path);
                            strncpy(g_cfg.dskb_path, new_path, sizeof(g_cfg.dskb_path));
                            char msg[96];
                            snprintf(msg, sizeof(msg), "CREATED & INSERTED DRIVE B: %s_save_%02d.dsk", game_name, found_slot);
                            ps3_toast_show(msg, 120);
                            ps3_config_save();
                        } else {
                            ps3_toast_show("ERROR CREATING BLANK 720KB DISK", 90);
                        }
                    }
                }
            } else if (s_active_tab == TAB_FILES) {
                if (pressed & PS3_BTN_UP) {
                    if (s_browser_sel > 0) s_browser_sel--;
                }
                if (pressed & PS3_BTN_DOWN) {
                    if (s_browser_sel + 1 < s_entry_count) s_browser_sel++;
                }
                if (pressed & PS3_BTN_LEFT) {
                    s_browser_sel -= 12;
                    if (s_browser_sel < 0) s_browser_sel = 0;
                }
                if (pressed & PS3_BTN_RIGHT) {
                    s_browser_sel += 12;
                    if (s_browser_sel >= s_entry_count) s_browser_sel = s_entry_count - 1;
                    if (s_browser_sel < 0) s_browser_sel = 0;
                }
                if (pressed & PS3_BTN_TRIANGLE) {
                    char *slash = strrchr(s_cur_dir, '/');
                    if (slash && slash != s_cur_dir) {
                        *slash = '\0';
                        scan_directory(s_cur_dir);
                    } else {
                        scan_directory("/");
                    }
                }
                if (pressed & PS3_BTN_SELECT) {
                    s_root_idx = (s_root_idx + 1) % NUM_QUICK_ROOTS;
                    snprintf(s_cur_dir, sizeof(s_cur_dir), "%s", s_quick_roots[s_root_idx]);
                    scan_directory(s_cur_dir);
                }
                if (pressed & PS3_BTN_SQUARE) {
                    if (s_entry_count > 0 && s_browser_sel < s_entry_count) {
                        BrowserEntry *e = &s_entries[s_browser_sel];
                        if (!e->is_dir) {
                            int nlen = strlen(e->name);
                            if (nlen > 4 && (strcasecmp(e->name + nlen - 4, ".dsk") == 0 ||
                                            strcasecmp(e->name + nlen - 4, ".fdi") == 0)) {
                                char full_path[512];
                                if (strcmp(s_cur_dir, "/") == 0) snprintf(full_path, sizeof(full_path), "/%s", e->name);
                                else snprintf(full_path, sizeof(full_path), "%s/%s", s_cur_dir, e->name);
                                if (ChangeDisk(0, full_path)) {
                                    strncpy(g_cfg.dska_path, full_path, sizeof(g_cfg.dska_path));
                                    char msg[96];
                                    snprintf(msg, sizeof(msg), "SWAPPED DRIVE A -> %s", e->name);
                                    ps3_toast_show(msg, 100);
                                    ps3_config_save();
                                    g_menu_mode = MENU_NONE;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (pressed & PS3_BTN_CROSS) {
                    if (s_entry_count > 0 && s_browser_sel < s_entry_count) {
                        BrowserEntry *e = &s_entries[s_browser_sel];
                        if (strcmp(e->name, "..") == 0) {
                            char *slash = strrchr(s_cur_dir, '/');
                            if (slash && slash != s_cur_dir) *slash = '\0';
                            else snprintf(s_cur_dir, sizeof(s_cur_dir), "/");
                            scan_directory(s_cur_dir);
                        } else if (e->is_dir) {
                            char next_dir[512];
                            if (strcmp(s_cur_dir, "/") == 0) snprintf(next_dir, sizeof(next_dir), "/%s", e->name);
                            else snprintf(next_dir, sizeof(next_dir), "%s/%s", s_cur_dir, e->name);
                            scan_directory(next_dir);
                        } else {
                            char full_path[512];
                            if (strcmp(s_cur_dir, "/") == 0) snprintf(full_path, sizeof(full_path), "/%s", e->name);
                            else snprintf(full_path, sizeof(full_path), "%s/%s", s_cur_dir, e->name);
                            if (load_media_file(full_path, s_browser_target_slot)) {
                                g_menu_mode = MENU_NONE;
                                break;
                            }
                        }
                    }
                }
            } else if (s_active_tab == TAB_STATES) {
                if (pressed & PS3_BTN_UP)   s_states_sel = (s_states_sel + 9) % 10;
                if (pressed & PS3_BTN_DOWN) s_states_sel = (s_states_sel + 1) % 10;

                if (pressed & PS3_BTN_CROSS) {
                    if (ps3_state_slot_exists(s_states_sel)) {
                        if (ps3_state_load_slot(s_states_sel)) {
                            char msg[64];
                            snprintf(msg, sizeof(msg), "LOADED STATE FROM SLOT #%d", s_states_sel);
                            ps3_toast_show(msg, 90);
                            g_menu_mode = MENU_NONE;
                            break;
                        }
                    } else {
                        ps3_toast_show("SLOT IS EMPTY - CANNOT LOAD", 90);
                    }
                }
                if (pressed & PS3_BTN_SQUARE) {
                    if (ps3_state_save_slot(s_states_sel)) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "SAVED STATE TO SLOT #%d", s_states_sel);
                        ps3_toast_show(msg, 90);
                    }
                }
                if (pressed & PS3_BTN_TRIANGLE) {
                    char game_name[64];
                    ps3_get_active_game_name(game_name, sizeof(game_name));
                    char path[512], img[512];
                    snprintf(path, sizeof(path), "%s/%s_slot%d.sta", FMSX_STATES_DIR, game_name, s_states_sel);
                    snprintf(img, sizeof(img), "%s/%s_slot%d.raw", FMSX_STATES_DIR, game_name, s_states_sel);
                    unlink(path);
                    unlink(img);
                    s_thumb_loaded_slot = -1;
                    ps3_toast_show("DELETED STATE FILE", 90);
                }
            } else if (s_active_tab == TAB_MACHINE) {
                if (pressed & PS3_BTN_UP)   s_machine_sel = (s_machine_sel + 8) % 9;
                if (pressed & PS3_BTN_DOWN) s_machine_sel = (s_machine_sel + 1) % 9;

                if (pressed & (PS3_BTN_LEFT | PS3_BTN_RIGHT)) {
                    int dir = (pressed & PS3_BTN_RIGHT) ? 1 : -1;
                    if (s_machine_sel == 0) {
                        g_cfg.msx_model = (g_cfg.msx_model + dir + 3) % 3;
                        ps3_apply_machine_config(0);
                    } else if (s_machine_sel == 1) {
                        g_cfg.video_region = (g_cfg.video_region + dir + 2) % 2;
                        ps3_apply_machine_config(0);
                    } else if (s_machine_sel == 2) {
                        g_cfg.cpu_turbo = !g_cfg.cpu_turbo;
                        if (g_cfg.cpu_turbo) {
                            HPeriod = (HPERIOD / 6) * 2;
                            VPeriod = ((g_cfg.video_region == 1 ? VPERIOD_PAL : VPERIOD_NTSC) / 6) * 2;
                        } else {
                            HPeriod = HPERIOD / 6;
                            VPeriod = (g_cfg.video_region == 1 ? VPERIOD_PAL : VPERIOD_NTSC) / 6;
                        }
                        ps3_toast_show(g_cfg.cpu_turbo ? "CPU SPEED: 7.16 MHz (TURBO 2X)" : "CPU SPEED: 3.58 MHz (AUTHENTIC)", 90);
                    } else if (s_machine_sel == 3) {
                        int cur = (g_cfg.ram_pages <= 4) ? 0 : (g_cfg.ram_pages <= 8) ? 1 : (g_cfg.ram_pages <= 16) ? 2 : 3;
                        cur = (cur + dir + 4) % 4;
                        g_cfg.ram_pages = (cur == 0) ? 4 : (cur == 1) ? 8 : (cur == 2) ? 16 : 32;
                        ps3_apply_machine_config(0);
                    } else if (s_machine_sel == 4) {
                        g_cfg.vram_pages = (g_cfg.vram_pages <= 4) ? 8 : 4;
                        ps3_apply_machine_config(0);
                    } else if (s_machine_sel == 5) {
                        g_cfg.fmpac_enabled = !g_cfg.fmpac_enabled;
                        ps3_apply_machine_config(0);
                    }
                    ps3_config_save();
                }

                if (pressed & PS3_BTN_CROSS) {
                    if (s_machine_sel == 0) {
                        g_cfg.msx_model = (g_cfg.msx_model + 1) % 3;
                        ps3_apply_machine_config(0);
                        ps3_config_save();
                    } else if (s_machine_sel == 1) {
                        g_cfg.video_region = !g_cfg.video_region;
                        ps3_apply_machine_config(0);
                        ps3_config_save();
                    } else if (s_machine_sel == 2) {
                        g_cfg.cpu_turbo = !g_cfg.cpu_turbo;
                        if (g_cfg.cpu_turbo) {
                            HPeriod = (HPERIOD / 6) * 2;
                            VPeriod = ((g_cfg.video_region == 1 ? VPERIOD_PAL : VPERIOD_NTSC) / 6) * 2;
                        } else {
                            HPeriod = HPERIOD / 6;
                            VPeriod = (g_cfg.video_region == 1 ? VPERIOD_PAL : VPERIOD_NTSC) / 6;
                        }
                        ps3_toast_show(g_cfg.cpu_turbo ? "CPU SPEED: 7.16 MHz (TURBO 2X)" : "CPU SPEED: 3.58 MHz (AUTHENTIC)", 90);
                        ps3_config_save();
                    } else if (s_machine_sel == 5) {
                        g_cfg.fmpac_enabled = !g_cfg.fmpac_enabled;
                        ps3_apply_machine_config(0);
                        ps3_config_save();
                    } else if (s_machine_sel == 6) {
                        ps3_apply_machine_config(1);
                        ps3_toast_show("MSX MACHINE CORE RESET", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_machine_sel == 7) {
                        ps3_bios_show_dialog(1);
                    } else if (s_machine_sel == 8) {
                        char msg[128];
                        ps3_bios_copy_from_usb(msg, sizeof(msg));
                        ps3_toast_show(msg, 120);
                    }
                }
            } else if (s_active_tab == TAB_DISPLAY) {
                if (pressed & PS3_BTN_UP)   s_settings_sel = (s_settings_sel + 10) % 11;
                if (pressed & PS3_BTN_DOWN) s_settings_sel = (s_settings_sel + 1) % 11;

                if (pressed & (PS3_BTN_LEFT | PS3_BTN_RIGHT)) {
                    int dir = (pressed & PS3_BTN_RIGHT) ? 1 : -1;
                    if (s_settings_sel == 0) {
                        g_cfg.aspect_ratio = (g_cfg.aspect_ratio + dir + 3) % 3;
                    } else if (s_settings_sel == 1) {
                        g_cfg.scanlines = (g_cfg.scanlines + dir + 3) % 3;
                    } else if (s_settings_sel == 2) {
                        g_cfg.unlimited_sprites = !g_cfg.unlimited_sprites;
                        if (g_cfg.unlimited_sprites) Mode |= MSX_ALLSPRITE;
                        else Mode &= ~MSX_ALLSPRITE;
                        ps3_toast_show(g_cfg.unlimited_sprites ? "SPRITE LIMIT: UNLIMITED (NO FLICKER)" : "SPRITE LIMIT: AUTHENTIC (4/8 FLICKER)", 90);
                    } else if (s_settings_sel == 3) {
                        g_cfg.palette = (g_cfg.palette + dir + 3) % 3;
                    } else if (s_settings_sel == 4) {
                        g_cfg.blur = !g_cfg.blur;
                    } else if (s_settings_sel == 5) {
                        g_cfg.sound_volume += dir * 5;
                        if (g_cfg.sound_volume < 0) g_cfg.sound_volume = 0;
                        if (g_cfg.sound_volume > 100) g_cfg.sound_volume = 100;
                    } else if (s_settings_sel == 6) {
                        g_cfg.vol_psg += dir * 5;
                        if (g_cfg.vol_psg < 0) g_cfg.vol_psg = 0;
                        if (g_cfg.vol_psg > 100) g_cfg.vol_psg = 100;
                        ps3_audio_update_mixer();
                    } else if (s_settings_sel == 7) {
                        g_cfg.vol_scc += dir * 5;
                        if (g_cfg.vol_scc < 0) g_cfg.vol_scc = 0;
                        if (g_cfg.vol_scc > 100) g_cfg.vol_scc = 100;
                        ps3_audio_update_mixer();
                    } else if (s_settings_sel == 8) {
                        g_cfg.vol_fmpac += dir * 5;
                        if (g_cfg.vol_fmpac < 0) g_cfg.vol_fmpac = 0;
                        if (g_cfg.vol_fmpac > 100) g_cfg.vol_fmpac = 100;
                        ps3_audio_update_mixer();
                    } else if (s_settings_sel == 9) {
                        g_cfg.vkbd_style = (g_cfg.vkbd_style + dir + 4) % 4;
                    }
                    ps3_config_save();
                }
                if (pressed & PS3_BTN_CROSS) {
                    if (s_settings_sel == 10) {
                        ps3_config_save();
                        ps3_toast_show("SETTINGS SAVED TO HDD", 90);
                    } else {
                        if (s_settings_sel == 0) g_cfg.aspect_ratio = (g_cfg.aspect_ratio + 1) % 3;
                        else if (s_settings_sel == 1) g_cfg.scanlines = (g_cfg.scanlines + 1) % 3;
                        else if (s_settings_sel == 2) {
                            g_cfg.unlimited_sprites = !g_cfg.unlimited_sprites;
                            if (g_cfg.unlimited_sprites) Mode |= MSX_ALLSPRITE;
                            else Mode &= ~MSX_ALLSPRITE;
                            ps3_toast_show(g_cfg.unlimited_sprites ? "SPRITE LIMIT: UNLIMITED (NO FLICKER)" : "SPRITE LIMIT: AUTHENTIC (4/8 FLICKER)", 90);
                        } else if (s_settings_sel == 3) g_cfg.palette = (g_cfg.palette + 1) % 3;
                        else if (s_settings_sel == 4) g_cfg.blur = !g_cfg.blur;
                        else if (s_settings_sel == 9) g_cfg.vkbd_style = (g_cfg.vkbd_style + 1) % 4;
                        ps3_config_save();
                    }
                }
            } else if (s_active_tab == TAB_VKBD) {
                if (pressed & PS3_BTN_UP)   s_vkbd_sel = (s_vkbd_sel + 7) % 8;
                if (pressed & PS3_BTN_DOWN) s_vkbd_sel = (s_vkbd_sel + 1) % 8;

                if (pressed & PS3_BTN_CROSS) {
                    if (s_vkbd_sel == 0) {
                        RewindTape();
                        ps3_vkbd_queue_macro("RUN\"CAS:\"\r");
                        ps3_toast_show("SENT COMMAND: RUN\"CAS:\"", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 1) {
                        RewindTape();
                        ps3_vkbd_queue_macro("BLOAD\"CAS:\",R\r");
                        ps3_toast_show("SENT COMMAND: BLOAD\"CAS:\",R", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 2) {
                        ps3_vkbd_queue_macro("FILES\r");
                        ps3_toast_show("SENT COMMAND: FILES", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 3) {
                        ps3_vkbd_queue_macro("SCREEN 1\r");
                        ps3_toast_show("SENT COMMAND: SCREEN 1", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 4) {
                        ps3_vkbd_queue_macro("SCREEN 2\r");
                        ps3_toast_show("SENT COMMAND: SCREEN 2", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 5) {
                        ps3_vkbd_queue_macro("COLOR 15,4,7\r");
                        ps3_toast_show("SENT COMMAND: COLOR 15,4,7", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 6) {
                        ps3_vkbd_queue_macro("_SYSTEM\r");
                        ps3_toast_show("SENT COMMAND: _SYSTEM", 90);
                        g_menu_mode = MENU_NONE;
                        break;
                    } else if (s_vkbd_sel == 7) {
                        g_menu_mode = MENU_NONE;
                        g_vkbd_mode = 1;
                        break;
                    }
                }
            } else if (s_active_tab == TAB_CREDITS) {
                if (pressed & PS3_BTN_TRIANGLE) {
                    ps3_apply_machine_config(1);
                    ps3_toast_show("MSX MACHINE RESET", 90);
                    g_menu_mode = MENU_NONE;
                    break;
                }
            }
        }

        ps3_render_msx_to_framebuf();
        ps3_draw_rect_alpha(g_framebuf, 0, 0, SCREEN_W, SCREEN_H, 0xFF040814U, 175);

        if (g_menu_mode == MENU_QUICK) {
            draw_menu_quick(g_framebuf);
        } else {
            if (s_active_tab == TAB_LIBRARY)       draw_tab_library(g_framebuf);
            else if (s_active_tab == TAB_SLOTS)    draw_tab_slots(g_framebuf);
            else if (s_active_tab == TAB_FILES)    draw_tab_files(g_framebuf);
            else if (s_active_tab == TAB_STATES)   draw_tab_states(g_framebuf);
            else if (s_active_tab == TAB_MACHINE)  draw_tab_machine(g_framebuf);
            else if (s_active_tab == TAB_DISPLAY)  draw_tab_display(g_framebuf);
            else if (s_active_tab == TAB_VKBD)     draw_tab_vkbd(g_framebuf);
            else if (s_active_tab == TAB_CREDITS)  draw_tab_credits(g_framebuf);
        }

        ps3_toast_draw(g_framebuf);
        ps3_present_framebuf();
        usleep(16000);
    }
}
