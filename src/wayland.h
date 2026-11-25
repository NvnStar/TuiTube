#ifndef TUITUBE_WAYLAND_H
#define TUITUBE_WAYLAND_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

typedef enum {
    TT_CURSOR_DEFAULT = 0,
    TT_CURSOR_TEXT,
    TT_CURSOR_POINTER,
    TT_CURSOR_COL_RESIZE,
    TT_CURSOR_NW_RESIZE,
    TT_CURSOR_NE_RESIZE,
    TT_CURSOR_N_RESIZE,
    TT_CURSOR_S_RESIZE,
    TT_CURSOR_E_RESIZE,
    TT_CURSOR_W_RESIZE
} TtCursorShape;

typedef enum {
    TT_BTN_LEFT = 1,
    TT_BTN_RIGHT = 2,
    TT_BTN_MIDDLE = 3,
    TT_BTN_BACK = 4,
    TT_BTN_FORWARD = 5
} TtMouseButton;

typedef struct {
    const char *preedit_text;
    int32_t preedit_cursor_begin;
    int32_t preedit_cursor_end;
    const char *commit_text;
    uint32_t delete_before;
    uint32_t delete_after;
    bool reset;
} WaylandImeEvent;

typedef struct {
    void (*on_render)(void *ctx, cairo_t *cr, int width, int height);
    void (*on_resize)(void *ctx, int width, int height);
    void (*on_pointer_motion)(void *ctx, double x, double y);
    void (*on_pointer_button)(void *ctx, TtMouseButton btn, bool pressed,
                              bool double_click, double x, double y);
    void (*on_pointer_axis)(void *ctx, double dx, double dy);
    void (*on_pointer_leave)(void *ctx);
    void (*on_key)(void *ctx, uint32_t mods, xkb_keysym_t sym,
                   const char *utf8, bool pressed);
    void (*on_ime)(void *ctx, const WaylandImeEvent *ev);
} WaylandCallbacks;

typedef struct WaylandApp WaylandApp;

WaylandApp *wayland_create(int init_width, int init_height, const char *title,
                           const WaylandCallbacks *cbs, void *user_ctx,
                           char *err_buf, size_t err_cap);
void wayland_destroy(WaylandApp *wl);

bool wayland_needs_csd(const WaylandApp *wl);
bool wayland_is_maximized(const WaylandApp *wl);
bool wayland_should_quit(const WaylandApp *wl);
void wayland_request_quit(WaylandApp *wl);
void wayland_request_redraw(WaylandApp *wl);
void wayland_set_cursor(WaylandApp *wl, TtCursorShape shape);

void wayland_start_move(WaylandApp *wl);
void wayland_start_resize(WaylandApp *wl, uint32_t edges);
void wayland_toggle_maximize(WaylandApp *wl);
void wayland_minimize(WaylandApp *wl);

bool wayland_clipboard_copy(WaylandApp *wl, const char *text);
char *wayland_clipboard_paste(WaylandApp *wl);

void wayland_ime_set_enabled(WaylandApp *wl, bool enabled);
void wayland_ime_set_surrounding(WaylandApp *wl, const char *text,
                                 int32_t cursor_byte, int32_t anchor_byte,
                                 bool external_change);
void wayland_ime_set_cursor_rect(WaylandApp *wl, int32_t x, int32_t y,
                                 int32_t w, int32_t h);

int wayland_poll_once(WaylandApp *wl, int extra_fd, int timeout_ms, bool *out_extra_ready);
void wayland_render_if_needed(WaylandApp *wl);

#endif
