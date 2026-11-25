#include "model.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

void media_item_free_fields(MediaItem *it) {
    if (!it) return;
    free(it->title);
    free(it->author);
    free(it->author_id);
    free(it->duration_text);
    free(it->view_count_text);
    free(it->published_text);
    free(it->description);
    free(it->thumbnail_url);
    memset(it, 0, sizeof(*it));
}

bool media_item_copy(MediaItem *dst, const MediaItem *src) {
    if (!dst || !src) return false;
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    snprintf(dst->id, sizeof(dst->id), "%s", src->id);
    dst->title = tt_strdup(src->title);
    dst->author = tt_strdup(src->author);
    dst->author_id = tt_strdup(src->author_id);
    dst->duration_text = tt_strdup(src->duration_text);
    dst->view_count_text = tt_strdup(src->view_count_text);
    dst->published_text = tt_strdup(src->published_text);
    dst->description = tt_strdup(src->description);
    dst->thumbnail_url = tt_strdup(src->thumbnail_url);
    dst->published_epoch = src->published_epoch;
    dst->is_live = src->is_live;
    dst->is_members_only = src->is_members_only;
    dst->watched = src->watched;
    dst->details_loaded = src->details_loaded;
    return true;
}

void media_list_init(MediaList *list) {
    memset(list, 0, sizeof(*list));
}

void media_list_clear(MediaList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        media_item_free_fields(&list->items[i]);
    }
    list->count = 0;
    free(list->heading);
    free(list->continuation_token);
    free(list->source_id);
    list->heading = NULL;
    list->continuation_token = NULL;
    list->source_id = NULL;
}

void media_list_free(MediaList *list) {
    if (!list) return;
    media_list_clear(list);
    free(list->items);
    list->items = NULL;
    list->cap = 0;
}

bool media_list_append_move(MediaList *list, MediaItem *item) {
    if (!list || !item) return false;
    if (list->count == list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 32;
        MediaItem *arr = realloc(list->items, new_cap * sizeof(MediaItem));
        if (!arr) return false;
        list->items = arr;
        list->cap = new_cap;
    }
    list->items[list->count++] = *item;
    memset(item, 0, sizeof(*item));
    return true;
}

bool media_list_append_copy(MediaList *list, const MediaItem *item) {
    MediaItem copy;
    if (!media_item_copy(&copy, item)) return false;
    if (!media_list_append_move(list, &copy)) {
        media_item_free_fields(&copy);
        return false;
    }
    return true;
}

void media_list_filter_members_only(MediaList *list) {
    if (!list || list->count == 0) return;
    size_t write_idx = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].is_members_only) {
            media_item_free_fields(&list->items[i]);
        } else {
            if (write_idx != i) {
                list->items[write_idx] = list->items[i];
                memset(&list->items[i], 0, sizeof(MediaItem));
            }
            write_idx++;
        }
    }
    list->count = write_idx;
}

static void fav_item_free_fields(FavoriteItem *it) {
    if (!it) return;
    free(it->title);
    free(it->thumbnail_url);
    free(it->extra_info);
    memset(it, 0, sizeof(*it));
}

void favlist_init(FavoriteList *fl) {
    memset(fl, 0, sizeof(*fl));
}

void favlist_free(FavoriteList *fl) {
    if (!fl) return;
    for (size_t i = 0; i < fl->count; i++) {
        fav_item_free_fields(&fl->items[i]);
    }
    free(fl->items);
    memset(fl, 0, sizeof(*fl));
}

static const char *kind_to_str(ItemKind k) {
    switch (k) {
        case ITEM_CHANNEL: return "channel";
        case ITEM_PLAYLIST: return "playlist";
        default: return "video";
    }
}

static ItemKind str_to_fav_kind(const char *s, const char *id) {
    if (s && tt_strcasecmp(s, "playlist") == 0) return ITEM_PLAYLIST;
    if (s && tt_strcasecmp(s, "channel") == 0) return ITEM_CHANNEL;
    if (id && strncmp(id, "UC", 2) != 0) return ITEM_PLAYLIST;
    return ITEM_CHANNEL;
}

bool favlist_load(FavoriteList *fl, const char *path) {
    favlist_free(fl);
    favlist_init(fl);
    if (!path || !tt_file_exists(path)) return true;

    size_t len = 0;
    char *raw = tt_read_file(path, &len);
    if (!raw) return false;

    yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS;
    yyjson_doc *doc = yyjson_read_opts(raw, len, flags, NULL, NULL);
    free(raw);
    if (!doc) return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t idx, max;
    yyjson_val *elem;
    yyjson_arr_foreach(root, idx, max, elem) {
        if (!elem || yyjson_is_null(elem) || !yyjson_is_obj(elem)) continue;
        yyjson_val *vid = yyjson_obj_get(elem, "id");
        if (!vid || !yyjson_is_str(vid)) continue;
        const char *id = yyjson_get_str(vid);
        if (!id || !id[0]) continue;

        yyjson_val *vk = yyjson_obj_get(elem, "kind");
        yyjson_val *vt = yyjson_obj_get(elem, "title");
        yyjson_val *vu = yyjson_obj_get(elem, "thumbnail_url");
        yyjson_val *ve = yyjson_obj_get(elem, "extra_info");

        FavoriteItem fi = {0};
        fi.kind = str_to_fav_kind(vk && yyjson_is_str(vk) ? yyjson_get_str(vk) : NULL, id);
        snprintf(fi.id, sizeof(fi.id), "%s", id);
        fi.title = tt_strdup((vt && yyjson_is_str(vt)) ? yyjson_get_str(vt) : id);
        fi.thumbnail_url = (vu && yyjson_is_str(vu)) ? tt_strdup(yyjson_get_str(vu)) : NULL;
        fi.extra_info = (ve && yyjson_is_str(ve)) ? tt_strdup(yyjson_get_str(ve)) : NULL;

        favlist_add(fl, &fi);
        fav_item_free_fields(&fi);
    }

    yyjson_doc_free(doc);
    return true;
}

bool favlist_save(const FavoriteList *fl, const char *path) {
    if (!fl || !path) return false;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return false;
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arr);

    for (size_t i = 0; i < fl->count; i++) {
        const FavoriteItem *it = &fl->items[i];
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, obj, "kind", kind_to_str(it->kind));
        yyjson_mut_obj_add_str(doc, obj, "id", it->id);
        yyjson_mut_obj_add_str(doc, obj, "title", it->title ? it->title : "");
        if (it->thumbnail_url) {
            yyjson_mut_obj_add_str(doc, obj, "thumbnail_url", it->thumbnail_url);
        }
        if (it->extra_info) {
            yyjson_mut_obj_add_str(doc, obj, "extra_info", it->extra_info);
        }
        yyjson_mut_arr_append(arr, obj);
    }

    size_t out_len = 0;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &out_len);
    yyjson_mut_doc_free(doc);
    if (!json) return false;

    bool ok = tt_write_file_atomic(path, json, out_len);
    free(json);
    return ok;
}

bool favlist_contains(const FavoriteList *fl, const char *id) {
    if (!fl || !id || !id[0]) return false;
    for (size_t i = 0; i < fl->count; i++) {
        if (strcmp(fl->items[i].id, id) == 0) return true;
    }
    return false;
}

bool favlist_add(FavoriteList *fl, const FavoriteItem *item) {
    if (!fl || !item || !item->id[0]) return false;
    for (size_t i = 0; i < fl->count; i++) {
        if (strcmp(fl->items[i].id, item->id) == 0) {
            if (item->title && item->title[0]) {
                free(fl->items[i].title);
                fl->items[i].title = tt_strdup(item->title);
            }
            if (item->thumbnail_url && item->thumbnail_url[0]) {
                free(fl->items[i].thumbnail_url);
                fl->items[i].thumbnail_url = tt_strdup(item->thumbnail_url);
            }
            if (item->extra_info && item->extra_info[0]) {
                free(fl->items[i].extra_info);
                fl->items[i].extra_info = tt_strdup(item->extra_info);
            }
            return true;
        }
    }
    if (fl->count == fl->cap) {
        size_t new_cap = fl->cap ? fl->cap * 2 : 16;
        FavoriteItem *arr = realloc(fl->items, new_cap * sizeof(FavoriteItem));
        if (!arr) return false;
        fl->items = arr;
        fl->cap = new_cap;
    }
    FavoriteItem copy = {0};
    copy.kind = (item->kind == ITEM_PLAYLIST) ? ITEM_PLAYLIST : ITEM_CHANNEL;
    snprintf(copy.id, sizeof(copy.id), "%s", item->id);
    copy.title = tt_strdup(item->title ? item->title : item->id);
    copy.thumbnail_url = tt_strdup(item->thumbnail_url);
    copy.extra_info = tt_strdup(item->extra_info);
    fl->items[fl->count++] = copy;
    return true;
}

bool favlist_remove(FavoriteList *fl, const char *id) {
    if (!fl || !id || !id[0]) return false;
    for (size_t i = 0; i < fl->count; i++) {
        if (strcmp(fl->items[i].id, id) == 0) {
            fav_item_free_fields(&fl->items[i]);
            if (i + 1 < fl->count) {
                memmove(&fl->items[i], &fl->items[i + 1],
                        (fl->count - i - 1) * sizeof(FavoriteItem));
            }
            fl->count--;
            return true;
        }
    }
    return false;
}

static void history_entry_free_fields(HistoryEntry *e) {
    if (!e) return;
    free(e->title);
    free(e->author);
    free(e->author_id);
    free(e->duration_text);
    free(e->thumbnail_url);
    memset(e, 0, sizeof(*e));
}

void history_init(HistoryList *hl) {
    memset(hl, 0, sizeof(*hl));
}

void history_free(HistoryList *hl) {
    if (!hl) return;
    for (size_t i = 0; i < hl->count; i++) {
        history_entry_free_fields(&hl->items[i]);
    }
    free(hl->items);
    memset(hl, 0, sizeof(*hl));
}

bool history_load(HistoryList *hl, const char *path) {
    history_free(hl);
    history_init(hl);
    if (!path || !tt_file_exists(path)) return true;

    size_t len = 0;
    char *raw = tt_read_file(path, &len);
    if (!raw) return false;

    yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS;
    yyjson_doc *doc = yyjson_read_opts(raw, len, flags, NULL, NULL);
    free(raw);
    if (!doc) return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t idx, max;
    yyjson_val *elem;
    yyjson_arr_foreach(root, idx, max, elem) {
        if (hl->count >= HISTORY_MAX_ENTRIES) break;
        if (!elem || yyjson_is_null(elem) || !yyjson_is_obj(elem)) continue;
        yyjson_val *vid = yyjson_obj_get(elem, "video_id");
        if (!vid || !yyjson_is_str(vid)) continue;
        const char *video_id = yyjson_get_str(vid);
        if (!video_id || !video_id[0] || history_contains(hl, video_id)) continue;

        if (hl->count == hl->cap) {
            size_t new_cap = hl->cap ? hl->cap * 2 : 32;
            if (new_cap > HISTORY_MAX_ENTRIES) new_cap = HISTORY_MAX_ENTRIES;
            HistoryEntry *arr = realloc(hl->items, new_cap * sizeof(HistoryEntry));
            if (!arr) break;
            hl->items = arr;
            hl->cap = new_cap;
        }

        yyjson_val *vt = yyjson_obj_get(elem, "title");
        yyjson_val *va = yyjson_obj_get(elem, "author");
        yyjson_val *vai = yyjson_obj_get(elem, "author_id");
        yyjson_val *vd = yyjson_obj_get(elem, "duration_text");
        yyjson_val *vu = yyjson_obj_get(elem, "thumbnail_url");
        yyjson_val *vw = yyjson_obj_get(elem, "watched_at");

        HistoryEntry e = {0};
        snprintf(e.video_id, sizeof(e.video_id), "%s", video_id);
        e.title = tt_strdup((vt && yyjson_is_str(vt)) ? yyjson_get_str(vt) : "");
        e.author = tt_strdup((va && yyjson_is_str(va)) ? yyjson_get_str(va) : "");
        e.author_id = tt_strdup((vai && yyjson_is_str(vai)) ? yyjson_get_str(vai) : "");
        e.duration_text = tt_strdup((vd && yyjson_is_str(vd)) ? yyjson_get_str(vd) : "");
        e.thumbnail_url = tt_strdup((vu && yyjson_is_str(vu)) ? yyjson_get_str(vu) : "");
        e.watched_at = (vw && yyjson_is_num(vw)) ? (int64_t)yyjson_get_sint(vw) : 0;

        hl->items[hl->count++] = e;
    }

    yyjson_doc_free(doc);
    return true;
}

bool history_save(const HistoryList *hl, const char *path) {
    if (!hl || !path) return false;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) return false;
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arr);

    for (size_t i = 0; i < hl->count; i++) {
        const HistoryEntry *e = &hl->items[i];
        yyjson_mut_val *obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, obj, "video_id", e->video_id);
        yyjson_mut_obj_add_str(doc, obj, "title", e->title ? e->title : "");
        yyjson_mut_obj_add_str(doc, obj, "author", e->author ? e->author : "");
        yyjson_mut_obj_add_str(doc, obj, "author_id", e->author_id ? e->author_id : "");
        yyjson_mut_obj_add_str(doc, obj, "duration_text", e->duration_text ? e->duration_text : "");
        yyjson_mut_obj_add_str(doc, obj, "thumbnail_url", e->thumbnail_url ? e->thumbnail_url : "");
        yyjson_mut_obj_add_sint(doc, obj, "watched_at", e->watched_at);
        yyjson_mut_arr_append(arr, obj);
    }

    size_t out_len = 0;
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &out_len);
    yyjson_mut_doc_free(doc);
    if (!json) return false;

    bool ok = tt_write_file_atomic(path, json, out_len);
    free(json);
    return ok;
}

bool history_record(HistoryList *hl, const MediaItem *video, int64_t now) {
    if (!hl || !video || !video->id[0]) return false;

    /* Remove existing duplicate so newest moves to front */
    for (size_t i = 0; i < hl->count; i++) {
        if (strcmp(hl->items[i].video_id, video->id) == 0) {
            history_entry_free_fields(&hl->items[i]);
            if (i + 1 < hl->count) {
                memmove(&hl->items[i], &hl->items[i + 1],
                        (hl->count - i - 1) * sizeof(HistoryEntry));
            }
            hl->count--;
            break;
        }
    }

    if (hl->count >= HISTORY_MAX_ENTRIES) {
        history_entry_free_fields(&hl->items[hl->count - 1]);
        hl->count--;
    }

    if (hl->count == hl->cap) {
        size_t new_cap = hl->cap ? hl->cap * 2 : 32;
        if (new_cap > HISTORY_MAX_ENTRIES) new_cap = HISTORY_MAX_ENTRIES;
        HistoryEntry *arr = realloc(hl->items, new_cap * sizeof(HistoryEntry));
        if (!arr) return false;
        hl->items = arr;
        hl->cap = new_cap;
    }

    if (hl->count > 0) {
        memmove(&hl->items[1], &hl->items[0], hl->count * sizeof(HistoryEntry));
    }

    HistoryEntry e = {0};
    snprintf(e.video_id, sizeof(e.video_id), "%.31s", video->id);
    e.title = tt_strdup(video->title ? video->title : "");
    e.author = tt_strdup(video->author ? video->author : "");
    e.author_id = tt_strdup(video->author_id ? video->author_id : "");
    e.duration_text = tt_strdup(video->duration_text ? video->duration_text : "");
    e.thumbnail_url = tt_strdup(video->thumbnail_url ? video->thumbnail_url : "");
    e.watched_at = now;
    hl->items[0] = e;
    hl->count++;
    return true;
}

bool history_contains(const HistoryList *hl, const char *video_id) {
    if (!hl || !video_id || !video_id[0]) return false;
    for (size_t i = 0; i < hl->count; i++) {
        if (strcmp(hl->items[i].video_id, video_id) == 0) return true;
    }
    return false;
}

void nav_entry_free_fields(NavEntry *e) {
    if (!e) return;
    media_list_free(&e->list);
    free(e->query);
    free(e->browse_id);
    free(e->browse_params);
    free(e->next_video_id);
    free(e->heading);
    memset(e, 0, sizeof(*e));
}

void nav_stack_init(NavStack *ns) {
    if (!ns) return;
    memset(ns, 0, sizeof(*ns));
}

void nav_stack_clear(NavStack *ns) {
    if (!ns) return;
    for (size_t i = 0; i < ns->count; i++) {
        nav_entry_free_fields(&ns->entries[i]);
    }
    ns->count = 0;
}

void nav_stack_free(NavStack *ns) {
    nav_stack_clear(ns);
}

bool nav_stack_can_pop(const NavStack *ns) {
    return ns && ns->count > 0;
}

bool nav_stack_push(NavStack *ns, const NavEntry *snapshot) {
    if (!ns || !snapshot) return false;

    NavEntry copy = {0};
    media_list_init(&copy.list);
    for (size_t i = 0; i < snapshot->list.count; i++) {
        if (!media_list_append_copy(&copy.list, &snapshot->list.items[i])) {
            nav_entry_free_fields(&copy);
            return false;
        }
    }
    copy.list.heading = tt_strdup(snapshot->list.heading);
    copy.list.continuation_token = tt_strdup(snapshot->list.continuation_token);
    copy.list.source_id = tt_strdup(snapshot->list.source_id);

    copy.selected_idx = snapshot->selected_idx;
    copy.scroll_y = snapshot->scroll_y;
    copy.view_mode = snapshot->view_mode;
    copy.filter_sort = snapshot->filter_sort;
    copy.filter_type = snapshot->filter_type;
    copy.filter_live_only = snapshot->filter_live_only;
    copy.query = tt_strdup(snapshot->query);
    copy.browse_id = tt_strdup(snapshot->browse_id);
    copy.browse_params = tt_strdup(snapshot->browse_params);
    copy.next_video_id = tt_strdup(snapshot->next_video_id);
    copy.heading = tt_strdup(snapshot->heading);

    if (ns->count == NAV_STACK_MAX) {
        nav_entry_free_fields(&ns->entries[0]);
        memmove(&ns->entries[0], &ns->entries[1], (NAV_STACK_MAX - 1) * sizeof(NavEntry));
        ns->entries[NAV_STACK_MAX - 1] = copy;
    } else {
        ns->entries[ns->count++] = copy;
    }
    return true;
}

bool nav_stack_pop(NavStack *ns, NavEntry *out) {
    if (!nav_stack_can_pop(ns) || !out) return false;
    *out = ns->entries[--ns->count];
    memset(&ns->entries[ns->count], 0, sizeof(NavEntry));
    return true;
}
