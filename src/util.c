#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_clear(StrBuf *sb) {
    sb->len = 0;
    if (sb->data) {
        sb->data[0] = '\0';
    }
}

bool sb_reserve(StrBuf *sb, size_t extra) {
    if (extra > SIZE_MAX - sb->len - 1) return false;
    size_t needed = sb->len + extra + 1;
    if (needed <= sb->cap) return true;
    size_t new_cap = sb->cap ? sb->cap : 64;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = needed;
            break;
        }
        new_cap *= 2;
    }
    char *p = realloc(sb->data, new_cap);
    if (!p) return false;
    sb->data = p;
    sb->cap = new_cap;
    return true;
}

bool sb_append_n(StrBuf *sb, const char *s, size_t n) {
    if (!s && n > 0) return false;
    if (!sb_reserve(sb, n)) return false;
    if (n > 0) {
        memcpy(sb->data + sb->len, s, n);
        sb->len += n;
    }
    sb->data[sb->len] = '\0';
    return true;
}

bool sb_append(StrBuf *sb, const char *s) {
    if (!s) return true;
    return sb_append_n(sb, s, strlen(s));
}

bool sb_append_char(StrBuf *sb, char c) {
    return sb_append_n(sb, &c, 1);
}

bool sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return false;
    }
    if (!sb_reserve(sb, (size_t)n)) {
        va_end(ap2);
        return false;
    }
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
    return true;
}

char *sb_take(StrBuf *sb) {
    if (!sb->data) {
        return tt_strdup("");
    }
    char *res = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return res;
}

char *tt_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

char *tt_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

char *tt_asprintf(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return NULL;
    }
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        va_end(ap2);
        return NULL;
    }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return buf;
}

int tt_strcasecmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int tt_strncasecmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb || a[i] == '\0' || b[i] == '\0') {
            return ca - cb;
        }
    }
    return 0;
}

const char *tt_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (tt_strncasecmp(p, needle, nlen) == 0) {
            return p;
        }
    }
    return NULL;
}

static bool is_utf8_cont(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

size_t utf8_prev_char(const char *s, size_t pos) {
    if (!s || pos == 0) return 0;
    pos--;
    while (pos > 0 && is_utf8_cont((unsigned char)s[pos])) {
        pos--;
    }
    return pos;
}

size_t utf8_next_char(const char *s, size_t pos, size_t len) {
    if (!s || pos >= len) return len;
    pos++;
    while (pos < len && is_utf8_cont((unsigned char)s[pos])) {
        pos++;
    }
    return pos;
}

size_t utf8_clamp_pos(const char *s, size_t pos, size_t len) {
    if (!s) return 0;
    if (pos >= len) return len;
    while (pos > 0 && is_utf8_cont((unsigned char)s[pos])) {
        pos--;
    }
    return pos;
}

int tt_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((in_len + 2) / 3) * 4;
    if (!out || out_cap < out_len + 1) return -1;

    size_t j = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t b0 = in[i];
        uint32_t b1 = (i + 1 < in_len) ? in[i + 1] : 0;
        uint32_t b2 = (i + 2 < in_len) ? in[i + 2] : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out[j++] = tbl[(triple >> 18) & 0x3F];
        out[j++] = tbl[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < in_len) ? tbl[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < in_len) ? tbl[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return (int)j;
}

char *tt_url_encode(const char *s) {
    if (!s) return tt_strdup("");
    StrBuf sb;
    sb_init(&sb);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            sb_append_char(&sb, (char)c);
        } else {
            sb_appendf(&sb, "%%%02X", c);
        }
    }
    return sb_take(&sb);
}

static void sb_append_utf8(StrBuf *sb, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF) return;
    if (cp <= 0x7F) {
        sb_append_char(sb, (char)cp);
    } else if (cp <= 0x7FF) {
        sb_append_char(sb, (char)(0xC0 | (cp >> 6)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        sb_append_char(sb, (char)(0xE0 | (cp >> 12)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_append_char(sb, (char)(0xF0 | (cp >> 18)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_append_char(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

char *tt_xml_unescape(const char *s) {
    if (!s) return tt_strdup("");
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '&') {
            const char *semi = strchr(s + i + 1, ';');
            if (semi && (size_t)(semi - (s + i + 1)) <= 12) {
                size_t elen = (size_t)(semi - (s + i + 1));
                const char *ent = s + i + 1;
                if (elen == 3 && strncmp(ent, "amp", 3) == 0) {
                    sb_append_char(&sb, '&');
                    i += elen + 1;
                    continue;
                } else if (elen == 2 && strncmp(ent, "lt", 2) == 0) {
                    sb_append_char(&sb, '<');
                    i += elen + 1;
                    continue;
                } else if (elen == 2 && strncmp(ent, "gt", 2) == 0) {
                    sb_append_char(&sb, '>');
                    i += elen + 1;
                    continue;
                } else if (elen == 4 && strncmp(ent, "quot", 4) == 0) {
                    sb_append_char(&sb, '"');
                    i += elen + 1;
                    continue;
                } else if (elen == 4 && strncmp(ent, "apos", 4) == 0) {
                    sb_append_char(&sb, '\'');
                    i += elen + 1;
                    continue;
                } else if (elen >= 2 && ent[0] == '#') {
                    unsigned long cp = 0;
                    char *endptr = NULL;
                    if (ent[1] == 'x' || ent[1] == 'X') {
                        cp = strtoul(ent + 2, &endptr, 16);
                    } else {
                        cp = strtoul(ent + 1, &endptr, 10);
                    }
                    if (endptr == semi && cp > 0 && cp <= 0x10FFFF) {
                        sb_append_utf8(&sb, (uint32_t)cp);
                        i += elen + 1;
                        continue;
                    }
                }
            }
        }
        sb_append_char(&sb, s[i]);
    }
    return sb_take(&sb);
}

bool tt_parse_age_seconds(const char *spec, int64_t *out_seconds) {
    if (!spec || !out_seconds) return false;
    const char *p = spec;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) return false;

    char *end = NULL;
    errno = 0;
    long long count = strtoll(p, &end, 10);
    if (errno != 0 || count <= 0 || end == p) return false;

    while (*end && isspace((unsigned char)*end)) end++;
    char unit[16] = {0};
    size_t ulen = 0;
    while (end[ulen] && !isspace((unsigned char)end[ulen]) && ulen + 1 < sizeof(unit)) {
        unit[ulen] = (char)tolower((unsigned char)end[ulen]);
        ulen++;
    }
    if (ulen == 0) return false;
    while (end[ulen] && isspace((unsigned char)end[ulen])) ulen++;
    if (end[ulen] != '\0') return false;

    size_t slen = strlen(unit);
    if (slen > 1 && unit[slen - 1] == 's') {
        unit[slen - 1] = '\0';
    }

    int64_t mult = 0;
    if (strcmp(unit, "hour") == 0) mult = 3600;
    else if (strcmp(unit, "day") == 0) mult = 86400;
    else if (strcmp(unit, "week") == 0) mult = 7 * 86400;
    else if (strcmp(unit, "month") == 0) mult = 30 * 86400;
    else if (strcmp(unit, "year") == 0) mult = 365 * 86400;
    else return false;

    if (count > INT64_MAX / mult) return false;
    *out_seconds = (int64_t)count * mult;
    return true;
}

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool tt_parse_iso8601_utc(const char *s, int64_t *out_epoch) {
    if (!s || !out_epoch) return false;
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    int off = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d%n", &yr, &mo, &dy, &hr, &mn, &sc, &off) < 6) {
        return false;
    }
    if (mo < 1 || mo > 12 || dy < 1 || dy > 31 || hr < 0 || hr > 23 || mn < 0 || mn > 59 || sc < 0 || sc > 60) {
        return false;
    }

    int64_t tz_offset = 0;
    const char *tz = s + off;
    if (*tz == '+' || *tz == '-') {
        int sign = (*tz == '+') ? 1 : -1;
        int tzh = 0, tzm = 0;
        if (sscanf(tz + 1, "%2d:%2d", &tzh, &tzm) >= 1) {
            tz_offset = sign * ((int64_t)tzh * 3600 + (int64_t)tzm * 60);
        }
    }

    int64_t days = days_from_civil(yr, (unsigned)mo, (unsigned)dy);
    *out_epoch = days * 86400 + (int64_t)hr * 3600 + (int64_t)mn * 60 + sc - tz_offset;
    return true;
}

bool tt_mkdir_p(const char *path, mode_t mode) {
    if (!path || !path[0]) return false;
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return false;
    return true;
}

bool tt_file_exists(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char *tt_read_file(const char *path, size_t *out_len) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 64L * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

bool tt_write_file_atomic(const char *path, const void *data, size_t len) {
    if (!path || (!data && len > 0)) return false;
    char *tmp = tt_asprintf("%s.tmp.%ld", path, (long)getpid());
    if (!tmp) return false;

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(tmp);
        return false;
    }
    bool ok = true;
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        ok = false;
    }
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;

    if (ok && rename(tmp, path) != 0) {
        ok = false;
    }
    if (!ok) {
        unlink(tmp);
    }
    free(tmp);
    return ok;
}

char *tt_path_join(const char *base, const char *rel) {
    if (!rel || !rel[0]) return NULL;
    if (rel[0] == '/') return tt_strdup(rel);
    if (!base || !base[0]) return tt_strdup(rel);
    size_t blen = strlen(base);
    if (base[blen - 1] == '/') {
        return tt_asprintf("%s%s", base, rel);
    }
    return tt_asprintf("%s/%s", base, rel);
}

void tt_sanitize_filename(const char *key, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    if (!key) {
        out[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; key[i] != '\0' && j + 1 < out_cap; i++) {
        unsigned char c = (unsigned char)key[i];
        if (isalnum(c) || c == '-' || c == '_') {
            out[j++] = (char)c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

static bool get_exe_dir(char *out, size_t cap) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0 || (size_t)n >= sizeof(exe)) return false;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash) return false;
    if (slash == exe) {
        if (cap < 2) return false;
        strcpy(out, "/");
        return true;
    }
    *slash = '\0';
    if (strlen(exe) >= cap) return false;
    strcpy(out, exe);
    return true;
}

bool tt_resolve_paths(const char *cli_config_dir,
                      char *out_config_dir, size_t cfg_cap,
                      char *out_cache_dir, size_t cache_cap,
                      bool *out_portable) {
    if (out_portable) *out_portable = false;

    const char *xdg_cache = getenv("XDG_CACHE_HOME");
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";

    if (cli_config_dir && cli_config_dir[0]) {
        if (snprintf(out_config_dir, cfg_cap, "%s", cli_config_dir) >= (int)cfg_cap) return false;
        if (xdg_cache && xdg_cache[0]) {
            snprintf(out_cache_dir, cache_cap, "%s/tuitube/thumbs", xdg_cache);
        } else {
            snprintf(out_cache_dir, cache_cap, "%s/.cache/tuitube/thumbs", home);
        }
    } else {
        char exe_dir[PATH_MAX];
        bool portable = false;
        if (get_exe_dir(exe_dir, sizeof(exe_dir))) {
            char *marker = tt_path_join(exe_dir, "tuitube_portable_mode.txt");
            if (marker && tt_file_exists(marker)) {
                portable = true;
            }
            free(marker);
        }

        if (portable) {
            if (out_portable) *out_portable = true;
            snprintf(out_config_dir, cfg_cap, "%s", exe_dir);
            snprintf(out_cache_dir, cache_cap, "%s/cache/thumbs", exe_dir);
        } else {
            if (xdg_config && xdg_config[0]) {
                snprintf(out_config_dir, cfg_cap, "%s/tuitube", xdg_config);
            } else {
                snprintf(out_config_dir, cfg_cap, "%s/.config/tuitube", home);
            }
            if (xdg_cache && xdg_cache[0]) {
                snprintf(out_cache_dir, cache_cap, "%s/tuitube/thumbs", xdg_cache);
            } else {
                snprintf(out_cache_dir, cache_cap, "%s/.cache/tuitube/thumbs", home);
            }
        }
    }

    tt_mkdir_p(out_config_dir, 0755);
    tt_mkdir_p(out_cache_dir, 0755);
    return true;
}
