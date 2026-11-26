#include "util.h"
#include "tt_keysyms.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include <wctype.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

#ifdef _WIN32
wchar_t *tt_utf8_to_wide(const char *utf8) {
    if (!utf8) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

char *tt_wide_to_utf8(const wchar_t *wide) {
    if (!wide) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *u = malloc((size_t)n);
    if (!u) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, u, n, NULL, NULL) <= 0) {
        free(u);
        return NULL;
    }
    return u;
}
#endif

static bool is_path_sep(char c) {
    return c == '/' || c == '\\';
}

bool tt_is_abs_path(const char *path) {
    if (!path || !path[0]) return false;
    if (path[0] == '/') return true;
    if (path[0] == '\\' && path[1] == '\\') return true;
    if (isalpha((unsigned char)path[0]) && path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\' || path[2] == '\0')) {
        return true;
    }
    return false;
}

static bool make_single_dir(const char *dir, unsigned int mode) {
#ifdef _WIN32
    (void)mode;
    wchar_t *wdir = tt_utf8_to_wide(dir);
    if (!wdir) return false;
    BOOL ok = CreateDirectoryW(wdir, NULL);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) {
            DWORD attr = GetFileAttributesW(wdir);
            ok = (attr != INVALID_FILE_ATTRIBUTES &&
                  (attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
        }
    }
    free(wdir);
    return ok != FALSE;
#else
    return mkdir(dir, (mode_t)mode) == 0 || errno == EEXIST;
#endif
}

bool tt_mkdir_p(const char *path, unsigned int mode) {
    if (!path || !path[0]) return false;
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    while (len > 1 && is_path_sep(tmp[len - 1])) {
        if (len == 3 && isalpha((unsigned char)tmp[0]) && tmp[1] == ':') {
            break;
        }
        tmp[--len] = '\0';
    }

    size_t start = 1;
    if (isalpha((unsigned char)tmp[0]) && tmp[1] == ':') {
        if (tmp[2] == '\0' || (is_path_sep(tmp[2]) && tmp[3] == '\0')) {
            return true;
        }
        start = is_path_sep(tmp[2]) ? 3 : 2;
    } else if (is_path_sep(tmp[0]) && is_path_sep(tmp[1])) {
        /* Skip \\server\share prefix */
        size_t i = 2;
        while (tmp[i] && !is_path_sep(tmp[i])) i++;
        if (tmp[i]) i++;
        while (tmp[i] && !is_path_sep(tmp[i])) i++;
        if (tmp[i]) i++;
        start = i;
        if (tmp[start] == '\0') return true;
    }

    for (char *p = tmp + start; *p; p++) {
        if (is_path_sep(*p)) {
            char saved = *p;
            *p = '\0';
            if (!make_single_dir(tmp, mode)) return false;
            *p = saved;
        }
    }
    if (!make_single_dir(tmp, mode)) return false;
    return true;
}

bool tt_file_exists(const char *path) {
    if (!path || !path[0]) return false;
#ifdef _WIN32
    wchar_t *wpath = tt_utf8_to_wide(path);
    if (!wpath) return false;
    DWORD attr = GetFileAttributesW(wpath);
    free(wpath);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

char *tt_read_file(const char *path, size_t *out_len) {
    if (!path) return NULL;
#ifdef _WIN32
    wchar_t *wpath = tt_utf8_to_wide(path);
    if (!wpath) return NULL;
    FILE *f = _wfopen(wpath, L"rb");
    free(wpath);
#else
    FILE *f = fopen(path, "rb");
#endif
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
#ifdef _WIN32
    char *tmp = tt_asprintf("%s.tmp.%lu", path, (unsigned long)GetCurrentProcessId());
    if (!tmp) return false;
    wchar_t *wtmp = tt_utf8_to_wide(tmp);
    wchar_t *wpath = tt_utf8_to_wide(path);
    free(tmp);
    if (!wtmp || !wpath) {
        free(wtmp);
        free(wpath);
        return false;
    }

    FILE *f = _wfopen(wtmp, L"wb");
    if (!f) {
        free(wtmp);
        free(wpath);
        return false;
    }
    bool ok = true;
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        ok = false;
    }
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;

    if (ok && !MoveFileExW(wtmp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
    }
    if (!ok) {
        DeleteFileW(wtmp);
    }
    free(wtmp);
    free(wpath);
    return ok;
#else
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
#endif
}

char *tt_path_join(const char *base, const char *rel) {
    if (!rel || !rel[0]) return NULL;
    if (tt_is_abs_path(rel)) return tt_strdup(rel);
    if (!base || !base[0]) return tt_strdup(rel);
    size_t blen = strlen(base);
    while (blen > 1 && is_path_sep(base[blen - 1])) {
        if (blen == 3 && isalpha((unsigned char)base[0]) && base[1] == ':') {
            break;
        }
        blen--;
    }
    if (is_path_sep(base[blen - 1])) {
        return tt_asprintf("%.*s%s", (int)blen, base, rel);
    }
    return tt_asprintf("%.*s/%s", (int)blen, base, rel);
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
#ifdef _WIN32
    wchar_t wexe[4096];
    DWORD n = GetModuleFileNameW(NULL, wexe, (DWORD)(sizeof(wexe) / sizeof(wexe[0])));
    if (n == 0 || n >= (DWORD)(sizeof(wexe) / sizeof(wexe[0]))) return false;
    char *exe = tt_wide_to_utf8(wexe);
    if (!exe) return false;
    char *slash = NULL;
    for (char *p = exe; *p; p++) {
        if (*p == '/' || *p == '\\') slash = p;
    }
    if (!slash) {
        free(exe);
        return false;
    }
    if (slash == exe) {
        free(exe);
        if (cap < 2) return false;
        strcpy(out, "/");
        return true;
    }
    if ((size_t)(slash - exe) == 2 && isalpha((unsigned char)exe[0]) && exe[1] == ':') {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    bool fits = strlen(exe) < cap;
    if (fits) strcpy(out, exe);
    free(exe);
    return fits;
#else
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
#endif
}

bool tt_resolve_paths(const char *cli_config_dir,
                      char *out_config_dir, size_t cfg_cap,
                      char *out_cache_dir, size_t cache_cap,
                      bool *out_portable) {
    if (out_portable) *out_portable = false;

    const char *xdg_cache = getenv("XDG_CACHE_HOME");
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    const char *localappdata = getenv("LOCALAPPDATA");
    const char *userprofile = getenv("USERPROFILE");
    if (!userprofile || !userprofile[0]) userprofile = getenv("HOME");
    if (!userprofile || !userprofile[0]) userprofile = getenv("TEMP");
    if (!userprofile || !userprofile[0]) userprofile = ".";
#else
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
#endif

    if (cli_config_dir && cli_config_dir[0]) {
        if (snprintf(out_config_dir, cfg_cap, "%s", cli_config_dir) >= (int)cfg_cap) return false;
        if (xdg_cache && xdg_cache[0]) {
            snprintf(out_cache_dir, cache_cap, "%s/tuitube/thumbs", xdg_cache);
#ifdef _WIN32
        } else if (localappdata && localappdata[0]) {
            snprintf(out_cache_dir, cache_cap, "%s/tuitube/thumbs", localappdata);
        } else {
            snprintf(out_cache_dir, cache_cap, "%s/AppData/Local/tuitube/thumbs", userprofile);
#else
        } else {
            snprintf(out_cache_dir, cache_cap, "%s/.cache/tuitube/thumbs", home);
#endif
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
#ifdef _WIN32
            } else if (appdata && appdata[0]) {
                snprintf(out_config_dir, cfg_cap, "%s/tuitube", appdata);
            } else {
                snprintf(out_config_dir, cfg_cap, "%s/AppData/Roaming/tuitube", userprofile);
#else
            } else {
                snprintf(out_config_dir, cfg_cap, "%s/.config/tuitube", home);
#endif
            }
            if (xdg_cache && xdg_cache[0]) {
                snprintf(out_cache_dir, cache_cap, "%s/tuitube/thumbs", xdg_cache);
#ifdef _WIN32
            } else if (localappdata && localappdata[0]) {
                snprintf(out_cache_dir, cache_cap, "%s/tuitube/thumbs", localappdata);
            } else {
                snprintf(out_cache_dir, cache_cap, "%s/AppData/Local/tuitube/thumbs", userprofile);
#else
            } else {
                snprintf(out_cache_dir, cache_cap, "%s/.cache/tuitube/thumbs", home);
#endif
            }
        }
    }

    tt_mkdir_p(out_config_dir, 0755);
    tt_mkdir_p(out_cache_dir, 0755);
    return true;
}

bool tt_win32_quote_arg(const char *arg, StrBuf *out) {
    if (!arg || !out) return false;
    bool need_quote = (arg[0] == '\0');
    for (const char *p = arg; *p && !need_quote; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v' || *p == '"') {
            need_quote = true;
        }
    }
    if (!need_quote) {
        return sb_append(out, arg);
    }
    if (!sb_append_char(out, '"')) return false;
    for (size_t i = 0;;) {
        size_t bs = 0;
        while (arg[i] == '\\') {
            bs++;
            i++;
        }
        if (arg[i] == '\0') {
            for (size_t k = 0; k < bs * 2u; k++) {
                if (!sb_append_char(out, '\\')) return false;
            }
            break;
        } else if (arg[i] == '"') {
            for (size_t k = 0; k < bs * 2u + 1u; k++) {
                if (!sb_append_char(out, '\\')) return false;
            }
            if (!sb_append_char(out, '"')) return false;
            i++;
        } else {
            for (size_t k = 0; k < bs; k++) {
                if (!sb_append_char(out, '\\')) return false;
            }
            if (!sb_append_char(out, arg[i])) return false;
            i++;
        }
    }
    return sb_append_char(out, '"');
}

char *tt_win32_build_cmdline(const char *const *argv) {
    if (!argv || !argv[0]) return NULL;
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; argv[i]; i++) {
        if (i > 0 && !sb_append_char(&sb, ' ')) {
            sb_free(&sb);
            return NULL;
        }
        if (!tt_win32_quote_arg(argv[i], &sb)) {
            sb_free(&sb);
            return NULL;
        }
    }
    return sb_take(&sb);
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool tt_ttf_read_family_name(const uint8_t *data, size_t len, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return false;
    out[0] = '\0';
    if (!data || len < 12) return false;

    uint32_t sfnt_off = 0;
    uint32_t tag0 = read_be32(data);
    if (tag0 == 0x74746366u) { /* 'ttcf' */
        if (len < 16) return false;
        uint32_t num_fonts = read_be32(data + 8);
        if (num_fonts == 0) return false;
        sfnt_off = read_be32(data + 12);
        if (sfnt_off > len || len - sfnt_off < 12) return false;
        tag0 = read_be32(data + sfnt_off);
    }

    if (tag0 != 0x00010000u && tag0 != 0x4F54544Fu && tag0 != 0x74727565u) {
        return false;
    }

    uint16_t num_tables = read_be16(data + sfnt_off + 4);
    size_t dir_off = (size_t)sfnt_off + 12u;
    if ((size_t)num_tables > (len - dir_off) / 16u) {
        return false;
    }

    uint32_t name_off = 0;
    uint32_t name_len = 0;
    bool found_name = false;
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *rec = data + dir_off + (size_t)i * 16u;
        if (read_be32(rec) == 0x6E616D65u) { /* 'name' */
            name_off = read_be32(rec + 8);
            name_len = read_be32(rec + 12);
            found_name = true;
            break;
        }
    }
    if (!found_name || name_len < 6 || name_off > len || name_len > len - name_off) {
        return false;
    }

    const uint8_t *nt = data + name_off;
    uint16_t count = read_be16(nt + 2);
    uint16_t str_off = read_be16(nt + 4);
    if (str_off > name_len || (size_t)count > (name_len - 6u) / 12u) {
        return false;
    }

    int best_score = -1;
    char *best = NULL;

    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *nr = nt + 6u + (size_t)i * 12u;
        uint16_t plat = read_be16(nr + 0);
        uint16_t enc = read_be16(nr + 2);
        uint16_t lang = read_be16(nr + 4);
        uint16_t nid = read_be16(nr + 6);
        uint16_t rlen = read_be16(nr + 8);
        uint16_t roff = read_be16(nr + 10);

        if (nid != 16u && nid != 1u) continue;
        if (rlen == 0) continue;
        if ((size_t)str_off + (size_t)roff > (size_t)name_len) continue;
        if ((size_t)rlen > (size_t)name_len - ((size_t)str_off + (size_t)roff)) continue;

        bool is_utf16 = (plat == 3u && (enc == 1u || enc == 10u)) || (plat == 0u);
        bool is_mac = (plat == 1u && enc == 0u);
        if (!is_utf16 && !is_mac) continue;

        int score = (nid == 16u) ? 100 : 10;
        if (plat == 3u) {
            score += 20;
            if (lang == 0x0409u) score += 4;
        } else if (plat == 0u) {
            score += 15;
        } else {
            score += 5;
            if (lang == 0u) score += 2;
        }
        if (score <= best_score) continue;

        const uint8_t *s = nt + str_off + roff;
        StrBuf cand;
        sb_init(&cand);
        if (is_utf16) {
            for (size_t j = 0; j + 1u < rlen; j += 2u) {
                uint16_t u = read_be16(s + j);
                if (u >= 0xD800u && u <= 0xDBFFu && j + 3u < rlen) {
                    uint16_t u2 = read_be16(s + j + 2u);
                    if (u2 >= 0xDC00u && u2 <= 0xDFFFu) {
                        uint32_t cp = 0x10000u +
                            (((uint32_t)(u - 0xD800u) << 10) | (uint32_t)(u2 - 0xDC00u));
                        sb_append_utf8(&cand, cp);
                        j += 2u;
                        continue;
                    }
                }
                if (u >= 0xD800u && u <= 0xDFFFu) continue;
                if (u != 0) sb_append_utf8(&cand, u);
            }
        } else {
            for (size_t j = 0; j < rlen; j++) {
                if (s[j] >= 0x20u && s[j] <= 0x7Eu) {
                    sb_append_char(&cand, (char)s[j]);
                }
            }
        }

        if (cand.len > 0) {
            best_score = score;
            free(best);
            best = sb_take(&cand);
        } else {
            sb_free(&cand);
        }
    }

    if (!best || !best[0]) {
        free(best);
        return false;
    }
    snprintf(out, out_cap, "%s", best);
    free(best);
    return out[0] != '\0';
}

#ifdef _WIN32
static bool win32_has_path_sep(const char *s) {
    if (!s) return false;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') return true;
    }
    return false;
}

static bool win32_has_ext(const char *s) {
    if (!s) return false;
    const char *base = s;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    const char *dot = strrchr(base, '.');
    return dot != NULL && dot[1] != '\0';
}

static bool win32_dir_exists_w(const wchar_t *wdir) {
    if (!wdir || !wdir[0]) return false;
    DWORD attr = GetFileAttributesW(wdir);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static wchar_t *win32_read_reg_path(HKEY root, const wchar_t *subkey) {
    DWORD cb = 0;
    LSTATUS st = RegGetValueW(root, subkey, L"Path",
                              RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                              NULL, NULL, &cb);
    if (st != ERROR_SUCCESS || cb < sizeof(wchar_t)) return NULL;
    wchar_t *buf = malloc((size_t)cb + sizeof(wchar_t));
    if (!buf) return NULL;
    st = RegGetValueW(root, subkey, L"Path",
                      RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                      NULL, buf, &cb);
    if (st != ERROR_SUCCESS) {
        free(buf);
        return NULL;
    }
    buf[cb / sizeof(wchar_t)] = L'\0';
    return buf;
}

static size_t win32_trim_path_seg(const wchar_t *s, size_t len) {
    while (len > 1u && (s[len - 1u] == L'\\' || s[len - 1u] == L'/')) {
        if (len == 3u && s[1] == L':') break;
        len--;
    }
    return len;
}

static bool win32_wcsieq_n(const wchar_t *a, size_t alen, const wchar_t *b, size_t blen) {
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; i++) {
        wint_t ca = (a[i] == L'/') ? (wint_t)L'\\' : towlower((wint_t)a[i]);
        wint_t cb = (b[i] == L'/') ? (wint_t)L'\\' : towlower((wint_t)b[i]);
        if (ca != cb) return false;
    }
    return true;
}

static char *win32_path_join_bs(const char *base, const char *rel) {
    if (!rel || !rel[0]) return NULL;
    if (!base || !base[0]) return tt_strdup(rel);
    size_t blen = strlen(base);
    while (blen > 1 && is_path_sep(base[blen - 1])) {
        if (blen == 3 && isalpha((unsigned char)base[0]) && base[1] == ':') break;
        blen--;
    }
    if (is_path_sep(base[blen - 1])) {
        return tt_asprintf("%.*s%s", (int)blen, base, rel);
    }
    return tt_asprintf("%.*s\\%s", (int)blen, base, rel);
}

static bool win32_path_contains_seg(const wchar_t *path_str, const wchar_t *seg, size_t seg_len) {
    if (!path_str || !seg || seg_len == 0) return false;
    size_t want_len = win32_trim_path_seg(seg, seg_len);
    const wchar_t *p = path_str;
    while (*p) {
        const wchar_t *semi = wcschr(p, L';');
        size_t slen = semi ? (size_t)(semi - p) : wcslen(p);
        while (slen > 0 && (*p == L' ' || *p == L'\t')) { p++; slen--; }
        while (slen > 0 && (p[slen - 1u] == L' ' || p[slen - 1u] == L'\t')) slen--;
        if (slen > 0) {
            size_t tlen = win32_trim_path_seg(p, slen);
            if (win32_wcsieq_n(p, tlen, seg, want_len)) return true;
        }
        if (!semi) break;
        p = semi + 1;
    }
    return false;
}

static bool win32_append_path_entries(wchar_t **cur_path, const wchar_t *extra, bool *changed) {
    if (!cur_path || !extra || !extra[0]) return true;
    const wchar_t *p = extra;
    while (*p) {
        const wchar_t *semi = wcschr(p, L';');
        size_t slen = semi ? (size_t)(semi - p) : wcslen(p);
        while (slen > 0 && (*p == L' ' || *p == L'\t')) { p++; slen--; }
        while (slen > 0 && (p[slen - 1u] == L' ' || p[slen - 1u] == L'\t')) slen--;
        if (slen > 0 && !win32_path_contains_seg(*cur_path, p, slen)) {
            size_t cur_len = *cur_path ? wcslen(*cur_path) : 0;
            bool need_semi = (cur_len > 0 && (*cur_path)[cur_len - 1u] != L';');
            size_t new_len = cur_len + (need_semi ? 1u : 0u) + slen;
            wchar_t *grown = realloc(*cur_path, (new_len + 1u) * sizeof(wchar_t));
            if (!grown) return false;
            if (need_semi) grown[cur_len++] = L';';
            memcpy(grown + cur_len, p, slen * sizeof(wchar_t));
            grown[cur_len + slen] = L'\0';
            *cur_path = grown;
            if (changed) *changed = true;
        }
        if (!semi) break;
        p = semi + 1;
    }
    return true;
}

void tt_win32_refresh_path(void) {
    DWORD need = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *merged = NULL;
    if (need > 0) {
        merged = malloc(((size_t)need + 1u) * sizeof(wchar_t));
        if (!merged) return;
        DWORD got = GetEnvironmentVariableW(L"PATH", merged, need + 1u);
        merged[got <= need ? got : need] = L'\0';
    } else {
        merged = calloc(1, sizeof(wchar_t));
        if (!merged) return;
    }

    bool changed = false;
    wchar_t *user_path = win32_read_reg_path(HKEY_CURRENT_USER, L"Environment");
    if (user_path) {
        win32_append_path_entries(&merged, user_path, &changed);
        free(user_path);
    }
    wchar_t *sys_path = win32_read_reg_path(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    if (sys_path) {
        win32_append_path_entries(&merged, sys_path, &changed);
        free(sys_path);
    }

    const char *localappdata = getenv("LOCALAPPDATA");
    if (localappdata && localappdata[0]) {
        char *wg_links = win32_path_join_bs(localappdata, "Microsoft\\WinGet\\Links");
        if (wg_links) {
            wchar_t *wdir = tt_utf8_to_wide(wg_links);
            free(wg_links);
            if (wdir) {
                if (win32_dir_exists_w(wdir)) {
                    win32_append_path_entries(&merged, wdir, &changed);
                }
                free(wdir);
            }
        }
    }

    const char *progfiles = getenv("ProgramFiles");
    if (progfiles && progfiles[0]) {
        char *wg_sys_links = win32_path_join_bs(progfiles, "WinGet\\Links");
        if (wg_sys_links) {
            wchar_t *wdir = tt_utf8_to_wide(wg_sys_links);
            free(wg_sys_links);
            if (wdir) {
                if (win32_dir_exists_w(wdir)) {
                    win32_append_path_entries(&merged, wdir, &changed);
                }
                free(wdir);
            }
        }
    }

    if (changed && merged && merged[0]) {
        SetEnvironmentVariableW(L"PATH", merged);
    }
    free(merged);
}

static char *win32_search_path_ext(const wchar_t *search_path, const wchar_t *wname, const wchar_t *wext) {
    DWORD need = SearchPathW(search_path, wname, wext, 0, NULL, NULL);
    if (need == 0) return NULL;
    wchar_t *buf = malloc(((size_t)need + 1u) * sizeof(wchar_t));
    if (!buf) return NULL;
    DWORD got = SearchPathW(search_path, wname, wext, need + 1u, buf, NULL);
    if (got == 0 || got > need) {
        free(buf);
        return NULL;
    }
    DWORD attr = GetFileAttributesW(buf);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        free(buf);
        return NULL;
    }
    char *utf8 = tt_wide_to_utf8(buf);
    free(buf);
    return utf8;
}

static char *win32_search_path(const wchar_t *search_path, const char *name) {
    if (!name || !name[0]) return NULL;
    wchar_t *wname = tt_utf8_to_wide(name);
    if (!wname) return NULL;
    char *res = NULL;
    if (win32_has_ext(name)) {
        res = win32_search_path_ext(search_path, wname, NULL);
    } else {
        res = win32_search_path_ext(search_path, wname, L".exe");
        if (!res) res = win32_search_path_ext(search_path, wname, L".com");
    }
    free(wname);
    return res;
}

static char *win32_try_in_dir(const char *dir, const char *name) {
    if (!dir || !dir[0] || !name || !name[0]) return NULL;
    if (win32_has_ext(name)) {
        char *cand = win32_path_join_bs(dir, name);
        if (cand && tt_file_exists(cand)) return cand;
        free(cand);
        return NULL;
    }
    static const char *const exts[] = {".exe", ".com"};
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        char *fname = tt_asprintf("%s%s", name, exts[i]);
        if (!fname) continue;
        char *cand = win32_path_join_bs(dir, fname);
        free(fname);
        if (cand && tt_file_exists(cand)) return cand;
        free(cand);
    }
    return NULL;
}

static char *win32_scan_winget_packages(const char *localappdata, const char *pkg_hint,
                                        const char *exe_name) {
    if (!localappdata || !localappdata[0] || !pkg_hint || !exe_name) return NULL;
    char *pkgs_dir = win32_path_join_bs(localappdata, "Microsoft\\WinGet\\Packages");
    if (!pkgs_dir) return NULL;
    char *pat = tt_asprintf("%s\\*", pkgs_dir);
    wchar_t *wpat = pat ? tt_utf8_to_wide(pat) : NULL;
    free(pat);
    if (!wpat) {
        free(pkgs_dir);
        return NULL;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    free(wpat);
    if (h == INVALID_HANDLE_VALUE) {
        free(pkgs_dir);
        return NULL;
    }

    char *found = NULL;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        char *dname = tt_wide_to_utf8(fd.cFileName);
        if (!dname) continue;
        if (tt_strcasestr(dname, pkg_hint) == NULL) {
            free(dname);
            continue;
        }
        char *pkg_path = win32_path_join_bs(pkgs_dir, dname);
        free(dname);
        if (!pkg_path) continue;

        found = win32_try_in_dir(pkg_path, exe_name);
        if (!found) {
            /* Check one nested subdirectory (e.g. extracted archive folder) */
            char *sub_pat = tt_asprintf("%s\\*", pkg_path);
            wchar_t *wsub = sub_pat ? tt_utf8_to_wide(sub_pat) : NULL;
            free(sub_pat);
            if (wsub) {
                WIN32_FIND_DATAW sfd;
                HANDLE sh = FindFirstFileW(wsub, &sfd);
                free(wsub);
                if (sh != INVALID_HANDLE_VALUE) {
                    do {
                        if ((sfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                        if (wcscmp(sfd.cFileName, L".") == 0 || wcscmp(sfd.cFileName, L"..") == 0) continue;
                        char *sname = tt_wide_to_utf8(sfd.cFileName);
                        if (!sname) continue;
                        char *sub_path = win32_path_join_bs(pkg_path, sname);
                        free(sname);
                        if (sub_path) {
                            found = win32_try_in_dir(sub_path, exe_name);
                            free(sub_path);
                        }
                    } while (!found && FindNextFileW(sh, &sfd));
                    FindClose(sh);
                }
            }
        }
        free(pkg_path);
    } while (!found && FindNextFileW(h, &fd));

    FindClose(h);
    free(pkgs_dir);
    return found;
}

char *tt_win32_resolve_exe(const char *exe) {
    if (!exe || !exe[0]) return NULL;

    if (win32_has_path_sep(exe)) {
        if (tt_file_exists(exe)) return tt_strdup(exe);
        if (!win32_has_ext(exe)) {
            char *c1 = tt_asprintf("%s.exe", exe);
            if (c1 && tt_file_exists(c1)) return c1;
            free(c1);
            char *c2 = tt_asprintf("%s.com", exe);
            if (c2 && tt_file_exists(c2)) return c2;
            free(c2);
        }
        return NULL;
    }

    /* 1. Current process PATH (and application directory) */
    char *found = win32_search_path(NULL, exe);
    if (found) return found;

    /* 2. Fresh registry PATH (covers winget/scoop/choco installs before shell restart) */
    wchar_t *user_path = win32_read_reg_path(HKEY_CURRENT_USER, L"Environment");
    if (user_path) {
        found = win32_search_path(user_path, exe);
        free(user_path);
        if (found) return found;
    }
    wchar_t *sys_path = win32_read_reg_path(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    if (sys_path) {
        found = win32_search_path(sys_path, exe);
        free(sys_path);
        if (found) return found;
    }

    /* 3. Well-known Windows package manager link/shim directories */
    const char *localappdata = getenv("LOCALAPPDATA");
    const char *progfiles = getenv("ProgramFiles");
    const char *progfiles_x86 = getenv("ProgramFiles(x86)");
    const char *userprofile = getenv("USERPROFILE");
    const char *progdata = getenv("ProgramData");

    const struct { const char *base; const char *sub; } shim_dirs[] = {
        {localappdata, "Microsoft\\WinGet\\Links"},
        {progfiles,    "WinGet\\Links"},
        {userprofile,  "scoop\\shims"},
        {progdata,     "chocolatey\\bin"},
        {localappdata, "Microsoft\\WindowsApps"},
    };
    for (size_t i = 0; i < sizeof(shim_dirs) / sizeof(shim_dirs[0]); i++) {
        if (!shim_dirs[i].base || !shim_dirs[i].base[0]) continue;
        char *dir = win32_path_join_bs(shim_dirs[i].base, shim_dirs[i].sub);
        if (!dir) continue;
        found = win32_try_in_dir(dir, exe);
        free(dir);
        if (found) return found;
    }

    /* 4. Tool-specific fallback locations */
    if (tt_strcasecmp(exe, "mpv") == 0 || tt_strcasecmp(exe, "mpv.exe") == 0) {
        found = win32_scan_winget_packages(localappdata, "mpv", "mpv");
        if (found) return found;

        const struct { const char *base; const char *sub; const char *bin; } mpv_dirs[] = {
            {progfiles,    "mpv",             "mpv"},
            {progfiles,    "MPV Player",      "mpv"},
            {localappdata, "Programs\\mpv",   "mpv"},
            {localappdata, "Programs\\mpv.net", "mpvnet.exe"},
            {progfiles,    "mpv.net",         "mpvnet.exe"},
        };
        for (size_t i = 0; i < sizeof(mpv_dirs) / sizeof(mpv_dirs[0]); i++) {
            if (!mpv_dirs[i].base || !mpv_dirs[i].base[0]) continue;
            char *dir = win32_path_join_bs(mpv_dirs[i].base, mpv_dirs[i].sub);
            if (!dir) continue;
            found = win32_try_in_dir(dir, mpv_dirs[i].bin);
            free(dir);
            if (found) return found;
        }
        /* mpv.net installed via winget / scoop / PATH */
        found = win32_search_path(NULL, "mpvnet");
        if (found) return found;
        found = win32_scan_winget_packages(localappdata, "mpv", "mpvnet");
        if (found) return found;
    } else if (tt_strcasecmp(exe, "yt-dlp") == 0 || tt_strcasecmp(exe, "yt-dlp.exe") == 0) {
        found = win32_scan_winget_packages(localappdata, "yt-dlp", "yt-dlp");
        if (found) return found;
    } else if (tt_strcasecmp(exe, "vlc") == 0 || tt_strcasecmp(exe, "vlc.exe") == 0) {
        if (progfiles && progfiles[0]) {
            char *dir = win32_path_join_bs(progfiles, "VideoLAN\\VLC");
            if (dir) {
                found = win32_try_in_dir(dir, "vlc.exe");
                free(dir);
                if (found) return found;
            }
        }
        if (progfiles_x86 && progfiles_x86[0]) {
            char *dir = win32_path_join_bs(progfiles_x86, "VideoLAN\\VLC");
            if (dir) {
                found = win32_try_in_dir(dir, "vlc.exe");
                free(dir);
                if (found) return found;
            }
        }
    }

    return NULL;
}
#endif

bool tt_spawn_capture_stdout(const char *const *argv, size_t max_bytes, StrBuf *out_stdout) {
    if (!argv || !argv[0] || !out_stdout) return false;
    sb_clear(out_stdout);

#ifdef _WIN32
    char *resolved_exe = tt_win32_resolve_exe(argv[0]);
    char *cmdline = tt_win32_build_cmdline(argv);
    if (!cmdline) {
        free(resolved_exe);
        return false;
    }
    wchar_t *wapp = (resolved_exe && tt_file_exists(resolved_exe))
                        ? tt_utf8_to_wide(resolved_exe)
                        : NULL;
    wchar_t *wcmd = tt_utf8_to_wide(cmdline);
    free(cmdline);
    free(resolved_exe);
    if (!wcmd) {
        free(wapp);
        return false;
    }

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE h_read = NULL, h_write = NULL;
    if (!CreatePipe(&h_read, &h_write, &sa, 0)) {
        free(wapp);
        free(wcmd);
        return false;
    }
    SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

    HANDLE h_nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h_write;
    si.hStdInput = (h_nul != INVALID_HANDLE_VALUE) ? h_nul : NULL;
    si.hStdError = (h_nul != INVALID_HANDLE_VALUE) ? h_nul : NULL;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessW(wapp, wcmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             NULL, NULL, &si, &pi);
    free(wapp);
    free(wcmd);
    CloseHandle(h_write);
    if (h_nul != INVALID_HANDLE_VALUE) CloseHandle(h_nul);
    if (!ok) {
        CloseHandle(h_read);
        return false;
    }
    CloseHandle(pi.hThread);

    char buf[8192];
    DWORD nread = 0;
    while (ReadFile(h_read, buf, (DWORD)sizeof(buf), &nread, NULL) && nread > 0) {
        if (max_bytes > 0 && out_stdout->len + (size_t)nread > max_bytes) {
            size_t rem = max_bytes > out_stdout->len ? max_bytes - out_stdout->len : 0;
            if (rem > 0) sb_append_n(out_stdout, buf, rem);
            break;
        }
        sb_append_n(out_stdout, buf, (size_t)nread);
    }
    CloseHandle(h_read);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    return exit_code == 0;
#else
    int pfd[2];
    if (pipe(pfd) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return false;
    }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    close(pfd[1]);
    char buf[8192];
    ssize_t n;
    while ((n = read(pfd[0], buf, sizeof(buf))) > 0) {
        if (max_bytes > 0 && out_stdout->len + (size_t)n > max_bytes) {
            size_t rem = max_bytes > out_stdout->len ? max_bytes - out_stdout->len : 0;
            if (rem > 0) sb_append_n(out_stdout, buf, rem);
            break;
        }
        sb_append_n(out_stdout, buf, (size_t)n);
    }
    close(pfd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
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
                if (!home || !home[0]) home = getenv("USERPROFILE");
                if (!home || !home[0]) home = getenv("TEMP");
                if (!home || !home[0]) home = "/tmp";
                int n = snprintf(out_dir, out_cap, "%s", home);
                if (n < 0 || (size_t)n >= out_cap) {
                    out_dir[0] = '\0';
                    return false;
                }
            } else if (vlen >= 2 && val_start[0] == '~' &&
                       (val_start[1] == '/' || val_start[1] == '\\')) {
                const char *home = getenv("HOME");
                if (!home || !home[0]) home = getenv("USERPROFILE");
                if (!home || !home[0]) home = getenv("TEMP");
                if (!home || !home[0]) home = "/tmp";
                int n = snprintf(out_dir, out_cap, "%s%c%.*s", home, val_start[1],
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
