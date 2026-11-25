#ifndef TUITUBE_UTIL_H
#define TUITUBE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_clear(StrBuf *sb);
bool sb_reserve(StrBuf *sb, size_t extra);
bool sb_append(StrBuf *sb, const char *s);
bool sb_append_n(StrBuf *sb, const char *s, size_t n);
bool sb_append_char(StrBuf *sb, char c);
bool sb_appendf(StrBuf *sb, const char *fmt, ...);
char *sb_take(StrBuf *sb);

char *tt_strdup(const char *s);
char *tt_strndup(const char *s, size_t n);
char *tt_asprintf(const char *fmt, ...);
int tt_strcasecmp(const char *a, const char *b);
int tt_strncasecmp(const char *a, const char *b, size_t n);
const char *tt_strcasestr(const char *haystack, const char *needle);

size_t utf8_prev_char(const char *s, size_t pos);
size_t utf8_next_char(const char *s, size_t pos, size_t len);
size_t utf8_clamp_pos(const char *s, size_t pos, size_t len);

int tt_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);
char *tt_url_encode(const char *s);
char *tt_xml_unescape(const char *s);

bool tt_parse_age_seconds(const char *spec, int64_t *out_seconds);
bool tt_parse_iso8601_utc(const char *s, int64_t *out_epoch);

bool tt_mkdir_p(const char *path, mode_t mode);
bool tt_file_exists(const char *path);
char *tt_read_file(const char *path, size_t *out_len);
bool tt_write_file_atomic(const char *path, const void *data, size_t len);
char *tt_path_join(const char *base, const char *rel);
void tt_sanitize_filename(const char *key, char *out, size_t out_cap);

bool tt_resolve_paths(const char *cli_config_dir,
                      char *out_config_dir, size_t cfg_cap,
                      char *out_cache_dir, size_t cache_cap,
                      bool *out_portable);

#endif
