#ifndef TUITUBE_PLAYER_H
#define TUITUBE_PLAYER_H

#include "config.h"
#include "model.h"

#include <stdbool.h>
#include <stddef.h>

void player_install_sigchld_handler(void);

bool player_map_video_format(const char *video_format, bool allow_hdr, bool all_codecs,
                             char *out, size_t out_cap);

bool player_launch(const TuiTubeConfig *cfg, const MediaItem *video,
                   char *err_buf, size_t err_cap);

bool player_copy_wl_copy(const char *text);

#endif
