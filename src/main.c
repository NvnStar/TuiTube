#include "config.h"
#include "http.h"
#include "image.h"
#include "innertube.h"
#include "model.h"
#include "player.h"
#include "ui.h"
#include "util.h"
#include "wayland.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    MODE_SEARCH = 0,
    MODE_BROWSE,
    MODE_NEXT,
    MODE_FEED,
    MODE_FAVORITES,
    MODE_HISTORY
} ViewMode;

typedef struct {
    TuiTubeConfig cfg;
    FavoriteList favs;
    HistoryList history;
    ThumbCache *thumbs;
    HttpPool *pool;
    WaylandApp *wl;
    UiState *ui;

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

static UiViewKind view_mode_to_ui_kind(ViewMode m) {
    switch (m) {
        case MODE_SEARCH:    return UI_VIEW_SEARCH;
        case MODE_BROWSE:    return UI_VIEW_BROWSE;
        case MODE_NEXT:      return UI_VIEW_NEXT;
        case MODE_FEED:      return UI_VIEW_FEED;
        case MODE_FAVORITES: return UI_VIEW_FAVORITES;
        case MODE_HISTORY:   return UI_VIEW_HISTORY;
        default:             return UI_VIEW_NONE;
    }
}

static void sync_ui_nav(AppState *app) {
    if (!app || !app->ui) return;
    ui_set_nav_state(app->ui,
                     app->has_view ? view_mode_to_ui_kind(app->view_mode) : UI_VIEW_NONE,
                     nav_stack_can_pop(&app->nav_stack));
    bool is_chan = app->has_view && app->view_mode == MODE_BROWSE &&
                   app->last_browse_id && strncmp(app->last_browse_id, "UC", 2) == 0;
    bool is_live_tab = is_chan && app->last_browse_params &&
                       strncmp(app->last_browse_params, "EgdzdHJlYW1z", 12) == 0;
    ui_set_channel_tabs(app->ui, is_chan, is_live_tab);
}

static void push_current_nav(AppState *app) {
    if (!app || !app->ui) return;
    MediaList *cur = ui_get_list(app->ui);
    if (!cur || cur->count == 0) return;

    NavEntry snap = {0};
    snap.list = *cur;
    snap.selected_idx = ui_get_selected_index(app->ui);
    snap.scroll_y = ui_get_list_scroll(app->ui);
    snap.view_mode = (int)app->view_mode;
    snap.filter_sort = (int)app->last_filter.sort;
    snap.filter_type = (int)app->last_filter.type;
    snap.filter_live_only = app->last_filter.live_only;
    snap.query = app->last_query;
    snap.browse_id = app->last_browse_id;
    snap.browse_params = app->last_browse_params;
    snap.next_video_id = app->last_next_video_id;
    snap.heading = app->last_heading;
    nav_stack_push(&app->nav_stack, &snap);
}

static char *favlist_path(const AppState *app) {
    return tt_path_join(app->cfg.config_dir, "favlist.json");
}

static char *history_path(const AppState *app) {
    return tt_path_join(app->cfg.config_dir, "history.json");
}

/* Async job payloads & results */
typedef struct {
    char *query;
    SearchFilter filter;
    char *continuation;
    bool append;
    bool hide_members_only;
} SearchJobReq;

typedef struct {
    bool ok;
    bool append;
    MediaList list;
    char error[256];
} ListJobRes;

static void free_search_req(void *p) {
    SearchJobReq *r = p;
    if (!r) return;
    free(r->query);
    free(r->continuation);
    free(r);
}

static void free_list_res(void *p) {
    ListJobRes *r = p;
    if (!r) return;
    media_list_free(&r->list);
    free(r);
}

static void *run_search_job(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    (void)gen; (void)pool;
    SearchJobReq *req = arg;
    ListJobRes *res = calloc(1, sizeof(ListJobRes));
    if (!res) return NULL;
    media_list_init(&res->list);
    res->append = req->append;
    res->list.heading = tt_strdup("Search Results");
    res->ok = innertube_search(curl, req->query, req->filter, req->continuation,
                               &res->list, res->error, sizeof(res->error));
    if (res->ok && req->hide_members_only) {
        media_list_filter_members_only(&res->list);
    }
    return res;
}

typedef struct {
    char *browse_id;
    char *params;
    char *continuation;
    char *heading;
    bool append;
    bool hide_members_only;
} BrowseJobReq;

static void free_browse_req(void *p) {
    BrowseJobReq *r = p;
    if (!r) return;
    free(r->browse_id);
    free(r->params);
    free(r->continuation);
    free(r->heading);
    free(r);
}

static void *run_browse_job(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    (void)gen; (void)pool;
    BrowseJobReq *req = arg;
    ListJobRes *res = calloc(1, sizeof(ListJobRes));
    if (!res) return NULL;
    media_list_init(&res->list);
    res->append = req->append;
    if (req->heading) res->list.heading = tt_strdup(req->heading);
    if (req->browse_id && strncmp(req->browse_id, "UC", 2) == 0) {
        bool live_tab = req->params && strncmp(req->params, "EgdzdHJlYW1z", 12) == 0;
        res->ok = innertube_browse_channel(curl, req->browse_id, live_tab,
                                           req->continuation, req->hide_members_only,
                                           &res->list, res->error, sizeof(res->error));
    } else {
        res->ok = innertube_browse(curl, req->browse_id, req->params, req->continuation,
                                   &res->list, res->error, sizeof(res->error));
        if (res->ok && req->hide_members_only) {
            media_list_filter_members_only(&res->list);
        }
    }
    if (!res->list.heading) res->list.heading = tt_strdup("Browse");
    return res;
}

typedef struct {
    char *video_id;
    char *continuation;
    bool load_list;
    bool append;
    bool hide_members_only;
} NextJobReq;

typedef struct {
    bool ok;
    bool load_list;
    bool append;
    MediaItem details;
    MediaList recommended;
    char error[256];
} NextJobRes;

static void free_next_req(void *p) {
    NextJobReq *r = p;
    if (!r) return;
    free(r->video_id);
    free(r->continuation);
    free(r);
}

static void free_next_res(void *p) {
    NextJobRes *r = p;
    if (!r) return;
    media_item_free_fields(&r->details);
    media_list_free(&r->recommended);
    free(r);
}

static void *run_next_job(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    (void)gen; (void)pool;
    NextJobReq *req = arg;
    NextJobRes *res = calloc(1, sizeof(NextJobRes));
    if (!res) return NULL;
    res->load_list = req->load_list;
    res->append = req->append;
    media_list_init(&res->recommended);
    res->recommended.heading = tt_strdup("Recommended");
    res->ok = innertube_next(curl, req->video_id, req->continuation,
                             req->continuation ? NULL : &res->details,
                             req->load_list ? &res->recommended : NULL,
                             res->error, sizeof(res->error));
    if (res->ok && req->load_list && req->hide_members_only) {
        media_list_filter_members_only(&res->recommended);
    }
    return res;
}

typedef struct {
    char *url;
    bool prefer_live_tab;
    bool hide_members_only;
} ResolveJobReq;

typedef struct {
    bool ok;
    ParsedUrl parsed;
    MediaList list;
    char error[256];
} ResolveJobRes;

static void free_resolve_req(void *p) {
    ResolveJobReq *r = p;
    if (!r) return;
    free(r->url);
    free(r);
}

static void free_resolve_res(void *p) {
    ResolveJobRes *r = p;
    if (!r) return;
    media_list_free(&r->list);
    free(r);
}

static void *run_resolve_job(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    (void)gen; (void)pool;
    ResolveJobReq *req = arg;
    ResolveJobRes *res = calloc(1, sizeof(ResolveJobRes));
    if (!res) return NULL;
    media_list_init(&res->list);
    res->ok = innertube_resolve_url(curl, req->url, &res->parsed, res->error, sizeof(res->error));
    if (res->ok && req->prefer_live_tab && res->parsed.kind == URL_CHANNEL) {
        res->parsed.prefer_live_tab = true;
    }
    if (res->ok && res->parsed.kind == URL_CHANNEL) {
        res->ok = innertube_browse_channel(curl, res->parsed.id, res->parsed.prefer_live_tab,
                                           NULL, req->hide_members_only,
                                           &res->list, res->error, sizeof(res->error));
    } else if (res->ok && res->parsed.kind == URL_PLAYLIST) {
        char *vl = tt_asprintf("VL%s", res->parsed.id);
        res->ok = innertube_browse(curl, vl, NULL, NULL,
                                   &res->list, res->error, sizeof(res->error));
        if (res->ok && req->hide_members_only) {
            media_list_filter_members_only(&res->list);
        }
        free(vl);
    }
    return res;
}

typedef struct {
    FavoriteList favs_copy;
    bool use_rss;
    int max_per_ch;
    int64_t max_age_sec;
    bool hide_members_only;
} FeedJobReq;

static void free_feed_req(void *p) {
    FeedJobReq *r = p;
    if (!r) return;
    favlist_free(&r->favs_copy);
    free(r);
}

static void *run_feed_job(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    FeedJobReq *req = arg;
    ListJobRes *res = calloc(1, sizeof(ListJobRes));
    if (!res) return NULL;
    media_list_init(&res->list);
    res->ok = innertube_fetch_feed(curl, &req->favs_copy, req->use_rss,
                                   req->max_per_ch, req->max_age_sec,
                                   (int64_t)time(NULL), req->hide_members_only,
                                   &res->list, pool, gen, res->error, sizeof(res->error));
    return res;
}

typedef struct {
    char *cache_dir;
    char *key;
    char *url;
} ThumbJobReq;

typedef struct {
    char *key;
    ImageBuffer img;
    bool ok;
} ThumbJobRes;

static void free_thumb_req(void *p) {
    ThumbJobReq *r = p;
    if (!r) return;
    free(r->cache_dir);
    free(r->key);
    free(r->url);
    free(r);
}

static void free_thumb_res(void *p) {
    ThumbJobRes *r = p;
    if (!r) return;
    free(r->key);
    image_buffer_free(&r->img);
    free(r);
}

static void *run_thumb_job(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    (void)gen; (void)pool;
    ThumbJobReq *req = arg;
    ThumbJobRes *res = calloc(1, sizeof(ThumbJobRes));
    if (!res) return NULL;
    res->key = tt_strdup(req->key);

    size_t disk_len = 0;
    uint8_t *disk_bytes = thumb_disk_load(req->cache_dir, req->key, &disk_len);
    if (disk_bytes) {
        res->ok = image_decode_bytes(disk_bytes, disk_len, &res->img);
        free(disk_bytes);
        if (res->ok) return res;
    }

    HttpResponse resp;
    if (http_get(curl, req->url, 8u * 1024u * 1024u, &resp)) {
        if (resp.status_code >= 200 && resp.status_code < 300 && resp.data && resp.len > 0) {
            res->ok = image_decode_bytes((const uint8_t *)resp.data, resp.len, &res->img);
            if (res->ok) {
                thumb_disk_save(req->cache_dir, req->key, (const uint8_t *)resp.data, resp.len);
            }
        }
        http_response_free(&resp);
    }
    return res;
}

/* UI Callback helpers */
static uint64_t bump_list_gen(AppState *app) {
    app->list_gen++;
    http_pool_set_active_gen(app->pool, app->list_gen);
    return app->list_gen;
}

static void submit_browse(AppState *app, const char *browse_id, const char *params,
                          const char *continuation, const char *heading,
                          bool append, bool push_nav) {
    BrowseJobReq *req = calloc(1, sizeof(BrowseJobReq));
    req->browse_id = tt_strdup(browse_id);
    req->params = tt_strdup(params);
    req->continuation = tt_strdup(continuation);
    req->heading = tt_strdup(heading);
    req->append = append;
    req->hide_members_only = app->cfg.hide_members_only;

    if (!append && push_nav) push_current_nav(app);
    uint64_t gen = bump_list_gen(app);
    app->view_mode = MODE_BROWSE;
    app->has_view = true;
    if (!append) {
        free(app->last_browse_id); app->last_browse_id = tt_strdup(req->browse_id);
        free(app->last_browse_params); app->last_browse_params = tt_strdup(req->params);
        free(app->last_heading); app->last_heading = tt_strdup(req->heading);
        ui_set_error(app->ui, NULL);
        ui_set_busy(app->ui, true);
    }
    sync_ui_nav(app);

    ui_set_status(app->ui, append ? "Loading more..." : "Loading %s...",
                  req->heading ? req->heading : "list");
    http_pool_submit(app->pool, JOB_BROWSE, gen, req, run_browse_job, free_browse_req, free_list_res);
}

static void submit_next(AppState *app, const char *video_id, const char *continuation,
                        bool load_list, bool append, bool push_nav) {
    NextJobReq *req = calloc(1, sizeof(NextJobReq));
    req->video_id = tt_strdup(video_id);
    req->continuation = tt_strdup(continuation);
    req->load_list = load_list;
    req->append = append;
    req->hide_members_only = app->cfg.hide_members_only;

    if (load_list && !append && push_nav) push_current_nav(app);
    uint64_t gen = load_list ? bump_list_gen(app) : ++app->detail_gen;
    if (!load_list) {
        http_pool_set_active_detail_gen(app->pool, gen);
    }
    if (load_list) {
        app->view_mode = MODE_NEXT;
        app->has_view = true;
        if (!append) {
            free(app->last_next_video_id);
            app->last_next_video_id = tt_strdup(req->video_id);
            free(app->last_heading);
            app->last_heading = tt_strdup("Recommended");
            ui_set_error(app->ui, NULL);
            ui_set_busy(app->ui, true);
        }
        sync_ui_nav(app);
        ui_set_status(app->ui, "Loading recommended videos...");
    }

    http_pool_submit(app->pool, load_list ? JOB_NEXT : JOB_DETAIL, gen, req,
                     run_next_job, free_next_req, free_next_res);
}

static void sync_selected_item_details(AppState *app) {
    if (!app || !app->ui) return;
    const MediaItem *sel = ui_get_selected_item(app->ui);
    if (sel && sel->kind == ITEM_VIDEO && sel->id[0] && !sel->details_loaded) {
        submit_next(app, sel->id, NULL, false, false, false);
    } else {
        app->detail_gen++;
        http_pool_set_active_detail_gen(app->pool, app->detail_gen);
    }
}

static void run_search_query(AppState *app, const char *query, SearchFilter filter, bool push_nav) {
    if (!query || !query[0]) {
        ui_set_status(app->ui, "Enter a search query or YouTube URL");
        return;
    }

    ParsedUrl parsed;
    if (innertube_parse_input_url(query, &parsed)) {
        if (parsed.kind == URL_VIDEO) {
            submit_next(app, parsed.id, NULL, true, false, push_nav);
            return;
        } else if (parsed.kind == URL_PLAYLIST) {
            char *vl = tt_asprintf("VL%s", parsed.id);
            submit_browse(app, vl, NULL, NULL, "Playlist", false, push_nav);
            free(vl);
            return;
        } else if (parsed.kind == URL_CHANNEL) {
            submit_browse(app, parsed.id,
                          parsed.prefer_live_tab ? INNERTUBE_CHAN_STREAMS_PARAMS : INNERTUBE_CHAN_VIDEOS_PARAMS,
                          NULL, "Channel Videos", false, push_nav);
            return;
        } else if (parsed.kind == URL_HANDLE) {
            char *qcopy = tt_strdup(query);
            if (push_nav) push_current_nav(app);
            uint64_t gen = bump_list_gen(app);
            app->view_mode = MODE_SEARCH;
            app->has_view = true;
            free(app->last_query); app->last_query = qcopy;
            app->last_filter = filter;
            free(app->last_heading); app->last_heading = tt_strdup(parsed.id);
            ui_set_error(app->ui, NULL);
            ui_set_busy(app->ui, true);
            sync_ui_nav(app);
            ResolveJobReq *req = calloc(1, sizeof(ResolveJobReq));
            req->url = tt_strdup(parsed.resolved_url);
            req->prefer_live_tab = parsed.prefer_live_tab;
            req->hide_members_only = app->cfg.hide_members_only;
            ui_set_status(app->ui, "Resolving %s...", parsed.id);
            http_pool_submit(app->pool, JOB_RESOLVE_URL, gen, req,
                             run_resolve_job, free_resolve_req, free_resolve_res);
            return;
        }
    }

    SearchJobReq *req = calloc(1, sizeof(SearchJobReq));
    req->query = tt_strdup(query);
    req->filter = filter;
    req->hide_members_only = app->cfg.hide_members_only;

    if (push_nav) push_current_nav(app);
    uint64_t gen = bump_list_gen(app);
    app->view_mode = MODE_SEARCH;
    app->has_view = true;
    free(app->last_query); app->last_query = tt_strdup(req->query);
    app->last_filter = filter;
    free(app->last_heading); app->last_heading = tt_strdup("Search Results");
    ui_set_error(app->ui, NULL);
    ui_set_busy(app->ui, true);
    sync_ui_nav(app);

    ui_set_status(app->ui, "Searching: %s...", req->query ? req->query : "");
    http_pool_submit(app->pool, JOB_SEARCH, gen, req, run_search_job, free_search_req, free_list_res);
}

static void cb_search(void *ctx, const char *query, SearchFilter filter) {
    run_search_query((AppState *)ctx, query, filter, true);
}

static void cb_next_page(void *ctx) {
    AppState *app = ctx;
    MediaList *cur = ui_get_list(app->ui);
    if (!cur || !cur->continuation_token || !cur->continuation_token[0]) {
        ui_set_loading_more(app->ui, false);
        return;
    }

    if (app->view_mode == MODE_SEARCH) {
        uint64_t gen = bump_list_gen(app);
        SearchJobReq *req = calloc(1, sizeof(SearchJobReq));
        req->query = tt_strdup(app->last_query);
        req->filter = app->last_filter;
        req->continuation = tt_strdup(cur->continuation_token);
        req->append = true;
        req->hide_members_only = app->cfg.hide_members_only;
        ui_set_status(app->ui, "Loading next page...");
        http_pool_submit(app->pool, JOB_SEARCH, gen, req, run_search_job, free_search_req, free_list_res);
    } else if (app->view_mode == MODE_BROWSE) {
        submit_browse(app, app->last_browse_id, app->last_browse_params,
                      cur->continuation_token, app->last_heading, true, false);
    } else if (app->view_mode == MODE_NEXT) {
        submit_next(app, app->last_next_video_id, cur->continuation_token, true, true, false);
    } else {
        ui_set_loading_more(app->ui, false);
    }
}

static void cb_show_favorites(void *ctx) {
    AppState *app = ctx;
    if (!(app->has_view && app->view_mode == MODE_FAVORITES)) {
        push_current_nav(app);
    }
    bump_list_gen(app);
    app->view_mode = MODE_FAVORITES;
    app->has_view = true;
    free(app->last_heading);
    app->last_heading = tt_strdup("Favorites");
    ui_set_error(app->ui, NULL);
    ui_set_busy(app->ui, false);

    MediaList ml;
    media_list_init(&ml);
    ml.heading = tt_strdup("Favorites");
    for (size_t i = 0; i < app->favs.count; i++) {
        const FavoriteItem *f = &app->favs.items[i];
        MediaItem it = {0};
        it.kind = f->kind;
        snprintf(it.id, sizeof(it.id), "%s", f->id);
        it.title = tt_strdup(f->title ? f->title : f->id);
        it.author = tt_strdup(f->kind == ITEM_CHANNEL ? "Channel" : "Playlist");
        it.view_count_text = tt_strdup(f->extra_info);
        it.thumbnail_url = tt_strdup(f->thumbnail_url);
        media_list_append_move(&ml, &it);
    }
    ui_set_list(app->ui, &ml, false);
    sync_ui_nav(app);
    sync_selected_item_details(app);
    ui_set_status(app->ui, "Favorites (%zu items)", app->favs.count);
}

static void run_feed_internal(AppState *app, bool push_nav) {
    if (app->favs.count == 0) {
        ui_set_status(app->ui, "Favorites list is empty — add channels or playlists first");
        return;
    }
    if (push_nav) push_current_nav(app);
    uint64_t gen = bump_list_gen(app);
    app->view_mode = MODE_FEED;
    app->has_view = true;
    free(app->last_heading);
    app->last_heading = tt_strdup("Subscription Feed");
    ui_set_error(app->ui, NULL);
    ui_set_busy(app->ui, true);
    sync_ui_nav(app);

    FeedJobReq *req = calloc(1, sizeof(FeedJobReq));
    favlist_init(&req->favs_copy);
    for (size_t i = 0; i < app->favs.count; i++) {
        favlist_add(&req->favs_copy, &app->favs.items[i]);
    }
    req->use_rss = app->cfg.feed_use_rss;
    req->max_per_ch = app->cfg.max_feed_videos_per_channel;
    req->max_age_sec = app->cfg.max_feed_age_sec;
    req->hide_members_only = app->cfg.hide_members_only;

    ui_set_status(app->ui, "loading feed... (%zu items left)", app->favs.count);
    http_pool_submit(app->pool, JOB_FEED, gen, req, run_feed_job, free_feed_req, free_list_res);
}

static void cb_show_feed(void *ctx) {
    run_feed_internal((AppState *)ctx, true);
}

static void cb_show_history(void *ctx) {
    AppState *app = ctx;
    if (!(app->has_view && app->view_mode == MODE_HISTORY)) {
        push_current_nav(app);
    }
    bump_list_gen(app);
    app->view_mode = MODE_HISTORY;
    app->has_view = true;
    free(app->last_heading);
    app->last_heading = tt_strdup("History");
    ui_set_error(app->ui, NULL);
    ui_set_busy(app->ui, false);

    MediaList ml;
    media_list_init(&ml);
    ml.heading = tt_strdup("History");
    for (size_t i = 0; i < app->history.count; i++) {
        const HistoryEntry *h = &app->history.items[i];
        MediaItem it = {0};
        it.kind = ITEM_VIDEO;
        snprintf(it.id, sizeof(it.id), "%s", h->video_id);
        it.title = tt_strdup(h->title);
        it.author = tt_strdup(h->author);
        it.author_id = tt_strdup(h->author_id);
        it.duration_text = tt_strdup(h->duration_text);
        it.thumbnail_url = tt_strdup(h->thumbnail_url);
        it.watched = true;
        media_list_append_move(&ml, &it);
    }
    ui_set_list(app->ui, &ml, false);
    sync_ui_nav(app);
    sync_selected_item_details(app);
    ui_set_status(app->ui, "History (%zu videos)", app->history.count);
}

static void cb_select_item(void *ctx, size_t index) {
    AppState *app = ctx;
    MediaList *ml = ui_get_list(app->ui);
    if (!ml || index >= ml->count) return;
    const MediaItem *it = &ml->items[index];
    if (it->kind == ITEM_VIDEO && it->id[0]) {
        submit_next(app, it->id, NULL, false, false, false);
    } else {
        app->detail_gen++;
        http_pool_set_active_detail_gen(app->pool, app->detail_gen);
    }
}

static void cb_activate_item(void *ctx, size_t index) {
    AppState *app = ctx;
    MediaList *ml = ui_get_list(app->ui);
    if (!ml || index >= ml->count) return;
    MediaItem *it = &ml->items[index];

    if (it->kind == ITEM_VIDEO) {
        char err[256] = {0};
        if (player_launch(&app->cfg, it, err, sizeof(err))) {
            ui_set_status(app->ui, "Launched %s: %s",
                          app->cfg.player_path ? app->cfg.player_path : "mpv",
                          it->title ? it->title : it->id);
            if (app->cfg.save_history) {
                history_record(&app->history, it, (int64_t)time(NULL));
                char *hp = history_path(app);
                if (hp) {
                    history_save(&app->history, hp);
                    free(hp);
                }
                ui_mark_video_watched(app->ui, it->id);
            }
        } else {
            ui_set_status(app->ui, "%s", err);
        }
    } else if (it->kind == ITEM_CHANNEL) {
        submit_browse(app, it->id, INNERTUBE_CHAN_VIDEOS_PARAMS, NULL, it->title, false, true);
    } else if (it->kind == ITEM_PLAYLIST) {
        char *vl = (strncmp(it->id, "VL", 2) == 0) ? tt_strdup(it->id) : tt_asprintf("VL%s", it->id);
        submit_browse(app, vl, NULL, NULL, it->title, false, true);
        free(vl);
    }
}

static void cb_download_video(void *ctx) {
    AppState *app = ctx;
    const MediaItem *it = ui_get_detail_item(app->ui);
    if (!it || it->kind != ITEM_VIDEO || !it->id[0]) {
        ui_set_status(app->ui, "Select a video to download");
        return;
    }
    char dir[PATH_MAX] = {0};
    char err[256] = {0};
    if (player_download_start(&app->cfg, it, dir, sizeof(dir), err, sizeof(err))) {
        ui_set_status(app->ui, "Downloading to %s: %s",
                      dir, (it->title && it->title[0]) ? it->title : it->id);
        wayland_request_redraw(app->wl);
    } else {
        ui_set_status(app->ui, "%s", err[0] ? err : "Failed to start download");
    }
}

static void cb_view_channel(void *ctx) {
    AppState *app = ctx;
    const MediaItem *it = ui_get_detail_item(app->ui);
    if (!it) return;
    const char *cid = (it->kind == ITEM_CHANNEL) ? it->id : it->author_id;
    const char *name = (it->kind == ITEM_CHANNEL) ? it->title : it->author;
    if (!cid || !cid[0]) {
        ui_set_status(app->ui, "No channel ID available for this item");
        return;
    }
    submit_browse(app, cid, INNERTUBE_CHAN_VIDEOS_PARAMS, NULL, name ? name : "Channel", false, true);
}

static void cb_switch_channel_tab(void *ctx, bool live_tab) {
    AppState *app = ctx;
    if (!app || app->view_mode != MODE_BROWSE || !app->last_browse_id || !app->last_browse_id[0]) {
        return;
    }
    submit_browse(app, app->last_browse_id,
                  live_tab ? INNERTUBE_CHAN_STREAMS_PARAMS : INNERTUBE_CHAN_VIDEOS_PARAMS,
                  NULL, app->last_heading, false, true);
}

static void cb_view_recommended(void *ctx) {
    AppState *app = ctx;
    const MediaItem *it = ui_get_detail_item(app->ui);
    if (!it || it->kind != ITEM_VIDEO || !it->id[0]) return;
    submit_next(app, it->id, NULL, true, false, true);
}

static void cb_navigate_back(void *ctx) {
    AppState *app = ctx;
    NavEntry entry = {0};
    if (!nav_stack_pop(&app->nav_stack, &entry)) {
        sync_ui_nav(app);
        ui_set_status(app->ui, "Nothing to go back to");
        return;
    }

    bump_list_gen(app);
    app->view_mode = (ViewMode)entry.view_mode;
    app->has_view = true;
    app->last_filter = (SearchFilter){
        (SearchSort)entry.filter_sort,
        (SearchType)entry.filter_type,
        entry.filter_live_only
    };
    free(app->last_query); app->last_query = entry.query; entry.query = NULL;
    free(app->last_browse_id); app->last_browse_id = entry.browse_id; entry.browse_id = NULL;
    free(app->last_browse_params); app->last_browse_params = entry.browse_params; entry.browse_params = NULL;
    free(app->last_next_video_id); app->last_next_video_id = entry.next_video_id; entry.next_video_id = NULL;
    free(app->last_heading); app->last_heading = entry.heading; entry.heading = NULL;

    ui_set_error(app->ui, NULL);
    ui_set_busy(app->ui, false);
    ui_restore_list_state(app->ui, &entry.list, entry.selected_idx, entry.scroll_y);
    sync_ui_nav(app);
    sync_selected_item_details(app);

    const MediaList *cur = ui_get_list(app->ui);
    ui_set_status(app->ui, "Back to %s",
                  (cur && cur->heading && cur->heading[0]) ? cur->heading : "previous view");
    nav_entry_free_fields(&entry);
}

static void cb_retry(void *ctx) {
    AppState *app = ctx;
    if (!app->has_view) return;
    switch (app->view_mode) {
        case MODE_SEARCH:
            if (app->last_query && app->last_query[0]) {
                run_search_query(app, app->last_query, app->last_filter, false);
            }
            break;
        case MODE_BROWSE:
            if (app->last_browse_id && app->last_browse_id[0]) {
                submit_browse(app, app->last_browse_id, app->last_browse_params,
                              NULL, app->last_heading, false, false);
            }
            break;
        case MODE_NEXT:
            if (app->last_next_video_id && app->last_next_video_id[0]) {
                submit_next(app, app->last_next_video_id, NULL, true, false, false);
            }
            break;
        case MODE_FEED:
            run_feed_internal(app, false);
            break;
        default:
            break;
    }
}

static void cb_toggle_favorite(void *ctx) {
    AppState *app = ctx;
    const MediaItem *it = ui_get_detail_item(app->ui);
    if (!it) return;

    FavoriteItem fi = {0};
    if (it->kind == ITEM_CHANNEL || it->kind == ITEM_PLAYLIST) {
        fi.kind = it->kind;
        snprintf(fi.id, sizeof(fi.id), "%s", it->id);
        fi.title = tt_strdup((it->title && it->title[0]) ? it->title : it->id);
        fi.thumbnail_url = it->thumbnail_url;
        fi.extra_info = it->view_count_text;
    } else if (it->author_id && it->author_id[0]) {
        fi.kind = ITEM_CHANNEL;
        snprintf(fi.id, sizeof(fi.id), "%s", it->author_id);
        fi.title = tt_strdup((it->author && it->author[0]) ? it->author : it->author_id);
    } else {
        ui_set_status(app->ui, "Cannot favorite: missing channel/playlist ID");
        return;
    }

    if (favlist_contains(&app->favs, fi.id)) {
        favlist_remove(&app->favs, fi.id);
        ui_set_status(app->ui, "Removed from favorites: %s", fi.title ? fi.title : fi.id);
    } else {
        favlist_add(&app->favs, &fi);
        ui_set_status(app->ui, "Added to favorites: %s", fi.title ? fi.title : fi.id);
    }
    free(fi.title);

    char *fp = favlist_path(app);
    if (fp) {
        favlist_save(&app->favs, fp);
        free(fp);
    }
    wayland_request_redraw(app->wl);
}

static void cb_clear_cache(void *ctx) {
    AppState *app = ctx;
    thumb_cache_clear(app->thumbs);
    ui_set_status(app->ui, "Thumbnail cache cleared");
}

static void cb_request_thumb(void *ctx, const char *key, const char *url) {
    AppState *app = ctx;
    ThumbJobReq *req = calloc(1, sizeof(ThumbJobReq));
    req->cache_dir = tt_strdup(app->cfg.cache_dir);
    req->key = tt_strdup(key);
    req->url = tt_strdup(url);
    http_pool_submit(app->pool, JOB_THUMBNAIL, 0, req,
                     run_thumb_job, free_thumb_req, free_thumb_res);
}

static void process_async_results(AppState *app) {
    http_pool_drain_notify(app->pool);
    HttpResult res;
    while (http_pool_pop_result(app->pool, &res)) {
        if (res.kind == RES_PROGRESS) {
            if (res.request_gen == app->list_gen && res.progress_text) {
                ui_set_status(app->ui, "%s", res.progress_text);
            }
            http_result_free(&res);
            continue;
        }

        if (res.job_type == JOB_THUMBNAIL) {
            ThumbJobRes *tr = res.data;
            if (tr && tr->key) {
                cairo_surface_t *surf = tr->ok ? image_buffer_to_cairo(&tr->img) : NULL;
                ui_on_thumb_loaded(app->ui, tr->key, surf);
                if (surf) cairo_surface_destroy(surf);
            }
        } else if (res.job_type == JOB_SEARCH || res.job_type == JOB_BROWSE || res.job_type == JOB_FEED) {
            if (res.request_gen == app->list_gen && res.data) {
                ListJobRes *lr = res.data;
                ui_set_busy(app->ui, false);
                if (lr->ok) {
                    ui_set_error(app->ui, NULL);
                    ui_set_list(app->ui, &lr->list, lr->append);
                    if (!lr->append) {
                        sync_selected_item_details(app);
                    }
                    ui_set_status(app->ui, "Loaded %zu items", ui_get_list(app->ui)->count);
                } else {
                    const char *emsg = lr->error[0] ? lr->error : "Request failed";
                    ui_set_loading_more(app->ui, false);
                    if (!lr->append) {
                        MediaList empty;
                        media_list_init(&empty);
                        empty.heading = tt_strdup(app->last_heading ? app->last_heading : "Error");
                        ui_set_list(app->ui, &empty, false);
                        sync_selected_item_details(app);
                        ui_set_error(app->ui, emsg);
                    }
                    ui_set_status(app->ui, "%s", emsg);
                }
            }
        } else if (res.job_type == JOB_DETAIL) {
            NextJobRes *nr = res.data;
            if (nr && res.request_gen == app->detail_gen && nr->ok && nr->details.id[0]) {
                ui_set_detail_item(app->ui, &nr->details);
            }
        } else if (res.job_type == JOB_NEXT) {
            NextJobRes *nr = res.data;
            if (nr) {
                if (nr->load_list && res.request_gen == app->list_gen) {
                    ui_set_busy(app->ui, false);
                    if (nr->ok) {
                        ui_set_error(app->ui, NULL);
                        ui_set_list(app->ui, &nr->recommended, nr->append);
                        if (!nr->append && nr->details.id[0]) {
                            ui_set_detail_item(app->ui, &nr->details);
                        }
                        ui_set_status(app->ui, "Loaded %zu recommended videos",
                                      ui_get_list(app->ui)->count);
                    } else {
                        const char *emsg = nr->error[0] ? nr->error : "Failed loading video";
                        ui_set_loading_more(app->ui, false);
                        if (!nr->append) {
                            MediaList empty;
                            media_list_init(&empty);
                            empty.heading = tt_strdup("Recommended");
                            ui_set_list(app->ui, &empty, false);
                            sync_selected_item_details(app);
                            ui_set_error(app->ui, emsg);
                        }
                        ui_set_status(app->ui, "%s", emsg);
                    }
                } else if (!nr->load_list && res.request_gen == app->detail_gen) {
                    if (nr->ok && nr->details.id[0]) {
                        ui_set_detail_item(app->ui, &nr->details);
                    }
                }
            }
        } else if (res.job_type == JOB_RESOLVE_URL) {
            ResolveJobRes *rr = res.data;
            if (res.request_gen == app->list_gen && rr) {
                if (rr->ok) {
                    if (rr->parsed.kind == URL_VIDEO) {
                        submit_next(app, rr->parsed.id, NULL, true, false, false);
                    } else {
                        ui_set_busy(app->ui, false);
                        ui_set_error(app->ui, NULL);
                        app->view_mode = MODE_BROWSE;
                        app->has_view = true;
                        free(app->last_browse_id);
                        app->last_browse_id = tt_strdup(rr->parsed.id);
                        free(app->last_browse_params);
                        app->last_browse_params = (rr->parsed.kind == URL_CHANNEL)
                                                      ? tt_strdup(rr->parsed.prefer_live_tab
                                                                      ? INNERTUBE_CHAN_STREAMS_PARAMS
                                                                      : INNERTUBE_CHAN_VIDEOS_PARAMS)
                                                      : NULL;
                        ui_set_list(app->ui, &rr->list, false);
                        sync_ui_nav(app);
                        sync_selected_item_details(app);
                        ui_set_status(app->ui, "Loaded %zu items", ui_get_list(app->ui)->count);
                    }
                } else {
                    const char *emsg = rr->error[0] ? rr->error : "URL resolve failed";
                    ui_set_busy(app->ui, false);
                    MediaList empty;
                    media_list_init(&empty);
                    empty.heading = tt_strdup(app->last_heading ? app->last_heading : "Error");
                    ui_set_list(app->ui, &empty, false);
                    sync_selected_item_details(app);
                    ui_set_error(app->ui, emsg);
                    ui_set_status(app->ui, "%s", emsg);
                }
            }
        }
        http_result_free(&res);
    }
}

static void poll_downloads(AppState *app) {
    if (player_poll_download_progress()) {
        wayland_request_redraw(app->wl);
    }
    DownloadEvent ev;
    while (player_poll_download_event(&ev)) {
        if (ev.ok) {
            if (ev.saved_path[0]) {
                ui_set_status(app->ui, "Saved: %s", ev.saved_path);
            } else {
                ui_set_status(app->ui, "Download complete: %s",
                              ev.title[0] ? ev.title : ev.video_id);
            }
        } else {
            ui_set_status(app->ui, "Download failed (code %d): %s",
                          ev.exit_code, ev.title[0] ? ev.title : ev.video_id);
        }
        wayland_request_redraw(app->wl);
    }
}

static volatile sig_atomic_t g_sig_stop = 0;

static void handle_stop_sig(int sig) {
    (void)sig;
    g_sig_stop = 1;
}

static void print_help(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n"
           "Lightweight native Wayland YouTube client.\n\n"
           "Options:\n"
           "  --config-dir <dir>   Override configuration directory\n"
           "  -h, --help           Show this help message and exit\n",
           prog);
}

static void wl_tramp_render(void *ctx, cairo_t *cr, int w, int h) {
    AppState *app = ctx;
    if (app->ui) ui_on_render(app->ui, cr, w, h);
}
static void wl_tramp_resize(void *ctx, int w, int h) {
    AppState *app = ctx;
    if (app->ui) ui_on_resize(app->ui, w, h);
}
static void wl_tramp_motion(void *ctx, double x, double y) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_motion(app->ui, x, y);
}
static void wl_tramp_button(void *ctx, TtMouseButton btn, bool pressed, bool dbl, double x, double y) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_button(app->ui, btn, pressed, dbl, x, y);
}
static void wl_tramp_axis(void *ctx, double dx, double dy) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_axis(app->ui, dx, dy);
}
static void wl_tramp_leave(void *ctx) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_leave(app->ui);
}
static void wl_tramp_key(void *ctx, uint32_t mods, xkb_keysym_t sym, const char *utf8, bool pressed) {
    AppState *app = ctx;
    if (app->ui) ui_on_key(app->ui, mods, sym, utf8, pressed);
}
static void wl_tramp_ime(void *ctx, const WaylandImeEvent *ev) {
    AppState *app = ctx;
    if (app->ui) ui_on_ime(app->ui, ev);
}

int main(int argc, char **argv) {
    const char *cli_config_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--config-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --config-dir requires an argument\n");
                return 1;
            }
            cli_config_dir = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_stop_sig);
    signal(SIGTERM, handle_stop_sig);
    player_install_sigchld_handler();

    if (!http_global_init()) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        return 1;
    }

    AppState app = {0};
    nav_stack_init(&app.nav_stack);
    if (!config_load(&app.cfg, cli_config_dir)) {
        fprintf(stderr, "Warning: failed to parse config file, using defaults\n");
    }

    favlist_init(&app.favs);
    char *fp = favlist_path(&app);
    if (fp) { favlist_load(&app.favs, fp); free(fp); }

    history_init(&app.history);
    char *hp = history_path(&app);
    if (hp) { history_load(&app.history, hp); free(hp); }

    app.thumbs = thumb_cache_create(app.cfg.cache_dir);
    app.pool = http_pool_create(4);

    WaylandCallbacks wl_cbs = {
        .on_render = wl_tramp_render,
        .on_resize = wl_tramp_resize,
        .on_pointer_motion = wl_tramp_motion,
        .on_pointer_button = wl_tramp_button,
        .on_pointer_axis = wl_tramp_axis,
        .on_pointer_leave = wl_tramp_leave,
        .on_key = wl_tramp_key,
        .on_ime = wl_tramp_ime,
    };

    char err[256] = {0};
    app.wl = wayland_create(1024, 640, "TuiTube", &wl_cbs, &app, err, sizeof(err));
    if (!app.wl) {
        fprintf(stderr, "Error: %s\n", err[0] ? err : "Failed to initialize Wayland");
        http_pool_destroy(app.pool);
        thumb_cache_destroy(app.thumbs);
        history_free(&app.history);
        favlist_free(&app.favs);
        config_free(&app.cfg);
        http_global_cleanup();
        return 1;
    }

    UiCallbacks ui_cbs = {
        .on_search = cb_search,
        .on_next_page = cb_next_page,
        .on_show_favorites = cb_show_favorites,
        .on_show_feed = cb_show_feed,
        .on_show_history = cb_show_history,
        .on_select_item = cb_select_item,
        .on_activate_item = cb_activate_item,
        .on_download_video = cb_download_video,
        .on_view_channel = cb_view_channel,
        .on_view_recommended = cb_view_recommended,
        .on_toggle_favorite = cb_toggle_favorite,
        .on_clear_cache = cb_clear_cache,
        .on_request_thumb = cb_request_thumb,
        .on_navigate_back = cb_navigate_back,
        .on_retry = cb_retry,
        .on_switch_channel_tab = cb_switch_channel_tab,
    };

    app.ui = ui_create(&app.cfg, app.wl, app.thumbs, &app.favs, &app.history, &ui_cbs, &app);
    if (!app.ui) {
        fprintf(stderr, "Error: failed to initialize UI\n");
        wayland_destroy(app.wl);
        http_pool_destroy(app.pool);
        thumb_cache_destroy(app.thumbs);
        history_free(&app.history);
        favlist_free(&app.favs);
        nav_stack_free(&app.nav_stack);
        config_free(&app.cfg);
        http_global_cleanup();
        return 1;
    }
    sync_ui_nav(&app);

    int notify_fd = http_pool_notify_fd(app.pool);
    while (!g_sig_stop && !wayland_should_quit(app.wl)) {
        poll_downloads(&app);
        wayland_render_if_needed(app.wl);
        bool extra_ready = false;
        int pr = wayland_poll_once(app.wl, notify_fd, 250, &extra_ready);
        if (pr < 0) break;
        if (extra_ready) {
            process_async_results(&app);
        }
        poll_downloads(&app);
    }

    ui_destroy(app.ui);
    wayland_destroy(app.wl);
    http_pool_destroy(app.pool);
    thumb_cache_destroy(app.thumbs);
    history_free(&app.history);
    favlist_free(&app.favs);
    nav_stack_free(&app.nav_stack);
    config_free(&app.cfg);
    free(app.last_query);
    free(app.last_browse_id);
    free(app.last_browse_params);
    free(app.last_next_video_id);
    free(app.last_heading);
    http_global_cleanup();
    return 0;
}
