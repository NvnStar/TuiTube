#include "config.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static ThemePair make_pair(uint8_t fr, uint8_t fg, uint8_t fb, uint8_t fa,
                           uint8_t br, uint8_t bg, uint8_t bb, uint8_t ba) {
    ThemePair p;
    p.fg = (ColorRGBA){fr, fg, fb, fa};
    p.bg = (ColorRGBA){br, bg, bb, ba};
    return p;
}

static double linearize_srgb(double s) {
    return (s <= 0.04045) ? (s / 12.92) : pow((s + 0.055) / 1.055, 2.4);
}

double theme_contrast_ratio(ColorRGBA fg, ColorRGBA bg) {
    double br = bg.r / 255.0;
    double bg_g = bg.g / 255.0;
    double bb = bg.b / 255.0;

    double fa = fg.a / 255.0;
    double fr = (fg.r / 255.0) * fa + br * (1.0 - fa);
    double fg_g = (fg.g / 255.0) * fa + bg_g * (1.0 - fa);
    double fb = (fg.b / 255.0) * fa + bb * (1.0 - fa);

    double l_fg = 0.2126 * linearize_srgb(fr) + 0.7152 * linearize_srgb(fg_g) + 0.0722 * linearize_srgb(fb);
    double l_bg = 0.2126 * linearize_srgb(br) + 0.7152 * linearize_srgb(bg_g) + 0.0722 * linearize_srgb(bb);

    double hi = l_fg > l_bg ? l_fg : l_bg;
    double lo = l_fg > l_bg ? l_bg : l_fg;
    return (hi + 0.05) / (lo + 0.05);
}

void theme_init_builtin(Theme *t, const char *name) {
    if (!t) return;
    if (name && tt_strcasecmp(name, "light") == 0) {
        t->base               = make_pair( 68,  75,  90, 255, 241, 244, 249, 255);
        t->text_normal        = make_pair( 23,  27,  36, 255, 255, 255, 255, 255);
        t->text_hover         = make_pair( 17,  20,  28, 255, 235, 240, 248, 255);
        t->text_pressed       = make_pair( 12,  14,  20, 255, 218, 226, 240, 255);
        t->text_selected      = make_pair(255, 255, 255, 255,  29,  92, 180, 255);
        t->elem_normal        = make_pair( 30,  35,  46, 255, 230, 235, 243, 255);
        t->elem_hover         = make_pair( 18,  22,  30, 255, 214, 222, 236, 255);
        t->elem_pressed       = make_pair( 12,  15,  22, 255, 196, 207, 226, 255);
        t->elem_selected      = make_pair(255, 255, 255, 255,  29,  92, 180, 255);
        t->scrollbar_normal   = make_pair(100, 110, 128, 255, 230, 235, 243, 255);
        t->scrollbar_hover    = make_pair( 76,  86, 104, 255, 222, 228, 238, 255);
        t->scrollbar_pressed  = make_pair( 54,  62,  78, 255, 214, 222, 236, 255);
        t->scrollbar_selected = make_pair( 29,  92, 180, 255, 214, 222, 236, 255);
    } else if (name && tt_strcasecmp(name, "yuuka") == 0) {
        t->base               = make_pair( 58,  66, 102, 255, 218, 225, 244, 255);
        t->text_normal        = make_pair( 28,  32,  50, 255, 244, 247, 253, 255);
        t->text_hover         = make_pair( 20,  24,  40, 255, 228, 234, 249, 255);
        t->text_pressed       = make_pair( 16,  18,  32, 255, 206, 215, 239, 255);
        t->text_selected      = make_pair(255, 255, 255, 255,  72,  88, 152, 255);
        t->elem_normal        = make_pair( 36,  42,  68, 255, 232, 237, 250, 255);
        t->elem_hover         = make_pair( 24,  30,  52, 255, 212, 220, 243, 255);
        t->elem_pressed       = make_pair( 18,  22,  40, 255, 190, 201, 232, 255);
        t->elem_selected      = make_pair(255, 255, 255, 255,  72,  88, 152, 255);
        t->scrollbar_normal   = make_pair( 92, 104, 148, 255, 226, 232, 246, 255);
        t->scrollbar_hover    = make_pair( 72,  84, 128, 255, 216, 224, 242, 255);
        t->scrollbar_pressed  = make_pair( 54,  66, 108, 255, 206, 216, 238, 255);
        t->scrollbar_selected = make_pair( 72,  88, 152, 255, 206, 216, 238, 255);
    } else {
        /* Default: "dark" (Cinema Dark) */
        t->base               = make_pair(166, 174, 194, 255,  15,  17,  23, 255);
        t->text_normal        = make_pair(236, 240, 248, 255,  22,  25,  34, 255);
        t->text_hover         = make_pair(248, 250, 255, 255,  34,  39,  54, 255);
        t->text_pressed       = make_pair(255, 255, 255, 255,  44,  51,  70, 255);
        t->text_selected      = make_pair(255, 255, 255, 255,  37,  84, 199, 255);
        t->elem_normal        = make_pair(226, 232, 244, 255,  30,  35,  48, 255);
        t->elem_hover         = make_pair(248, 250, 255, 255,  45,  52,  70, 255);
        t->elem_pressed       = make_pair(255, 255, 255, 255,  58,  67,  90, 255);
        t->elem_selected      = make_pair(255, 255, 255, 255,  37,  84, 199, 255);
        t->scrollbar_normal   = make_pair(118, 128, 150, 255,  20,  23,  32, 255);
        t->scrollbar_hover    = make_pair(154, 165, 190, 255,  24,  28,  38, 255);
        t->scrollbar_pressed  = make_pair(192, 202, 224, 255,  28,  33,  45, 255);
        t->scrollbar_selected = make_pair( 88, 140, 244, 255,  28,  33,  45, 255);
    }
}

static uint8_t clamp_u8(int64_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static bool parse_theme_pair(yyjson_val *obj, const char *key, ThemePair *out) {
    yyjson_val *arr = yyjson_obj_get(obj, key);
    if (!arr || !yyjson_is_arr(arr) || yyjson_arr_size(arr) < 8) return false;
    uint8_t vals[8];
    for (size_t i = 0; i < 8; i++) {
        yyjson_val *elem = yyjson_arr_get(arr, i);
        if (!elem || !yyjson_is_num(elem)) return false;
        vals[i] = clamp_u8(yyjson_get_sint(elem));
    }
    *out = make_pair(vals[0], vals[1], vals[2], vals[3],
                     vals[4], vals[5], vals[6], vals[7]);
    return true;
}

bool theme_load_file(Theme *t, const char *path) {
    if (!t || !path || !path[0]) return false;
    size_t len = 0;
    char *raw = tt_read_file(path, &len);
    if (!raw) return false;

    yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS;
    yyjson_doc *doc = yyjson_read_opts(raw, len, flags, NULL, NULL);
    free(raw);
    if (!doc) return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    int parsed = 0;
    parsed += parse_theme_pair(root, "base", &t->base);
    parsed += parse_theme_pair(root, "text_normal", &t->text_normal);
    parsed += parse_theme_pair(root, "text_hover", &t->text_hover);
    parsed += parse_theme_pair(root, "text_pressed", &t->text_pressed);
    parsed += parse_theme_pair(root, "text_selected", &t->text_selected);
    parsed += parse_theme_pair(root, "elem_normal", &t->elem_normal);
    parsed += parse_theme_pair(root, "elem_hover", &t->elem_hover);
    parsed += parse_theme_pair(root, "elem_pressed", &t->elem_pressed);
    parsed += parse_theme_pair(root, "elem_selected", &t->elem_selected);
    parsed += parse_theme_pair(root, "scrollbar_normal", &t->scrollbar_normal);
    parsed += parse_theme_pair(root, "scrollbar_hover", &t->scrollbar_hover);
    parsed += parse_theme_pair(root, "scrollbar_pressed", &t->scrollbar_pressed);
    parsed += parse_theme_pair(root, "scrollbar_selected", &t->scrollbar_selected);

    yyjson_doc_free(doc);
    return parsed > 0;
}

KeyAction config_action_from_string(const char *s) {
    if (!s) return ACT_NONE;
    static const struct { const char *name; KeyAction act; } map[] = {
        {"quit", ACT_QUIT},
        {"play_video", ACT_PLAY_VIDEO},
        {"download_video", ACT_DOWNLOAD_VIDEO},
        {"view_user_videos", ACT_VIEW_USER_VIDEOS},
        {"view_recommended_videos", ACT_VIEW_RECOMMENDED_VIDEOS},
        {"next_page", ACT_NEXT_PAGE},
        {"copy_video_url", ACT_COPY_VIDEO_URL},
        {"copy_video_title", ACT_COPY_VIDEO_TITLE},
        {"copy_video_description", ACT_COPY_VIDEO_DESCRIPTION},
        {"show_favorites", ACT_SHOW_FAVORITES},
        {"show_feed", ACT_SHOW_FEED},
        {"show_history", ACT_SHOW_HISTORY},
        {"add_favorites", ACT_ADD_FAVORITES},
        {"set_dark_theme", ACT_SET_DARK_THEME},
        {"set_light_theme", ACT_SET_LIGHT_THEME},
        {"set_yuuka_theme", ACT_SET_YUUKA_THEME},
        {"set_custom_theme", ACT_SET_CUSTOM_THEME},
        {"clear_thumbnail_cache", ACT_CLEAR_THUMBNAIL_CACHE},
        {"inc_list_size", ACT_INC_LIST_SIZE},
        {"dec_list_size", ACT_DEC_LIST_SIZE},
        {"focus_search_input", ACT_FOCUS_SEARCH_INPUT},
        {"focus_playlist", ACT_FOCUS_PLAYLIST},
        {"navigate_back", ACT_NAV_BACK},
        {"toggle_help", ACT_TOGGLE_HELP},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(s, map[i].name) == 0) return map[i].act;
    }
    return ACT_NONE;
}

bool config_mods_from_string(const char *s, uint32_t *out_mods) {
    if (!out_mods) return false;
    *out_mods = 0;
    if (!s || !s[0]) return true;

    const char *p = s;
    while (*p) {
        while (*p == '-' || *p == '+' || isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != '-' && *p != '+' && !isspace((unsigned char)*p)) p++;
        size_t len = (size_t)(p - start);
        if (len == 4 && tt_strncasecmp(start, "CTRL", 4) == 0) *out_mods |= TT_MOD_CTRL;
        else if (len == 5 && tt_strncasecmp(start, "SHIFT", 5) == 0) *out_mods |= TT_MOD_SHIFT;
        else if (len == 3 && tt_strncasecmp(start, "ALT", 3) == 0) *out_mods |= TT_MOD_ALT;
        else if (len == 5 && tt_strncasecmp(start, "SUPER", 5) == 0) *out_mods |= TT_MOD_SUPER;
        else return false;
    }
    return true;
}

bool config_key_from_string(const char *s, xkb_keysym_t *out_sym) {
    if (!s || !out_sym || !s[0]) return false;
    if (s[1] == '\0') {
        unsigned char c = (unsigned char)s[0];
        if (c >= 'A' && c <= 'Z') { *out_sym = (xkb_keysym_t)c; return true; }
        if (c >= 'a' && c <= 'z') { *out_sym = (xkb_keysym_t)toupper(c); return true; }
        if (c >= '0' && c <= '9') { *out_sym = (xkb_keysym_t)c; return true; }
        switch (c) {
            case ' ':  *out_sym = XKB_KEY_space; return true;
            case '\'': *out_sym = XKB_KEY_apostrophe; return true;
            case ',':  *out_sym = XKB_KEY_comma; return true;
            case '-':  *out_sym = XKB_KEY_minus; return true;
            case '.':  *out_sym = XKB_KEY_period; return true;
            case '/':  *out_sym = XKB_KEY_slash; return true;
            case '?':  *out_sym = XKB_KEY_question; return true;
            case ';':  *out_sym = XKB_KEY_semicolon; return true;
            case '=':  *out_sym = XKB_KEY_equal; return true;
            case '[':  *out_sym = XKB_KEY_bracketleft; return true;
            case ']':  *out_sym = XKB_KEY_bracketright; return true;
            case '\\': *out_sym = XKB_KEY_backslash; return true;
            case '`':  *out_sym = XKB_KEY_grave; return true;
            default:   return false;
        }
    }
    if ((s[0] == 'F' || s[0] == 'f') && isdigit((unsigned char)s[1])) {
        char *end = NULL;
        long fn = strtol(s + 1, &end, 10);
        if (end && *end == '\0' && fn >= 1 && fn <= 12) {
            *out_sym = (xkb_keysym_t)(XKB_KEY_F1 + (fn - 1));
            return true;
        }
    }
    static const struct { const char *name; xkb_keysym_t sym; } named[] = {
        {"SPACE", XKB_KEY_space},
        {"ESCAPE", XKB_KEY_Escape},
        {"ENTER", XKB_KEY_Return},
        {"RETURN", XKB_KEY_Return},
        {"TAB", XKB_KEY_Tab},
        {"BACKSPACE", XKB_KEY_BackSpace},
        {"INSERT", XKB_KEY_Insert},
        {"DELETE", XKB_KEY_Delete},
        {"LEFT", XKB_KEY_Left},
        {"RIGHT", XKB_KEY_Right},
        {"UP", XKB_KEY_Up},
        {"DOWN", XKB_KEY_Down},
        {"PAGEUP", XKB_KEY_Page_Up},
        {"PAGEDOWN", XKB_KEY_Page_Down},
        {"HOME", XKB_KEY_Home},
        {"END", XKB_KEY_End},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (tt_strcasecmp(s, named[i].name) == 0) {
            *out_sym = named[i].sym;
            return true;
        }
    }
    return false;
}

static xkb_keysym_t normalize_keysym(xkb_keysym_t sym) {
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
        return sym - (XKB_KEY_a - XKB_KEY_A);
    }
    if (sym == XKB_KEY_KP_Enter) return XKB_KEY_Return;
    return sym;
}

KeyAction config_match_keybinding(const TuiTubeConfig *cfg, uint32_t mods, xkb_keysym_t sym) {
    if (!cfg) return ACT_NONE;
    xkb_keysym_t norm = normalize_keysym(sym);
    for (size_t i = cfg->key_binding_count; i > 0; i--) {
        const KeyBinding *kb = &cfg->key_bindings[i - 1];
        if (kb->mods == mods && kb->keysym == norm) {
            return kb->action;
        }
    }
    if (norm == XKB_KEY_question && (mods & TT_MOD_SHIFT)) {
        uint32_t unshifted = mods & ~TT_MOD_SHIFT;
        for (size_t i = cfg->key_binding_count; i > 0; i--) {
            const KeyBinding *kb = &cfg->key_bindings[i - 1];
            if (kb->mods == unshifted && kb->keysym == norm) {
                return kb->action;
            }
        }
    }
    return ACT_NONE;
}

static const char *keysym_label(xkb_keysym_t sym, char *buf, size_t cap) {
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
        snprintf(buf, cap, "%c", (char)sym);
        return buf;
    }
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
        snprintf(buf, cap, "%c", (char)sym);
        return buf;
    }
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
        snprintf(buf, cap, "F%u", (unsigned)(sym - XKB_KEY_F1 + 1));
        return buf;
    }
    switch (sym) {
        case XKB_KEY_space:        return "Space";
        case XKB_KEY_Escape:       return "Escape";
        case XKB_KEY_Return:       return "Enter";
        case XKB_KEY_Tab:          return "Tab";
        case XKB_KEY_BackSpace:    return "BackSpace";
        case XKB_KEY_Insert:       return "Insert";
        case XKB_KEY_Delete:       return "Delete";
        case XKB_KEY_Left:         return "Left";
        case XKB_KEY_Right:        return "Right";
        case XKB_KEY_Up:           return "Up";
        case XKB_KEY_Down:         return "Down";
        case XKB_KEY_Page_Up:      return "PageUp";
        case XKB_KEY_Page_Down:    return "PageDown";
        case XKB_KEY_Home:         return "Home";
        case XKB_KEY_End:          return "End";
        case XKB_KEY_apostrophe:   return "'";
        case XKB_KEY_comma:        return ",";
        case XKB_KEY_minus:        return "-";
        case XKB_KEY_period:       return ".";
        case XKB_KEY_slash:        return "/";
        case XKB_KEY_question:     return "?";
        case XKB_KEY_semicolon:    return ";";
        case XKB_KEY_equal:        return "=";
        case XKB_KEY_bracketleft:  return "[";
        case XKB_KEY_bracketright: return "]";
        case XKB_KEY_backslash:    return "\\";
        case XKB_KEY_grave:        return "`";
        default:                   return "?";
    }
}

bool config_format_action_shortcut(const TuiTubeConfig *cfg, KeyAction act, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return false;
    out[0] = '\0';
    if (!cfg || act == ACT_NONE) return false;

    for (size_t i = cfg->key_binding_count; i > 0; i--) {
        const KeyBinding *kb = &cfg->key_bindings[i - 1];
        if (kb->action == act) {
            char kbuf[16];
            const char *kname = keysym_label(kb->keysym, kbuf, sizeof(kbuf));
            snprintf(out, out_cap, "%s%s%s%s%s",
                     (kb->mods & TT_MOD_CTRL) ? "Ctrl+" : "",
                     (kb->mods & TT_MOD_ALT) ? "Alt+" : "",
                     (kb->mods & TT_MOD_SHIFT) ? "Shift+" : "",
                     (kb->mods & TT_MOD_SUPER) ? "Super+" : "",
                     kname);
            return true;
        }
    }
    return false;
}

static void add_keybinding(TuiTubeConfig *cfg, KeyAction act, uint32_t mods, xkb_keysym_t sym) {
    if (act == ACT_NONE) return;
    KeyBinding *arr = realloc(cfg->key_bindings, (cfg->key_binding_count + 1) * sizeof(KeyBinding));
    if (!arr) return;
    cfg->key_bindings = arr;
    cfg->key_bindings[cfg->key_binding_count++] = (KeyBinding){act, mods, normalize_keysym(sym)};
}

static void add_default_keybindings(TuiTubeConfig *cfg) {
    add_keybinding(cfg, ACT_QUIT,               TT_MOD_CTRL, XKB_KEY_Q);
    add_keybinding(cfg, ACT_FOCUS_SEARCH_INPUT, TT_MOD_CTRL, XKB_KEY_L);
    add_keybinding(cfg, ACT_FOCUS_SEARCH_INPUT, 0,           XKB_KEY_slash);
    add_keybinding(cfg, ACT_PLAY_VIDEO,         0,           XKB_KEY_Return);
    add_keybinding(cfg, ACT_DOWNLOAD_VIDEO,     TT_MOD_CTRL, XKB_KEY_S);
    add_keybinding(cfg, ACT_SHOW_FAVORITES,     TT_MOD_CTRL, XKB_KEY_F);
    add_keybinding(cfg, ACT_SHOW_FEED,          TT_MOD_CTRL, XKB_KEY_R);
    add_keybinding(cfg, ACT_SHOW_HISTORY,       TT_MOD_CTRL, XKB_KEY_H);
    add_keybinding(cfg, ACT_ADD_FAVORITES,      TT_MOD_CTRL, XKB_KEY_D);
    add_keybinding(cfg, ACT_COPY_VIDEO_URL,     TT_MOD_CTRL, XKB_KEY_Y);
    add_keybinding(cfg, ACT_NEXT_PAGE,          TT_MOD_CTRL, XKB_KEY_N);
    add_keybinding(cfg, ACT_INC_LIST_SIZE,      TT_MOD_CTRL, XKB_KEY_bracketright);
    add_keybinding(cfg, ACT_DEC_LIST_SIZE,      TT_MOD_CTRL, XKB_KEY_bracketleft);
    add_keybinding(cfg, ACT_NAV_BACK,           0,           XKB_KEY_BackSpace);
    add_keybinding(cfg, ACT_NAV_BACK,           TT_MOD_ALT,  XKB_KEY_Left);
    add_keybinding(cfg, ACT_TOGGLE_HELP,        0,           XKB_KEY_question);
    add_keybinding(cfg, ACT_TOGGLE_HELP,        0,           XKB_KEY_F1);
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void config_init_defaults(TuiTubeConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->scale = 0.0;
    cfg->font = tt_strdup("");
    cfg->font_size = 13;
    cfg->thumb_size = 72;
    cfg->playlist_width = 380;
    cfg->player_path = tt_strdup("mpv");
    cfg->video_format = tt_strdup("1080p60");
    cfg->sub_language = tt_strdup("en");
    cfg->theme_name = tt_strdup("dark");
    cfg->theme_path = tt_strdup("");
    cfg->mascot = tt_strdup("");
    cfg->single_click_select = false;
    cfg->show_extra_metadata = false;
    cfg->infinite_scroll = false;
    cfg->save_history = true;
    cfg->dim_watched_videos = true;
    cfg->feed_use_rss = true;
    cfg->max_feed_videos_per_channel = 15;
    cfg->max_feed_age = tt_strdup("1 month");
    tt_parse_age_seconds(cfg->max_feed_age, &cfg->max_feed_age_sec);
    cfg->allow_hdr = false;
    cfg->all_codecs = false;
    cfg->hide_members_only = false;
    cfg->download_dir = tt_strdup("");
    add_default_keybindings(cfg);
    theme_init_builtin(&cfg->theme, "dark");
}

double config_effective_ui_scale(const TuiTubeConfig *cfg) {
    if (!cfg || cfg->scale <= 0.0) return 1.0;
    if (cfg->scale < 0.5) return 0.5;
    if (cfg->scale > 3.0) return 3.0;
    return cfg->scale;
}

char *config_resolve_asset_path(const TuiTubeConfig *cfg, const char *rel_or_abs) {
    if (!cfg || !rel_or_abs || !rel_or_abs[0]) return NULL;
    return tt_path_join(cfg->config_dir, rel_or_abs);
}

char *config_resolve_download_dir(const TuiTubeConfig *cfg) {
    const char *env_home = getenv("HOME");
    const char *home = (env_home && env_home[0]) ? env_home : "/tmp";

    if (cfg && cfg->download_dir && cfg->download_dir[0]) {
        if (strcmp(cfg->download_dir, "~") == 0) {
            return tt_strdup(home);
        }
        if (strncmp(cfg->download_dir, "~/", 2) == 0) {
            return tt_path_join(home, cfg->download_dir + 2);
        }
        if (cfg->download_dir[0] == '/') {
            return tt_strdup(cfg->download_dir);
        }
        return tt_path_join(cfg->config_dir, cfg->download_dir);
    }

    if (cfg && cfg->portable_mode) {
        return tt_path_join(cfg->config_dir, "downloads");
    }

    const char *xdg_dl = getenv("XDG_DOWNLOAD_DIR");
    if (xdg_dl && xdg_dl[0] == '/') {
        return tt_strdup(xdg_dl);
    }
    return tt_path_join(home, "Downloads");
}

bool config_apply_theme(TuiTubeConfig *cfg, const char *theme_name) {
    if (!cfg || !theme_name) return false;
    if (tt_strcasecmp(theme_name, "custom") == 0) {
        char *resolved = config_resolve_asset_path(cfg, cfg->theme_path);
        Theme loaded = cfg->theme;
        theme_init_builtin(&loaded, "dark");
        bool ok = resolved && theme_load_file(&loaded, resolved);
        free(resolved);
        if (!ok) return false;
        cfg->theme = loaded;
        free(cfg->theme_name);
        cfg->theme_name = tt_strdup("custom");
        return true;
    }
    if (tt_strcasecmp(theme_name, "light") == 0 ||
        tt_strcasecmp(theme_name, "dark") == 0 ||
        tt_strcasecmp(theme_name, "yuuka") == 0) {
        char *copy = tt_strdup(theme_name);
        theme_init_builtin(&cfg->theme, theme_name);
        free(cfg->theme_name);
        cfg->theme_name = copy;
        return true;
    }
    return false;
}

static void replace_str_field(yyjson_val *obj, const char *key, char **dst) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_str(v)) {
        const char *s = yyjson_get_str(v);
        char *copy = tt_strdup(s ? s : "");
        if (copy) {
            free(*dst);
            *dst = copy;
        }
    }
}

static void read_bool_field(yyjson_val *obj, const char *key, bool *dst) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_bool(v)) {
        *dst = yyjson_get_bool(v);
    }
}

bool config_load(TuiTubeConfig *cfg, const char *cli_config_dir) {
    config_init_defaults(cfg);
    tt_resolve_paths(cli_config_dir,
                     cfg->config_dir, sizeof(cfg->config_dir),
                     cfg->cache_dir, sizeof(cfg->cache_dir),
                     &cfg->portable_mode);

    char *cfg_path = tt_path_join(cfg->config_dir, "tuitube.json");
    if (!cfg_path) return false;

    size_t len = 0;
    char *raw = tt_read_file(cfg_path, &len);
    free(cfg_path);
    if (!raw) {
        return true; /* Missing config file -> use defaults cleanly */
    }

    yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS;
    yyjson_doc *doc = yyjson_read_opts(raw, len, flags, NULL, NULL);
    free(raw);
    if (!doc) {
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_val *v = yyjson_obj_get(root, "scale");
    if (v && yyjson_is_num(v)) {
        double sc = yyjson_get_num(v);
        if (sc >= 0.0 && sc <= 8.0) cfg->scale = sc;
    }
    replace_str_field(root, "font", &cfg->font);

    v = yyjson_obj_get(root, "font_size");
    if (v && yyjson_is_num(v)) cfg->font_size = clamp_int((int)yyjson_get_sint(v), 8, 48);

    v = yyjson_obj_get(root, "thumb_size");
    if (v && yyjson_is_num(v)) cfg->thumb_size = clamp_int((int)yyjson_get_sint(v), 36, 240);

    v = yyjson_obj_get(root, "playlist_width");
    if (v && yyjson_is_num(v)) cfg->playlist_width = clamp_int((int)yyjson_get_sint(v), 200, 1200);

    replace_str_field(root, "player_path", &cfg->player_path);
    replace_str_field(root, "video_format", &cfg->video_format);
    replace_str_field(root, "sub_language", &cfg->sub_language);
    replace_str_field(root, "theme", &cfg->theme_name);
    replace_str_field(root, "theme_path", &cfg->theme_path);
    replace_str_field(root, "mascot", &cfg->mascot);

    read_bool_field(root, "single_click_select", &cfg->single_click_select);
    read_bool_field(root, "show_extra_metadata", &cfg->show_extra_metadata);
    read_bool_field(root, "infinite_scroll", &cfg->infinite_scroll);
    read_bool_field(root, "save_history", &cfg->save_history);
    read_bool_field(root, "dim_watched_videos", &cfg->dim_watched_videos);
    read_bool_field(root, "feed_use_rss", &cfg->feed_use_rss);
    read_bool_field(root, "allow_hdr", &cfg->allow_hdr);
    read_bool_field(root, "all_codecs", &cfg->all_codecs);
    if (yyjson_obj_get(root, "hide_members_only")) {
        read_bool_field(root, "hide_members_only", &cfg->hide_members_only);
    } else {
        read_bool_field(root, "hide_member_videos", &cfg->hide_members_only);
    }
    replace_str_field(root, "download_dir", &cfg->download_dir);

    v = yyjson_obj_get(root, "max_feed_videos_per_channel");
    if (v && yyjson_is_num(v)) {
        cfg->max_feed_videos_per_channel = clamp_int((int)yyjson_get_sint(v), 1, 100);
    }

    replace_str_field(root, "max_feed_age", &cfg->max_feed_age);
    int64_t age_sec = 0;
    if (tt_parse_age_seconds(cfg->max_feed_age, &age_sec)) {
        cfg->max_feed_age_sec = age_sec;
    }

    yyjson_val *kbarr = yyjson_obj_get(root, "key_bindings");
    if (kbarr && yyjson_is_arr(kbarr)) {
        size_t idx, max;
        yyjson_val *elem;
        yyjson_arr_foreach(kbarr, idx, max, elem) {
            if (!yyjson_is_obj(elem)) continue;
            yyjson_val *va = yyjson_obj_get(elem, "action");
            yyjson_val *vm = yyjson_obj_get(elem, "mod");
            yyjson_val *vk = yyjson_obj_get(elem, "key");
            if (!va || !yyjson_is_str(va) || !vk || !yyjson_is_str(vk)) continue;
            KeyAction act = config_action_from_string(yyjson_get_str(va));
            uint32_t mods = 0;
            const char *mstr = (vm && yyjson_is_str(vm)) ? yyjson_get_str(vm) : "";
            xkb_keysym_t sym = XKB_KEY_NoSymbol;
            if (act != ACT_NONE &&
                config_mods_from_string(mstr, &mods) &&
                config_key_from_string(yyjson_get_str(vk), &sym)) {
                add_keybinding(cfg, act, mods, sym);
            }
        }
    }

    yyjson_doc_free(doc);

    if (!config_apply_theme(cfg, cfg->theme_name)) {
        theme_init_builtin(&cfg->theme, "dark");
    }
    return true;
}

void config_free(TuiTubeConfig *cfg) {
    if (!cfg) return;
    free(cfg->font);
    free(cfg->player_path);
    free(cfg->video_format);
    free(cfg->sub_language);
    free(cfg->theme_name);
    free(cfg->theme_path);
    free(cfg->mascot);
    free(cfg->max_feed_age);
    free(cfg->download_dir);
    free(cfg->key_bindings);
    memset(cfg, 0, sizeof(*cfg));
}
