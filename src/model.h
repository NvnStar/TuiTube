#ifndef TUITUBE_MODEL_H
#define TUITUBE_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define HISTORY_MAX_ENTRIES 500
#define NAV_STACK_MAX 16

typedef enum {
    ITEM_VIDEO = 0,
    ITEM_CHANNEL = 1,
    ITEM_PLAYLIST = 2
} ItemKind;

typedef struct {
    ItemKind kind;
    char id[64];
    char *title;
    char *author;
    char *author_id;
    char *duration_text;
    char *view_count_text;
    char *published_text;
    char *description;
    char *thumbnail_url;
    int64_t published_epoch;
    bool is_live;
    bool watched;
} MediaItem;

typedef struct {
    MediaItem *items;
    size_t count;
    size_t cap;
    char *heading;
    char *continuation_token;
    char *source_id;
} MediaList;

typedef struct {
    ItemKind kind;
    char id[64];
    char *title;
    char *thumbnail_url;
    char *extra_info;
} FavoriteItem;

typedef struct {
    FavoriteItem *items;
    size_t count;
    size_t cap;
} FavoriteList;

typedef struct {
    char video_id[32];
    char *title;
    char *author;
    char *author_id;
    char *duration_text;
    char *thumbnail_url;
    int64_t watched_at;
} HistoryEntry;

typedef struct {
    HistoryEntry *items;
    size_t count;
    size_t cap;
} HistoryList;

typedef struct {
    MediaList list;
    ssize_t selected_idx;
    double scroll_y;
    int view_mode;
    int filter_sort;
    int filter_type;
    bool filter_live_only;
    char *query;
    char *browse_id;
    char *browse_params;
    char *next_video_id;
    char *heading;
} NavEntry;

typedef struct {
    NavEntry entries[NAV_STACK_MAX];
    size_t count;
} NavStack;

void media_item_free_fields(MediaItem *it);
bool media_item_copy(MediaItem *dst, const MediaItem *src);

void media_list_init(MediaList *list);
void media_list_clear(MediaList *list);
void media_list_free(MediaList *list);
bool media_list_append_move(MediaList *list, MediaItem *item);
bool media_list_append_copy(MediaList *list, const MediaItem *item);

void favlist_init(FavoriteList *fl);
void favlist_free(FavoriteList *fl);
bool favlist_load(FavoriteList *fl, const char *path);
bool favlist_save(const FavoriteList *fl, const char *path);
bool favlist_contains(const FavoriteList *fl, const char *id);
bool favlist_add(FavoriteList *fl, const FavoriteItem *item);
bool favlist_remove(FavoriteList *fl, const char *id);

void history_init(HistoryList *hl);
void history_free(HistoryList *hl);
bool history_load(HistoryList *hl, const char *path);
bool history_save(const HistoryList *hl, const char *path);
bool history_record(HistoryList *hl, const MediaItem *video, int64_t now);
bool history_contains(const HistoryList *hl, const char *video_id);

void nav_entry_free_fields(NavEntry *e);
void nav_stack_init(NavStack *ns);
void nav_stack_clear(NavStack *ns);
void nav_stack_free(NavStack *ns);
bool nav_stack_can_pop(const NavStack *ns);
bool nav_stack_push(NavStack *ns, const NavEntry *snapshot);
bool nav_stack_pop(NavStack *ns, NavEntry *out);

#endif
