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

static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
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
