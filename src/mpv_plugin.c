#define _GNU_SOURCE

#include "app.h"
#include "config.h"
#include "http.h"
#include "mpv_cplugin.h"
#include "player.h"
#include "ui.h"
#include "util.h"
#include "wayland.h"

#include <cairo.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

enum {
    TT_OVERLAY_ID = 61
};

enum {
    OBS_OSD_W = 1,
    OBS_OSD_H = 2,
    OBS_HIDPI = 3,
    OBS_MOUSE = 4
};

struct WaylandApp {
    mpv_handle *mpv;
    bool visible;
    bool dirty;
    bool quit;
    bool ime_wanted;
    bool ime_applied;
    bool saved_host_ime;
    bool has_saved_host_ime;
    int osd_w;
    int osd_h;
    double hidpi_scale;
    int logical_w;
    int logical_h;
    double mouse_phys_x;
    double mouse_phys_y;
    int fb_fd;
    void *fb_data;
    size_t fb_cap;
    AppState *app;
    char client_name[64];
    int wakeup_pipe[2];
};

static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;
static bool g_curl_ok = false;

static void plugin_curl_init_once(void) {
    g_curl_ok = http_global_init();
}

static const int s_plugin_anchor = 0;

static void sync_host_ime(WaylandApp *wl) {
    if (!wl || !wl->mpv) return;
    bool want = (wl->visible && wl->ime_wanted);
    if (want && !wl->ime_applied) {
        if (!wl->has_saved_host_ime) {
            int host_ime = 0;
            if (mpv_get_property(wl->mpv, "input-ime", MPV_FORMAT_FLAG, &host_ime) >= 0) {
                wl->saved_host_ime = (host_ime != 0);
                wl->has_saved_host_ime = true;
            }
        }
        int on = 1;
        mpv_set_property(wl->mpv, "input-ime", MPV_FORMAT_FLAG, &on);
        wl->ime_applied = true;
    } else if (!want && wl->ime_applied) {
        int restore = wl->has_saved_host_ime ? (wl->saved_host_ime ? 1 : 0) : 0;
        mpv_set_property(wl->mpv, "input-ime", MPV_FORMAT_FLAG, &restore);
        wl->ime_applied = false;
        wl->has_saved_host_ime = false;
    }
}

static void trim_process_heap(void) {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

static void release_fb(WaylandApp *wl) {
    if (!wl) return;
    if (wl->fb_data && wl->fb_data != MAP_FAILED && wl->fb_cap > 0) {
        munmap(wl->fb_data, wl->fb_cap);
    }
    wl->fb_data = NULL;
    wl->fb_cap = 0;
    if (wl->fb_fd >= 0) {
        close(wl->fb_fd);
        wl->fb_fd = -1;
    }
}

static bool ensure_fb_capacity(WaylandApp *wl, size_t needed) {
    size_t target_cap = tt_shm_pool_capacity(needed);
    if (target_cap == 0) return false;

    if (wl->fb_fd >= 0 && wl->fb_data && !tt_shm_should_shrink(needed, wl->fb_cap)) {
        if (needed <= wl->fb_cap) {
            return true;
        }
        if (ftruncate(wl->fb_fd, (off_t)target_cap) == 0) {
            void *remapped = mremap(wl->fb_data, wl->fb_cap, target_cap, MREMAP_MAYMOVE);
            if (remapped != MAP_FAILED) {
                wl->fb_data = remapped;
                wl->fb_cap = target_cap;
                return true;
            }
        }
    }

    release_fb(wl);
    int fd = memfd_create("tuitube-osd", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, (off_t)target_cap) != 0) {
        close(fd);
        return false;
    }
    void *data = mmap(NULL, target_cap, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    wl->fb_fd = fd;
    wl->fb_data = data;
    wl->fb_cap = target_cap;
    return true;
}

static void update_mouse_pos_node(WaylandApp *wl, const mpv_node *node) {
    if (!wl || !node || node->format != MPV_FORMAT_NODE_MAP || !node->u.list) return;
    double x = wl->mouse_phys_x;
    double y = wl->mouse_phys_y;
    bool has_x = false;
    bool has_y = false;
    bool hover = true;

    const mpv_node_list *map = node->u.list;
    for (int i = 0; i < map->num; i++) {
        const char *k = map->keys ? map->keys[i] : NULL;
        const mpv_node *v = &map->values[i];
        if (!k) continue;
        if (strcmp(k, "x") == 0) {
            if (v->format == MPV_FORMAT_DOUBLE) { x = v->u.double_; has_x = true; }
            else if (v->format == MPV_FORMAT_INT64) { x = (double)v->u.int64; has_x = true; }
        } else if (strcmp(k, "y") == 0) {
            if (v->format == MPV_FORMAT_DOUBLE) { y = v->u.double_; has_y = true; }
            else if (v->format == MPV_FORMAT_INT64) { y = (double)v->u.int64; has_y = true; }
        } else if (strcmp(k, "hover") == 0) {
            if (v->format == MPV_FORMAT_FLAG) hover = (v->u.flag != 0);
        }
    }

    if (has_x) wl->mouse_phys_x = x;
    if (has_y) wl->mouse_phys_y = y;

    if (!wl->visible || !wl->app || !wl->app->ui) return;
    if (!hover) {
        ui_on_pointer_leave(wl->app->ui);
        return;
    }
    if ((has_x || has_y) &&
        wl->osd_w > 0 && wl->osd_h > 0 &&
        wl->logical_w > 0 && wl->logical_h > 0) {
        double mx = wl->mouse_phys_x * ((double)wl->logical_w / (double)wl->osd_w);
        double my = wl->mouse_phys_y * ((double)wl->logical_h / (double)wl->osd_h);
        ui_on_pointer_motion(wl->app->ui, mx, my);
    }
}

static void mpv_overlay_set_visible(WaylandApp *wl, bool show) {
    if (!wl || !wl->mpv) return;
    if (show && !wl->visible) {
        int64_t ow = 0, oh = 0;
        double hs = 1.0;
        if (mpv_get_property(wl->mpv, "osd-width", MPV_FORMAT_INT64, &ow) >= 0 && ow > 0) {
            wl->osd_w = (int)ow;
        }
        if (mpv_get_property(wl->mpv, "osd-height", MPV_FORMAT_INT64, &oh) >= 0 && oh > 0) {
            wl->osd_h = (int)oh;
        }
        if (mpv_get_property(wl->mpv, "display-hidpi-scale", MPV_FORMAT_DOUBLE, &hs) >= 0 &&
            isfinite(hs) && hs > 0.0) {
            wl->hidpi_scale = hs;
        }

        int pw = 0, ph = 0, lw = 0, lh = 0;
        if (tt_mpv_osd_dims(wl->osd_w, wl->osd_h, wl->hidpi_scale, &pw, &ph, &lw, &lh)) {
            if (lw != wl->logical_w || lh != wl->logical_h) {
                wl->logical_w = lw;
                wl->logical_h = lh;
                if (wl->app && wl->app->ui) {
                    ui_on_resize(wl->app->ui, lw, lh);
                }
            }
        }

        wl->visible = true;
        wl->dirty = true;

        mpv_node mnode;
        memset(&mnode, 0, sizeof(mnode));
        if (mpv_get_property(wl->mpv, "mouse-pos", MPV_FORMAT_NODE, &mnode) >= 0) {
            update_mouse_pos_node(wl, &mnode);
            mpv_free_node_contents(&mnode);
        }

        mpv_observe_property(wl->mpv, OBS_MOUSE, "mouse-pos", MPV_FORMAT_NODE);
        const char *en_cmd[] = {"enable-section", "tuitube_overlay", "exclusive", NULL};
        mpv_command(wl->mpv, en_cmd);
        sync_host_ime(wl);
    } else if (!show && wl->visible) {
        wl->visible = false;
        wl->dirty = false;

        const char *dis_cmd[] = {"disable-section", "tuitube_overlay", NULL};
        mpv_command(wl->mpv, dis_cmd);
        mpv_unobserve_property(wl->mpv, OBS_MOUSE);
        sync_host_ime(wl);

        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%d", TT_OVERLAY_ID);
        const char *rem_cmd[] = {"overlay-remove", id_str, NULL};
        mpv_command(wl->mpv, rem_cmd);
        release_fb(wl);
        if (wl->app) {
            app_release_hidden_resources(wl->app);
        }
        trim_process_heap();
    }
}

static void mpv_overlay_render_if_needed(WaylandApp *wl) {
    if (!wl || !wl->visible || !wl->dirty || !wl->app || !wl->app->ui) return;

    int pw = 0, ph = 0, lw = 0, lh = 0;
    if (!tt_mpv_osd_dims(wl->osd_w, wl->osd_h, wl->hidpi_scale, &pw, &ph, &lw, &lh)) {
        return;
    }

    if (lw != wl->logical_w || lh != wl->logical_h) {
        wl->logical_w = lw;
        wl->logical_h = lh;
        ui_on_resize(wl->app->ui, lw, lh);
    }

    size_t stride = (size_t)pw * 4u;
    size_t needed = stride * (size_t)ph;
    if (!ensure_fb_capacity(wl, needed)) {
        return;
    }

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        (unsigned char *)wl->fb_data, CAIRO_FORMAT_ARGB32, pw, ph, (int)stride);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return;
    }

    cairo_t *cr = cairo_create(surf);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_scale(cr, (double)pw / (double)lw, (double)ph / (double)lh);
    ui_on_render(wl->app->ui, cr, lw, lh);

    cairo_surface_flush(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl->dirty = false;

    if (lseek(wl->fb_fd, 0, SEEK_SET) < 0) {
        return;
    }

    char id_arg[16], fd_arg[32], pw_arg[16], ph_arg[16], stride_arg[32];
    snprintf(id_arg, sizeof(id_arg), "%d", TT_OVERLAY_ID);
    snprintf(fd_arg, sizeof(fd_arg), "@%d", wl->fb_fd);
    snprintf(pw_arg, sizeof(pw_arg), "%d", pw);
    snprintf(ph_arg, sizeof(ph_arg), "%d", ph);
    snprintf(stride_arg, sizeof(stride_arg), "%zu", stride);

    const char *cmd[] = {
        "overlay-add", id_arg, "0", "0", fd_arg, "0", "bgra",
        pw_arg, ph_arg, stride_arg, NULL
    };
    mpv_command(wl->mpv, cmd);
}

/* Host WaylandApp shim functions required by ui.c / app.c */
bool wayland_needs_csd(const WaylandApp *wl) {
    (void)wl;
    return false;
}

bool wayland_is_maximized(const WaylandApp *wl) {
    (void)wl;
    return true;
}

bool wayland_should_quit(const WaylandApp *wl) {
    return wl ? wl->quit : true;
}

void wayland_request_quit(WaylandApp *wl) {
    mpv_overlay_set_visible(wl, false);
}

void wayland_request_redraw(WaylandApp *wl) {
    if (wl) wl->dirty = true;
}

void wayland_set_opaque(WaylandApp *wl, bool opaque) {
    (void)wl; (void)opaque;
}

void wayland_set_cursor(WaylandApp *wl, TtCursorShape shape) {
    (void)wl; (void)shape;
}

void wayland_start_move(WaylandApp *wl) {
    (void)wl;
}

void wayland_start_resize(WaylandApp *wl, uint32_t edges) {
    (void)wl; (void)edges;
}

void wayland_toggle_maximize(WaylandApp *wl) {
    (void)wl;
}

void wayland_minimize(WaylandApp *wl) {
    (void)wl;
}

bool wayland_clipboard_copy(WaylandApp *wl, const char *text) {
    if (!text) return false;
    if (wl && wl->mpv &&
        mpv_set_property_string(wl->mpv, "clipboard/text", text) >= 0) {
        return true;
    }
    return player_copy_wl_copy(text);
}

char *wayland_clipboard_paste(WaylandApp *wl) {
    if (!wl || !wl->mpv) return NULL;
    const char *upd_cmd[] = {"update-clipboard", "text", NULL};
    (void)mpv_command(wl->mpv, upd_cmd);
    char *s = mpv_get_property_string(wl->mpv, "clipboard/text");
    if (!s) return NULL;
    char *dup = tt_strdup(s);
    mpv_free(s);
    return dup;
}

void wayland_ime_set_enabled(WaylandApp *wl, bool enabled) {
    if (!wl) return;
    wl->ime_wanted = enabled;
    sync_host_ime(wl);
}

void wayland_ime_set_surrounding(WaylandApp *wl, const char *text,
                                 int32_t cursor_byte, int32_t anchor_byte,
                                 bool external_change) {
    (void)wl; (void)text; (void)cursor_byte; (void)anchor_byte; (void)external_change;
}

void wayland_ime_set_cursor_rect(WaylandApp *wl, int32_t x, int32_t y,
                                 int32_t w, int32_t h) {
    (void)wl; (void)x; (void)y; (void)w; (void)h;
}

/* Playback hook */
static bool mpv_host_play_video(void *play_ctx, const TuiTubeConfig *cfg,
                                const MediaItem *video,
                                char *err_buf, size_t err_cap) {
    WaylandApp *wl = play_ctx;
    if (!wl || !wl->mpv || !cfg || !video || !video->id[0]) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "No video selected");
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video->id);

    const char *title = (video->title && video->title[0]) ? video->title : video->id;
    char ytdl_fmt[512];

    char *opt_keys[3];
    mpv_node opt_vals[3];
    int opt_count = 0;

    opt_keys[opt_count] = "force-media-title";
    opt_vals[opt_count].format = MPV_FORMAT_STRING;
    opt_vals[opt_count].u.string = (char *)title;
    opt_count++;

    if (cfg->has_video_format && cfg->video_format && cfg->video_format[0]) {
        player_map_video_format(cfg->video_format, cfg->allow_hdr, cfg->all_codecs,
                                ytdl_fmt, sizeof(ytdl_fmt));
        opt_keys[opt_count] = "ytdl-format";
        opt_vals[opt_count].format = MPV_FORMAT_STRING;
        opt_vals[opt_count].u.string = ytdl_fmt;
        opt_count++;
    }

    if (cfg->has_sub_language && cfg->sub_language && cfg->sub_language[0]) {
        opt_keys[opt_count] = "slang";
        opt_vals[opt_count].format = MPV_FORMAT_STRING;
        opt_vals[opt_count].u.string = cfg->sub_language;
        opt_count++;
    }

    mpv_node_list opts_list = {
        .num = opt_count,
        .values = opt_vals,
        .keys = opt_keys
    };

    mpv_node cmd_vals[5];
    int cmd_num = 0;
    cmd_vals[cmd_num].format = MPV_FORMAT_STRING;
    cmd_vals[cmd_num].u.string = "loadfile";
    cmd_num++;

    cmd_vals[cmd_num].format = MPV_FORMAT_STRING;
    cmd_vals[cmd_num].u.string = url;
    cmd_num++;

    cmd_vals[cmd_num].format = MPV_FORMAT_STRING;
    cmd_vals[cmd_num].u.string = "replace";
    cmd_num++;

    if (mpv_client_api_version() >= MPV_MAKE_VERSION(2, 3)) {
        cmd_vals[cmd_num].format = MPV_FORMAT_INT64;
        cmd_vals[cmd_num].u.int64 = -1;
        cmd_num++;
    }

    cmd_vals[cmd_num].format = MPV_FORMAT_NODE_MAP;
    cmd_vals[cmd_num].u.list = &opts_list;
    cmd_num++;

    mpv_node_list cmd_list = {
        .num = cmd_num,
        .values = cmd_vals,
        .keys = NULL
    };
    mpv_node cmd_node = {
        .format = MPV_FORMAT_NODE_ARRAY,
        .u.list = &cmd_list
    };

    int rc = mpv_command_node(wl->mpv, &cmd_node, NULL);
    if (rc < 0) {
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "mpv loadfile failed: %s", mpv_error_string(rc));
        }
        return false;
    }

    int pause_flag = 0;
    mpv_set_property(wl->mpv, "pause", MPV_FORMAT_FLAG, &pause_flag);
    mpv_overlay_set_visible(wl, false);
    return true;
}

static bool resolve_plugin_config_dir(mpv_handle *mpv, char *out_dir, size_t out_cap) {
    if (!out_dir || out_cap == 0) return false;
    out_dir[0] = '\0';

    char *sopts = mpv_get_property_string(mpv, "script-opts");
    if (sopts) {
        bool ok = tt_mpv_parse_script_opt_config_dir(sopts, out_dir, out_cap);
        mpv_free(sopts);
        if (ok) return true;
    }

    Dl_info dli;
    if (dladdr(&s_plugin_anchor, &dli) != 0 && dli.dli_fname && dli.dli_fname[0]) {
        const char *slash = strrchr(dli.dli_fname, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - dli.dli_fname);
            if (dir_len == 0) dir_len = 1;
            if (dir_len < out_cap) {
                char so_dir[PATH_MAX];
                if (dir_len < sizeof(so_dir)) {
                    memcpy(so_dir, dli.dli_fname, dir_len);
                    so_dir[dir_len] = '\0';
                    char *marker = tt_path_join(so_dir, "tuitube_portable_mode.txt");
                    bool exists = marker && tt_file_exists(marker);
                    free(marker);
                    if (exists) {
                        snprintf(out_dir, out_cap, "%s", so_dir);
                        return true;
                    }
                }
            }
        }
    }

    const char *xdg_cfg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    if (xdg_cfg && xdg_cfg[0]) {
        snprintf(out_dir, out_cap, "%s/tuitube", xdg_cfg);
    } else {
        snprintf(out_dir, out_cap, "%s/.config/tuitube", home);
    }
    return true;
}

static void sanitize_client_name(const char *raw, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (!raw || !raw[0]) {
        snprintf(out, cap, "tuitube");
        return;
    }
    size_t j = 0;
    for (size_t i = 0; raw[i] != '\0' && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (isalnum(c) || c == '-' || c == '_') {
            out[j++] = (char)c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
    if (j == 0) snprintf(out, cap, "tuitube");
}

static bool mpv_key_name_from_keysym(xkb_keysym_t sym, char *out, size_t cap) {
    if (!out || cap == 0) return false;
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
        snprintf(out, cap, "%c", (char)(sym - XKB_KEY_A + 'a'));
        return true;
    }
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
        snprintf(out, cap, "%c", (char)sym);
        return true;
    }
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
        snprintf(out, cap, "%c", (char)sym);
        return true;
    }
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
        snprintf(out, cap, "F%u", (unsigned)(sym - XKB_KEY_F1 + 1));
        return true;
    }
    switch (sym) {
        case XKB_KEY_Escape:       snprintf(out, cap, "ESC"); return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:     snprintf(out, cap, "ENTER"); return true;
        case XKB_KEY_Tab:          snprintf(out, cap, "TAB"); return true;
        case XKB_KEY_BackSpace:    snprintf(out, cap, "BS"); return true;
        case XKB_KEY_Delete:       snprintf(out, cap, "DEL"); return true;
        case XKB_KEY_Insert:       snprintf(out, cap, "INS"); return true;
        case XKB_KEY_Left:         snprintf(out, cap, "LEFT"); return true;
        case XKB_KEY_Right:        snprintf(out, cap, "RIGHT"); return true;
        case XKB_KEY_Up:           snprintf(out, cap, "UP"); return true;
        case XKB_KEY_Down:         snprintf(out, cap, "DOWN"); return true;
        case XKB_KEY_Page_Up:      snprintf(out, cap, "PGUP"); return true;
        case XKB_KEY_Page_Down:    snprintf(out, cap, "PGDWN"); return true;
        case XKB_KEY_Home:         snprintf(out, cap, "HOME"); return true;
        case XKB_KEY_End:          snprintf(out, cap, "END"); return true;
        case XKB_KEY_space:        snprintf(out, cap, "SPACE"); return true;
        case XKB_KEY_apostrophe:   snprintf(out, cap, "'"); return true;
        case XKB_KEY_comma:        snprintf(out, cap, ","); return true;
        case XKB_KEY_minus:        snprintf(out, cap, "-"); return true;
        case XKB_KEY_period:       snprintf(out, cap, "."); return true;
        case XKB_KEY_slash:        snprintf(out, cap, "/"); return true;
        case XKB_KEY_question:     snprintf(out, cap, "?"); return true;
        case XKB_KEY_semicolon:    snprintf(out, cap, ";"); return true;
        case XKB_KEY_equal:        snprintf(out, cap, "="); return true;
        case XKB_KEY_bracketleft:  snprintf(out, cap, "["); return true;
        case XKB_KEY_bracketright: snprintf(out, cap, "]"); return true;
        case XKB_KEY_backslash:    snprintf(out, cap, "\\"); return true;
        case XKB_KEY_grave:        snprintf(out, cap, "`"); return true;
        default:                   return false;
    }
}

static void setup_input_sections(WaylandApp *wl, const TuiTubeConfig *cfg) {
    const char *cname = wl->client_name;

    StrBuf def_sb;
    sb_init(&def_sb);
    sb_appendf(&def_sb, "F12 script-binding %s/toggle\n", cname);
    const char *def_cmd[] = {"define-section", "tuitube_default", def_sb.data ? def_sb.data : "", "default", NULL};
    mpv_command(wl->mpv, def_cmd);
    sb_free(&def_sb);

    const char *en_def_cmd[] = {"enable-section", "tuitube_default", NULL};
    mpv_command(wl->mpv, en_def_cmd);

    StrBuf ov_sb;
    sb_init(&ov_sb);

    /* Mouse bindings */
    sb_appendf(&ov_sb, "MOUSE_MOVE script-binding %s/mouse_move\n", cname);
    sb_appendf(&ov_sb, "MOUSE_LEAVE script-binding %s/mouse_leave\n", cname);
    static const char *const mouse_btns[] = {
        "MBTN_LEFT", "MBTN_LEFT_DBL", "MBTN_RIGHT",
        "MBTN_MID", "MBTN_BACK", "MBTN_FORWARD"
    };
    for (size_t i = 0; i < sizeof(mouse_btns) / sizeof(mouse_btns[0]); i++) {
        sb_appendf(&ov_sb, "%s script-binding %s/input\n", mouse_btns[i], cname);
    }
    static const char *const wheel_btns[] = {
        "WHEEL_UP", "WHEEL_DOWN", "WHEEL_LEFT", "WHEEL_RIGHT"
    };
    for (size_t i = 0; i < sizeof(wheel_btns) / sizeof(wheel_btns[0]); i++) {
        sb_appendf(&ov_sb, "%s repeatable script-binding %s/input\n", wheel_btns[i], cname);
    }

    /* Toggle shortcut inside overlay */
    sb_appendf(&ov_sb, "F12 script-binding %s/toggle\n", cname);

    /* Printable / Unicode / IME */
    sb_appendf(&ov_sb, "ANY_UNICODE repeatable script-binding %s/input\n", cname);
    sb_appendf(&ov_sb, "SPACE repeatable script-binding %s/input\n", cname);
    sb_appendf(&ov_sb, "SHARP repeatable script-binding %s/input\n", cname);
    sb_appendf(&ov_sb, "KP_DIVIDE repeatable script-binding %s/input\n", cname);

    /* Navigation & editing keys across modifier prefixes */
    static const char *const mod_prefixes[] = {
        "", "Shift+", "Ctrl+", "Alt+", "Ctrl+Shift+", "Ctrl+Alt+"
    };
    static const struct { const char *key; bool rep; } nav_keys[] = {
        {"ESC", false}, {"ENTER", false}, {"KP_ENTER", false}, {"TAB", false},
        {"BS", true}, {"DEL", true}, {"INS", false},
        {"UP", true}, {"DOWN", true}, {"LEFT", true}, {"RIGHT", true},
        {"PGUP", true}, {"PGDWN", true}, {"HOME", false}, {"END", false},
        {"F1", false}, {"F2", false}, {"F3", false}, {"F4", false},
        {"F5", false}, {"F6", false}, {"F7", false}, {"F8", false},
        {"F9", false}, {"F10", false}, {"F11", false}
    };
    for (size_t m = 0; m < sizeof(mod_prefixes) / sizeof(mod_prefixes[0]); m++) {
        for (size_t k = 0; k < sizeof(nav_keys) / sizeof(nav_keys[0]); k++) {
            sb_appendf(&ov_sb, "%s%s%s script-binding %s/input\n",
                       mod_prefixes[m], nav_keys[k].key,
                       nav_keys[k].rep ? " repeatable" : "", cname);
        }
    }

    /* Modified printable shortcuts */
    for (char c = 'a'; c <= 'z'; c++) {
        sb_appendf(&ov_sb, "Ctrl+%c script-binding %s/input\n", c, cname);
        sb_appendf(&ov_sb, "Ctrl+Shift+%c script-binding %s/input\n", c, cname);
    }
    static const char *const ctrl_punct[] = {
        "[", "]", "/", "-", "=", "\\", ";", "'", ",", "."
    };
    for (size_t i = 0; i < sizeof(ctrl_punct) / sizeof(ctrl_punct[0]); i++) {
        sb_appendf(&ov_sb, "Ctrl+%s script-binding %s/input\n", ctrl_punct[i], cname);
    }

    /* Additional configured modifier+key combinations */
    if (cfg) {
        for (size_t i = 0; i < cfg->key_binding_count; i++) {
            const KeyBinding *kb = &cfg->key_bindings[i];
            if (kb->mods == 0) continue;
            char kname[16];
            if (!mpv_key_name_from_keysym(kb->keysym, kname, sizeof(kname))) continue;
            sb_appendf(&ov_sb, "%s%s%s%s%s script-binding %s/input\n",
                       (kb->mods & TT_MOD_CTRL) ? "Ctrl+" : "",
                       (kb->mods & TT_MOD_ALT) ? "Alt+" : "",
                       (kb->mods & TT_MOD_SHIFT) ? "Shift+" : "",
                       (kb->mods & TT_MOD_SUPER) ? "Meta+" : "",
                       kname, cname);
        }
    }

    /* Catch-all so unmapped keys never leak to mpv while exclusive */
    sb_appendf(&ov_sb, "UNMAPPED repeatable script-binding %s/noop\n", cname);

    const char *ov_cmd[] = {"define-section", "tuitube_overlay", ov_sb.data ? ov_sb.data : "", "force", NULL};
    mpv_command(wl->mpv, ov_cmd);
    sb_free(&ov_sb);
}

static bool has_binding_suffix(const char *arg, const char *suffix) {
    if (!arg || !suffix) return false;
    if (strcmp(arg, suffix) == 0) return true;
    size_t alen = strlen(arg);
    size_t slen = strlen(suffix);
    if (alen > slen + 1 && arg[alen - slen - 1] == '/' &&
        strcmp(arg + (alen - slen), suffix) == 0) {
        return true;
    }
    return false;
}

static void handle_client_message(WaylandApp *wl, const mpv_event_client_message *msg) {
    if (!wl || !msg || msg->num_args < 1 || !msg->args || !msg->args[0]) return;
    const char *cmd = msg->args[0];

    if (strcmp(cmd, "tuitube-toggle") == 0) {
        mpv_overlay_set_visible(wl, !wl->visible);
        return;
    }
    if (strcmp(cmd, "tuitube-open") == 0) {
        mpv_overlay_set_visible(wl, true);
        return;
    }
    if (strcmp(cmd, "tuitube-close") == 0) {
        mpv_overlay_set_visible(wl, false);
        return;
    }

    if (strcmp(cmd, "key-binding") == 0 && msg->num_args >= 3 && msg->args[1] && msg->args[2]) {
        const char *bname = msg->args[1];
        const char *state = msg->args[2];

        if (has_binding_suffix(bname, "toggle")) {
            if (state[0] == 'd' || state[0] == 'p') {
                mpv_overlay_set_visible(wl, !wl->visible);
            }
            return;
        }

        if (!wl->visible || !wl->app || !wl->app->ui) return;
        if (has_binding_suffix(bname, "noop")) return;

        if (has_binding_suffix(bname, "mouse_move")) {
            mpv_node mnode;
            memset(&mnode, 0, sizeof(mnode));
            if (mpv_get_property(wl->mpv, "mouse-pos", MPV_FORMAT_NODE, &mnode) >= 0) {
                update_mouse_pos_node(wl, &mnode);
                mpv_free_node_contents(&mnode);
            }
            return;
        }

        if (has_binding_suffix(bname, "mouse_leave")) {
            if (state[0] == 'd' || state[0] == 'p') {
                ui_on_pointer_leave(wl->app->ui);
            }
            return;
        }

        if (has_binding_suffix(bname, "input") && msg->num_args >= 5) {
            TtMpvKeyEvent kev;
            const char *kname = msg->args[3];
            const char *ktext = msg->args[4];
            const char *scale = (msg->num_args >= 6) ? msg->args[5] : NULL;
            if (!tt_mpv_parse_key_event(state, kname, ktext, scale, &kev)) {
                return;
            }

            double mx = 0.0, my = 0.0;
            if (wl->osd_w > 0 && wl->osd_h > 0 && wl->logical_w > 0 && wl->logical_h > 0) {
                mx = wl->mouse_phys_x * ((double)wl->logical_w / (double)wl->osd_w);
                my = wl->mouse_phys_y * ((double)wl->logical_h / (double)wl->osd_h);
            }

            UiState *ui = wl->app->ui;
            if (kev.kind == TT_MPV_INPUT_BUTTON) {
                ui_on_pointer_button(ui, (TtMouseButton)kev.mouse_btn,
                                     kev.pressed, kev.double_click, mx, my);
                if (kev.is_press_pulse) {
                    ui_on_pointer_button(ui, (TtMouseButton)kev.mouse_btn,
                                         false, false, mx, my);
                }
            } else if (kev.kind == TT_MPV_INPUT_WHEEL) {
                ui_on_pointer_axis(ui, kev.wheel_dx, kev.wheel_dy);
            } else if (kev.kind == TT_MPV_INPUT_KEY) {
                if (kev.pressed && kev.keysym == XKB_KEY_Escape && kev.mods == 0 &&
                    !ui_can_escape_internally(ui)) {
                    mpv_overlay_set_visible(wl, false);
                } else {
                    ui_on_key(ui, kev.mods, (xkb_keysym_t)kev.keysym,
                              kev.utf8[0] ? kev.utf8 : NULL, kev.pressed);
                }
            }
        }
    }
}

static void mpv_wakeup_cb(void *d) {
    WaylandApp *wl = d;
    if (!wl || wl->wakeup_pipe[1] < 0) return;
    char b = 1;
    ssize_t r = write(wl->wakeup_pipe[1], &b, 1);
    (void)r;
}

static void drain_mpv_events(WaylandApp *wl) {
    for (;;) {
        mpv_event *ev = mpv_wait_event(wl->mpv, 0);
        if (!ev || ev->event_id == MPV_EVENT_NONE) break;

        if (ev->event_id == MPV_EVENT_SHUTDOWN) {
            wl->quit = true;
            break;
        }

        if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE && ev->data) {
            mpv_event_property *prop = ev->data;
            if (ev->reply_userdata == OBS_OSD_W && prop->format == MPV_FORMAT_INT64 && prop->data) {
                int nw = (int)(*(int64_t *)prop->data);
                if (nw != wl->osd_w) {
                    wl->osd_w = nw;
                    if (wl->visible) wl->dirty = true;
                }
            } else if (ev->reply_userdata == OBS_OSD_H && prop->format == MPV_FORMAT_INT64 && prop->data) {
                int nh = (int)(*(int64_t *)prop->data);
                if (nh != wl->osd_h) {
                    wl->osd_h = nh;
                    if (wl->visible) wl->dirty = true;
                }
            } else if (ev->reply_userdata == OBS_HIDPI && prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                double ns = *(double *)prop->data;
                if (isfinite(ns) && ns > 0.0 && ns != wl->hidpi_scale) {
                    wl->hidpi_scale = ns;
                    if (wl->visible) wl->dirty = true;
                }
            } else if (ev->reply_userdata == OBS_MOUSE && prop->format == MPV_FORMAT_NODE && prop->data) {
                update_mouse_pos_node(wl, (const mpv_node *)prop->data);
            }
        } else if (ev->event_id == MPV_EVENT_CLIENT_MESSAGE && ev->data) {
            handle_client_message(wl, (const mpv_event_client_message *)ev->data);
        }
    }
}

TT_MPV_EXPORT int mpv_open_cplugin(mpv_handle *mpv) {
    if (!mpv) return -1;

    pthread_once(&g_curl_once, plugin_curl_init_once);
    if (!g_curl_ok) return -1;

    WaylandApp wl;
    memset(&wl, 0, sizeof(wl));
    wl.mpv = mpv;
    wl.hidpi_scale = 1.0;
    wl.fb_fd = -1;
    wl.wakeup_pipe[0] = -1;
    wl.wakeup_pipe[1] = -1;

    if (pipe2(wl.wakeup_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        return -1;
    }

    sanitize_client_name(mpv_client_name(mpv), wl.client_name, sizeof(wl.client_name));

    char cfg_dir[PATH_MAX] = {0};
    resolve_plugin_config_dir(mpv, cfg_dir, sizeof(cfg_dir));

    AppState app;
    if (!app_init(&app, cfg_dir, &wl, mpv_host_play_video, &wl, "mpv")) {
        close(wl.wakeup_pipe[0]);
        close(wl.wakeup_pipe[1]);
        return -1;
    }
    wl.app = &app;

    mpv_observe_property(mpv, OBS_OSD_W, "osd-width", MPV_FORMAT_INT64);
    mpv_observe_property(mpv, OBS_OSD_H, "osd-height", MPV_FORMAT_INT64);
    mpv_observe_property(mpv, OBS_HIDPI, "display-hidpi-scale", MPV_FORMAT_DOUBLE);

    setup_input_sections(&wl, &app.cfg);
    mpv_set_wakeup_callback(mpv, mpv_wakeup_cb, &wl);

    int notify_fd = app_notify_fd(&app);

    while (!wl.quit) {
        drain_mpv_events(&wl);
        if (wl.quit) break;

        app_poll_downloads(&app);
        mpv_overlay_render_if_needed(&wl);

        struct pollfd pfds[2];
        nfds_t nfds = 0;
        pfds[nfds].fd = wl.wakeup_pipe[0];
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;

        if (notify_fd >= 0) {
            pfds[nfds].fd = notify_fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int timeout_ms = (player_get_active_download_summary(NULL, NULL, 0) > 0) ? 250 : -1;
        int pr = poll(pfds, nfds, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (pfds[0].revents & POLLIN) {
            char drain[64];
            while (read(wl.wakeup_pipe[0], drain, sizeof(drain)) > 0) {
            }
        }
        if (nfds > 1 && (pfds[1].revents & POLLIN)) {
            app_process_async_results(&app);
            if (!wl.visible) {
                ui_release_hidden_caches(app.ui);
                trim_process_heap();
            }
        }
        app_poll_downloads(&app);
    }

    mpv_set_wakeup_callback(mpv, NULL, NULL);
    mpv_overlay_set_visible(&wl, false);
    mpv_unobserve_property(mpv, OBS_OSD_W);
    mpv_unobserve_property(mpv, OBS_OSD_H);
    mpv_unobserve_property(mpv, OBS_HIDPI);
    app_destroy(&app);
    close(wl.wakeup_pipe[0]);
    close(wl.wakeup_pipe[1]);
    return 0;
}
