#include "player.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ACTIVE_DOWNLOADS 8

typedef struct {
    volatile sig_atomic_t active;
    volatile sig_atomic_t done;
    volatile sig_atomic_t pid;
    volatile sig_atomic_t exit_status;
    char video_id[64];
    char title[128];
    char dir[PATH_MAX];
    char file_path[PATH_MAX];
    int out_fd;
    double progress;
    char line_buf[1024];
    size_t line_len;
} DownloadSlot;

static DownloadSlot g_dl_slots[MAX_ACTIVE_DOWNLOADS] = {
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
    {.out_fd = -1, .progress = -1.0}, {.out_fd = -1, .progress = -1.0},
};

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

void player_install_sigchld_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
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
    const char *base = strrchr(player_path, '/');
    base = base ? base + 1 : player_path;
    return tt_strcasecmp(base, "vlc") == 0 || tt_strcasecmp(base, "vlc.exe") == 0;
}

static bool make_cloexec_pipe(int fds[2]) {
    if (pipe(fds) != 0) return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
}

bool player_launch(const TuiTubeConfig *cfg, const MediaItem *video,
                   char *err_buf, size_t err_cap) {
    if (!cfg || !video || !video->id[0]) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "No video selected");
        return false;
    }

    const char *player = (cfg->player_path && cfg->player_path[0]) ? cfg->player_path : "mpv";
    char url[128];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video->id);

    const char *title = (video->title && video->title[0]) ? video->title : video->id;

    int err_pipe[2];
    if (!make_cloexec_pipe(err_pipe)) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create pipe");
        return false;
    }

    char ytdl_fmt[512];
    player_map_video_format(cfg->video_format, cfg->allow_hdr, cfg->all_codecs,
                            ytdl_fmt, sizeof(ytdl_fmt));

    char *arg_fmt = tt_asprintf("--ytdl-format=%s", ytdl_fmt);
    char *arg_title_mpv = tt_asprintf("--force-media-title=%s", title);
    char *arg_title_vlc = tt_asprintf("--meta-title=%s", title);
    char *arg_slang = (cfg->sub_language && cfg->sub_language[0])
                          ? tt_asprintf("--slang=%s", cfg->sub_language)
                          : NULL;

    pid_t pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(err_pipe[0]);
        close(err_pipe[1]);
        free(arg_fmt);
        free(arg_title_mpv);
        free(arg_title_vlc);
        free(arg_slang);
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
            char *argv_mpv[8];
            int idx = 0;
            argv_mpv[idx++] = (char *)player;
            argv_mpv[idx++] = "--ytdl=yes";
            argv_mpv[idx++] = arg_fmt;
            argv_mpv[idx++] = arg_title_mpv;
            if (arg_slang) argv_mpv[idx++] = arg_slang;
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

static bool drain_slot_stdout(DownloadSlot *slot) {
    if (!slot || slot->out_fd < 0) return false;
    bool changed = false;
    char buf[512];
    for (;;) {
        ssize_t n = read(slot->out_fd, buf, sizeof(buf));
        if (n > 0) {
            for (ssize_t k = 0; k < n; k++) {
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
        } else if (n == 0) {
            if (slot->line_len > 0) {
                slot->line_buf[slot->line_len] = '\0';
                if (process_download_line(slot, slot->line_buf)) {
                    changed = true;
                }
                slot->line_len = 0;
            }
            close(slot->out_fd);
            slot->out_fd = -1;
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    return changed;
}

bool player_poll_download_progress(void) {
    bool changed = false;
    for (int i = 0; i < MAX_ACTIVE_DOWNLOADS; i++) {
        if (g_dl_slots[i].active == 1 && g_dl_slots[i].out_fd >= 0) {
            if (drain_slot_stdout(&g_dl_slots[i])) {
                changed = true;
            }
        }
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

    const char *title = (video->title && video->title[0]) ? video->title : video->id;
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

    char *dir = config_resolve_download_dir(cfg);
    if (!dir || !tt_mkdir_p(dir, 0755)) {
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create download directory");
        return false;
    }
    if (out_dir_buf && out_dir_cap > 0) {
        snprintf(out_dir_buf, out_dir_cap, "%s", dir);
    }

    int err_pipe[2];
    if (!make_cloexec_pipe(err_pipe)) {
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create pipe");
        return false;
    }

    int out_pipe[2];
    if (!make_cloexec_pipe(out_pipe)) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        free(dir);
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to create output pipe");
        return false;
    }

    char ytdl_fmt[512];
    player_map_video_format(cfg->video_format, cfg->allow_hdr, cfg->all_codecs,
                            ytdl_fmt, sizeof(ytdl_fmt));

    char url[128];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video->id);

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

        char *argv[] = {
            "yt-dlp",
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
        execvp("yt-dlp", argv);

        int err_no = errno;
        ssize_t wr = write(err_pipe[1], &err_no, sizeof(err_no));
        (void)wr;
        _exit(127);
    }

    close(err_pipe[1]);
    close(out_pipe[1]);
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
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Failed to launch yt-dlp");
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
}

bool player_poll_download_event(DownloadEvent *out_ev) {
    if (!out_ev) return false;
    sigset_t blk, oldmask;
    sigemptyset(&blk);
    sigaddset(&blk, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blk, &oldmask);

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
}

bool player_copy_wl_copy(const char *text) {
    if (!text) return false;
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
}
