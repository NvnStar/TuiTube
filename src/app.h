#ifndef TUITUBE_APP_H
#define TUITUBE_APP_H

#include "config.h"
#include "http.h"
#include "image.h"
#include "innertube.h"
#include "model.h"
#include "ui.h"
#include "wayland.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MODE_SEARCH = 0,
    MODE_BROWSE,
    MODE_NEXT,
    MODE_FEED,
    MODE_FAVORITES,
    MODE_HISTORY
} ViewMode;

typedef bool (*AppPlayVideoFn)(void *play_ctx, const TuiTubeConfig *cfg,
                               const MediaItem *video,
                               char *err_buf, size_t err_cap);

typedef struct {
    TuiTubeConfig cfg;
    FavoriteList favs;
    HistoryList history;
    ThumbCache *thumbs;
    HttpPool *pool;
    WaylandApp *wl;
    UiState *ui;

    AppPlayVideoFn play_fn;
    void *play_ctx;
    char player_label[64];

    uint64_t list_gen;
    uint64_t detail_gen;

    ViewMode view_mode;
    bool has_view;
    NavStack nav_stack;
    char *last_query;
    SearchFilter last_filter;
    char *last_browse_id;
    char *last_browse_params;
    char *last_next_video_id;
    char *last_heading;
} AppState;

bool app_init(AppState *app, const char *cli_config_dir, WaylandApp *wl,
              AppPlayVideoFn play_fn, void *play_ctx, const char *player_display_name);
bool app_init_ex(AppState *app, const char *cli_config_dir, const char *override_config_file,
                 WaylandApp *wl, AppPlayVideoFn play_fn, void *play_ctx,
                 const char *player_display_name);
void app_destroy(AppState *app);
int app_notify_fd(const AppState *app);
void *app_notify_handle(const AppState *app);
void app_process_async_results(AppState *app);
void app_poll_downloads(AppState *app);
void app_release_hidden_resources(AppState *app);

#endif
