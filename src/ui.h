#ifndef TUITUBE_UI_H
#define TUITUBE_UI_H

#include "config.h"
#include "image.h"
#include "innertube.h"
#include "model.h"
#include "util.h"
#include "wayland.h"

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct UiState UiState;

typedef enum {
    UI_VIEW_NONE = 0,
    UI_VIEW_SEARCH,
    UI_VIEW_BROWSE,
    UI_VIEW_NEXT,
    UI_VIEW_FEED,
    UI_VIEW_FAVORITES,
    UI_VIEW_HISTORY
} UiViewKind;

typedef struct {
    void (*on_search)(void *ctx, const char *query, SearchFilter filter);
    void (*on_next_page)(void *ctx);
    void (*on_show_favorites)(void *ctx);
    void (*on_show_feed)(void *ctx);
    void (*on_show_history)(void *ctx);
    void (*on_select_item)(void *ctx, size_t index);
    void (*on_activate_item)(void *ctx, size_t index);
    void (*on_download_video)(void *ctx);
    void (*on_view_channel)(void *ctx);
    void (*on_view_recommended)(void *ctx);
    void (*on_toggle_favorite)(void *ctx);
    void (*on_clear_cache)(void *ctx);
    void (*on_request_thumb)(void *ctx, const char *key, const char *url);
    void (*on_navigate_back)(void *ctx);
    void (*on_retry)(void *ctx);
    void (*on_switch_channel_tab)(void *ctx, bool live_tab);
} UiCallbacks;

UiState *ui_create(TuiTubeConfig *cfg, WaylandApp *wl, ThumbCache *thumbs,
                   FavoriteList *favs, HistoryList *history,
                   const UiCallbacks *cbs, void *user_ctx);
void ui_destroy(UiState *ui);

void ui_reload_font(UiState *ui);
void ui_reload_mascot(UiState *ui);

void ui_set_status(UiState *ui, const char *fmt, ...);
void ui_set_list(UiState *ui, MediaList *new_list, bool append);
void ui_set_detail_item(UiState *ui, const MediaItem *detail);
void ui_mark_video_watched(UiState *ui, const char *video_id);
void ui_on_thumb_loaded(UiState *ui, const char *key, cairo_surface_t *surf);
void ui_set_loading_more(UiState *ui, bool loading);
void ui_set_nav_state(UiState *ui, UiViewKind view_kind, bool can_go_back);
void ui_set_channel_tabs(UiState *ui, bool show_tabs, bool live_tab);
void ui_set_busy(UiState *ui, bool busy);
void ui_set_error(UiState *ui, const char *err_msg);

MediaList *ui_get_list(UiState *ui);
const MediaItem *ui_get_selected_item(const UiState *ui);
const MediaItem *ui_get_detail_item(const UiState *ui);
SearchFilter ui_get_filter(const UiState *ui);
void ui_set_search_text(UiState *ui, const char *text);
double ui_get_list_scroll(const UiState *ui);
ssize_t ui_get_selected_index(const UiState *ui);
void ui_restore_list_state(UiState *ui, MediaList *restored, ssize_t selected_idx, double scroll_y);
bool ui_can_escape_internally(const UiState *ui);
void ui_release_hidden_caches(UiState *ui);

/* Wayland event handlers */
void ui_on_render(void *ctx, cairo_t *cr, int width, int height);
void ui_on_resize(void *ctx, int width, int height);
void ui_on_pointer_motion(void *ctx, double x, double y);
void ui_on_pointer_button(void *ctx, TtMouseButton btn, bool pressed,
                          bool double_click, double x, double y);
void ui_on_pointer_axis(void *ctx, double dx, double dy);
void ui_on_pointer_leave(void *ctx);
void ui_on_key(void *ctx, uint32_t mods, xkb_keysym_t sym,
               const char *utf8, bool pressed);
void ui_on_ime(void *ctx, const WaylandImeEvent *ev);

#endif
