#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wayland.h"
#include "config.h"
#include "player.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "util.h"
#include "xdg-decoration-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-compose.h>

typedef struct {
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int width;
    int height;
    int stride;
    bool busy;
} ShmBuffer;

typedef struct WaylandOffer {
    struct wl_data_offer *offer;
    bool has_utf8;
    bool has_plain;
    struct WaylandOffer *next;
} WaylandOffer;

struct WaylandApp {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_output *output;
    struct wl_data_device_manager *data_mgr;
    struct zxdg_decoration_manager_v1 *deco_mgr;
    struct zwp_text_input_manager_v3 *ti_mgr;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *toplevel_deco;

    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct zwp_text_input_v3 *text_input;
    struct wl_data_device *data_device;
    struct wl_data_source *clipboard_source;
    WaylandOffer *offers;
    WaylandOffer *selection_offer;
    char *owned_clipboard;

    bool ti_focused;
    bool ime_enabled;
    bool ime_committed_enabled;
    uint32_t ti_commit_serial;
    char *ti_pending_preedit;
    int32_t ti_pending_cursor_begin;
    int32_t ti_pending_cursor_end;
    char *ti_pending_commit;
    uint32_t ti_pending_del_before;
    uint32_t ti_pending_del_after;
    char *ime_surrounding_text;
    int32_t ime_surrounding_cursor;
    int32_t ime_surrounding_anchor;
    bool ime_has_surrounding;
    int32_t ime_rect_x;
    int32_t ime_rect_y;
    int32_t ime_rect_w;
    int32_t ime_rect_h;
    bool ime_has_rect;

    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    TtCursorShape current_cursor;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct xkb_compose_table *xkb_compose_table;
    struct xkb_compose_state *xkb_compose_state;
    int repeat_timer_fd;
    int repeat_rate;
    int repeat_delay;
    uint32_t repeat_keycode;
    bool repeating;

    ShmBuffer buffers[2];
    int width;
    int height;
    int pending_width;
    int pending_height;
    int buf_scale;
    bool configured;
    bool maximized;
    bool needs_csd;
    bool dirty;
    bool quit;

    double ptr_x;
    double ptr_y;
    uint32_t last_serial;
    uint32_t pointer_serial;
    uint64_t last_click_ms;
    double last_click_x;
    double last_click_y;
    uint32_t last_click_btn;
    double axis_accum_x;
    double axis_accum_y;

    WaylandCallbacks cbs;
    void *user_ctx;
};

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void shm_buffer_destroy(ShmBuffer *b) {
    if (b->buffer) wl_buffer_destroy(b->buffer);
    if (b->data && b->data != MAP_FAILED) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static void buffer_release_cb(void *data, struct wl_buffer *wl_buffer) {
    (void)wl_buffer;
    ShmBuffer *b = data;
    b->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release_cb
};

static bool shm_buffer_ensure(WaylandApp *wl, ShmBuffer *b, int lw, int lh, int scale) {
    int pw = lw * scale;
    int ph = lh * scale;
    if (pw <= 0 || ph <= 0) return false;
    if (b->buffer && b->width == pw && b->height == ph) return true;

    shm_buffer_destroy(b);

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw);
    size_t size = (size_t)stride * (size_t)ph;

    int fd = memfd_create("tuitube-shm", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return false;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(wl->shm, fd, (int32_t)size);
    close(fd);
    if (!pool) {
        munmap(data, size);
        return false;
    }

    struct wl_buffer *buf = wl_shm_pool_create_buffer(
        pool, 0, pw, ph, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!buf) {
        munmap(data, size);
        return false;
    }

    wl_buffer_add_listener(buf, &buffer_listener, b);
    b->buffer = buf;
    b->data = data;
    b->size = size;
    b->width = pw;
    b->height = ph;
    b->stride = stride;
    b->busy = false;
    return true;
}

/* xdg_wm_base */
static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping
};

/* xdg_surface */
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    WaylandApp *wl = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    if (wl->pending_width > 0 && wl->pending_height > 0) {
        if (wl->pending_width != wl->width || wl->pending_height != wl->height) {
            wl->width = wl->pending_width;
            wl->height = wl->pending_height;
            if (wl->cbs.on_resize) {
                wl->cbs.on_resize(wl->user_ctx, wl->width, wl->height);
            }
        }
    }
    wl->configured = true;
    wl->dirty = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure
};

/* xdg_toplevel */
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height, struct wl_array *states) {
    (void)toplevel;
    WaylandApp *wl = data;
    if (width > 0 && height > 0) {
        wl->pending_width = width < 640 ? 640 : width;
        wl->pending_height = height < 400 ? 400 : height;
    }
    bool max = false;
    uint32_t *s;
    wl_array_for_each(s, states) {
        if (*s == XDG_TOPLEVEL_STATE_MAXIMIZED) max = true;
    }
    wl->maximized = max;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    WaylandApp *wl = data;
    wl->quit = true;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close
};

/* zxdg_toplevel_decoration_v1 */
static void deco_configure(void *data, struct zxdg_toplevel_decoration_v1 *deco, uint32_t mode) {
    (void)deco;
    WaylandApp *wl = data;
    wl->needs_csd = (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    wl->dirty = true;
}

static const struct zxdg_toplevel_decoration_v1_listener deco_listener = {
    .configure = deco_configure
};

/* wl_output */
static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t sub,
                            const char *make, const char *model, int32_t tr) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sub;
    (void)make; (void)model; (void)tr;
}
static void output_mode(void *d, struct wl_output *o, uint32_t flags,
                        int32_t w, int32_t h, int32_t refresh) {
    (void)d; (void)o; (void)flags; (void)w; (void)h; (void)refresh;
}
static void output_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void output_scale(void *data, struct wl_output *o, int32_t factor) {
    (void)o;
    WaylandApp *wl = data;
    if (factor >= 1 && factor <= 4 && factor != wl->buf_scale) {
        wl->buf_scale = factor;
        wl->dirty = true;
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale
};

/* Cursor */
static struct wl_cursor *load_cursor_fallback(struct wl_cursor_theme *theme,
                                              const char *const *names, size_t n) {
    if (!theme) return NULL;
    for (size_t i = 0; i < n; i++) {
        struct wl_cursor *c = wl_cursor_theme_get_cursor(theme, names[i]);
        if (c) return c;
    }
    return wl_cursor_theme_get_cursor(theme, "left_ptr");
}

static void apply_cursor(WaylandApp *wl, TtCursorShape shape) {
    if (!wl->pointer || !wl->cursor_theme || !wl->cursor_surface) return;
    static const char *const def_names[] = {"default", "left_ptr"};
    static const char *const txt_names[] = {"text", "xterm", "ibeam"};
    static const char *const ptr_names[] = {"pointer", "hand2", "hand1"};
    static const char *const col_names[] = {"col-resize", "ew-resize", "sb_h_double_arrow"};
    static const char *const nw_names[]  = {"nw-resize", "top_left_corner"};
    static const char *const ne_names[]  = {"ne-resize", "top_right_corner"};
    static const char *const n_names[]   = {"n-resize", "top_side"};
    static const char *const s_names[]   = {"s-resize", "bottom_side"};
    static const char *const e_names[]   = {"e-resize", "right_side"};
    static const char *const w_names[]   = {"w-resize", "left_side"};

    struct wl_cursor *c = NULL;
    switch (shape) {
        case TT_CURSOR_TEXT:       c = load_cursor_fallback(wl->cursor_theme, txt_names, 3); break;
        case TT_CURSOR_POINTER:    c = load_cursor_fallback(wl->cursor_theme, ptr_names, 3); break;
        case TT_CURSOR_COL_RESIZE: c = load_cursor_fallback(wl->cursor_theme, col_names, 3); break;
        case TT_CURSOR_NW_RESIZE:  c = load_cursor_fallback(wl->cursor_theme, nw_names, 2); break;
        case TT_CURSOR_NE_RESIZE:  c = load_cursor_fallback(wl->cursor_theme, ne_names, 2); break;
        case TT_CURSOR_N_RESIZE:   c = load_cursor_fallback(wl->cursor_theme, n_names, 2); break;
        case TT_CURSOR_S_RESIZE:   c = load_cursor_fallback(wl->cursor_theme, s_names, 2); break;
        case TT_CURSOR_E_RESIZE:   c = load_cursor_fallback(wl->cursor_theme, e_names, 2); break;
        case TT_CURSOR_W_RESIZE:   c = load_cursor_fallback(wl->cursor_theme, w_names, 2); break;
        default:                   c = load_cursor_fallback(wl->cursor_theme, def_names, 2); break;
    }
    if (!c || c->image_count == 0) return;
    struct wl_cursor_image *img = c->images[0];
    struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
    if (!buf) return;
    wl_pointer_set_cursor(wl->pointer, wl->pointer_serial, wl->cursor_surface,
                          (int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
    wl_surface_attach(wl->cursor_surface, buf, 0, 0);
    wl_surface_damage(wl->cursor_surface, 0, 0, (int32_t)img->width, (int32_t)img->height);
    wl_surface_commit(wl->cursor_surface);
}

void wayland_set_cursor(WaylandApp *wl, TtCursorShape shape) {
    if (!wl || wl->current_cursor == shape) return;
    wl->current_cursor = shape;
    apply_cursor(wl, shape);
}

/* Pointer */
static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)p; (void)surface;
    WaylandApp *wl = data;
    wl->pointer_serial = serial;
    wl->last_serial = serial;
    wl->ptr_x = wl_fixed_to_double(sx);
    wl->ptr_y = wl_fixed_to_double(sy);
    apply_cursor(wl, wl->current_cursor);
    if (wl->cbs.on_pointer_motion) {
        wl->cbs.on_pointer_motion(wl->user_ctx, wl->ptr_x, wl->ptr_y);
    }
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *surface) {
    (void)p; (void)serial; (void)surface;
    WaylandApp *wl = data;
    if (wl->cbs.on_pointer_leave) {
        wl->cbs.on_pointer_leave(wl->user_ctx);
    }
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
    (void)p; (void)time;
    WaylandApp *wl = data;
    wl->ptr_x = wl_fixed_to_double(sx);
    wl->ptr_y = wl_fixed_to_double(sy);
    if (wl->cbs.on_pointer_motion) {
        wl->cbs.on_pointer_motion(wl->user_ctx, wl->ptr_x, wl->ptr_y);
    }
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)p; (void)time;
    WaylandApp *wl = data;
    wl->last_serial = serial;
    wl->pointer_serial = serial;

    TtMouseButton btn;
    if (button == BTN_LEFT) btn = TT_BTN_LEFT;
    else if (button == BTN_RIGHT) btn = TT_BTN_RIGHT;
    else if (button == BTN_MIDDLE) btn = TT_BTN_MIDDLE;
    else if (button == BTN_SIDE) btn = TT_BTN_BACK;
    else if (button == BTN_EXTRA) btn = TT_BTN_FORWARD;
    else return;

    bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    bool dbl = false;
    if (pressed && button == BTN_LEFT) {
        uint64_t now = monotonic_ms();
        if (wl->last_click_btn == button &&
            (now - wl->last_click_ms) <= 400 &&
            fabs(wl->ptr_x - wl->last_click_x) <= 5.0 &&
            fabs(wl->ptr_y - wl->last_click_y) <= 5.0) {
            dbl = true;
            wl->last_click_ms = 0;
        } else {
            wl->last_click_ms = now;
            wl->last_click_x = wl->ptr_x;
            wl->last_click_y = wl->ptr_y;
            wl->last_click_btn = button;
        }
    }

    if (wl->cbs.on_pointer_button) {
        wl->cbs.on_pointer_button(wl->user_ctx, btn, pressed, dbl, wl->ptr_x, wl->ptr_y);
    }
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
    (void)p; (void)time;
    WaylandApp *wl = data;
    double v = wl_fixed_to_double(value);
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) wl->axis_accum_y += v;
    else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) wl->axis_accum_x += v;
}

static void pointer_frame(void *data, struct wl_pointer *p) {
    (void)p;
    WaylandApp *wl = data;
    if (wl->axis_accum_x != 0.0 || wl->axis_accum_y != 0.0) {
        if (wl->cbs.on_pointer_axis) {
            wl->cbs.on_pointer_axis(wl->user_ctx, wl->axis_accum_x, wl->axis_accum_y);
        }
        wl->axis_accum_x = 0.0;
        wl->axis_accum_y = 0.0;
    }
}

static void pointer_axis_source(void *d, struct wl_pointer *p, uint32_t src) {
    (void)d; (void)p; (void)src;
}
static void pointer_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {
    (void)d; (void)p; (void)t; (void)a;
}
static void pointer_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t disc) {
    (void)d; (void)p; (void)a; (void)disc;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete
};

/* Keyboard */
static void disarm_repeat_timer(WaylandApp *wl) {
    if (wl->repeat_timer_fd < 0) return;
    struct itimerspec ts = {0};
    timerfd_settime(wl->repeat_timer_fd, 0, &ts, NULL);
    wl->repeating = false;
}

static void arm_repeat_timer(WaylandApp *wl, uint32_t keycode) {
    if (wl->repeat_timer_fd < 0 || wl->repeat_rate <= 0) return;
    struct itimerspec ts = {0};
    long delay_ms = wl->repeat_delay > 0 ? wl->repeat_delay : 400;
    long interval_ns = 1000000000L / wl->repeat_rate;
    ts.it_value.tv_sec = delay_ms / 1000;
    ts.it_value.tv_nsec = (delay_ms % 1000) * 1000000L;
    ts.it_interval.tv_sec = interval_ns / 1000000000L;
    ts.it_interval.tv_nsec = interval_ns % 1000000000L;
    timerfd_settime(wl->repeat_timer_fd, 0, &ts, NULL);
    wl->repeat_keycode = keycode;
    wl->repeating = true;
}

static uint32_t active_mods(WaylandApp *wl) {
    uint32_t mods = 0;
    if (!wl->xkb_state) return 0;
    if (xkb_state_mod_name_is_active(wl->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= TT_MOD_CTRL;
    if (xkb_state_mod_name_is_active(wl->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= TT_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(wl->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= TT_MOD_ALT;
    if (xkb_state_mod_name_is_active(wl->xkb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= TT_MOD_SUPER;
    return mods;
}

static xkb_keysym_t normalize_keypad_sym(xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_KP_Enter:     return XKB_KEY_Return;
        case XKB_KEY_KP_Left:      return XKB_KEY_Left;
        case XKB_KEY_KP_Right:     return XKB_KEY_Right;
        case XKB_KEY_KP_Up:        return XKB_KEY_Up;
        case XKB_KEY_KP_Down:      return XKB_KEY_Down;
        case XKB_KEY_KP_Home:      return XKB_KEY_Home;
        case XKB_KEY_KP_End:       return XKB_KEY_End;
        case XKB_KEY_KP_Page_Up:   return XKB_KEY_Page_Up;
        case XKB_KEY_KP_Page_Down: return XKB_KEY_Page_Down;
        case XKB_KEY_KP_Delete:    return XKB_KEY_Delete;
        case XKB_KEY_KP_Insert:    return XKB_KEY_Insert;
        default:                   return sym;
    }
}

static void dispatch_xkb_key(WaylandApp *wl, uint32_t xkb_code, bool pressed) {
    if (!wl->xkb_state || !wl->cbs.on_key) return;
    uint32_t mods = active_mods(wl);
    xkb_keysym_t sym = normalize_keypad_sym(xkb_state_key_get_one_sym(wl->xkb_state, xkb_code));
    char utf8[64] = {0};

    if (pressed) {
        bool has_shortcut_mod = (mods & (TT_MOD_CTRL | TT_MOD_ALT | TT_MOD_SUPER)) != 0;
        if (has_shortcut_mod) {
            /* Fall back to base Latin layout keysym so shortcuts work under non-Latin XKB groups */
            if (wl->xkb_keymap && sym > 0x7fu && !(sym >= 0xff00u && sym <= 0xffffu)) {
                const xkb_keysym_t *syms = NULL;
                int n = xkb_keymap_key_get_syms_by_level(wl->xkb_keymap, xkb_code, 0, 0, &syms);
                if (n > 0 && syms && syms[0] >= 0x20u && syms[0] <= 0x7eu) {
                    sym = syms[0];
                }
            }
        } else if (wl->xkb_compose_state) {
            xkb_compose_state_feed(wl->xkb_compose_state, sym);
            enum xkb_compose_status st = xkb_compose_state_get_status(wl->xkb_compose_state);
            if (st == XKB_COMPOSE_COMPOSING) {
                utf8[0] = '\0';
            } else if (st == XKB_COMPOSE_COMPOSED) {
                int n = xkb_compose_state_get_utf8(wl->xkb_compose_state, utf8, sizeof(utf8));
                if (n < 0) utf8[0] = '\0';
                xkb_keysym_t csym = xkb_compose_state_get_one_sym(wl->xkb_compose_state);
                if (csym != XKB_KEY_NoSymbol) sym = csym;
                xkb_compose_state_reset(wl->xkb_compose_state);
            } else if (st == XKB_COMPOSE_CANCELLED) {
                utf8[0] = '\0';
                xkb_compose_state_reset(wl->xkb_compose_state);
            } else {
                int n = xkb_state_key_get_utf8(wl->xkb_state, xkb_code, utf8, sizeof(utf8));
                if (n < 0) utf8[0] = '\0';
            }
        } else {
            int n = xkb_state_key_get_utf8(wl->xkb_state, xkb_code, utf8, sizeof(utf8));
            if (n < 0) utf8[0] = '\0';
        }
    }
    wl->cbs.on_key(wl->user_ctx, mods, sym, utf8, pressed);
}

static void keyboard_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
                            int32_t fd, uint32_t size) {
    (void)kb;
    WaylandApp *wl = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;

    struct xkb_keymap *km = xkb_keymap_new_from_string(
        wl->xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!km) return;

    struct xkb_state *st = xkb_state_new(km);
    if (!st) {
        xkb_keymap_unref(km);
        return;
    }
    if (wl->xkb_state) xkb_state_unref(wl->xkb_state);
    if (wl->xkb_keymap) xkb_keymap_unref(wl->xkb_keymap);
    wl->xkb_keymap = km;
    wl->xkb_state = st;
}

static void keyboard_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
    (void)kb; (void)surface; (void)keys;
    WaylandApp *wl = data;
    wl->last_serial = serial;
}

static void keyboard_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
                           struct wl_surface *surface) {
    (void)kb; (void)serial; (void)surface;
    WaylandApp *wl = data;
    disarm_repeat_timer(wl);
    if (wl->xkb_compose_state) {
        xkb_compose_state_reset(wl->xkb_compose_state);
    }
}

static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    (void)kb; (void)time;
    WaylandApp *wl = data;
    wl->last_serial = serial;
    uint32_t xkb_code = key + 8;
    bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    dispatch_xkb_key(wl, xkb_code, pressed);

    if (pressed) {
        if (wl->xkb_keymap && xkb_keymap_key_repeats(wl->xkb_keymap, xkb_code)) {
            arm_repeat_timer(wl, xkb_code);
        } else {
            disarm_repeat_timer(wl);
        }
    } else if (wl->repeating && wl->repeat_keycode == xkb_code) {
        disarm_repeat_timer(wl);
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group) {
    (void)kb; (void)serial;
    WaylandApp *wl = data;
    if (wl->xkb_state) {
        xkb_state_update_mask(wl->xkb_state, mods_depressed, mods_latched,
                              mods_locked, 0, 0, group);
    }
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb,
                                 int32_t rate, int32_t delay) {
    (void)kb;
    WaylandApp *wl = data;
    wl->repeat_rate = rate;
    wl->repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info
};

/* Seat */
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    WaylandApp *wl = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl->pointer) {
        wl->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(wl->pointer, &pointer_listener, wl);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->keyboard) {
        wl->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(wl->keyboard, &keyboard_listener, wl);
    }
}

static void seat_name(void *d, struct wl_seat *s, const char *name) {
    (void)d; (void)s; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name
};

/* Text Input v3 (IME: Fcitx5, IBus, etc.) */
static void ti_clear_pending(WaylandApp *wl) {
    free(wl->ti_pending_preedit);
    wl->ti_pending_preedit = NULL;
    wl->ti_pending_cursor_begin = 0;
    wl->ti_pending_cursor_end = 0;
    free(wl->ti_pending_commit);
    wl->ti_pending_commit = NULL;
    wl->ti_pending_del_before = 0;
    wl->ti_pending_del_after = 0;
}

static void ime_commit(WaylandApp *wl) {
    if (!wl || !wl->text_input) return;
    zwp_text_input_v3_commit(wl->text_input);
    wl->ti_commit_serial++;
    wl_display_flush(wl->display);
}

static void ime_send_enable_state(WaylandApp *wl) {
    if (!wl || !wl->text_input || !wl->ti_focused) return;
    zwp_text_input_v3_enable(wl->text_input);
    zwp_text_input_v3_set_content_type(
        wl->text_input,
        ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
        ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_set_surrounding_text(
        wl->text_input,
        wl->ime_surrounding_text ? wl->ime_surrounding_text : "",
        wl->ime_has_surrounding ? wl->ime_surrounding_cursor : 0,
        wl->ime_has_surrounding ? wl->ime_surrounding_anchor : 0);
    if (wl->ime_has_rect) {
        zwp_text_input_v3_set_cursor_rectangle(
            wl->text_input,
            wl->ime_rect_x, wl->ime_rect_y,
            wl->ime_rect_w, wl->ime_rect_h);
    } else {
        zwp_text_input_v3_set_cursor_rectangle(wl->text_input, 48, 8, 1, 24);
    }
    wl->ime_committed_enabled = true;
    ime_commit(wl);
}

static void ti_enter(void *data, struct zwp_text_input_v3 *ti, struct wl_surface *surface) {
    (void)ti;
    WaylandApp *wl = data;
    ti_clear_pending(wl);
    wl->ti_focused = (surface == wl->surface);
    wl->ime_committed_enabled = false;
    if (wl->ti_focused && wl->ime_enabled) {
        ime_send_enable_state(wl);
    }
}

static void ti_leave(void *data, struct zwp_text_input_v3 *ti, struct wl_surface *surface) {
    (void)ti;
    WaylandApp *wl = data;
    if (surface != wl->surface) return;
    wl->ti_focused = false;
    wl->ime_committed_enabled = false;
    ti_clear_pending(wl);
    if (wl->cbs.on_ime) {
        WaylandImeEvent ev = {.reset = true};
        wl->cbs.on_ime(wl->user_ctx, &ev);
    }
}

static void ti_preedit_string(void *data, struct zwp_text_input_v3 *ti,
                              const char *text, int32_t cursor_begin, int32_t cursor_end) {
    (void)ti;
    WaylandApp *wl = data;
    free(wl->ti_pending_preedit);
    wl->ti_pending_preedit = (text && text[0]) ? tt_strdup(text) : NULL;
    wl->ti_pending_cursor_begin = cursor_begin;
    wl->ti_pending_cursor_end = cursor_end;
}

static void ti_commit_string(void *data, struct zwp_text_input_v3 *ti, const char *text) {
    (void)ti;
    WaylandApp *wl = data;
    free(wl->ti_pending_commit);
    wl->ti_pending_commit = (text && text[0]) ? tt_strdup(text) : NULL;
}

static void ti_delete_surrounding_text(void *data, struct zwp_text_input_v3 *ti,
                                       uint32_t before_length, uint32_t after_length) {
    (void)ti;
    WaylandApp *wl = data;
    wl->ti_pending_del_before = before_length;
    wl->ti_pending_del_after = after_length;
}

static void ti_done(void *data, struct zwp_text_input_v3 *ti, uint32_t serial) {
    (void)ti;
    (void)serial;
    WaylandApp *wl = data;

    char *preedit = wl->ti_pending_preedit;
    int32_t cbegin = wl->ti_pending_cursor_begin;
    int32_t cend = wl->ti_pending_cursor_end;
    char *commit = wl->ti_pending_commit;
    uint32_t del_before = wl->ti_pending_del_before;
    uint32_t del_after = wl->ti_pending_del_after;

    wl->ti_pending_preedit = NULL;
    wl->ti_pending_cursor_begin = 0;
    wl->ti_pending_cursor_end = 0;
    wl->ti_pending_commit = NULL;
    wl->ti_pending_del_before = 0;
    wl->ti_pending_del_after = 0;

    if (wl->cbs.on_ime) {
        WaylandImeEvent ev = {
            .preedit_text = preedit,
            .preedit_cursor_begin = cbegin,
            .preedit_cursor_end = cend,
            .commit_text = commit,
            .delete_before = del_before,
            .delete_after = del_after,
            .reset = false
        };
        wl->cbs.on_ime(wl->user_ctx, &ev);
    }

    free(preedit);
    free(commit);
}

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = ti_enter,
    .leave = ti_leave,
    .preedit_string = ti_preedit_string,
    .commit_string = ti_commit_string,
    .delete_surrounding_text = ti_delete_surrounding_text,
    .done = ti_done
};

void wayland_ime_set_enabled(WaylandApp *wl, bool enabled) {
    if (!wl) return;
    wl->ime_enabled = enabled;
    if (!wl->text_input || !wl->ti_focused) return;

    if (enabled) {
        if (!wl->ime_committed_enabled) {
            ime_send_enable_state(wl);
        }
    } else {
        if (wl->ime_committed_enabled) {
            zwp_text_input_v3_disable(wl->text_input);
            wl->ime_committed_enabled = false;
            ime_commit(wl);
        }
    }
}

void wayland_ime_set_surrounding(WaylandApp *wl, const char *text,
                                 int32_t cursor_byte, int32_t anchor_byte,
                                 bool external_change) {
    if (!wl) return;
    const char *src = text ? text : "";
    size_t len = strlen(src);
    size_t cur = cursor_byte < 0 ? 0 : ((size_t)cursor_byte > len ? len : (size_t)cursor_byte);
    size_t anc = anchor_byte < 0 ? 0 : ((size_t)anchor_byte > len ? len : (size_t)anchor_byte);
    cur = utf8_clamp_pos(src, cur, len);
    anc = utf8_clamp_pos(src, anc, len);

    /* Respect Wayland text-input-v3 4000-byte surrounding text limit */
    const size_t max_bytes = 3900;
    size_t win_start = 0;
    size_t win_end = len;
    if (len > max_bytes) {
        size_t half = max_bytes / 2;
        win_start = cur > half ? (cur - half) : 0;
        while (win_start < len && ((unsigned char)src[win_start] & 0xC0u) == 0x80u) {
            win_start++;
        }
        win_end = win_start + max_bytes;
        if (win_end > len) win_end = len;
        win_end = utf8_clamp_pos(src, win_end, len);
        if (cur < win_start) cur = win_start;
        if (cur > win_end) cur = win_end;
        if (anc < win_start) anc = win_start;
        if (anc > win_end) anc = win_end;
    }

    size_t slice_len = win_end - win_start;
    int32_t rel_cur = (int32_t)(cur - win_start);
    int32_t rel_anc = (int32_t)(anc - win_start);

    if (wl->ime_has_surrounding && wl->ime_surrounding_text &&
        strlen(wl->ime_surrounding_text) == slice_len &&
        memcmp(wl->ime_surrounding_text, src + win_start, slice_len) == 0 &&
        wl->ime_surrounding_cursor == rel_cur &&
        wl->ime_surrounding_anchor == rel_anc &&
        !external_change) {
        return;
    }

    char *copy = tt_strndup(src + win_start, slice_len);
    if (!copy) return;
    free(wl->ime_surrounding_text);
    wl->ime_surrounding_text = copy;
    wl->ime_surrounding_cursor = rel_cur;
    wl->ime_surrounding_anchor = rel_anc;
    wl->ime_has_surrounding = true;

    if (wl->text_input && wl->ti_focused && wl->ime_committed_enabled) {
        zwp_text_input_v3_set_surrounding_text(
            wl->text_input, wl->ime_surrounding_text, rel_cur, rel_anc);
        zwp_text_input_v3_set_text_change_cause(
            wl->text_input,
            external_change ? ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER
                            : ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD);
        ime_commit(wl);
    }
}

void wayland_ime_set_cursor_rect(WaylandApp *wl, int32_t x, int32_t y,
                                 int32_t w, int32_t h) {
    if (!wl) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (wl->ime_has_rect &&
        wl->ime_rect_x == x && wl->ime_rect_y == y &&
        wl->ime_rect_w == w && wl->ime_rect_h == h) {
        return;
    }
    wl->ime_rect_x = x;
    wl->ime_rect_y = y;
    wl->ime_rect_w = w;
    wl->ime_rect_h = h;
    wl->ime_has_rect = true;

    if (wl->text_input && wl->ti_focused && wl->ime_committed_enabled) {
        zwp_text_input_v3_set_cursor_rectangle(wl->text_input, x, y, w, h);
        ime_commit(wl);
    }
}

/* Data Offer / Clipboard */
static void data_offer_handle(void *data, struct wl_data_offer *offer, const char *mime) {
    (void)offer;
    WaylandOffer *wo = data;
    if (!mime) return;
    if (strcmp(mime, "text/plain;charset=utf-8") == 0) wo->has_utf8 = true;
    else if (strcmp(mime, "text/plain") == 0) wo->has_plain = true;
}

static void data_offer_source_actions(void *d, struct wl_data_offer *o, uint32_t a) {
    (void)d; (void)o; (void)a;
}
static void data_offer_action(void *d, struct wl_data_offer *o, uint32_t a) {
    (void)d; (void)o; (void)a;
}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_handle,
    .source_actions = data_offer_source_actions,
    .action = data_offer_action
};

static void free_offer(WaylandOffer *wo) {
    if (!wo) return;
    if (wo->offer) wl_data_offer_destroy(wo->offer);
    free(wo);
}

static void data_device_offer(void *data, struct wl_data_device *dev, struct wl_data_offer *id) {
    (void)dev;
    WaylandApp *wl = data;
    if (!id) return;
    WaylandOffer *wo = calloc(1, sizeof(WaylandOffer));
    if (!wo) {
        wl_data_offer_destroy(id);
        return;
    }
    wo->offer = id;
    wo->next = wl->offers;
    wl->offers = wo;
    wl_data_offer_add_listener(id, &data_offer_listener, wo);
}

static void data_device_enter(void *d, struct wl_data_device *dev, uint32_t s,
                              struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y,
                              struct wl_data_offer *id) {
    (void)d; (void)dev; (void)s; (void)surf; (void)x; (void)y; (void)id;
}
static void data_device_leave(void *d, struct wl_data_device *dev) { (void)d; (void)dev; }
static void data_device_motion(void *d, struct wl_data_device *dev, uint32_t t,
                               wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)dev; (void)t; (void)x; (void)y;
}
static void data_device_drop(void *d, struct wl_data_device *dev) { (void)d; (void)dev; }

static void data_device_selection(void *data, struct wl_data_device *dev,
                                  struct wl_data_offer *id) {
    (void)dev;
    WaylandApp *wl = data;
    if (wl->selection_offer) {
        free_offer(wl->selection_offer);
        wl->selection_offer = NULL;
    }
    if (!id) return;

    WaylandOffer **prev = &wl->offers;
    while (*prev) {
        WaylandOffer *cur = *prev;
        if (cur->offer == id) {
            *prev = cur->next;
            cur->next = NULL;
            wl->selection_offer = cur;
            return;
        }
        prev = &cur->next;
    }
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_offer,
    .enter = data_device_enter,
    .leave = data_device_leave,
    .motion = data_device_motion,
    .drop = data_device_drop,
    .selection = data_device_selection
};

static void data_source_target(void *d, struct wl_data_source *s, const char *m) {
    (void)d; (void)s; (void)m;
}

static void data_source_send(void *data, struct wl_data_source *source,
                             const char *mime_type, int32_t fd) {
    (void)source; (void)mime_type;
    WaylandApp *wl = data;
    if (wl->owned_clipboard) {
        size_t len = strlen(wl->owned_clipboard);
        size_t off = 0;
        while (off < len) {
            ssize_t w = write(fd, wl->owned_clipboard + off, len - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
    }
    close(fd);
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
    WaylandApp *wl = data;
    if (wl->clipboard_source == source) {
        wl->clipboard_source = NULL;
    }
    wl_data_source_destroy(source);
}

static void data_source_dnd_drop(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void data_source_dnd_fin(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static void data_source_action(void *d, struct wl_data_source *s, uint32_t a) {
    (void)d; (void)s; (void)a;
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop,
    .dnd_finished = data_source_dnd_fin,
    .action = data_source_action
};

/* Registry */
static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version) {
    WaylandApp *wl = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        wl->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, v);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        wl->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        uint32_t v = version < 2 ? version : 2;
        wl->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, v);
        xdg_wm_base_add_listener(wl->wm_base, &wm_base_listener, wl);
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 7 ? version : 7;
        wl->seat = wl_registry_bind(reg, name, &wl_seat_interface, v);
        wl_seat_add_listener(wl->seat, &seat_listener, wl);
    } else if (strcmp(iface, wl_output_interface.name) == 0 && !wl->output) {
        uint32_t v = version < 3 ? version : 3;
        wl->output = wl_registry_bind(reg, name, &wl_output_interface, v);
        wl_output_add_listener(wl->output, &output_listener, wl);
    } else if (strcmp(iface, wl_data_device_manager_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        wl->data_mgr = wl_registry_bind(reg, name, &wl_data_device_manager_interface, v);
    } else if (strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
        wl->deco_mgr = wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(iface, zwp_text_input_manager_v3_interface.name) == 0) {
        wl->ti_mgr = wl_registry_bind(reg, name, &zwp_text_input_manager_v3_interface, 1);
    }
}

static void registry_global_remove(void *d, struct wl_registry *r, uint32_t n) {
    (void)d; (void)r; (void)n;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove
};

WaylandApp *wayland_create(int init_width, int init_height, const char *title,
                           const WaylandCallbacks *cbs, void *user_ctx,
                           char *err_buf, size_t err_cap) {
    WaylandApp *wl = calloc(1, sizeof(WaylandApp));
    if (!wl) return NULL;

    wl->width = init_width >= 640 ? init_width : 1024;
    wl->height = init_height >= 400 ? init_height : 640;
    wl->pending_width = wl->width;
    wl->pending_height = wl->height;
    wl->buf_scale = 1;
    wl->repeat_rate = 25;
    wl->repeat_delay = 400;
    wl->repeat_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (cbs) wl->cbs = *cbs;
    wl->user_ctx = user_ctx;

    wl->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!wl->xkb_ctx) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to initialize xkbcommon");
        wayland_destroy(wl);
        return NULL;
    }

    const char *locale = getenv("LC_ALL");
    if (!locale || !locale[0]) locale = getenv("LC_CTYPE");
    if (!locale || !locale[0]) locale = getenv("LANG");
    if (!locale || !locale[0]) locale = "C";
    wl->xkb_compose_table = xkb_compose_table_new_from_locale(
        wl->xkb_ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (wl->xkb_compose_table) {
        wl->xkb_compose_state = xkb_compose_state_new(
            wl->xkb_compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }

    wl->display = wl_display_connect(NULL);
    if (!wl->display) {
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "Failed to connect to Wayland display (is WAYLAND_DISPLAY set?)");
        }
        wayland_destroy(wl);
        return NULL;
    }

    wl->registry = wl_display_get_registry(wl->display);
    wl_registry_add_listener(wl->registry, &registry_listener, wl);
    wl_display_roundtrip(wl->display);
    wl_display_roundtrip(wl->display);

    if (!wl->compositor || !wl->shm || !wl->wm_base) {
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "Wayland compositor missing required protocols");
        }
        wayland_destroy(wl);
        return NULL;
    }

    if (wl->data_mgr && wl->seat) {
        wl->data_device = wl_data_device_manager_get_data_device(wl->data_mgr, wl->seat);
        wl_data_device_add_listener(wl->data_device, &data_device_listener, wl);
    }

    if (wl->ti_mgr && wl->seat) {
        wl->text_input = zwp_text_input_manager_v3_get_text_input(wl->ti_mgr, wl->seat);
        if (wl->text_input) {
            zwp_text_input_v3_add_listener(wl->text_input, &text_input_listener, wl);
        }
    }

    wl->cursor_theme = wl_cursor_theme_load(NULL, 24, wl->shm);
    wl->cursor_surface = wl_compositor_create_surface(wl->compositor);

    wl->surface = wl_compositor_create_surface(wl->compositor);
    wl->xdg_surface = xdg_wm_base_get_xdg_surface(wl->wm_base, wl->surface);
    xdg_surface_add_listener(wl->xdg_surface, &xdg_surface_listener, wl);

    wl->toplevel = xdg_surface_get_toplevel(wl->xdg_surface);
    xdg_toplevel_add_listener(wl->toplevel, &toplevel_listener, wl);
    xdg_toplevel_set_title(wl->toplevel, title ? title : "TuiTube");
    xdg_toplevel_set_app_id(wl->toplevel, "tuitube");
    xdg_toplevel_set_min_size(wl->toplevel, 640, 400);

    if (wl->deco_mgr) {
        wl->toplevel_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(
            wl->deco_mgr, wl->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(wl->toplevel_deco, &deco_listener, wl);
        zxdg_toplevel_decoration_v1_set_mode(
            wl->toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        wl->needs_csd = false;
    } else {
        wl->needs_csd = true;
    }

    wl_surface_commit(wl->surface);
    wl_display_roundtrip(wl->display);
    wl->dirty = true;
    return wl;
}

void wayland_destroy(WaylandApp *wl) {
    if (!wl) return;
    shm_buffer_destroy(&wl->buffers[0]);
    shm_buffer_destroy(&wl->buffers[1]);

    if (wl->clipboard_source) wl_data_source_destroy(wl->clipboard_source);
    free(wl->owned_clipboard);
    free_offer(wl->selection_offer);
    while (wl->offers) {
        WaylandOffer *n = wl->offers->next;
        free_offer(wl->offers);
        wl->offers = n;
    }
    if (wl->data_device) wl_data_device_destroy(wl->data_device);

    if (wl->toplevel_deco) zxdg_toplevel_decoration_v1_destroy(wl->toplevel_deco);
    if (wl->toplevel) xdg_toplevel_destroy(wl->toplevel);
    if (wl->xdg_surface) xdg_surface_destroy(wl->xdg_surface);
    if (wl->surface) wl_surface_destroy(wl->surface);
    if (wl->cursor_surface) wl_surface_destroy(wl->cursor_surface);
    if (wl->cursor_theme) wl_cursor_theme_destroy(wl->cursor_theme);

    if (wl->pointer) wl_pointer_destroy(wl->pointer);
    if (wl->keyboard) wl_keyboard_destroy(wl->keyboard);
    if (wl->text_input) zwp_text_input_v3_destroy(wl->text_input);
    ti_clear_pending(wl);
    free(wl->ime_surrounding_text);
    if (wl->xkb_compose_state) xkb_compose_state_unref(wl->xkb_compose_state);
    if (wl->xkb_compose_table) xkb_compose_table_unref(wl->xkb_compose_table);
    if (wl->xkb_state) xkb_state_unref(wl->xkb_state);
    if (wl->xkb_keymap) xkb_keymap_unref(wl->xkb_keymap);
    if (wl->xkb_ctx) xkb_context_unref(wl->xkb_ctx);
    if (wl->repeat_timer_fd >= 0) close(wl->repeat_timer_fd);

    if (wl->ti_mgr) zwp_text_input_manager_v3_destroy(wl->ti_mgr);
    if (wl->deco_mgr) zxdg_decoration_manager_v1_destroy(wl->deco_mgr);
    if (wl->data_mgr) wl_data_device_manager_destroy(wl->data_mgr);
    if (wl->output) wl_output_destroy(wl->output);
    if (wl->seat) wl_seat_destroy(wl->seat);
    if (wl->wm_base) xdg_wm_base_destroy(wl->wm_base);
    if (wl->shm) wl_shm_destroy(wl->shm);
    if (wl->compositor) wl_compositor_destroy(wl->compositor);
    if (wl->registry) wl_registry_destroy(wl->registry);
    if (wl->display) wl_display_disconnect(wl->display);
    free(wl);
}

bool wayland_needs_csd(const WaylandApp *wl) { return wl ? wl->needs_csd : false; }
bool wayland_is_maximized(const WaylandApp *wl) { return wl ? wl->maximized : false; }
bool wayland_should_quit(const WaylandApp *wl) { return wl ? wl->quit : true; }
void wayland_request_quit(WaylandApp *wl) { if (wl) wl->quit = true; }
void wayland_request_redraw(WaylandApp *wl) { if (wl) wl->dirty = true; }

void wayland_start_move(WaylandApp *wl) {
    if (wl && wl->toplevel && wl->seat) {
        xdg_toplevel_move(wl->toplevel, wl->seat, wl->last_serial);
    }
}

void wayland_start_resize(WaylandApp *wl, uint32_t edges) {
    if (wl && wl->toplevel && wl->seat) {
        xdg_toplevel_resize(wl->toplevel, wl->seat, wl->last_serial, edges);
    }
}

void wayland_toggle_maximize(WaylandApp *wl) {
    if (!wl || !wl->toplevel) return;
    if (wl->maximized) xdg_toplevel_unset_maximized(wl->toplevel);
    else xdg_toplevel_set_maximized(wl->toplevel);
}

void wayland_minimize(WaylandApp *wl) {
    if (wl && wl->toplevel) xdg_toplevel_set_minimized(wl->toplevel);
}

bool wayland_clipboard_copy(WaylandApp *wl, const char *text) {
    if (!text) return false;
    if (wl && wl->data_mgr && wl->data_device) {
        free(wl->owned_clipboard);
        wl->owned_clipboard = tt_strdup(text);
        if (wl->clipboard_source) {
            wl_data_source_destroy(wl->clipboard_source);
            wl->clipboard_source = NULL;
        }
        wl->clipboard_source = wl_data_device_manager_create_data_source(wl->data_mgr);
        if (wl->clipboard_source) {
            wl_data_source_add_listener(wl->clipboard_source, &data_source_listener, wl);
            wl_data_source_offer(wl->clipboard_source, "text/plain;charset=utf-8");
            wl_data_source_offer(wl->clipboard_source, "text/plain");
            wl_data_device_set_selection(wl->data_device, wl->clipboard_source, wl->last_serial);
            wl_display_flush(wl->display);
        }
    }
    player_copy_wl_copy(text);
    return true;
}

char *wayland_clipboard_paste(WaylandApp *wl) {
    if (!wl) return NULL;
    /* If TuiTube itself currently owns the clipboard source, return our copy directly */
    if (wl->clipboard_source && wl->owned_clipboard) {
        return tt_strdup(wl->owned_clipboard);
    }
    if (!wl->selection_offer || !wl->selection_offer->offer) return NULL;
    const char *mime = NULL;
    if (wl->selection_offer->has_utf8) mime = "text/plain;charset=utf-8";
    else if (wl->selection_offer->has_plain) mime = "text/plain";
    else return NULL;

    int pfd[2];
    if (pipe(pfd) != 0) return NULL;
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);

    wl_data_offer_receive(wl->selection_offer->offer, mime, pfd[1]);
    close(pfd[1]);
    wl_display_roundtrip(wl->display);

    StrBuf sb;
    sb_init(&sb);
    struct pollfd p = {.fd = pfd[0], .events = POLLIN};
    char buf[4096];
    for (int iter = 0; iter < 50; iter++) {
        int r = poll(&p, 1, 20);
        if (r <= 0) break;
        ssize_t n = read(pfd[0], buf, sizeof(buf));
        if (n > 0) {
            if (sb.len + (size_t)n > 1024u * 1024u) break;
            sb_append_n(&sb, buf, (size_t)n);
        } else if (n == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }
    close(pfd[0]);
    if (sb.len == 0) {
        sb_free(&sb);
        return NULL;
    }
    return sb_take(&sb);
}

void wayland_render_if_needed(WaylandApp *wl) {
    if (!wl || !wl->configured || !wl->dirty) return;
    if (wl->width <= 0 || wl->height <= 0) return;

    ShmBuffer *b = NULL;
    for (int i = 0; i < 2; i++) {
        if (!wl->buffers[i].busy) {
            b = &wl->buffers[i];
            break;
        }
    }
    if (!b) return;

    if (!shm_buffer_ensure(wl, b, wl->width, wl->height, wl->buf_scale)) return;

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        b->data, CAIRO_FORMAT_ARGB32, b->width, b->height, b->stride);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf) cairo_surface_destroy(surf);
        return;
    }
    cairo_t *cr = cairo_create(surf);
    cairo_scale(cr, (double)wl->buf_scale, (double)wl->buf_scale);

    if (wl->cbs.on_render) {
        wl->cbs.on_render(wl->user_ctx, cr, wl->width, wl->height);
    }

    cairo_destroy(cr);
    cairo_surface_flush(surf);
    cairo_surface_destroy(surf);

    wl_surface_set_buffer_scale(wl->surface, wl->buf_scale);
    wl_surface_attach(wl->surface, b->buffer, 0, 0);
    wl_surface_damage(wl->surface, 0, 0, wl->width, wl->height);
    b->busy = true;
    wl->dirty = false;
    wl_surface_commit(wl->surface);
    wl_display_flush(wl->display);
}

int wayland_poll_once(WaylandApp *wl, int extra_fd, int timeout_ms, bool *out_extra_ready) {
    if (out_extra_ready) *out_extra_ready = false;
    if (!wl) return -1;

    while (wl_display_prepare_read(wl->display) != 0) {
        wl_display_dispatch_pending(wl->display);
    }
    wl_display_flush(wl->display);

    struct pollfd pfds[3];
    nfds_t nfds = 0;

    int wl_idx = (int)nfds;
    pfds[nfds++] = (struct pollfd){.fd = wl_display_get_fd(wl->display), .events = POLLIN};

    int timer_idx = -1;
    if (wl->repeat_timer_fd >= 0) {
        timer_idx = (int)nfds;
        pfds[nfds++] = (struct pollfd){.fd = wl->repeat_timer_fd, .events = POLLIN};
    }

    int extra_idx = -1;
    if (extra_fd >= 0) {
        extra_idx = (int)nfds;
        pfds[nfds++] = (struct pollfd){.fd = extra_fd, .events = POLLIN};
    }

    int ret = poll(pfds, nfds, timeout_ms);
    if (ret < 0) {
        wl_display_cancel_read(wl->display);
        if (errno == EINTR) return 0;
        return -1;
    }

    if (pfds[wl_idx].revents & POLLIN) {
        if (wl_display_read_events(wl->display) < 0) {
            return -1;
        }
        wl_display_dispatch_pending(wl->display);
    } else {
        wl_display_cancel_read(wl->display);
    }

    if (pfds[wl_idx].revents & (POLLERR | POLLHUP)) {
        return -1;
    }

    if (timer_idx >= 0 && (pfds[timer_idx].revents & POLLIN)) {
        uint64_t exp = 0;
        ssize_t r = read(wl->repeat_timer_fd, &exp, sizeof(exp));
        (void)r;
        if (wl->repeating) {
            dispatch_xkb_key(wl, wl->repeat_keycode, true);
        }
    }

    if (extra_idx >= 0 && (pfds[extra_idx].revents & POLLIN)) {
        if (out_extra_ready) *out_extra_ready = true;
    }

    return ret;
}
