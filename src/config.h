#ifndef TUITUBE_CONFIG_H
#define TUITUBE_CONFIG_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} ColorRGBA;

typedef struct {
    ColorRGBA fg;
    ColorRGBA bg;
} ThemePair;

typedef struct {
    ThemePair base;
    ThemePair text_normal;
    ThemePair text_hover;
    ThemePair text_pressed;
    ThemePair text_selected;
    ThemePair elem_normal;
    ThemePair elem_hover;
    ThemePair elem_pressed;
    ThemePair elem_selected;
    ThemePair scrollbar_normal;
    ThemePair scrollbar_hover;
    ThemePair scrollbar_pressed;
    ThemePair scrollbar_selected;
} Theme;

typedef enum {
    ACT_NONE = 0,
    ACT_QUIT,
    ACT_PLAY_VIDEO,
    ACT_VIEW_USER_VIDEOS,
    ACT_VIEW_RECOMMENDED_VIDEOS,
    ACT_NEXT_PAGE,
    ACT_COPY_VIDEO_URL,
    ACT_COPY_VIDEO_TITLE,
    ACT_COPY_VIDEO_DESCRIPTION,
    ACT_SHOW_FAVORITES,
    ACT_SHOW_FEED,
    ACT_SHOW_HISTORY,
    ACT_ADD_FAVORITES,
    ACT_SET_DARK_THEME,
    ACT_SET_LIGHT_THEME,
    ACT_SET_YUUKA_THEME,
    ACT_SET_CUSTOM_THEME,
    ACT_CLEAR_THUMBNAIL_CACHE,
    ACT_INC_LIST_SIZE,
    ACT_DEC_LIST_SIZE,
    ACT_FOCUS_SEARCH_INPUT,
    ACT_FOCUS_PLAYLIST,
    ACT_NAV_BACK,
    ACT_TOGGLE_HELP
} KeyAction;

enum {
    TT_MOD_CTRL  = 1u << 0,
    TT_MOD_SHIFT = 1u << 1,
    TT_MOD_ALT   = 1u << 2,
    TT_MOD_SUPER = 1u << 3
};

typedef struct {
    KeyAction action;
    uint32_t mods;
    xkb_keysym_t keysym;
} KeyBinding;

typedef struct {
    double scale;
    char *font;
    int font_size;
    int thumb_size;
    int playlist_width;
    char *player_path;
    char *video_format;
    char *sub_language;
    char *theme_name;
    char *theme_path;
    char *mascot;
    bool single_click_select;
    bool show_extra_metadata;
    bool infinite_scroll;
    bool save_history;
    bool dim_watched_videos;
    bool feed_use_rss;
    int max_feed_videos_per_channel;
    char *max_feed_age;
    int64_t max_feed_age_sec;
    bool allow_hdr;
    bool all_codecs;

    KeyBinding *key_bindings;
    size_t key_binding_count;

    char config_dir[PATH_MAX];
    char cache_dir[PATH_MAX];
    bool portable_mode;
    Theme theme;
} TuiTubeConfig;

void theme_init_builtin(Theme *t, const char *name);
bool theme_load_file(Theme *t, const char *path);
double theme_contrast_ratio(ColorRGBA fg, ColorRGBA bg);

KeyAction config_action_from_string(const char *s);
bool config_mods_from_string(const char *s, uint32_t *out_mods);
bool config_key_from_string(const char *s, xkb_keysym_t *out_sym);
KeyAction config_match_keybinding(const TuiTubeConfig *cfg, uint32_t mods, xkb_keysym_t sym);
bool config_format_action_shortcut(const TuiTubeConfig *cfg, KeyAction act, char *out, size_t out_cap);

void config_init_defaults(TuiTubeConfig *cfg);
bool config_load(TuiTubeConfig *cfg, const char *cli_config_dir);
bool config_apply_theme(TuiTubeConfig *cfg, const char *theme_name);
double config_effective_ui_scale(const TuiTubeConfig *cfg);
char *config_resolve_asset_path(const TuiTubeConfig *cfg, const char *rel_or_abs);
void config_free(TuiTubeConfig *cfg);

#endif
