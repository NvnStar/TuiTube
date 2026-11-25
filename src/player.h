#ifndef TUITUBE_PLAYER_H
#define TUITUBE_PLAYER_H

#include "config.h"
#include "model.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool ok;
    int exit_code;
    char video_id[64];
    char title[128];
    char saved_path[PATH_MAX];
} DownloadEvent;

void player_install_sigchld_handler(void);

bool player_map_video_format(const char *video_format, bool allow_hdr, bool all_codecs,
                             char *out, size_t out_cap);

bool player_launch(const TuiTubeConfig *cfg, const MediaItem *video,
                   char *err_buf, size_t err_cap);

bool player_download_start(const TuiTubeConfig *cfg, const MediaItem *video,
                           char *out_dir_buf, size_t out_dir_cap,
                           char *err_buf, size_t err_cap);
bool player_is_downloading(const char *video_id);
bool player_get_download_progress(const char *video_id, double *out_progress);
size_t player_get_active_download_summary(double *out_progress, char *out_title, size_t title_cap);
bool player_poll_download_progress(void);
bool player_poll_download_event(DownloadEvent *out_ev);
bool player_parse_ytdlp_progress_line(const char *line, double *out_progress);

bool player_copy_wl_copy(const char *text);

#endif
