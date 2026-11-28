#ifndef TUITUBE_INNERTUBE_H
#define TUITUBE_INNERTUBE_H

#include "http.h"
#include "model.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INNERTUBE_CHAN_VIDEOS_PARAMS  "EgZ2aWRlb3PyBgQKAjoA"
#define INNERTUBE_CHAN_STREAMS_PARAMS "EgdzdHJlYW1z8gYECgJ6AA=="

typedef enum {
    SORT_RELEVANCE = 0,
    SORT_UPLOAD_DATE = 1,
    SORT_VIEW_COUNT = 2,
    SORT_RATING = 3
} SearchSort;

#define INNERTUBE_CHAN_VIDEOS_PARAMS  "EgZ2aWRlb3PyBgQKAjoA"
#define INNERTUBE_CHAN_STREAMS_PARAMS "EgdzdHJlYW1z8gYECgJ6AA=="

typedef enum {
    TYPE_ALL = 0,
    TYPE_VIDEO = 1,
    TYPE_CHANNEL = 2,
    TYPE_PLAYLIST = 3
} SearchType;

typedef struct {
    SearchSort sort;
    SearchType type;
    bool live_only;
} SearchFilter;

#define INNERTUBE_CHAN_VIDEOS_PARAMS  "EgZ2aWRlb3PyBgQKAjoA"
#define INNERTUBE_CHAN_STREAMS_PARAMS "EgdzdHJlYW1z8gYECgJ6AA=="

typedef enum {
    URL_NONE = 0,
    URL_VIDEO,
    URL_PLAYLIST,
    URL_CHANNEL,
    URL_HANDLE
} ParsedUrlKind;

typedef struct {
    ParsedUrlKind kind;
    char id[128];
    char resolved_url[256];
    bool prefer_live_tab;
} ParsedUrl;

void innertube_set_locale(const char *hl, const char *gl);
void innertube_get_locale(char *out_hl, size_t hl_cap, char *out_gl, size_t gl_cap);
bool innertube_format_accept_language(const char *hl, char *out, size_t out_cap);
void innertube_set_ytdlp_path(const char *path);
void innertube_get_ytdlp_path(char *out, size_t out_cap);

bool innertube_encode_search_params(SearchFilter filter, char *out, size_t out_cap);
bool innertube_parse_input_url(const char *input, ParsedUrl *out);

bool innertube_parse_search_json(const char *json, size_t len, MediaList *out_list,
                                 char *err_buf, size_t err_cap);
bool innertube_parse_browse_json(const char *json, size_t len, MediaList *out_list,
                                 char *err_buf, size_t err_cap);
bool innertube_parse_next_json(const char *json, size_t len, const char *video_id,
                               MediaItem *out_details, MediaList *out_recommended,
                               char *err_buf, size_t err_cap);
bool innertube_parse_resolve_url_json(const char *json, size_t len, ParsedUrl *out);
bool innertube_parse_ytdlp_json(const char *json, size_t len, MediaList *out_list);

bool innertube_parse_oembed_json(const char *json, size_t len, char *out, size_t out_cap);
bool innertube_parse_player_title_json(const char *json, size_t len, char *out, size_t out_cap);
bool innertube_parse_player_metadata_json(const char *json, size_t len, char *out_title,
                                          size_t title_cap, char **out_description);
bool innertube_parse_original_title_fallback(const char *oembed_json, size_t oembed_len,
                                             bool oembed_ok, const char *player_json,
                                             size_t player_len, bool player_ok,
                                             char *out, size_t out_cap);
bool innertube_resolve_original_title(CURL *curl, const char *video_id, char *out, size_t out_cap);
bool innertube_resolve_original_metadata(CURL *curl, const char *video_id, char *out_title,
                                         size_t title_cap, char **out_description);
void innertube_original_title_cache_reset(void);

bool innertube_parse_rss_xml(const char *xml, size_t xml_len, MediaList *out_list, int max_items);
void innertube_filter_and_sort_feed(MediaList *list, int64_t now_epoch, int64_t max_age_sec);

bool innertube_search(CURL *curl, const char *query, SearchFilter filter,
                      const char *continuation, MediaList *out_list,
                      char *err_buf, size_t err_cap);
bool innertube_browse(CURL *curl, const char *browse_id, const char *params,
                      const char *continuation, MediaList *out_list,
                      char *err_buf, size_t err_cap);
bool innertube_browse_channel(CURL *curl, const char *channel_id, bool live_tab,
                              const char *continuation, bool hide_members_only,
                              MediaList *out_list, char *err_buf, size_t err_cap);
bool innertube_next(CURL *curl, const char *video_id, const char *continuation,
                    MediaItem *out_details, MediaList *out_recommended,
                    char *err_buf, size_t err_cap);
bool innertube_resolve_url(CURL *curl, const char *url, ParsedUrl *out,
                           char *err_buf, size_t err_cap);
bool innertube_fetch_feed(CURL *curl, const FavoriteList *favs, bool use_rss,
                          int max_per_channel, int64_t max_age_sec, int64_t now_epoch,
                          bool hide_members_only, MediaList *out_list,
                          HttpPool *pool, uint64_t gen,
                          char *err_buf, size_t err_cap);

#endif
