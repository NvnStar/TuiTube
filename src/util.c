#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

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

bool tt_shm_target_dims(int logical_w, int logical_h, uint32_t scale_120,
                        int int_scale, int *out_pw, int *out_ph) {
    if (logical_w <= 0 || logical_h <= 0 || !out_pw || !out_ph) return false;

    int64_t pw64 = 0;
    int64_t ph64 = 0;
    if (scale_120 > 0u) {
        uint32_t s120 = scale_120 < 60u ? 60u : (scale_120 > 480u ? 480u : scale_120);
        pw64 = ((int64_t)logical_w * (int64_t)s120 + 60) / 120;
        ph64 = ((int64_t)logical_h * (int64_t)s120 + 60) / 120;
        if (pw64 < 1) pw64 = 1;
        if (ph64 < 1) ph64 = 1;
    } else {
        int sc = int_scale < 1 ? 1 : (int_scale > 4 ? 4 : int_scale);
        pw64 = (int64_t)logical_w * (int64_t)sc;
        ph64 = (int64_t)logical_h * (int64_t)sc;
    }

    if (pw64 > 32767 || ph64 > 32767 ||
        (uint64_t)pw64 * 4u * (uint64_t)ph64 > (uint64_t)INT32_MAX) {
        return false;
    }

    *out_pw = (int)pw64;
    *out_ph = (int)ph64;
    return true;
}

size_t tt_shm_pool_capacity(size_t needed_bytes) {
    if (needed_bytes == 0 || needed_bytes > (size_t)INT32_MAX) return 0;
    size_t rem = needed_bytes % TT_SHM_ALIGN_BYTES;
    size_t cap = rem == 0 ? needed_bytes : (needed_bytes + (TT_SHM_ALIGN_BYTES - rem));
    if (cap > (size_t)INT32_MAX) cap = (size_t)INT32_MAX;
    return cap;
}

bool tt_shm_should_shrink(size_t needed_bytes, size_t current_cap) {
    return needed_bytes > 0 && current_cap > needed_bytes &&
           (current_cap - needed_bytes) >= TT_SHM_SHRINK_THRESHOLD_BYTES;
}

bool tt_mpv_osd_dims(int64_t osd_w, int64_t osd_h, double hidpi_scale,
                     int *out_phys_w, int *out_phys_h,
                     int *out_logical_w, int *out_logical_h) {
    if (osd_w <= 0 || osd_h <= 0 ||
        !out_phys_w || !out_phys_h || !out_logical_w || !out_logical_h) {
        return false;
    }
    if (osd_w > 32767 || osd_h > 32767 ||
        (uint64_t)osd_w * 4u * (uint64_t)osd_h > (uint64_t)INT32_MAX) {
        return false;
    }

    double sc = (hidpi_scale > 0.0 && isfinite(hidpi_scale)) ? hidpi_scale : 1.0;
    if (sc < 0.5) sc = 0.5;
    if (sc > 4.0) sc = 4.0;

    int lw = (int)lround((double)osd_w / sc);
    int lh = (int)lround((double)osd_h / sc);
    if (lw < 1) lw = 1;
    if (lh < 1) lh = 1;

    *out_phys_w = (int)osd_w;
    *out_phys_h = (int)osd_h;
    *out_logical_w = lw;
    *out_logical_h = lh;
    return true;
}

bool tt_mpv_parse_script_opt_config_dir(const char *script_opts,
                                        char *out_dir, size_t out_cap) {
    if (!out_dir || out_cap == 0) return false;
    out_dir[0] = '\0';
    if (!script_opts || !script_opts[0]) return false;

    bool found = false;
    const char *p = script_opts;
    while (*p) {
        while (*p == ',') p++;
        if (!*p) break;

        const char *key = p;
        const char *eq = NULL;
        while (*p && *p != ',' && *p != '=') p++;
        if (*p != '=') {
            if (*p == ',') p++;
            continue;
        }
        eq = p;
        size_t klen = (size_t)(eq - key);
        const char *v = eq + 1;

        const char *val_start = v;
        size_t vlen = 0;
        if (*v == '%' && isdigit((unsigned char)v[1])) {
            char *after_len = NULL;
            unsigned long plen = strtoul(v + 1, &after_len, 10);
            if (after_len > v + 1 && *after_len == '%') {
                val_start = after_len + 1;
                size_t avail = strlen(val_start);
                vlen = (plen <= avail) ? (size_t)plen : avail;
                p = val_start + vlen;
                if (*p == ',') p++;
            } else {
                const char *comma = strchr(v, ',');
                vlen = comma ? (size_t)(comma - v) : strlen(v);
                p = comma ? comma + 1 : v + vlen;
            }
        } else {
            const char *comma = strchr(v, ',');
            vlen = comma ? (size_t)(comma - v) : strlen(v);
            p = comma ? comma + 1 : v + vlen;
        }

        if (klen == 18 &&
            (strncmp(key, "tuitube-config_dir", 18) == 0 ||
             strncmp(key, "tuitube_config_dir", 18) == 0)) {
            if (vlen == 0) {
                continue;
            }
            if (vlen == 1 && val_start[0] == '~') {
                const char *home = getenv("HOME");
                if (!home || !home[0]) home = "/tmp";
                int n = snprintf(out_dir, out_cap, "%s", home);
                if (n < 0 || (size_t)n >= out_cap) {
                    out_dir[0] = '\0';
                    return false;
                }
            } else if (vlen >= 2 && val_start[0] == '~' && val_start[1] == '/') {
                const char *home = getenv("HOME");
                if (!home || !home[0]) home = "/tmp";
                int n = snprintf(out_dir, out_cap, "%s/%.*s", home,
                                 (int)(vlen - 2), val_start + 2);
                if (n < 0 || (size_t)n >= out_cap) {
                    out_dir[0] = '\0';
                    return false;
                }
            } else {
                if (vlen >= out_cap) {
                    out_dir[0] = '\0';
                    return false;
                }
                memcpy(out_dir, val_start, vlen);
                out_dir[vlen] = '\0';
            }
            found = true;
        }
    }

    return found;
}

static void copy_utf8_bounded(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;
    size_t slen = strlen(src);
    size_t n = slen;
    if (n >= cap) {
        n = utf8_clamp_pos(src, cap - 1, slen);
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool tt_mpv_parse_key_event(const char *state_str, const char *key_name,
                            const char *key_text, const char *scale_str,
                            TtMpvKeyEvent *out) {
    if (!state_str || !state_str[0] || !key_name || !key_name[0] || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    bool canceled = (state_str[1] != '\0' && state_str[2] == 'c');
    switch (state_str[0]) {
        case 'd':
            out->pressed = !canceled;
            out->repeat = false;
            break;
        case 'r':
            out->pressed = !canceled;
            out->repeat = !canceled;
            break;
        case 'p':
            out->pressed = !canceled;
            out->repeat = false;
            out->is_press_pulse = !canceled;
            break;
        case 'u':
            out->pressed = false;
            out->repeat = false;
            break;
        default:
            return false;
    }

    /* Strip modifier prefixes matching TT_MOD_CTRL/SHIFT/ALT/SUPER (1,2,4,8) */
    const char *base = key_name;
    for (;;) {
        if (tt_strncasecmp(base, "Ctrl+", 5) == 0) {
            out->mods |= (1u << 0);
            base += 5;
        } else if (tt_strncasecmp(base, "Shift+", 6) == 0) {
            out->mods |= (1u << 1);
            base += 6;
        } else if (tt_strncasecmp(base, "Alt+", 4) == 0) {
            out->mods |= (1u << 2);
            base += 4;
        } else if (tt_strncasecmp(base, "Meta+", 5) == 0) {
            out->mods |= (1u << 3);
            base += 5;
        } else if (tt_strncasecmp(base, "Super+", 6) == 0) {
            out->mods |= (1u << 3);
            base += 6;
        } else {
            break;
        }
    }
    if (!base[0]) return false;

    /* Mouse buttons */
    if (tt_strcasecmp(base, "MBTN_LEFT") == 0) {
        out->kind = TT_MPV_INPUT_BUTTON;
        out->mouse_btn = 1;
        return true;
    }
    if (tt_strcasecmp(base, "MBTN_LEFT_DBL") == 0) {
        if (!out->pressed) return false;
        out->kind = TT_MPV_INPUT_BUTTON;
        out->mouse_btn = 1;
        out->double_click = true;
        return true;
    }
    if (tt_strcasecmp(base, "MBTN_RIGHT") == 0) {
        out->kind = TT_MPV_INPUT_BUTTON;
        out->mouse_btn = 2;
        return true;
    }
    if (tt_strcasecmp(base, "MBTN_MID") == 0) {
        out->kind = TT_MPV_INPUT_BUTTON;
        out->mouse_btn = 3;
        return true;
    }
    if (tt_strcasecmp(base, "MBTN_BACK") == 0) {
        out->kind = TT_MPV_INPUT_BUTTON;
        out->mouse_btn = 4;
        return true;
    }
    if (tt_strcasecmp(base, "MBTN_FORWARD") == 0) {
        out->kind = TT_MPV_INPUT_BUTTON;
        out->mouse_btn = 5;
        return true;
    }

    /* Wheel / Axis */
    bool w_up = (tt_strcasecmp(base, "WHEEL_UP") == 0 || tt_strcasecmp(base, "AXIS_UP") == 0);
    bool w_down = (tt_strcasecmp(base, "WHEEL_DOWN") == 0 || tt_strcasecmp(base, "AXIS_DOWN") == 0);
    bool w_left = (tt_strcasecmp(base, "WHEEL_LEFT") == 0 || tt_strcasecmp(base, "AXIS_LEFT") == 0);
    bool w_right = (tt_strcasecmp(base, "WHEEL_RIGHT") == 0 || tt_strcasecmp(base, "AXIS_RIGHT") == 0);
    if (w_up || w_down || w_left || w_right) {
        if (!out->pressed) return false;
        double sc = 1.0;
        if (scale_str && scale_str[0]) {
            char *end = NULL;
            double v = strtod(scale_str, &end);
            if (end != scale_str && isfinite(v) && v > 0.0) {
                sc = v;
            }
        }
        if (sc < 0.05) sc = 0.05;
        if (sc > 20.0) sc = 20.0;
        double step = 15.0 * sc;
        out->kind = TT_MPV_INPUT_WHEEL;
        if (w_up) out->wheel_dy = -step;
        else if (w_down) out->wheel_dy = step;
        else if (w_left) out->wheel_dx = -step;
        else if (w_right) out->wheel_dx = step;
        return true;
    }

    /* Keyboard & ANY_UNICODE / IME text */
    out->kind = TT_MPV_INPUT_KEY;

    static const struct { const char *name; uint32_t sym; } named[] = {
        {"ESC",       XKB_KEY_Escape},
        {"ESCAPE",    XKB_KEY_Escape},
        {"ENTER",     XKB_KEY_Return},
        {"RETURN",    XKB_KEY_Return},
        {"KP_ENTER",  XKB_KEY_KP_Enter},
        {"TAB",       XKB_KEY_Tab},
        {"BS",        XKB_KEY_BackSpace},
        {"BACKSPACE", XKB_KEY_BackSpace},
        {"DEL",       XKB_KEY_Delete},
        {"DELETE",    XKB_KEY_Delete},
        {"INS",       XKB_KEY_Insert},
        {"INSERT",    XKB_KEY_Insert},
        {"UP",        XKB_KEY_Up},
        {"DOWN",      XKB_KEY_Down},
        {"LEFT",      XKB_KEY_Left},
        {"RIGHT",     XKB_KEY_Right},
        {"PGUP",      XKB_KEY_Page_Up},
        {"PAGE_UP",   XKB_KEY_Page_Up},
        {"PGDWN",     XKB_KEY_Page_Down},
        {"PAGE_DOWN", XKB_KEY_Page_Down},
        {"HOME",      XKB_KEY_Home},
        {"END",       XKB_KEY_End},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (tt_strcasecmp(base, named[i].name) == 0) {
            out->keysym = named[i].sym;
            return true;
        }
    }

    if (tt_strcasecmp(base, "SPACE") == 0) {
        out->keysym = XKB_KEY_space;
        if ((out->mods & ~(1u << 1)) == 0) {
            out->utf8[0] = ' ';
            out->utf8[1] = '\0';
        }
        return true;
    }
    if (tt_strcasecmp(base, "KP_DIVIDE") == 0) {
        out->keysym = XKB_KEY_slash;
        if ((out->mods & ((1u << 0) | (1u << 2) | (1u << 3))) == 0) {
            out->utf8[0] = '/';
            out->utf8[1] = '\0';
        }
        return true;
    }
    if (tt_strcasecmp(base, "SHARP") == 0) {
        out->keysym = (uint32_t)'#';
        if ((out->mods & ((1u << 0) | (1u << 2) | (1u << 3))) == 0) {
            out->utf8[0] = '#';
            out->utf8[1] = '\0';
        }
        return true;
    }
    if ((base[0] == 'F' || base[0] == 'f') && isdigit((unsigned char)base[1])) {
        char *end = NULL;
        long fn = strtol(base + 1, &end, 10);
        if (end && *end == '\0' && fn >= 1 && fn <= 12) {
            out->keysym = (uint32_t)(XKB_KEY_F1 + (fn - 1));
            return true;
        }
    }

    if (base[1] == '\0' &&
        (unsigned char)base[0] >= 0x21 && (unsigned char)base[0] <= 0x7E) {
        unsigned char c = (unsigned char)base[0];
        if (c >= 'A' && c <= 'Z') {
            out->mods |= (1u << 1);
        }
        out->keysym = (uint32_t)c;
        if ((out->mods & ((1u << 0) | (1u << 2) | (1u << 3))) == 0) {
            const char *txt = (key_text && key_text[0]) ? key_text : base;
            copy_utf8_bounded(out->utf8, sizeof(out->utf8), txt);
        }
        return true;
    }

    const char *src_text = NULL;
    if (tt_strcasecmp(base, "ANY_UNICODE") == 0) {
        src_text = key_text;
    } else if ((unsigned char)base[0] >= 0x80) {
        src_text = (key_text && key_text[0]) ? key_text : base;
    } else if (key_text && key_text[0]) {
        src_text = key_text;
    }

    if (!src_text || !src_text[0]) {
        return false;
    }

    copy_utf8_bounded(out->utf8, sizeof(out->utf8), src_text);
    if (!out->utf8[0]) return false;

    if (out->utf8[1] == '\0' &&
        (unsigned char)out->utf8[0] >= 0x20 && (unsigned char)out->utf8[0] <= 0x7E) {
        unsigned char c = (unsigned char)out->utf8[0];
        if (c == ' ') {
            out->keysym = XKB_KEY_space;
        } else {
            if (c >= 'A' && c <= 'Z') {
                out->mods |= (1u << 1);
            }
            out->keysym = (uint32_t)c;
        }
    } else {
        out->keysym = XKB_KEY_NoSymbol;
    }
    return true;
}
