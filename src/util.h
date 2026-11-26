#ifndef TUITUBE_UTIL_H
#define TUITUBE_UTIL_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#include <basetsd.h>
#include <wchar.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#else
#include <sys/types.h>
#endif

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

bool tt_mkdir_p(const char *path, unsigned int mode);
bool tt_file_exists(const char *path);
char *tt_read_file(const char *path, size_t *out_len);
bool tt_write_file_atomic(const char *path, const void *data, size_t len);
bool tt_is_abs_path(const char *path);
char *tt_path_join(const char *base, const char *rel);
void tt_sanitize_filename(const char *key, char *out, size_t out_cap);

#ifdef _WIN32
wchar_t *tt_utf8_to_wide(const char *utf8);
char *tt_wide_to_utf8(const wchar_t *wide);
#endif

bool tt_win32_quote_arg(const char *arg, StrBuf *out);
char *tt_win32_build_cmdline(const char *const *argv);
bool tt_ttf_read_family_name(const uint8_t *data, size_t len, char *out, size_t out_cap);
bool tt_spawn_capture_stdout(const char *const *argv, size_t max_bytes, StrBuf *out_stdout);

bool tt_resolve_paths(const char *cli_config_dir,
                      char *out_config_dir, size_t cfg_cap,
                      char *out_cache_dir, size_t cache_cap,
                      bool *out_portable);

#define TT_SHM_ALIGN_BYTES (256u * 1024u)
#define TT_SHM_SHRINK_THRESHOLD_BYTES (512u * 1024u)

bool tt_shm_target_dims(int logical_w, int logical_h, uint32_t scale_120,
                        int int_scale, int *out_pw, int *out_ph);
size_t tt_shm_pool_capacity(size_t needed_bytes);
bool tt_shm_should_shrink(size_t needed_bytes, size_t current_cap);

typedef enum {
    TT_MPV_INPUT_NONE = 0,
    TT_MPV_INPUT_KEY,
    TT_MPV_INPUT_BUTTON,
    TT_MPV_INPUT_WHEEL
} TtMpvInputKind;

typedef struct {
    TtMpvInputKind kind;
    bool pressed;
    bool repeat;
    bool double_click;
    bool is_press_pulse;
    uint32_t mods;       /* TT_MOD_CTRL / SHIFT / ALT / SUPER bitmask (1,2,4,8) */
    uint32_t keysym;     /* xkb_keysym_t */
    char utf8[64];
    int mouse_btn;       /* 1=left, 2=right, 3=middle, 4=back, 5=forward */
    double wheel_dx;
    double wheel_dy;
} TtMpvKeyEvent;

bool tt_mpv_osd_dims(int64_t osd_w, int64_t osd_h, double hidpi_scale,
                     int *out_phys_w, int *out_phys_h,
                     int *out_logical_w, int *out_logical_h);
bool tt_mpv_parse_script_opt_config_dir(const char *script_opts,
                                        char *out_dir, size_t out_cap);
bool tt_mpv_parse_key_event(const char *state_str, const char *key_name,
                            const char *key_text, const char *scale_str,
                            TtMpvKeyEvent *out);

#endif
