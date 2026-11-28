#include "player.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define MAX_ACTIVE_DOWNLOADS 8

typedef struct {
#ifdef _WIN32
    int active;
    int done;
    HANDLE proc_handle;
    HANDLE out_read;
    DWORD exit_code;
#else
    volatile sig_atomic_t active;
    volatile sig_atomic_t done;
    volatile sig_atomic_t pid;
    volatile sig_atomic_t exit_status;
    int out_fd;
#endif
    char video_id[64];
    char title[128];
    char dir[PATH_MAX];
    char file_path[PATH_MAX];
    double progress;
    char line_buf[1024];
    size_t line_len;
} DownloadSlot;

#ifdef _WIN32
static DownloadSlot g_dl_slots[MAX_ACTIVE_DOWNLOADS] = {
    {.progress = -1.0}, {.progress = -1.0}, {.progress = -1.0}, {.progress = -1.0},
    {.progress = -1.0}, {.progress = -1.0}, {.progress = -1.0}, {.progress = -1.0},
};
#else
static DownloadSlot g_dl_slots[MAX_ACTIVE_DOWNLOADS] = {
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
};
#endif

#ifndef _WIN32
static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
            if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 0 &&
                g_dl_slots[i].pid == (sig_atomic_t)pid) {
                g_dl_slots[i].exit_status = (sig_atomic_t)status;
                g_dl_slots[i].done = 1;
                break;
            }
        }
    }
    errno = saved_errno;
}
#endif

void player_install_sigchld_handler(void) {
#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
#endif
}

bool player_map_video_format(const char *video_format, bool allow_hdr, bool all_codecs,
                             char *out, size_t out_cap) {
    if (!out || out_cap == 0) return false;
    const char *fmt = (video_format && video_format[0]) ? video_format : "1080p60";

    if (strchr(fmt, '[') || strchr(fmt, '/')) {
        snprintf(out, out_cap, "%s", fmt);
        return true;
    }

    char *end = NULL;
    long h = strtol(fmt, &end, 10);
    if (end == fmt || h <= 0 || h > 8640) {
        h = 1080;
        end = "";
    }

    int fps = 30;
    const char *p = end;
    if (*p == 'p' || *p == 'P') p++;
    if (strncmp(p, "60", 2) == 0) {
        fps = 60;
        p += 2;
    } else if (strncmp(p, "_60", 3) == 0) {
        fps = 60;
        p += 3;
    }

    const char *codec = "avc1";
    if (tt_strcasestr(p, "av1") != NULL) {
        codec = "av01";
    } else if (tt_strcasestr(p, "vp9") != NULL) {
        codec = "vp9";
    }

    const char *hdr_flt = allow_hdr ? "" : "[dynamic_range!=HDR]";
    if (all_codecs) {
        snprintf(out, out_cap,
                 "bestvideo[height<=%ld][fps<=%d]%s+bestaudio/best[height<=%ld]/best",
                 h, fps, hdr_flt, h);
    } else {
        snprintf(out, out_cap,
                 "bestvideo[height<=%ld][fps<=%d]%s[vcodec^=%s]+bestaudio/"
                 "bestvideo[height<=%ld][fps<=%d]%s+bestaudio/"
                 "best[height<=%ld]/best",
                 h, fps, hdr_flt, codec,
                 h, fps, hdr_flt,
                 h);
    }
    return true;
}

static bool is_vlc_path(const char *player_path) {
    if (!player_path) return false;
    const char *slash = strrchr(player_path, '/');
    const char *bslash = strrchr(player_path, '\\');
    const char *sep = slash;
    if (bslash && (!sep || bslash > sep)) sep = bslash;
    const char *base = sep ? sep + 1 : player_path;
    return tt_strcasecmp(base, "vlc") == 0 || tt_strcasecmp(base, "vlc.exe") == 0;
}

#ifdef _WIN32
static bool win32_file_is_gui_exe(const wchar_t *wpath) {
    if (!wpath || !wpath[0]) return false;
    FILE *f = _wfopen(wpath, L"rb");
    if (!f) return false;
    uint8_t buf[1024];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return tt_pe_is_gui_subsystem(buf, n);
}

static bool win32_has_path_sep_char(const char *s) {
    if (!s) return false;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') return true;
    }
    return false;
}

static bool win32_is_in_process_path_env(const char *exe_name) {
    DWORD need = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (need == 0) return false;
    wchar_t *env_path = malloc(((size_t)need + 1u) * sizeof(wchar_t));
    if (!env_path) return false;
    DWORD got = GetEnvironmentVariableW(L"PATH", env_path, need + 1u);
    if (got == 0 || got > need) {
        free(env_path);
        return false;
    }
    env_path[got] = L'\0';
    if (!env_path[0]) {
        free(env_path);
        return false;
    }

    const char *name = (exe_name && exe_name[0]) ? exe_name : "yt-dlp";
    wchar_t *wname = tt_utf8_to_wide(name);
    if (!wname) {
        free(env_path);
        return false;
    }

    static const wchar_t *const exts[] = {L".exe", L".com"};
    bool found = false;
    for (size_t i = 0; i < 2 && !found; i++) {
        DWORD sz = SearchPathW(env_path, wname, exts[i], 0, NULL, NULL);
        if (sz == 0) continue;
        wchar_t *buf = malloc(((size_t)sz + 1u) * sizeof(wchar_t));
        if (!buf) continue;
        DWORD r = SearchPathW(env_path, wname, exts[i], sz + 1u, buf, NULL);
        if (r > 0 && r <= sz) {
            DWORD attr = GetFileAttributesW(buf);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                found = true;
            }
        }
        free(buf);
    }

    free(wname);
    free(env_path);
    return found;
}
#else
static char *posix_resolve_exe(const char *name) {
    if (!name || !name[0]) return NULL;
    if (strchr(name, '/') != NULL) {
        if (tt_dir_exists(name)) {
            char *cand = tt_path_join(name, "yt-dlp");
            if (cand && tt_file_exists(cand) && access(cand, X_OK) == 0) {
                return cand;
            }
            free(cand);
            return NULL;
        }
        if (tt_file_exists(name) && access(name, X_OK) == 0) {
            return tt_strdup(name);
        }
        return NULL;
    }
    const char *path_env = getenv("PATH");
    if (!path_env || !path_env[0]) {
        path_env = "/usr/local/bin:/usr/bin:/bin";
    }
    const char *p = path_env;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t slen = colon ? (size_t)(colon - p) : strlen(p);
        char *seg = (slen > 0) ? tt_strndup(p, slen) : tt_strdup(".");
        if (seg) {
            char *cand = tt_path_join(seg, name);
            free(seg);
            if (cand) {
                if (tt_file_exists(cand) && access(cand, X_OK) == 0) {
                    return cand;
                }
                free(cand);
            }
        }
        if (!colon) break;
        p = colon + 1;
    }
    return NULL;
}

static bool posix_dir_is_in_path_env(const char *dir) {
    if (!dir || !dir[0]) return false;
    const char *path_env = getenv("PATH");
    if (!path_env || !path_env[0]) return false;

    size_t dlen = strlen(dir);
    while (dlen > 1 && dir[dlen - 1] == '/') dlen--;

    const char *p = path_env;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t slen = colon ? (size_t)(colon - p) : strlen(p);
        while (slen > 1 && p[slen - 1] == '/') slen--;
        if (slen == dlen && strncmp(p, dir, dlen) == 0) {
            return true;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return false;
}
#endif

static char *normalize_ytdlp_spec(const char *raw) {
    if (!raw) return tt_strdup("");
    const char *s = raw;
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    while (len > start && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        len--;
    }
    if (len - start >= 2 && s[start] == '"' && s[len - 1] == '"') {
        start++;
        len--;
        while (start < len && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
            start++;
        }
        while (len > start && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
            len--;
        }
    }
    for (size_t i = start; i < len; i++) {
        if (s[i] == '\r' || s[i] == '\n') return NULL;
    }
    return tt_strndup(s + start, len - start);
}

static char *expand_home_spec(const char *spec) {
    if (!spec) return NULL;
    if (strcmp(spec, "~") == 0 ||
        (spec[0] == '~' && (spec[1] == '/' || spec[1] == '\\'))) {
        const char *env_home = getenv("HOME");
#ifdef _WIN32
        if (!env_home || !env_home[0]) env_home = getenv("USERPROFILE");
        if (!env_home || !env_home[0]) env_home = getenv("TEMP");
#endif
        const char *home = (env_home && env_home[0]) ? env_home : "/tmp";
        if (spec[1] == '\0') return tt_strdup(home);
        return tt_path_join(home, spec + 2);
    }
    return tt_strdup(spec);
}

static bool spec_has_path_sep(const char *s) {
    if (!s) return false;
#ifdef _WIN32
    return win32_has_path_sep_char(s);
#else
    return strchr(s, '/') != NULL;
#endif
}

static char *probe_ytdlp_in_dir(const char *dir) {
    if (!dir || !dir[0]) return NULL;
#ifdef _WIN32
    static const char *const bins[] = {"yt-dlp.exe", "yt-dlp.com"};
    for (size_t i = 0; i < 2; i++) {
        char *cand = tt_path_join(dir, bins[i]);
        if (!cand) continue;
        if (tt_file_exists(cand)) {
            char *res = tt_win32_resolve_exe(cand);
            free(cand);
            if (res) return res;
        } else {
            free(cand);
        }
    }
    return NULL;
#else
    char *cand = tt_path_join(dir, "yt-dlp");
    if (cand && tt_file_exists(cand) && access(cand, X_OK) == 0) {
        return cand;
    }
    free(cand);
    return NULL;
#endif
}

static char *probe_ytdlp_candidate(const char *cand) {
    if (!cand || !cand[0]) return NULL;
    if (tt_dir_exists(cand)) {
        return probe_ytdlp_in_dir(cand);
    }
#ifdef _WIN32
    if (win32_has_path_sep_char(cand)) {
        return tt_win32_resolve_exe(cand);
    }
    if (tt_file_exists(cand)) {
        return tt_strdup(cand);
    }
    return NULL;
#else
    if (tt_file_exists(cand) && access(cand, X_OK) == 0) {
        return tt_strdup(cand);
    }
    return NULL;
#endif
}

static char *extract_parent_dir(const char *path) {
    if (!path || !path[0]) return NULL;
    const char *last_sep = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (!last_sep) return NULL;
    if (last_sep == path) return tt_strdup("/");
    size_t dlen = (size_t)(last_sep - path);
#ifdef _WIN32
    if (dlen == 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
        dlen = 3;
    }
#endif
    return tt_strndup(path, dlen);
}

char *player_resolve_ytdlp_path(const TuiTubeConfig *cfg, bool *out_needs_mpv_opt) {
    if (out_needs_mpv_opt) *out_needs_mpv_opt = false;

    char *explicit_spec = normalize_ytdlp_spec(cfg ? cfg->ytdlp_path : NULL);
    if (!explicit_spec) return NULL;

    char exe_dir[PATH_MAX];
    bool have_exe_dir = tt_get_exe_dir(exe_dir, sizeof(exe_dir));

    /* Case A: Explicit ytdlp_path */
    if (explicit_spec[0] != '\0') {
        char *expanded = expand_home_spec(explicit_spec);
        free(explicit_spec);
        if (!expanded) return NULL;

        char *resolved = NULL;
        if (spec_has_path_sep(expanded)) {
            if (tt_is_abs_path(expanded)) {
                resolved = probe_ytdlp_candidate(expanded);
            } else if (cfg && cfg->config_dir[0]) {
                char *cand = config_resolve_asset_path(cfg, expanded);
                resolved = probe_ytdlp_candidate(cand);
                free(cand);
                if (!resolved && !cfg->portable_mode && have_exe_dir) {
                    char *exe_cand = tt_path_join(exe_dir, expanded);
                    resolved = probe_ytdlp_candidate(exe_cand);
                    free(exe_cand);
                }
            } else {
                resolved = probe_ytdlp_candidate(expanded);
                if (!resolved && have_exe_dir) {
                    char *exe_cand = tt_path_join(exe_dir, expanded);
                    resolved = probe_ytdlp_candidate(exe_cand);
                    free(exe_cand);
                }
            }
        } else {
            if (cfg && cfg->config_dir[0]) {
                char *cfg_cand = tt_path_join(cfg->config_dir, expanded);
                resolved = probe_ytdlp_candidate(cfg_cand);
                free(cfg_cand);
            }
            if (!resolved && have_exe_dir) {
                char *exe_cand = tt_path_join(exe_dir, expanded);
                resolved = probe_ytdlp_candidate(exe_cand);
                free(exe_cand);
            }
            if (!resolved) {
#ifdef _WIN32
                resolved = tt_win32_resolve_exe(expanded);
#else
                resolved = posix_resolve_exe(expanded);
#endif
            }
        }
        free(expanded);
        if (resolved) {
            if (out_needs_mpv_opt) *out_needs_mpv_opt = true;
        }
        return resolved;
    }
    free(explicit_spec);

    /* Case B: Default auto-discovery */
    /* 1. <config_dir> drop-in binary */
    if (cfg && cfg->config_dir[0]) {
        char *found = probe_ytdlp_in_dir(cfg->config_dir);
        if (found) {
            if (out_needs_mpv_opt) *out_needs_mpv_opt = true;
            return found;
        }
    }

#ifdef _WIN32
    /* 3. Standard PATH & Windows package-manager discovery (includes <exe_dir>) */
    char *win_found = tt_win32_resolve_exe("yt-dlp");
    if (win_found) {
        if (out_needs_mpv_opt) {
            *out_needs_mpv_opt = !win32_is_in_process_path_env("yt-dlp");
        }
        return win_found;
    }
#else
    /* 2. Standalone <exe_dir> drop-in binary (POSIX non-PATH dir) */
    if (have_exe_dir && !posix_dir_is_in_path_env(exe_dir)) {
        char *found = probe_ytdlp_in_dir(exe_dir);
        if (found) {
            if (out_needs_mpv_opt) *out_needs_mpv_opt = true;
            return found;
        }
    }

    /* 3. Standard PATH */
    char *posix_found = posix_resolve_exe("yt-dlp");
    if (posix_found) {
        if (out_needs_mpv_opt) *out_needs_mpv_opt = false;
        return posix_found;
    }
    if (have_exe_dir) {
        char *found = probe_ytdlp_in_dir(exe_dir);
        if (found) {
            if (out_needs_mpv_opt) *out_needs_mpv_opt = true;
            return found;
        }
    }
#endif

    /* 4. Player sibling directory fallback */
    const char *player = (cfg && cfg->player_path && cfg->player_path[0]) ? cfg->player_path : "mpv";
#ifdef _WIN32
    char *resolved_player = tt_win32_resolve_exe(player);
#else
    char *resolved_player = posix_resolve_exe(player);
#endif
    if (resolved_player) {
        char *player_dir = extract_parent_dir(resolved_player);
        free(resolved_player);
        if (player_dir) {
            char *found = probe_ytdlp_in_dir(player_dir);
            free(player_dir);
            if (found) {
                if (out_needs_mpv_opt) *out_needs_mpv_opt = true;
                return found;
            }
        }
    }

    return NULL;
}

const char *player_video_title_for(const MediaItem *video, bool use_original) {
    if (!video) return NULL;
    const char *title = media_item_display_title_for(video, use_original);
    return (title && title[0]) ? title : video->id;
}

const char *player_video_title(const MediaItem *video) {
    return player_video_title_for(video, true);
}

bool player_build_title_args_for(const MediaItem *video, bool use_original,
                                 char **out_mpv, char **out_vlc) {
    if (out_mpv) *out_mpv = NULL;
    if (out_vlc) *out_vlc = NULL;
    if (!video || !out_mpv || !out_vlc) return false;
    const char *title = player_video_title_for(video, use_original);
    if (!title) return false;
    *out_mpv = tt_asprintf("--force-media-title=%s", title);
    *out_vlc = tt_asprintf("--meta-title=%s", title);
    if (!*out_mpv || !*out_vlc) {
        free(*out_mpv);
        free(*out_vlc);
        *out_mpv = NULL;
        *out_vlc = NULL;
        return false;
    }
    return true;
}

bool player_build_title_args(const MediaItem *video, char **out_mpv, char **out_vlc) {
    return player_build_title_args_for(video, true, out_mpv, out_vlc);
}

bool player_build_mpv_ytdl_script_opt(const TuiTubeConfig *cfg, char *out, size_t out_cap) {
    if (out && out_cap > 0) out[0] = '\0';
    if (!cfg || !out || out_cap == 0) return false;

    char *explicit_spec = normalize_ytdlp_spec(cfg->ytdlp_path);
    if (!explicit_spec) return false;

    bool needs_opt = false;
    char *resolved = player_resolve_ytdlp_path(cfg, &needs_opt);
    const char *target = NULL;
    if (resolved && needs_opt) {
        target = resolved;
    } else if (!resolved && explicit_spec[0] != '\0') {
        target = explicit_spec;
    } else {
        free(resolved);
        free(explicit_spec);
        return false;
    }

#ifdef _WIN32
    const char forbidden_delim = ';';
#else
    const char forbidden_delim = ':';
#endif
    for (const char *p = target; *p; p++) {
        if (*p == '\r' || *p == '\n' || *p == forbidden_delim) {
            free(resolved);
            free(explicit_spec);
            return false;
        }
    }

    int n = snprintf(out, out_cap, "ytdl_hook-ytdl_path=%s", target);
    free(resolved);
    free(explicit_spec);
    if (n <= 0 || (size_t)n >= out_cap) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void player_check_deps(const TuiTubeConfig *cfg, bool *out_has_player, bool *out_has_ytdlp) {
    const char *player = (cfg && cfg->player_path && cfg->player_path[0]) ? cfg->player_path : "mpv";
    if (out_has_player) {
#ifdef _WIN32
        char *resolved_player = tt_win32_resolve_exe(player);
#else
        char *resolved_player = posix_resolve_exe(player);
#endif
        *out_has_player = (resolved_player != NULL);
        free(resolved_player);
    }
    if (out_has_ytdlp) {
        char *ytdlp = player_resolve_ytdlp_path(cfg, NULL);
        *out_has_ytdlp = (ytdlp != NULL);
        free(ytdlp);
    }
}

#ifndef _WIN32
static bool make_cloexec_pipe(int fds[2]) {
    if (pipe(fds) != 0) return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
}
#endif

bool player_launch(const TuiTubeConfig *cfg, const MediaItem *video,
                   char *err_buf, size_t err_cap) {
    if (!cfg || !video || !video->id[0]) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "No video selected");
        return false;
    }

    const char *player = (cfg->player_path && cfg->player_path[0]) ? cfg->player_path : "mpv";
    char url[128];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video->id);

    char ytdl_fmt[512];
    player_map_video_format(cfg->video_format, cfg->allow_hdr, cfg->all_codecs,
                            ytdl_fmt, sizeof(ytdl_fmt));

    char *arg_fmt = tt_asprintf("--ytdl-format=%s", ytdl_fmt);
    char *arg_title_mpv = NULL;
    char *arg_title_vlc = NULL;
    if (!player_build_title_args_for(video, cfg->original_titles,
                                     &arg_title_mpv, &arg_title_vlc)) {
        free(arg_fmt);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Out of memory");
        return false;
    }
    char *arg_slang = (cfg->sub_language && cfg->sub_language[0])
                          ? tt_asprintf("--slang=%s", cfg->sub_language)
                          : NULL;
    char ytdl_script_opt[PATH_MAX + 64];
    char *arg_ytdl_path = player_build_mpv_ytdl_script_opt(cfg, ytdl_script_opt, sizeof(ytdl_script_opt))
                              ? tt_asprintf("--script-opts-append=%s", ytdl_script_opt)
                              : NULL;

#ifdef _WIN32
    char *resolved_player = tt_win32_resolve_exe(player);
    const char *launch_exe = (resolved_player && resolved_player[0]) ? resolved_player : player;
    bool is_vlc = is_vlc_path(launch_exe);
    if (!is_vlc) {
        char *resolved_ytdlp = player_resolve_ytdlp_path(cfg, NULL);
        if (resolved_ytdlp) {
            tt_win32_ensure_dir_in_path(resolved_ytdlp);
            free(resolved_ytdlp);
        }
    }
    char *cmdline = NULL;
    if (is_vlc) {
        const char *argv_vlc[] = {launch_exe, arg_title_vlc, url, NULL};
        cmdline = tt_win32_build_cmdline(argv_vlc);
    } else {
        const char *argv_mpv[12];
        int idx = 0;
        argv_mpv[idx++] = launch_exe;
        argv_mpv[idx++] = "--ytdl=yes";
        argv_mpv[idx++] = "--force-window=immediate";
        argv_mpv[idx++] = arg_fmt;
        argv_mpv[idx++] = arg_title_mpv;
        if (arg_slang) argv_mpv[idx++] = arg_slang;
        if (arg_ytdl_path) argv_mpv[idx++] = arg_ytdl_path;
        argv_mpv[idx++] = url;
        argv_mpv[idx] = NULL;
        cmdline = tt_win32_build_cmdline(argv_mpv);
    }

    free(arg_fmt);
    free(arg_title_mpv);
    free(arg_title_vlc);
    free(arg_slang);
    free(arg_ytdl_path);

    if (!cmdline) {
        free(resolved_player);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Out of memory");
        return false;
    }

    wchar_t *wapp = (resolved_player && tt_file_exists(resolved_player))
                        ? tt_utf8_to_wide(resolved_player)
                        : NULL;
    wchar_t *wcmd = tt_utf8_to_wide(cmdline);
    free(cmdline);
    free(resolved_player);
    if (!wcmd) {
        free(wapp);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Out of memory");
        return false;
    }

    wchar_t *wcwd = NULL;
    if (wapp) {
        const wchar_t *last_sep = NULL;
        for (const wchar_t *p = wapp; *p; p++) {
            if (*p == L'\\' || *p == L'/') last_sep = p;
        }
        if (last_sep && last_sep > wapp) {
            size_t dlen = (size_t)(last_sep - wapp);
            if (dlen == 2 && wapp[1] == L':') dlen = 3;
            wcwd = malloc((dlen + 1u) * sizeof(wchar_t));
            if (wcwd) {
                memcpy(wcwd, wapp, dlen * sizeof(wchar_t));
                wcwd[dlen] = L'\0';
            }
        }
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    /* Omit CREATE_NO_WINDOW for GUI subsystem binaries (e.g., mpv.exe); retain it
       for console wrappers/shims so no stray console window flashes. */
    DWORD create_flags = CREATE_UNICODE_ENVIRONMENT |
                         ((wapp && win32_file_is_gui_exe(wapp)) ? 0 : CREATE_NO_WINDOW);
    BOOL ok = CreateProcessW(wapp, wcmd, NULL, NULL, FALSE,
                             create_flags,
                             NULL, wcwd, &si, &pi);
    DWORD last_err = ok ? 0 : GetLastError();
    free(wcwd);
    free(wapp);
    free(wcmd);
    if (!ok) {
        if (err_buf && err_cap) {
            if (last_err == ERROR_FILE_NOT_FOUND || last_err == ERROR_PATH_NOT_FOUND) {
                snprintf(err_buf, err_cap, "Failed to launch player: %s (not found in PATH)", player);
            } else {
                snprintf(err_buf, err_cap, "Failed to launch player: %s (error %lu)",
                         player, (unsigned long)last_err);
            }
        }
        return false;
    }

    if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
        DWORD ec = 0;
        if (GetExitCodeProcess(pi.hProcess, &ec) && ec >= 0xC0000000u) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            if (err_buf && err_cap) {
                snprintf(err_buf, err_cap, "Failed to start player: %s (error 0x%08lX)",
                         player, (unsigned long)ec);
            }
            return false;
        }
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    int err_pipe[2];
    if (!make_cloexec_pipe(err_pipe)) {
        free(arg_fmt);
        free(arg_title_mpv);
        free(arg_title_vlc);
        free(arg_slang);
        free(arg_ytdl_path);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create pipe");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(err_pipe[0]);
        close(err_pipe[1]);
        free(arg_fmt);
        free(arg_title_mpv);
        free(arg_title_vlc);
        free(arg_slang);
        free(arg_ytdl_path);
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "Failed to launch player: %s", strerror(saved));
        }
        return false;
    }

    if (pid == 0) {
        setsid();
        close(err_pipe[0]);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        if (is_vlc_path(player)) {
            char *argv_vlc[] = {
                (char *)player,
                arg_title_vlc,
                url,
                NULL
            };
            execvp(player, argv_vlc);
        } else {
            char *argv_mpv[12];
            int idx = 0;
            argv_mpv[idx++] = (char *)player;
            argv_mpv[idx++] = "--ytdl=yes";
            argv_mpv[idx++] = "--force-window=immediate";
            argv_mpv[idx++] = arg_fmt;
            argv_mpv[idx++] = arg_title_mpv;
            if (arg_slang) argv_mpv[idx++] = arg_slang;
            if (arg_ytdl_path) argv_mpv[idx++] = arg_ytdl_path;
            argv_mpv[idx++] = url;
            argv_mpv[idx] = NULL;
            execvp(player, argv_mpv);
        }

        int err_no = errno;
        ssize_t wr = write(err_pipe[1], &err_no, sizeof(err_no));
        (void)wr;
        _exit(127);
    }

    close(err_pipe[1]);
    free(arg_fmt);
    free(arg_title_mpv);
    free(arg_title_vlc);
    free(arg_slang);
    free(arg_ytdl_path);

    int child_errno = 0;
    ssize_t n = read(err_pipe[0], &child_errno, sizeof(child_errno));
    close(err_pipe[0]);

    if (n > 0) {
        waitpid(pid, NULL, 0);
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "Failed to launch player: %s", player);
        }
        return false;
    }

    return true;
#endif
}

bool player_is_downloading(const char *video_id) {
    if (!video_id || !video_id[0]) return false;
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 0 &&
            strcmp(g_dl_slots[i].video_id, video_id) == 0) {
            return true;
        }
    }
    return false;
}

bool player_get_download_progress(const char *video_id, double *out_progress) {
    if (!video_id || !video_id[0]) return false;
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 0 &&
            strcmp(g_dl_slots[i].video_id, video_id) == 0) {
            if (out_progress) *out_progress = g_dl_slots[i].progress;
            return true;
        }
    }
    return false;
}

size_t player_get_active_download_summary(double *out_progress, char *out_title, size_t title_cap) {
    size_t count = 0;
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 0) {
            if (count == 0) {
                if (out_progress) *out_progress = g_dl_slots[i].progress;
                if (out_title && title_cap > 0) {
                    snprintf(out_title, title_cap, "%s", g_dl_slots[i].title);
                }
            }
            count++;
        }
    }
    return count;
}

static bool parse_nonneg_ll(const char *s, long long *out) {
    if (!s) return false;
    while (*s == ' ') s++;
    if (!isdigit((unsigned char)*s)) return false;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (end == s || v < 0) return false;
    *out = v;
    return true;
}

bool player_parse_ytdlp_progress_line(const char *line, double *out_progress) {
    if (!line || !out_progress || strncmp(line, "TT_DL:", 6) != 0) return false;
    const char *p = line + 6;
    const char *c1 = strchr(p, ':');
    const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
    const char *c3 = c2 ? strchr(c2 + 1, ':') : NULL;

    double val = -1.0;
    if (c1 && c2 && c3) {
        char f1[32] = {0}, f2[32] = {0}, f3[32] = {0};
        size_t l1 = (size_t)(c1 - p); if (l1 >= sizeof(f1)) l1 = sizeof(f1) - 1;
        size_t l2 = (size_t)(c2 - (c1 + 1)); if (l2 >= sizeof(f2)) l2 = sizeof(f2) - 1;
        size_t l3 = (size_t)(c3 - (c2 + 1)); if (l3 >= sizeof(f3)) l3 = sizeof(f3) - 1;
        memcpy(f1, p, l1);
        memcpy(f2, c1 + 1, l2);
        memcpy(f3, c2 + 1, l3);

        long long dl = -1, tot = -1, est = -1;
        if (parse_nonneg_ll(f1, &dl)) {
            if (parse_nonneg_ll(f2, &tot) && tot > 0) {
                val = (double)dl / (double)tot;
            } else if (parse_nonneg_ll(f3, &est) && est > 0) {
                val = (double)dl / (double)est;
            }
        }
        p = c3 + 1;
    }

    if (val < 0.0) {
        while (*p && !isdigit((unsigned char)*p) && *p != '.') p++;
        if (isdigit((unsigned char)*p) || *p == '.') {
            char *end = NULL;
            double pct = strtod(p, &end);
            if (end != p) {
                val = pct / 100.0;
            }
        }
    }

    if (val < 0.0) return false;
    if (val > 1.0) val = 1.0;
    *out_progress = val;
    return true;
}

static bool process_download_line(DownloadSlot *slot, const char *line) {
    if (!line || !line[0]) return false;
    const char *dl_tag = strstr(line, "TT_DL:");
    if (dl_tag) {
        double p = -1.0;
        if (player_parse_ytdlp_progress_line(dl_tag, &p)) {
            double diff = p - slot->progress;
            if (diff < 0.0) diff = -diff;
            if (slot->progress < 0.0 || diff >= 0.005 || p == 1.0) {
                slot->progress = p;
                return true;
            }
        }
        return false;
    }
    const char *file_tag = strstr(line, "TT_FILE:");
    if (file_tag) {
        const char *fp = file_tag + 8;
        while (*fp == ' ') fp++;
        if (fp[0]) {
            snprintf(slot->file_path, sizeof(slot->file_path), "%s", fp);
            size_t fl = strlen(slot->file_path);
            while (fl > 0 && (slot->file_path[fl - 1] == '\r' ||
                              slot->file_path[fl - 1] == '\n' ||
                              slot->file_path[fl - 1] == ' ')) {
                slot->file_path[--fl] = '\0';
            }
        }
    }
    return false;
}

static bool feed_slot_bytes(DownloadSlot *slot, const char *buf, size_t n) {
    bool changed = false;
    for (size_t k = 0; k < n; k++) {
        char c = buf[k];
        if (c == '\n' || c == '\r') {
            slot->line_buf[slot->line_len] = '\0';
            if (process_download_line(slot, slot->line_buf)) {
                changed = true;
            }
            slot->line_len = 0;
        } else if (slot->line_len + 1 < sizeof(slot->line_buf)) {
            slot->line_buf[slot->line_len++] = c;
        }
    }
    return changed;
}

static bool flush_slot_line(DownloadSlot *slot) {
    if (slot->line_len == 0) return false;
    slot->line_buf[slot->line_len] = '\0';
    bool changed = process_download_line(slot, slot->line_buf);
    slot->line_len = 0;
    return changed;
}

static bool drain_slot_stdout(DownloadSlot *slot) {
#ifdef _WIN32
    if (!slot || slot->out_read == NULL) return false;
    bool changed = false;
    char buf[512];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(slot->out_read, NULL, 0, NULL, &avail, NULL)) {
            if (flush_slot_line(slot)) changed = true;
            CloseHandle(slot->out_read);
            slot->out_read = NULL;
            break;
        }
        if (avail == 0) {
            break;
        }
        DWORD to_read = avail < (DWORD)sizeof(buf) ? avail : (DWORD)sizeof(buf);
        DWORD nread = 0;
        if (!ReadFile(slot->out_read, buf, to_read, &nread, NULL) || nread == 0) {
            if (flush_slot_line(slot)) changed = true;
            CloseHandle(slot->out_read);
            slot->out_read = NULL;
            break;
        }
        if (feed_slot_bytes(slot, buf, (size_t)nread)) {
            changed = true;
        }
    }
    return changed;
#else
    if (!slot || slot->out_fd < 0) return false;
    bool changed = false;
    char buf[512];
    for (;;) {
        ssize_t n = read(slot->out_fd, buf, sizeof(buf));
        if (n > 0) {
            if (feed_slot_bytes(slot, buf, (size_t)n)) {
                changed = true;
            }
        } else if (n == 0) {
            if (flush_slot_line(slot)) changed = true;
            close(slot->out_fd);
            slot->out_fd = -1;
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    return changed;
#endif
}

bool player_poll_download_progress(void) {
    bool changed = false;
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
#ifdef _WIN32
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].out_read != NULL) {
            if (drain_slot_stdout(&g_dl_slots[i])) {
                changed = true;
            }
        }
#else
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].out_fd >= 0) {
            if (drain_slot_stdout(&g_dl_slots[i])) {
                changed = true;
            }
        }
#endif
    }
    return changed;
}

bool player_download_start(const TuiTubeConfig *cfg, const MediaItem *video,
                           char *out_dir_buf, size_t out_dir_cap,
                           char *err_buf, size_t err_cap) {
    if (out_dir_buf && out_dir_cap > 0) out_dir_buf[0] = '\0';
    if (!cfg || !video || video->kind != ITEM_VIDEO || !video->id[0]) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "No video selected");
        return false;
    }

    const char *title = player_video_title_for(video, cfg->original_titles);
    if (player_is_downloading(video->id)) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Already downloading: %s", title);
        return false;
    }

    int slot = -1;
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "Too many active downloads (max %d)", MAX_ACTIVE_DOWNLOADS);
        }
        return false;
    }

    char *explicit_spec = normalize_ytdlp_spec(cfg->ytdlp_path);
    char *resolved_ytdlp = player_resolve_ytdlp_path(cfg, NULL);
    if (!resolved_ytdlp && (!explicit_spec || explicit_spec[0] != '\0')) {
        if (err_buf && err_cap) {
            const char *display_spec = (explicit_spec && explicit_spec[0])
                                           ? explicit_spec
                                           : (cfg->ytdlp_path ? cfg->ytdlp_path : "");
#ifndef _WIN32
            char *expanded = explicit_spec ? expand_home_spec(explicit_spec) : NULL;
            const char *check_path = expanded;
            char *rel_cand = NULL;
            if (expanded && !tt_is_abs_path(expanded) && cfg->config_dir[0]) {
                rel_cand = tt_path_join(cfg->config_dir, expanded);
                if (rel_cand && tt_file_exists(rel_cand)) check_path = rel_cand;
            }
            if (check_path && tt_file_exists(check_path) && access(check_path, X_OK) != 0) {
                snprintf(err_buf, err_cap,
                         "Failed to launch yt-dlp: '%s' is not executable (run chmod +x)",
                         display_spec);
            } else {
                snprintf(err_buf, err_cap,
                         "Failed to launch yt-dlp: ytdlp_path '%s' not found",
                         display_spec);
            }
            free(rel_cand);
            free(expanded);
#else
            snprintf(err_buf, err_cap,
                     "Failed to launch yt-dlp: ytdlp_path '%s' not found",
                     display_spec);
#endif
        }
        free(explicit_spec);
        return false;
    }
    free(explicit_spec);

    char *dir = config_resolve_download_dir(cfg);
    if (!dir || !tt_mkdir_p(dir, 0755)) {
        free(resolved_ytdlp);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create download directory");
        return false;
    }
    if (out_dir_buf && out_dir_cap > 0) {
        snprintf(out_dir_buf, out_dir_cap, "%s", dir);
    }

    char ytdl_fmt[512];
    player_map_video_format(cfg->video_format, cfg->allow_hdr, cfg->all_codecs,
                            ytdl_fmt, sizeof(ytdl_fmt));

    char url[128];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video->id);

#ifdef _WIN32
    if (resolved_ytdlp) {
        tt_win32_ensure_dir_in_path(resolved_ytdlp);
    }
    const char *ytdlp_exe = (resolved_ytdlp && resolved_ytdlp[0]) ? resolved_ytdlp : "yt-dlp";
    const char *argv[] = {
        ytdlp_exe,
        "--no-playlist",
        "--no-simulate",
        "--progress",
        "--newline",
        "--progress-template",
        "download:TT_DL:%(progress.downloaded_bytes)s:%(progress.total_bytes)s:%(progress.total_bytes_estimate)s:%(progress._percent_str)s",
        "--print",
        "after_move:TT_FILE:%(filepath)s",
        "-f", ytdl_fmt,
        "-P", dir,
        "-o", "%(title).200B [%(id)s].%(ext)s",
        "--",
        url,
        NULL
    };
    char *cmdline = tt_win32_build_cmdline(argv);
    if (!cmdline) {
        free(resolved_ytdlp);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Out of memory");
        return false;
    }
    wchar_t *wapp = (resolved_ytdlp && tt_file_exists(resolved_ytdlp))
                        ? tt_utf8_to_wide(resolved_ytdlp)
                        : NULL;
    wchar_t *wcmd = tt_utf8_to_wide(cmdline);
    free(cmdline);
    free(resolved_ytdlp);
    if (!wcmd) {
        free(wapp);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Out of memory");
        return false;
    }

    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE out_read = NULL, out_write = NULL;
    if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
        free(wapp);
        free(wcmd);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create output pipe");
        return false;
    }
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

    HANDLE h_nul = CreateFileW(L"NUL", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_write;
    si.hStdError = out_write;
    si.hStdInput = (h_nul != INVALID_HANDLE_VALUE) ? h_nul : NULL;

    BOOL ok = CreateProcessW(wapp, wcmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             NULL, NULL, &si, &pi);
    DWORD last_err = ok ? 0 : GetLastError();
    free(wapp);
    free(wcmd);
    CloseHandle(out_write);
    if (h_nul != INVALID_HANDLE_VALUE) CloseHandle(h_nul);
    if (!ok) {
        CloseHandle(out_read);
        free(dir);
        if (err_buf && err_cap) {
            if (last_err == ERROR_FILE_NOT_FOUND || last_err == ERROR_PATH_NOT_FOUND) {
                snprintf(err_buf, err_cap,
                         "Failed to launch yt-dlp (install yt-dlp or set ytdlp_path in tuitube.json)");
            } else {
                snprintf(err_buf, err_cap, "Failed to launch yt-dlp (error %lu)",
                         (unsigned long)last_err);
            }
        }
        return false;
    }
    CloseHandle(pi.hThread);

    g_dl_slots[slot].proc_handle = pi.hProcess;
    g_dl_slots[slot].exit_code = 0;
    g_dl_slots[slot].done = 0;
    snprintf(g_dl_slots[slot].video_id, sizeof(g_dl_slots[slot].video_id), "%s", video->id);
    snprintf(g_dl_slots[slot].title, sizeof(g_dl_slots[slot].title), "%s", title);
    snprintf(g_dl_slots[slot].dir, sizeof(g_dl_slots[slot].dir), "%s", dir);
    g_dl_slots[slot].file_path[0] = '\0';
    g_dl_slots[slot].out_read = out_read;
    g_dl_slots[slot].progress = -1.0;
    g_dl_slots[slot].line_len = 0;
    g_dl_slots[slot].active = 1;

    free(dir);
    return true;
#else
    int err_pipe[2];
    if (!make_cloexec_pipe(err_pipe)) {
        free(resolved_ytdlp);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create pipe");
        return false;
    }

    int out_pipe[2];
    if (!make_cloexec_pipe(out_pipe)) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        free(resolved_ytdlp);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create output pipe");
        return false;
    }

    sigset_t blk, oldmask;
    sigemptyset(&blk);
    sigaddset(&blk, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blk, &oldmask);

    pid_t pid = fork();
    if (pid < 0) {
        int saved = errno;
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        close(err_pipe[0]);
        close(err_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        free(resolved_ytdlp);
        free(dir);
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "Failed to launch yt-dlp: %s", strerror(saved));
        }
        return false;
    }

    if (pid == 0) {
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        setsid();
        close(err_pipe[0]);
        close(out_pipe[0]);

        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDIN_FILENO) close(devnull);
        }

        setenv("PYTHONUNBUFFERED", "1", 1);

        const char *ytdlp_exe = (resolved_ytdlp && resolved_ytdlp[0]) ? resolved_ytdlp : "yt-dlp";
        char *argv[] = {
            (char *)ytdlp_exe,
            "--no-playlist",
            "--no-simulate",
            "--progress",
            "--newline",
            "--progress-template",
            "download:TT_DL:%(progress.downloaded_bytes)s:%(progress.total_bytes)s:%(progress.total_bytes_estimate)s:%(progress._percent_str)s",
            "--print",
            "after_move:TT_FILE:%(filepath)s",
            "-f", ytdl_fmt,
            "-P", dir,
            "-o", "%(title).200B [%(id)s].%(ext)s",
            "--",
            url,
            NULL
        };
        execvp(ytdlp_exe, argv);

        int err_no = errno;
        ssize_t wr = write(err_pipe[1], &err_no, sizeof(err_no));
        (void)wr;
        _exit(127);
    }

    close(err_pipe[1]);
    close(out_pipe[1]);
    free(resolved_ytdlp);
    int child_errno = 0;
    ssize_t n = read(err_pipe[0], &child_errno, sizeof(child_errno));
    close(err_pipe[0]);

    if (n > 0) {
        waitpid(pid, NULL, 0);
        close(out_pipe[0]);
        memset(&g_dl_slots[slot], 0, sizeof(g_dl_slots[slot]));
        g_dl_slots[slot].out_fd = -1;
        g_dl_slots[slot].progress = -1.0;
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        free(dir);
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap,
                     "Failed to launch yt-dlp (install yt-dlp or set ytdlp_path in tuitube.json)");
        }
        return false;
    }

    int fl = fcntl(out_pipe[0], F_GETFL, 0);
    if (fl >= 0) fcntl(out_pipe[0], F_SETFL, fl | O_NONBLOCK);

    g_dl_slots[slot].pid = (sig_atomic_t)pid;
    g_dl_slots[slot].exit_status = 0;
    g_dl_slots[slot].done = 0;
    snprintf(g_dl_slots[slot].video_id, sizeof(g_dl_slots[slot].video_id), "%s", video->id);
    snprintf(g_dl_slots[slot].title, sizeof(g_dl_slots[slot].title), "%s", title);
    snprintf(g_dl_slots[slot].dir, sizeof(g_dl_slots[slot].dir), "%s", dir);
    g_dl_slots[slot].file_path[0] = '\0';
    g_dl_slots[slot].out_fd = out_pipe[0];
    g_dl_slots[slot].progress = -1.0;
    g_dl_slots[slot].line_len = 0;
    g_dl_slots[slot].active = 1;
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    free(dir);
    return true;
#endif
}

bool player_poll_download_event(DownloadEvent *out_ev) {
    if (!out_ev) return false;
#ifdef _WIN32
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 0 &&
            g_dl_slots[i].proc_handle != NULL) {
            if (WaitForSingleObject(g_dl_slots[i].proc_handle, 0) == WAIT_OBJECT_0) {
                DWORD ec = 1;
                GetExitCodeProcess(g_dl_slots[i].proc_handle, &ec);
                CloseHandle(g_dl_slots[i].proc_handle);
                g_dl_slots[i].proc_handle = NULL;
                g_dl_slots[i].exit_code = ec;
                g_dl_slots[i].done = 1;
            }
        }
    }

    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 1) {
            drain_slot_stdout(&g_dl_slots[i]);
            if (g_dl_slots[i].out_read != NULL) {
                CloseHandle(g_dl_slots[i].out_read);
                g_dl_slots[i].out_read = NULL;
            }
            DWORD ec = g_dl_slots[i].exit_code;
            memset(out_ev, 0, sizeof(*out_ev));
            snprintf(out_ev->video_id, sizeof(out_ev->video_id), "%s", g_dl_slots[i].video_id);
            snprintf(out_ev->title, sizeof(out_ev->title), "%s", g_dl_slots[i].title);
            snprintf(out_ev->saved_path, sizeof(out_ev->saved_path), "%s",
                     g_dl_slots[i].file_path[0] ? g_dl_slots[i].file_path : g_dl_slots[i].dir);
            g_dl_slots[i].active = 0;
            g_dl_slots[i].done = 0;
            g_dl_slots[i].proc_handle = NULL;
            g_dl_slots[i].exit_code = 0;
            g_dl_slots[i].dir[0] = '\0';
            g_dl_slots[i].file_path[0] = '\0';
            g_dl_slots[i].progress = -1.0;
            g_dl_slots[i].line_len = 0;

            out_ev->ok = (ec == 0);
            out_ev->exit_code = (int)ec;
            return true;
        }
    }
    return false;
#else
    sigset_t blk, oldmask;
    sigemptyset(&blk);
    sigaddset(&blk, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blk, &oldmask);

    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 0 && g_dl_slots[i].pid > 0) {
            int wstatus = 0;
            pid_t wp = waitpid((pid_t)g_dl_slots[i].pid, &wstatus, WNOHANG);
            if (wp == (pid_t)g_dl_slots[i].pid) {
                g_dl_slots[i].exit_status = (sig_atomic_t)wstatus;
                g_dl_slots[i].done = 1;
            } else if (wp < 0 && errno == ECHILD) {
                g_dl_slots[i].exit_status = 0;
                g_dl_slots[i].done = 1;
            }
        }
    }

    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].done == 1) {
            drain_slot_stdout(&g_dl_slots[i]);
            if (g_dl_slots[i].out_fd >= 0) {
                close(g_dl_slots[i].out_fd);
                g_dl_slots[i].out_fd = -1;
            }
            int st = (int)g_dl_slots[i].exit_status;
            memset(out_ev, 0, sizeof(*out_ev));
            snprintf(out_ev->video_id, sizeof(out_ev->video_id), "%s", g_dl_slots[i].video_id);
            snprintf(out_ev->title, sizeof(out_ev->title), "%s", g_dl_slots[i].title);
            snprintf(out_ev->saved_path, sizeof(out_ev->saved_path), "%s",
                     g_dl_slots[i].file_path[0] ? g_dl_slots[i].file_path : g_dl_slots[i].dir);
            g_dl_slots[i].active = 0;
            g_dl_slots[i].done = 0;
            g_dl_slots[i].pid = 0;
            g_dl_slots[i].exit_status = 0;
            g_dl_slots[i].dir[0] = '\0';
            g_dl_slots[i].file_path[0] = '\0';
            g_dl_slots[i].progress = -1.0;
            g_dl_slots[i].line_len = 0;
            sigprocmask(SIG_SETMASK, &oldmask, NULL);

            out_ev->ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
            out_ev->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            return true;
        }
    }

    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    return false;
#endif
}

bool player_copy_wl_copy(const char *text) {
    if (!text) return false;
#ifdef _WIN32
    wchar_t *w = tt_utf8_to_wide(text);
    if (!w) return false;
    if (!OpenClipboard(NULL)) {
        free(w);
        return false;
    }
    EmptyClipboard();
    size_t bytes = (wcslen(w) + 1u) * sizeof(wchar_t);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hmem) {
        CloseClipboard();
        free(w);
        return false;
    }
    void *dst = GlobalLock(hmem);
    if (!dst) {
        GlobalFree(hmem);
        CloseClipboard();
        free(w);
        return false;
    }
    memcpy(dst, w, bytes);
    GlobalUnlock(hmem);
    free(w);
    if (!SetClipboardData(CF_UNICODETEXT, hmem)) {
        GlobalFree(hmem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
#else
    int pfd[2];
    if (!make_cloexec_pipe(pfd)) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return false;
    }

    if (pid == 0) {
        setsid();
        close(pfd[1]);
        dup2(pfd[0], STDIN_FILENO);
        close(pfd[0]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        char *argv[] = {"wl-copy", NULL};
        execvp("wl-copy", argv);
        _exit(127);
    }

    close(pfd[0]);
    size_t len = strlen(text);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(pfd[1], text + off, len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(pfd[1]);
    return true;
#endif
}
