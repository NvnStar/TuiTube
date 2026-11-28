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

static const char *default_country_for_hl(const char *hl, const char *fallback_gl) {
    static const struct { const char *hl; const char *gl; } map[] = {
        {"vi",    "VN"},
        {"en",    "US"},
        {"ja",    "JP"},
        {"ko",    "KR"},
        {"zh-TW", "TW"},
        {"zh-HK", "HK"},
        {"zh-CN", "CN"},
        {"fr",    "FR"},
        {"de",    "DE"},
        {"es",    "ES"},
        {"pt-BR", "BR"},
        {"pt-PT", "PT"},
        {"ru",    "RU"},
        {"id",    "ID"},
        {"th",    "TH"},
    };
    if (hl) {
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
            if (strcmp(hl, map[i].hl) == 0) return map[i].gl;
        }
    }
    return (fallback_gl && fallback_gl[0]) ? fallback_gl : "US";
}

void config_resolve_locale(TuiTubeConfig *cfg) {
    if (!cfg) return;

    char sys_hl[16] = "en";
    char sys_gl[8] = "US";
    tt_detect_system_locale(sys_hl, sizeof(sys_hl), sys_gl, sizeof(sys_gl));

    if (!cfg->language || !cfg->language[0] || tt_strcasecmp(cfg->language, "auto") == 0) {
        snprintf(cfg->resolved_hl, sizeof(cfg->resolved_hl), "%s", sys_hl);
        snprintf(cfg->resolved_gl, sizeof(cfg->resolved_gl), "%s", sys_gl);
    } else {
        char parsed_hl[16] = {0};
        char parsed_gl[8] = {0};
        if (tt_parse_locale_tag(cfg->language, parsed_hl, sizeof(parsed_hl),
                                parsed_gl, sizeof(parsed_gl))) {
            snprintf(cfg->resolved_hl, sizeof(cfg->resolved_hl), "%s", parsed_hl);
            const char *gl = parsed_gl[0] ? parsed_gl : default_country_for_hl(parsed_hl, sys_gl);
            snprintf(cfg->resolved_gl, sizeof(cfg->resolved_gl), "%s", gl);
        } else {
            snprintf(cfg->resolved_hl, sizeof(cfg->resolved_hl), "%s", sys_hl);
            snprintf(cfg->resolved_gl, sizeof(cfg->resolved_gl), "%s", sys_gl);
        }
    }

    if (cfg->region && cfg->region[0] && tt_strcasecmp(cfg->region, "auto") != 0) {
        const char *p = cfg->region;
        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        if (len == 2 &&
            isalpha((unsigned char)p[0]) &&
            isalpha((unsigned char)p[1])) {
            cfg->resolved_gl[0] = (char)toupper((unsigned char)p[0]);
            cfg->resolved_gl[1] = (char)toupper((unsigned char)p[1]);
            cfg->resolved_gl[2] = '\0';
        }
    }
}

void config_init_defaults(TuiTubeConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->language = tt_strdup("auto");
    cfg->region = tt_strdup("");
    cfg->scale = 0.0;
    cfg->font = tt_strdup("");
    cfg->font_size = 13;
    cfg->thumb_size = 72;
    cfg->playlist_width = 380;
    cfg->player_path = tt_strdup("mpv");
    cfg->ytdlp_path = tt_strdup("");
    cfg->has_ytdlp_path = false;
    cfg->video_format = tt_strdup("1080p60");
    cfg->has_video_format = false;
    cfg->sub_language = tt_strdup("en");
    cfg->has_sub_language = false;
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
    cfg->original_titles = true;
    cfg->download_dir = tt_strdup("");
    add_default_keybindings(cfg);
    theme_init_builtin(&cfg->theme, "dark");
    config_resolve_locale(cfg);
}

double config_effective_ui_scale(const TuiTubeConfig *cfg) {
    if (!cfg || cfg->scale <= 0.0) return 1.0;
    if (cfg->scale < 0.5) return 0.5;
    if (cfg->scale > 3.0) return 3.0;
    return cfg->scale;
}

char *config_resolve_asset_path(const TuiTubeConfig *cfg, const char *rel_or_abs) {
    if (!cfg || !rel_or_abs || !rel_or_abs[0]) return NULL;
    if (!tt_is_abs_path(rel_or_abs) && cfg->config_file_dir[0]) {
        char *from_cfg_file = tt_path_join(cfg->config_file_dir, rel_or_abs);
        if (from_cfg_file && tt_file_exists(from_cfg_file)) {
            return from_cfg_file;
        }
        free(from_cfg_file);
    }
    char *primary = tt_path_join(cfg->config_dir, rel_or_abs);
    if (!tt_is_abs_path(rel_or_abs) && (!primary || !tt_file_exists(primary)) && !cfg->portable_mode) {
        char exe_dir[PATH_MAX];
        if (tt_get_exe_dir(exe_dir, sizeof(exe_dir))) {
            char *fallback = tt_path_join(exe_dir, rel_or_abs);
            if (fallback && tt_file_exists(fallback)) {
                free(primary);
                return fallback;
            }
            free(fallback);
        }
    }
    return primary;
}

char *config_resolve_download_dir(const TuiTubeConfig *cfg) {
    const char *env_home = getenv("HOME");
#ifdef _WIN32
    if (!env_home || !env_home[0]) env_home = getenv("USERPROFILE");
    if (!env_home || !env_home[0]) env_home = getenv("TEMP");
#endif
    const char *home = (env_home && env_home[0]) ? env_home : "/tmp";

    if (cfg && cfg->download_dir && cfg->download_dir[0]) {
        if (strcmp(cfg->download_dir, "~") == 0) {
            return tt_strdup(home);
        }
        if (cfg->download_dir[0] == '~' &&
            (cfg->download_dir[1] == '/' || cfg->download_dir[1] == '\\')) {
            return tt_path_join(home, cfg->download_dir + 2);
        }
        if (tt_is_abs_path(cfg->download_dir)) {
            return tt_strdup(cfg->download_dir);
        }
        return tt_path_join(cfg->config_dir, cfg->download_dir);
    }

    if (cfg && cfg->portable_mode) {
        return tt_path_join(cfg->config_dir, "downloads");
    }

    const char *xdg_dl = getenv("XDG_DOWNLOAD_DIR");
    if (xdg_dl && tt_is_abs_path(xdg_dl)) {
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

static void trim_ascii_and_outer_quotes_inplace(char *s) {
    if (!s) return;
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
    size_t out_len = len - start;
    if (start > 0 && out_len > 0) {
        memmove(s, s + start, out_len);
    }
    s[out_len] = '\0';
}

static void read_bool_field(yyjson_val *obj, const char *key, bool *dst) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_bool(v)) {
        *dst = yyjson_get_bool(v);
    }
}

static void copy_parent_dir(const char *path, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!path || !path[0]) return;
    const char *last_sep = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (!last_sep) return;
    size_t dlen = (size_t)(last_sep - path);
    if (dlen == 0) {
        if (out_cap >= 2) {
            out[0] = '/';
            out[1] = '\0';
        }
        return;
    }
#ifdef _WIN32
    if (dlen == 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
        dlen = 3;
    }
#endif
    if (dlen >= out_cap) return;
    memcpy(out, path, dlen);
    out[dlen] = '\0';
}

bool config_load(TuiTubeConfig *cfg, const char *cli_config_dir) {
    return config_load_from(cfg, cli_config_dir, NULL);
}

bool config_load_from(TuiTubeConfig *cfg, const char *cli_config_dir,
                      const char *override_config_file) {
    config_init_defaults(cfg);
    tt_resolve_paths(cli_config_dir,
                     cfg->config_dir, sizeof(cfg->config_dir),
                     cfg->cache_dir, sizeof(cfg->cache_dir),
                     &cfg->portable_mode);

    size_t len = 0;
    char *raw = NULL;
    if (override_config_file && override_config_file[0]) {
        raw = tt_read_file(override_config_file, &len);
        if (raw) {
            copy_parent_dir(override_config_file, cfg->config_file_dir, sizeof(cfg->config_file_dir));
        }
    }
    if (!raw) {
        char *cfg_path = tt_path_join(cfg->config_dir, "tuitube.json");
        if (!cfg_path) return false;
        raw = tt_read_file(cfg_path, &len);
        free(cfg_path);
    }
    if (!raw && (!cli_config_dir || !cli_config_dir[0]) && !cfg->portable_mode) {
        char exe_dir[PATH_MAX];
        if (tt_get_exe_dir(exe_dir, sizeof(exe_dir))) {
            char *fallback_path = tt_path_join(exe_dir, "tuitube.json");
            if (fallback_path) {
                raw = tt_read_file(fallback_path, &len);
                free(fallback_path);
            }
        }
    }
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

    replace_str_field(root, "language", &cfg->language);
    replace_str_field(root, "region", &cfg->region);

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
    replace_str_field(root, "ytdlp_path", &cfg->ytdlp_path);
    trim_ascii_and_outer_quotes_inplace(cfg->ytdlp_path);
    cfg->has_ytdlp_path = (cfg->ytdlp_path && cfg->ytdlp_path[0] != '\0');
    yyjson_val *vf = yyjson_obj_get(root, "video_format");
    if (vf && yyjson_is_str(vf) && yyjson_get_str(vf)[0] != '\0') {
        cfg->has_video_format = true;
    }
    replace_str_field(root, "video_format", &cfg->video_format);
    yyjson_val *sl = yyjson_obj_get(root, "sub_language");
    if (sl && yyjson_is_str(sl) && yyjson_get_str(sl)[0] != '\0') {
        cfg->has_sub_language = true;
    }
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
    read_bool_field(root, "original_titles", &cfg->original_titles);
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
    config_resolve_locale(cfg);
    return true;
}

void config_free(TuiTubeConfig *cfg) {
    if (!cfg) return;
    free(cfg->language);
    free(cfg->region);
    free(cfg->font);
    free(cfg->player_path);
    free(cfg->ytdlp_path);
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

bool config_mpv_key_name(xkb_keysym_t sym, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return false;
    out[0] = '\0';

    int n = -1;
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) {
        n = snprintf(out, out_cap, "%c", (char)(sym - XKB_KEY_A + 'a'));
    } else if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) {
        n = snprintf(out, out_cap, "%c", (char)sym);
    } else if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) {
        n = snprintf(out, out_cap, "%c", (char)sym);
    } else if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
        n = snprintf(out, out_cap, "F%u", (unsigned)(sym - XKB_KEY_F1 + 1));
    } else {
        const char *named = NULL;
        switch (sym) {
            case XKB_KEY_Escape:       named = "ESC"; break;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:     named = "ENTER"; break;
            case XKB_KEY_Tab:          named = "TAB"; break;
            case XKB_KEY_BackSpace:    named = "BS"; break;
            case XKB_KEY_Delete:       named = "DEL"; break;
            case XKB_KEY_Insert:       named = "INS"; break;
            case XKB_KEY_Left:         named = "LEFT"; break;
            case XKB_KEY_Right:        named = "RIGHT"; break;
            case XKB_KEY_Up:           named = "UP"; break;
            case XKB_KEY_Down:         named = "DOWN"; break;
            case XKB_KEY_Page_Up:      named = "PGUP"; break;
            case XKB_KEY_Page_Down:    named = "PGDWN"; break;
            case XKB_KEY_Home:         named = "HOME"; break;
            case XKB_KEY_End:          named = "END"; break;
            case XKB_KEY_space:        named = "SPACE"; break;
            case XKB_KEY_apostrophe:   named = "'"; break;
            case XKB_KEY_comma:        named = ","; break;
            case XKB_KEY_minus:        named = "-"; break;
            case XKB_KEY_period:       named = "."; break;
            case XKB_KEY_slash:        named = "/"; break;
            case XKB_KEY_question:     named = "?"; break;
            case XKB_KEY_semicolon:    named = ";"; break;
            case XKB_KEY_equal:        named = "="; break;
            case XKB_KEY_bracketleft:  named = "["; break;
            case XKB_KEY_bracketright: named = "]"; break;
            case XKB_KEY_backslash:    named = "\\"; break;
            case XKB_KEY_grave:        named = "`"; break;
            default:                   return false;
        }
        n = snprintf(out, out_cap, "%s", named);
    }

    if (n < 0 || (size_t)n >= out_cap) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static void sanitize_close_win_cmd(const char *close_win_cmd,
                                   const char **out_start,
                                   size_t *out_len) {
    static const char fallback[] = "quit";
    *out_start = fallback;
    *out_len = sizeof(fallback) - 1u;

    if (!close_win_cmd) return;

    for (const char *p = close_win_cmd; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r') return;
    }

    const char *start = close_win_cmd;
    while (*start == ' ' || *start == '\t') start++;

    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }

    if (len == 0 || start[0] == '#') return;
    if (len == 6 && tt_strncasecmp(start, "ignore", 6) == 0) return;
    if (len == 4 && tt_strncasecmp(start, "noop", 4) == 0) return;
    if (len >= 5 && tt_strncasecmp(start + (len - 5), "/noop", 5) == 0) return;

    *out_start = start;
    *out_len = len;
}

bool config_build_mpv_overlay_section(const TuiTubeConfig *cfg,
                                      const char *client_name,
                                      const char *close_win_cmd,
                                      StrBuf *out) {
    if (!out) return false;
    sb_clear(out);

    const char *cname = (client_name && client_name[0]) ? client_name : "tuitube";

    const char *close_slice = NULL;
    size_t close_len = 0;
    sanitize_close_win_cmd(close_win_cmd, &close_slice, &close_len);

    bool ok = sb_append(out, "CLOSE_WIN ") &&
              sb_append_n(out, close_slice, close_len) &&
              sb_append_char(out, '\n');

    /* Mouse bindings */
    ok = ok && sb_appendf(out, "MOUSE_MOVE script-binding %s/mouse_move\n", cname);
    ok = ok && sb_appendf(out, "MOUSE_LEAVE script-binding %s/mouse_leave\n", cname);

    static const char *const mouse_btns[] = {
        "MBTN_LEFT", "MBTN_LEFT_DBL", "MBTN_RIGHT",
        "MBTN_MID", "MBTN_BACK", "MBTN_FORWARD"
    };
    for (size_t i = 0; ok && i < sizeof(mouse_btns) / sizeof(mouse_btns[0]); i++) {
        ok = sb_appendf(out, "%s script-binding %s/input\n", mouse_btns[i], cname);
    }

    static const char *const wheel_btns[] = {
        "WHEEL_UP", "WHEEL_DOWN", "WHEEL_LEFT", "WHEEL_RIGHT"
    };
    for (size_t i = 0; ok && i < sizeof(wheel_btns) / sizeof(wheel_btns[0]); i++) {
        ok = sb_appendf(out, "%s repeatable script-binding %s/input\n", wheel_btns[i], cname);
    }

    /* Toggle shortcut inside overlay */
    ok = ok && sb_appendf(out, "F12 script-binding %s/toggle\n", cname);

    /* Printable / Unicode / IME */
    ok = ok && sb_appendf(out, "ANY_UNICODE repeatable script-binding %s/input\n", cname);
    ok = ok && sb_appendf(out, "SPACE repeatable script-binding %s/input\n", cname);
    ok = ok && sb_appendf(out, "SHARP repeatable script-binding %s/input\n", cname);
    ok = ok && sb_appendf(out, "KP_DIVIDE repeatable script-binding %s/input\n", cname);

    /* Navigation & editing keys across modifier prefixes */
    static const char *const mod_prefixes[] = {
        "", "Shift+", "Ctrl+", "Alt+", "Ctrl+Shift+", "Ctrl+Alt+"
    };
    static const struct { const char *key; bool rep; } nav_keys[] = {
        {"ESC", false}, {"ENTER", false}, {"KP_ENTER", false}, {"TAB", false},
        {"BS", true}, {"DEL", true}, {"INS", false},
        {"UP", true}, {"DOWN", true}, {"LEFT", true}, {"RIGHT", true},
        {"PGUP", true}, {"PGDWN", true}, {"HOME", false}, {"END", false},
        {"F1", false}, {"F2", false}, {"F3", false}, {"F4", false},
        {"F5", false}, {"F6", false}, {"F7", false}, {"F8", false},
        {"F9", false}, {"F10", false}, {"F11", false}
    };
    for (size_t m = 0; ok && m < sizeof(mod_prefixes) / sizeof(mod_prefixes[0]); m++) {
        for (size_t k = 0; ok && k < sizeof(nav_keys) / sizeof(nav_keys[0]); k++) {
            ok = sb_appendf(out, "%s%s%s script-binding %s/input\n",
                            mod_prefixes[m], nav_keys[k].key,
                            nav_keys[k].rep ? " repeatable" : "", cname);
        }
    }

    /* Modified printable shortcuts */
    for (char c = 'a'; ok && c <= 'z'; c++) {
        ok = sb_appendf(out, "Ctrl+%c script-binding %s/input\n", c, cname) &&
             sb_appendf(out, "Ctrl+Shift+%c script-binding %s/input\n", c, cname);
    }
    static const char *const ctrl_punct[] = {
        "[", "]", "/", "-", "=", "\\", ";", "'", ",", "."
    };
    for (size_t i = 0; ok && i < sizeof(ctrl_punct) / sizeof(ctrl_punct[0]); i++) {
        ok = sb_appendf(out, "Ctrl+%s script-binding %s/input\n", ctrl_punct[i], cname);
    }

    /* Additional configured modifier+key combinations (no WM shortcuts hardcoded) */
    if (cfg) {
        for (size_t i = 0; ok && i < cfg->key_binding_count; i++) {
            const KeyBinding *kb = &cfg->key_bindings[i];
            if (kb->mods == 0) continue;
            char kname[16];
            if (!config_mpv_key_name(kb->keysym, kname, sizeof(kname))) continue;
            ok = sb_appendf(out, "%s%s%s%s%s script-binding %s/input\n",
                            (kb->mods & TT_MOD_CTRL) ? "Ctrl+" : "",
                            (kb->mods & TT_MOD_ALT) ? "Alt+" : "",
                            (kb->mods & TT_MOD_SHIFT) ? "Shift+" : "",
                            (kb->mods & TT_MOD_SUPER) ? "Meta+" : "",
                            kname, cname);
        }
    }

    /* Catch-all so unmapped keys never leak to mpv while exclusive */
    ok = ok && sb_appendf(out, "UNMAPPED repeatable script-binding %s/noop\n", cname);

    if (!ok || !out->data) {
        sb_free(out);
        return false;
    }
    return true;
}
