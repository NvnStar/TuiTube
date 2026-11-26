#include "ui.h"
#include "player.h"
#include "util.h"

#include <ctype.h>
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#include <math.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SP_XS 4.0
#define SP_SM 8.0
#define SP_MD 12.0
#define SP_LG 16.0
#define SP_XL 24.0

typedef struct {
    double x, y, w, h;
} Rect;

typedef enum {
    FOCUS_SEARCH = 0,
    FOCUS_LIST,
    FOCUS_DETAIL
} FocusZone;

typedef enum {
    ICON_NONE = 0,
    ICON_BACK,
    ICON_SEARCH,
    ICON_CLEAR,
    ICON_SLIDERS,
    ICON_RSS,
    ICON_STAR,
    ICON_STAR_FILLED,
    ICON_HISTORY,
    ICON_HELP,
    ICON_PLAY,
    ICON_DOWNLOAD,
    ICON_CHANNEL,
    ICON_SPARKLE,
    ICON_COPY,
    ICON_RETRY,
    ICON_PLAYLIST
} UiIcon;

typedef enum {
    HIT_NONE = 0,
    HIT_CSD_MIN,
    HIT_CSD_MAX,
    HIT_CSD_CLOSE,
    HIT_CSD_TITLEBAR,
    HIT_BTN_BACK,
    HIT_BTN_SEARCH_CLEAR,
    HIT_SEARCH_BOX,
    HIT_BTN_GO,
    HIT_BTN_FILTERS,
    HIT_BTN_FEED,
    HIT_BTN_FAVS,
    HIT_BTN_HISTORY,
    HIT_BTN_HELP,
    HIT_BTN_SORT,
    HIT_BTN_TYPE,
    HIT_BTN_LIVE,
    HIT_BTN_CHAN_VIDEOS,
    HIT_BTN_CHAN_LIVE,
    HIT_BTN_NEXT_PAGE,
    HIT_BTN_RETRY,
    HIT_SPLITTER,
    HIT_SCROLLBAR,
    HIT_LIST_ITEM,
    HIT_LIST_AREA,
    HIT_DETAIL_THUMB,
    HIT_DETAIL_AUTHOR,
    HIT_ACT_PLAY,
    HIT_ACT_DOWNLOAD,
    HIT_ACT_CHANNEL,
    HIT_ACT_RECOMMENDED,
    HIT_ACT_FAVORITE,
    HIT_ACT_COPY_URL,
    HIT_DESC_SCROLLBAR,
    HIT_RIGHT_PANE,
    HIT_CTX_ITEM,
    HIT_CTX_MENU,
    HIT_HELP_CLOSE,
    HIT_HELP_MODAL,
    HIT_HELP_BACKDROP
} HitId;

typedef enum {
    CTX_ACT_PLAY = 0,
    CTX_ACT_DOWNLOAD,
    CTX_ACT_CHANNEL,
    CTX_ACT_RECOMMENDED,
    CTX_ACT_FAVORITE,
    CTX_ACT_COPY_URL,
    CTX_ACT_COPY_TITLE,
    CTX_ACT_COPY_DESC
} CtxAction;

typedef struct {
    bool open;
    double x;
    double y;
    CtxAction actions[8];
    char labels[8][48];
    UiIcon icons[8];
    Rect item_rects[8];
    Rect box;
    size_t count;
    ssize_t hover_idx;
} CtxMenu;

typedef struct {
    bool has_csd;
    bool compact_toolbar;
    Rect csd_bar, csd_min, csd_max, csd_close;
    Rect toolbar, toolbar_sep;
    Rect btn_back, search_box, btn_search_clear;
    Rect btn_go, btn_filters, btn_feed, btn_favs, btn_history, btn_help;
    Rect filter_bar, btn_sort, btn_type, btn_live;
    Rect left_pane, left_header, btn_chan_videos, btn_chan_live, btn_next_page, list_area, scrollbar;
    Rect btn_retry;
    Rect splitter;
    Rect right_pane;
    Rect detail_thumb, detail_author;
    Rect act_play, act_download, act_channel, act_recommended, act_fav, act_copy;
    Rect desc_area, desc_scrollbar;
    Rect status_bar;
    Rect help_modal_box, help_close_btn;
} Layout;

struct UiState {
    TuiTubeConfig *cfg;
    WaylandApp *wl;
    ThumbCache *thumbs;
    FavoriteList *favs;
    HistoryList *history;
    UiCallbacks cbs;
    void *user_ctx;

    int win_w;
    int win_h;

    char *font_family;
    PangoLayout *measure_pl;
    cairo_surface_t *mascot_surf;

    char *search_buf;
    size_t search_len;
    size_t search_cap;
    size_t search_cursor;
    bool search_sel_all;
    double last_search_tx;

    char *ime_preedit;
    size_t ime_preedit_len;
    int32_t ime_cursor_begin;
    int32_t ime_cursor_end;

    FocusZone focus_zone;
    bool show_filters;
    SearchFilter filter;

    UiViewKind view_kind;
    bool can_go_back;
    bool show_channel_tabs;
    bool channel_live_tab;
    bool busy;
    char error_text[256];

    MediaList list;
    ssize_t selected_idx;
    ssize_t hover_idx;
    MediaItem detail_item;
    bool has_detail_item;

    double list_scroll_y;
    double desc_scroll_y;
    double desc_max_scroll;
    bool loading_more;

    char status_text[256];

    double ptr_x;
    double ptr_y;
    HitId hover_id;
    HitId pressed_id;
    bool dragging_splitter;
    double splitter_drag_offset;
    bool dragging_scrollbar;
    double scrollbar_drag_offset;
    bool dragging_desc_scrollbar;
    double desc_sb_drag_offset;

    bool show_help;
    CtxMenu ctx_menu;

    Layout last_layout;
    bool has_last_layout;

    char **inflight_keys;
    size_t inflight_count;
    size_t inflight_cap;
    char **failed_keys;
    size_t failed_count;
    size_t failed_cap;
};

static bool pt_in_rect(double x, double y, Rect r) {
    return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void set_cairo_color(cairo_t *cr, ColorRGBA c, double alpha_mult) {
    cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                          (c.a / 255.0) * alpha_mult);
}

static ColorRGBA mix_color(ColorRGBA a, ColorRGBA b, double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    ColorRGBA out;
    out.r = (uint8_t)lround(a.r * (1.0 - t) + b.r * t);
    out.g = (uint8_t)lround(a.g * (1.0 - t) + b.g * t);
    out.b = (uint8_t)lround(a.b * (1.0 - t) + b.b * t);
    out.a = (uint8_t)lround(a.a * (1.0 - t) + b.a * t);
    return out;
}

static double align4(double v) {
    return ceil(v / 4.0) * 4.0;
}

static void logical_viewport(const UiState *ui, double *out_w, double *out_h) {
    double sc = config_effective_ui_scale(ui->cfg);
    double raw_w = ui->win_w > 0 ? (double)ui->win_w : 1024.0;
    double raw_h = ui->win_h > 0 ? (double)ui->win_h : 640.0;
    *out_w = raw_w / sc;
    *out_h = raw_h / sc;
}

static void draw_rounded_rect(cairo_t *cr, Rect r, double rad) {
    if (r.w <= 0 || r.h <= 0) return;
    double rx = rad;
    if (rx * 2.0 > r.w) rx = r.w / 2.0;
    if (rx * 2.0 > r.h) rx = r.h / 2.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, r.x + r.w - rx, r.y + rx, rx, -M_PI / 2.0, 0);
    cairo_arc(cr, r.x + r.w - rx, r.y + r.h - rx, rx, 0, M_PI / 2.0);
    cairo_arc(cr, r.x + rx, r.y + r.h - rx, rx, M_PI / 2.0, M_PI);
    cairo_arc(cr, r.x + rx, r.y + rx, rx, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

static PangoFontDescription *make_font_desc_w(const UiState *ui, double scale_mult, PangoWeight weight) {
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, ui->font_family ? ui->font_family : "Sans");
    double sz = (double)ui->cfg->font_size * scale_mult;
    if (sz < 7.0) sz = 7.0;
    pango_font_description_set_absolute_size(desc, sz * PANGO_SCALE);
    pango_font_description_set_weight(desc, weight);
    return desc;
}

static PangoFontDescription *make_font_desc(const UiState *ui, double scale_mult, bool bold) {
    return make_font_desc_w(ui, scale_mult, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
}

static void measure_text_size(const UiState *ui, const char *text, double scale_mult,
                              PangoWeight weight, double *out_w, double *out_h) {
    if (!ui || !ui->measure_pl || !text || !text[0]) {
        if (out_w) *out_w = 0.0;
        if (out_h) *out_h = 0.0;
        return;
    }
    PangoFontDescription *desc = make_font_desc_w(ui, scale_mult, weight);
    pango_layout_set_font_description(ui->measure_pl, desc);
    pango_font_description_free(desc);
    pango_layout_set_width(ui->measure_pl, -1);
    pango_layout_set_single_paragraph_mode(ui->measure_pl, TRUE);
    pango_layout_set_text(ui->measure_pl, text, -1);
    int pw = 0, ph = 0;
    pango_layout_get_pixel_size(ui->measure_pl, &pw, &ph);
    if (out_w) *out_w = (double)pw;
    if (out_h) *out_h = (double)ph;
}

static void draw_text_line(cairo_t *cr, const UiState *ui, const char *text,
                           double x, double y, double max_w, double max_h,
                           ColorRGBA col, double alpha_mult, double scale_mult,
                           bool bold, PangoAlignment align, bool vcenter) {
    if (!text || !text[0] || max_w <= 2 || max_h <= 2) return;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = make_font_desc(ui, scale_mult, bold);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, (int)(max_w * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_alignment(layout, align);

    int lw = 0, lh = 0;
    pango_layout_get_pixel_size(layout, &lw, &lh);
    double ty = vcenter ? y + (max_h - lh) * 0.5 : y;

    set_cairo_color(cr, col, alpha_mult);
    cairo_move_to(cr, x, ty);
    pango_cairo_show_layout(cr, layout);
    cairo_new_path(cr);
    g_object_unref(layout);
}

static void draw_icon(cairo_t *cr, UiIcon icon, Rect box, ColorRGBA col, double alpha) {
    if (icon == ICON_NONE || box.w <= 1.0 || box.h <= 1.0) return;

    cairo_save(cr);
    cairo_new_path(cr);
    cairo_translate(cr, box.x, box.y);
    cairo_scale(cr, box.w / 16.0, box.h / 16.0);
    set_cairo_color(cr, col, alpha);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    switch (icon) {
        case ICON_BACK:
            cairo_move_to(cr, 13.0, 8.0);
            cairo_line_to(cr, 3.0, 8.0);
            cairo_move_to(cr, 7.5, 3.5);
            cairo_line_to(cr, 3.0, 8.0);
            cairo_line_to(cr, 7.5, 12.5);
            cairo_stroke(cr);
            break;
        case ICON_SEARCH:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 7.0, 7.0, 4.5, 0.0, 2.0 * M_PI);
            cairo_move_to(cr, 10.3, 10.3);
            cairo_line_to(cr, 13.8, 13.8);
            cairo_stroke(cr);
            break;
        case ICON_CLEAR:
            cairo_move_to(cr, 4.0, 4.0);
            cairo_line_to(cr, 12.0, 12.0);
            cairo_move_to(cr, 12.0, 4.0);
            cairo_line_to(cr, 4.0, 12.0);
            cairo_stroke(cr);
            break;
        case ICON_SLIDERS:
            cairo_move_to(cr, 2.5, 4.0);  cairo_line_to(cr, 13.5, 4.0);
            cairo_move_to(cr, 2.5, 8.0);  cairo_line_to(cr, 13.5, 8.0);
            cairo_move_to(cr, 2.5, 12.0); cairo_line_to(cr, 13.5, 12.0);
            cairo_move_to(cr, 6.0, 2.5);  cairo_line_to(cr, 6.0, 5.5);
            cairo_move_to(cr, 10.5, 6.5); cairo_line_to(cr, 10.5, 9.5);
            cairo_move_to(cr, 5.0, 10.5); cairo_line_to(cr, 5.0, 13.5);
            cairo_stroke(cr);
            break;
        case ICON_RSS:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 4.0, 12.0, 1.3, 0.0, 2.0 * M_PI);
            cairo_fill(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 4.0, 12.0, 5.2, -M_PI / 2.0, 0.0);
            cairo_stroke(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 4.0, 12.0, 9.2, -M_PI / 2.0, 0.0);
            cairo_stroke(cr);
            break;
        case ICON_STAR:
        case ICON_STAR_FILLED: {
            for (int i = 0; i < 10; i++) {
                double ang = -M_PI / 2.0 + i * (M_PI / 5.0);
                double rad = (i % 2 == 0) ? 6.2 : 2.8;
                double px = 8.0 + cos(ang) * rad;
                double py = 8.2 + sin(ang) * rad;
                if (i == 0) cairo_move_to(cr, px, py);
                else cairo_line_to(cr, px, py);
            }
            cairo_close_path(cr);
            if (icon == ICON_STAR_FILLED) cairo_fill_preserve(cr);
            cairo_stroke(cr);
            break;
        }
        case ICON_HISTORY:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.3, 8.0, 5.5, -2.5, 2.5);
            cairo_stroke(cr);
            cairo_move_to(cr, 2.5, 3.8);
            cairo_line_to(cr, 3.0, 7.2);
            cairo_line_to(cr, 6.4, 6.4);
            cairo_stroke(cr);
            cairo_move_to(cr, 8.3, 5.0);
            cairo_line_to(cr, 8.3, 8.2);
            cairo_line_to(cr, 10.6, 9.6);
            cairo_stroke(cr);
            break;
        case ICON_HELP:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.0, 8.0, 6.2, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.0, 6.3, 1.9, M_PI, 0.35);
            cairo_line_to(cr, 8.0, 9.4);
            cairo_stroke(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.0, 11.7, 0.8, 0.0, 2.0 * M_PI);
            cairo_fill(cr);
            break;
        case ICON_PLAY:
            cairo_move_to(cr, 4.5, 2.6);
            cairo_line_to(cr, 13.5, 8.0);
            cairo_line_to(cr, 4.5, 13.4);
            cairo_close_path(cr);
            cairo_fill(cr);
            break;
        case ICON_DOWNLOAD:
            cairo_move_to(cr, 8.0, 2.5);
            cairo_line_to(cr, 8.0, 10.5);
            cairo_move_to(cr, 4.8, 7.5);
            cairo_line_to(cr, 8.0, 10.8);
            cairo_line_to(cr, 11.2, 7.5);
            cairo_move_to(cr, 3.0, 11.5);
            cairo_line_to(cr, 3.0, 13.5);
            cairo_line_to(cr, 13.0, 13.5);
            cairo_line_to(cr, 13.0, 11.5);
            cairo_stroke(cr);
            break;
        case ICON_CHANNEL:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.0, 5.2, 2.7, 0.0, 2.0 * M_PI);
            cairo_stroke(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.0, 14.5, 5.2, M_PI * 1.15, M_PI * 1.85);
            cairo_stroke(cr);
            break;
        case ICON_SPARKLE:
            cairo_move_to(cr, 8.0, 2.0);
            cairo_line_to(cr, 9.6, 6.4);
            cairo_line_to(cr, 14.0, 8.0);
            cairo_line_to(cr, 9.6, 9.6);
            cairo_line_to(cr, 8.0, 14.0);
            cairo_line_to(cr, 6.4, 9.6);
            cairo_line_to(cr, 2.0, 8.0);
            cairo_line_to(cr, 6.4, 6.4);
            cairo_close_path(cr);
            cairo_stroke(cr);
            break;
        case ICON_COPY:
            cairo_rectangle(cr, 5.5, 5.5, 8.0, 8.5);
            cairo_stroke(cr);
            cairo_move_to(cr, 3.5, 10.5);
            cairo_line_to(cr, 2.5, 10.5);
            cairo_line_to(cr, 2.5, 2.5);
            cairo_line_to(cr, 10.5, 2.5);
            cairo_line_to(cr, 10.5, 3.5);
            cairo_stroke(cr);
            break;
        case ICON_RETRY:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 8.0, 8.0, 5.2, -0.4, 1.5 * M_PI);
            cairo_stroke(cr);
            cairo_move_to(cr, 8.0, 1.4);
            cairo_line_to(cr, 11.4, 3.2);
            cairo_line_to(cr, 8.4, 5.2);
            cairo_stroke(cr);
            break;
        case ICON_PLAYLIST:
            cairo_move_to(cr, 3.0, 4.5);  cairo_line_to(cr, 13.0, 4.5);
            cairo_move_to(cr, 3.0, 8.0);  cairo_line_to(cr, 13.0, 8.0);
            cairo_move_to(cr, 3.0, 11.5); cairo_line_to(cr, 9.5, 11.5);
            cairo_stroke(cr);
            break;
        default:
            break;
    }

    cairo_restore(cr);
}

static void draw_download_progress_button(cairo_t *cr, const UiState *ui, Rect r,
                                          const char *label, double progress) {
    if (r.w <= 1.0 || r.h <= 1.0) return;
    const Theme *t = &ui->cfg->theme;
    ThemePair tp = t->elem_normal;

    /* 1. Base button background */
    draw_rounded_rect(cr, r, 6.0);
    set_cairo_color(cr, tp.bg, 1.0);
    cairo_fill(cr);

    /* 2. Crisp bottom progress track + fill clipped to button corners */
    double track_h = 3.0;
    cairo_save(cr);
    draw_rounded_rect(cr, r, 6.0);
    cairo_clip(cr);
    set_cairo_color(cr, t->base.bg, 0.75);
    cairo_rectangle(cr, r.x, r.y + r.h - track_h, r.w, track_h);
    cairo_fill(cr);
    if (progress >= 0.0 && progress <= 1.0) {
        double fw = r.w * progress;
        if (fw > 0.5) {
            set_cairo_color(cr, t->elem_selected.bg, 1.0);
            cairo_rectangle(cr, r.x, r.y + r.h - track_h, fw, track_h);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);

    /* 3. Crisp accent border */
    draw_rounded_rect(cr, r, 6.0);
    set_cairo_color(cr, t->elem_selected.bg, 0.65);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* 4. Icon and label centered cleanly above the bottom track */
    double content_h = r.h - track_h;
    double icon_sz = fmin(content_h - 8.0, fmax(13.0, ui->cfg->font_size * 1.05));
    double tw = 0.0;
    measure_text_size(ui, label, 0.92, PANGO_WEIGHT_BOLD, &tw, NULL);
    double gap = 6.0;
    double total = icon_sz + gap + tw;
    if (total > r.w - 12.0) total = r.w - 12.0;
    double sx = r.x + (r.w - total) * 0.5;
    Rect ib = {sx, r.y + (content_h - icon_sz) * 0.5, icon_sz, icon_sz};
    draw_icon(cr, ICON_DOWNLOAD, ib, tp.fg, 1.0);
    double tx = sx + icon_sz + gap;
    double avail = (r.x + r.w - 6.0) - tx;
    draw_text_line(cr, ui, label, tx, r.y, avail, content_h,
                   tp.fg, 1.0, 0.92, true, PANGO_ALIGN_LEFT, true);
}

static void draw_button_ex(cairo_t *cr, const UiState *ui, Rect r,
                           UiIcon icon, const char *label, HitId id,
                           bool active, bool enabled, bool primary_cta) {
    if (r.w <= 1.0 || r.h <= 1.0) return;
    const Theme *t = &ui->cfg->theme;
    ThemePair tp = t->elem_normal;
    if (primary_cta) {
        tp = t->elem_selected;
        if (enabled && ui->pressed_id == id) {
            tp.bg = mix_color(t->elem_selected.bg, (ColorRGBA){0, 0, 0, 255}, 0.18);
        } else if (enabled && ui->hover_id == id) {
            tp.bg = mix_color(t->elem_selected.bg, (ColorRGBA){255, 255, 255, 255}, 0.14);
        }
    } else if (active) {
        tp = t->elem_selected;
    } else if (enabled && ui->pressed_id == id) {
        tp = t->elem_pressed;
    } else if (enabled && ui->hover_id == id) {
        tp = t->elem_hover;
    }

    double alpha = enabled ? 1.0 : 0.4;
    draw_rounded_rect(cr, r, 6.0);
    set_cairo_color(cr, tp.bg, alpha);
    cairo_fill_preserve(cr);
    set_cairo_color(cr, (active || primary_cta) ? tp.bg : t->base.fg,
                    (active || primary_cta) ? alpha * 0.9 : alpha * 0.25);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    double icon_sz = fmin(r.h - 10.0, fmax(13.0, ui->cfg->font_size * 1.05));
    if (icon != ICON_NONE && (!label || !label[0])) {
        Rect ib = {r.x + (r.w - icon_sz) * 0.5, r.y + (r.h - icon_sz) * 0.5, icon_sz, icon_sz};
        draw_icon(cr, icon, ib, tp.fg, alpha);
    } else if (icon != ICON_NONE && label && label[0]) {
        double tw = 0.0;
        measure_text_size(ui, label, 0.92, (active || primary_cta) ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL, &tw, NULL);
        double gap = 5.0;
        double total = icon_sz + gap + tw;
        if (total > r.w - 10.0) total = r.w - 10.0;
        double sx = r.x + (r.w - total) * 0.5;
        Rect ib = {sx, r.y + (r.h - icon_sz) * 0.5, icon_sz, icon_sz};
        draw_icon(cr, icon, ib, tp.fg, alpha);
        double tx = sx + icon_sz + gap;
        double avail = (r.x + r.w - 4.0) - tx;
        draw_text_line(cr, ui, label, tx, r.y, avail, r.h,
                       tp.fg, alpha, 0.92, active || primary_cta, PANGO_ALIGN_LEFT, true);
    } else if (label && label[0]) {
        draw_text_line(cr, ui, label, r.x + 6.0, r.y, r.w - 12.0, r.h,
                       tp.fg, alpha, 0.92, active || primary_cta, PANGO_ALIGN_CENTER, true);
    }
}

static double row_height(const UiState *ui) {
    int th = ui->cfg->thumb_size;
    double min_text_h = ui->cfg->font_size * (ui->cfg->show_extra_metadata ? 4.5 : 3.5) + 16.0;
    double h = (double)th + 16.0;
    return h > min_text_h ? h : min_text_h;
}

static double measure_btn_width(const UiState *ui, bool with_icon, const char *label, double min_w) {
    double icon_sz = fmax(13.0, ui->cfg->font_size * 1.05);
    double tw = 0.0;
    if (label && label[0]) {
        measure_text_size(ui, label, 0.92, PANGO_WEIGHT_SEMIBOLD, &tw, NULL);
    }
    double w = 14.0 + (with_icon ? icon_sz : 0.0) + (with_icon && label && label[0] ? 6.0 : 0.0) + tw;
    w = align4(w);
    return w > min_w ? w : min_w;
}

static const char *sort_label(SearchSort s) {
    switch (s) {
        case SORT_UPLOAD_DATE: return "Sort: Date";
        case SORT_VIEW_COUNT:  return "Sort: Views";
        case SORT_RATING:      return "Sort: Rating";
        default:               return "Sort: Relevance";
    }
}

static const char *type_label(SearchType t) {
    switch (t) {
        case TYPE_VIDEO:    return "Type: Video";
        case TYPE_CHANNEL:  return "Type: Channel";
        case TYPE_PLAYLIST: return "Type: Playlist";
        default:            return "Type: All";
    }
}

static void compute_layout(const UiState *ui, Layout *L) {
    Layout prev = ui->last_layout;
    bool had_prev = ui->has_last_layout;
    memset(L, 0, sizeof(*L));

    double W = 1024.0, H = 640.0;
    logical_viewport(ui, &W, &H);
    double y = 0.0;

    L->has_csd = wayland_needs_csd(ui->wl);
    if (L->has_csd) {
        double csd_h = fmax(28.0, align4(ui->cfg->font_size * 2.1));
        double bw = csd_h - 2.0;
        double bh = csd_h - 8.0;
        L->csd_bar = (Rect){0, 0, W, csd_h};
        L->csd_close = (Rect){W - bw - 6.0, 4.0, bw, bh};
        L->csd_max   = (Rect){W - (bw + 4.0) * 2.0 - 2.0, 4.0, bw, bh};
        L->csd_min   = (Rect){W - (bw + 4.0) * 3.0 - 2.0, 4.0, bw, bh};
        y += csd_h;
    }

    double tb_h = fmax(42.0, align4(ui->cfg->font_size * 2.7));
    L->toolbar = (Rect){0, y, W, tb_h};
    double pad = SP_SM;
    double btn_h = tb_h - SP_SM - 2.0;
    double by = y + (tb_h - btn_h) * 0.5;

    bool compact = (W < 760.0);
    double w_back = btn_h;
    double w_help = btn_h;
    double w_go   = compact ? btn_h : measure_btn_width(ui, false, "Search", 60.0);
    double w_flt  = compact ? btn_h : measure_btn_width(ui, true, "Filters", 64.0);
    double w_feed = compact ? btn_h : measure_btn_width(ui, true, "Feed", 58.0);
    double w_fav  = compact ? btn_h : measure_btn_width(ui, true, "Favorites", 78.0);
    double w_hist = compact ? btn_h : measure_btn_width(ui, true, "History", 68.0);

    double sep_gap = SP_SM + 1.0;
    double right_block = w_go + SP_XS + w_flt + sep_gap + w_feed + SP_XS + w_fav + SP_XS + w_hist + SP_XS + w_help;
    if (!compact && (W - pad * 2.0 - w_back - SP_XS - right_block) < 150.0) {
        compact = true;
        w_go   = btn_h;
        w_flt  = btn_h;
        w_feed = btn_h;
        w_fav  = btn_h;
        w_hist = btn_h;
    }
    L->compact_toolbar = compact;

    L->btn_back = (Rect){pad, by, w_back, btn_h};

    double rx = W - pad;
    L->btn_help    = (Rect){rx - w_help, by, w_help, btn_h}; rx -= w_help + SP_XS;
    L->btn_history = (Rect){rx - w_hist, by, w_hist, btn_h}; rx -= w_hist + SP_XS;
    L->btn_favs    = (Rect){rx - w_fav,  by, w_fav,  btn_h}; rx -= w_fav + SP_XS;
    L->btn_feed    = (Rect){rx - w_feed, by, w_feed, btn_h}; rx -= w_feed + SP_SM;
    L->toolbar_sep = (Rect){rx, by + 4.0, 1.0, btn_h - 8.0}; rx -= SP_SM;
    L->btn_filters = (Rect){rx - w_flt,  by, w_flt,  btn_h}; rx -= w_flt + SP_XS;
    L->btn_go      = (Rect){rx - w_go,   by, w_go,   btn_h}; rx -= w_go + SP_XS;

    double sb_x = L->btn_back.x + L->btn_back.w + SP_XS;
    double sb_w = rx - sb_x;
    if (sb_w < 80.0) sb_w = 80.0;
    L->search_box = (Rect){sb_x, by, sb_w, btn_h};
    double clr_sz = btn_h - 6.0;
    if (clr_sz < 18.0) clr_sz = 18.0;
    L->btn_search_clear = (Rect){L->search_box.x + L->search_box.w - clr_sz - 4.0,
                                 L->search_box.y + (L->search_box.h - clr_sz) * 0.5,
                                 clr_sz, clr_sz};
    y += tb_h;

    if (ui->show_filters) {
        double fb_h = fmax(34.0, align4(ui->cfg->font_size * 2.2));
        L->filter_bar = (Rect){0, y, W, fb_h};
        double fy = y + SP_XS, fh = fb_h - SP_SM;
        double fx = pad;
        double w_sort = measure_btn_width(ui, false, sort_label(ui->filter.sort), 120.0);
        double w_type = measure_btn_width(ui, false, type_label(ui->filter.type), 108.0);
        double w_live = measure_btn_width(ui, false, ui->filter.live_only ? "Live: On" : "Live: Off", 80.0);
        L->btn_sort = (Rect){fx, fy, w_sort, fh}; fx += w_sort + SP_SM;
        L->btn_type = (Rect){fx, fy, w_type, fh}; fx += w_type + SP_SM;
        L->btn_live = (Rect){fx, fy, w_live, fh};
        y += fb_h;
    }

    double status_h = fmax(26.0, align4(ui->cfg->font_size * 1.8));
    L->status_bar = (Rect){0, H - status_h, W, status_h};
    double body_h = H - y - status_h;
    if (body_h < 60.0) body_h = 60.0;

    double split_w = 5.0;
    double lw = (double)ui->cfg->playlist_width;
    double max_lw = W - 240.0;
    if (W < 720.0) {
        double lim = W * 0.52;
        if (lim < max_lw) max_lw = lim;
    }
    if (max_lw < 180.0) max_lw = W * 0.5;
    if (lw < 180.0) lw = 180.0;
    if (lw > max_lw) lw = max_lw;

    L->left_pane = (Rect){0, y, lw, body_h};
    double hdr_h = fmax(32.0, align4(ui->cfg->font_size * 2.1));
    L->left_header = (Rect){0, y, lw, hdr_h};
    double hdr_right = lw - SP_SM;
    if (ui->list.continuation_token && ui->list.continuation_token[0]) {
        double nw = measure_btn_width(ui, false, "Next Page", 76.0);
        if (nw > lw * 0.42) nw = lw * 0.42;
        L->btn_next_page = (Rect){hdr_right - nw, y + 3.0, nw, hdr_h - 6.0};
        hdr_right = L->btn_next_page.x - SP_XS;
    }
    if (ui->show_channel_tabs) {
        double vw = measure_btn_width(ui, false, "Videos", 54.0);
        double lvw = measure_btn_width(ui, false, "Live", 44.0);
        if (vw + SP_XS + lvw < lw - 60.0) {
            L->btn_chan_live = (Rect){hdr_right - lvw, y + 3.0, lvw, hdr_h - 6.0};
            L->btn_chan_videos = (Rect){L->btn_chan_live.x - SP_XS - vw, y + 3.0, vw, hdr_h - 6.0};
        }
    }

    double sb_bar_w = 12.0;
    L->list_area = (Rect){0, y + hdr_h, lw - sb_bar_w, body_h - hdr_h};
    L->scrollbar = (Rect){lw - sb_bar_w, y + hdr_h, sb_bar_w, body_h - hdr_h};

    if (ui->list.count == 0 && ui->error_text[0] != '\0') {
        double rw = measure_btn_width(ui, true, "Retry", 92.0);
        double rh_btn = fmax(32.0, align4(ui->cfg->font_size * 2.1));
        L->btn_retry = (Rect){
            L->list_area.x + (L->list_area.w - rw) * 0.5,
            L->list_area.y + L->list_area.h * 0.5 + SP_LG,
            rw, rh_btn
        };
    }

    L->splitter = (Rect){lw, y, split_w, body_h};
    L->right_pane = (Rect){lw + split_w, y, W - (lw + split_w), body_h};
    if (L->right_pane.w < 0.0) L->right_pane.w = 0.0;

    if (ui->show_help) {
        double mw = fmin(540.0, W - 40.0);
        double mh = fmin(548.0, H - 40.0);
        if (mw < 260.0) mw = fmax(200.0, W - 16.0);
        if (mh < 220.0) mh = fmax(180.0, H - 16.0);
        L->help_modal_box = (Rect){(W - mw) * 0.5, (H - mh) * 0.5, mw, mh};
        L->help_close_btn = (Rect){L->help_modal_box.x + mw - 34.0,
                                   L->help_modal_box.y + 10.0, 24.0, 24.0};
    }

    if (had_prev && fabs(prev.right_pane.x - L->right_pane.x) < 0.5 &&
        fabs(prev.right_pane.w - L->right_pane.w) < 0.5 &&
        fabs(prev.right_pane.y - L->right_pane.y) < 0.5 &&
        fabs(prev.right_pane.h - L->right_pane.h) < 0.5) {
        L->detail_thumb = prev.detail_thumb;
        L->detail_author = prev.detail_author;
        L->act_play = prev.act_play;
        L->act_download = prev.act_download;
        L->act_channel = prev.act_channel;
        L->act_recommended = prev.act_recommended;
        L->act_fav = prev.act_fav;
        L->act_copy = prev.act_copy;
        L->desc_area = prev.desc_area;
        L->desc_scrollbar = prev.desc_scrollbar;
    }
}

static bool str_list_contains(char **arr, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(arr[i], s) == 0) return true;
    }
    return false;
}

static void str_list_add(char ***arr, size_t *n, size_t *cap, const char *s) {
    if (str_list_contains(*arr, *n, s)) return;
    if (*n == *cap) {
        size_t nc = *cap ? (*cap * 2) : 32;
        char **na = realloc(*arr, nc * sizeof(char *));
        if (!na) return;
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*n)++] = tt_strdup(s);
}

static void str_list_remove(char **arr, size_t *n, const char *s) {
    for (size_t i = 0; i < *n; i++) {
        if (strcmp(arr[i], s) == 0) {
            free(arr[i]);
            if (i + 1 < *n) memmove(&arr[i], &arr[i + 1], (*n - i - 1) * sizeof(char *));
            (*n)--;
            return;
        }
    }
}

static void str_list_free(char ***arr, size_t *n, size_t *cap) {
    for (size_t i = 0; i < *n; i++) free((*arr)[i]);
    free(*arr);
    *arr = NULL;
    *n = 0;
    *cap = 0;
}

static void make_thumb_key(const MediaItem *it, char *out, size_t cap) {
    if (it->kind == ITEM_VIDEO) snprintf(out, cap, "vid_%s", it->id);
    else if (it->kind == ITEM_CHANNEL) snprintf(out, cap, "ch_%s", it->id);
    else snprintf(out, cap, "pl_%s", it->id);
}

static cairo_surface_t *acquire_thumb(UiState *ui, const MediaItem *it) {
    if (!it || !it->id[0]) return NULL;
    char key[96];
    make_thumb_key(it, key, sizeof(key));

    cairo_surface_t *s = thumb_cache_get(ui->thumbs, key);
    if (s) return s;

    if (!str_list_contains(ui->inflight_keys, ui->inflight_count, key) &&
        !str_list_contains(ui->failed_keys, ui->failed_count, key) &&
        ui->cbs.on_request_thumb) {
        char url[512];
        image_resolve_thumb_url(it->kind, it->id, it->thumbnail_url, url, sizeof(url));
        if (url[0]) {
            str_list_add(&ui->inflight_keys, &ui->inflight_count, &ui->inflight_cap, key);
            ui->cbs.on_request_thumb(ui->user_ctx, key, url);
        }
    }
    return NULL;
}

static void draw_surface_fit(cairo_t *cr, cairo_surface_t *surf, Rect box, bool cover, double radius) {
    if (!surf || box.w <= 1 || box.h <= 1) return;
    int sw = cairo_image_surface_get_width(surf);
    int sh = cairo_image_surface_get_height(surf);
    if (sw <= 0 || sh <= 0) return;

    double sx = box.w / sw;
    double sy = box.h / sh;
    double sc = cover ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
    double dw = sw * sc;
    double dh = sh * sc;
    double dx = box.x + (box.w - dw) * 0.5;
    double dy = box.y + (box.h - dh) * 0.5;

    cairo_save(cr);
    if (radius > 0.0) draw_rounded_rect(cr, box, radius);
    else cairo_rectangle(cr, box.x, box.y, box.w, box.h);
    cairo_clip(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static UiIcon placeholder_icon_for_kind(ItemKind kind) {
    if (kind == ITEM_CHANNEL) return ICON_CHANNEL;
    if (kind == ITEM_PLAYLIST) return ICON_PLAYLIST;
    return ICON_PLAY;
}

static void draw_thumb_placeholder(cairo_t *cr, const Theme *t, Rect box, ItemKind kind) {
    double isz = fmin(box.w, box.h) * 0.42;
    if (isz > 36.0) isz = 36.0;
    if (isz < 12.0) isz = 12.0;
    Rect ib = {box.x + (box.w - isz) * 0.5, box.y + (box.h - isz) * 0.5, isz, isz};
    draw_icon(cr, placeholder_icon_for_kind(kind), ib, t->base.fg, 0.28);
}

static void ime_clear_preedit(UiState *ui) {
    if (!ui) return;
    free(ui->ime_preedit);
    ui->ime_preedit = NULL;
    ui->ime_preedit_len = 0;
    ui->ime_cursor_begin = 0;
    ui->ime_cursor_end = 0;
}

static void ime_sync_surrounding(UiState *ui, bool external_change) {
    if (!ui || !ui->wl) return;
    int32_t cursor = ui->search_sel_all ? (int32_t)ui->search_len : (int32_t)ui->search_cursor;
    int32_t anchor = ui->search_sel_all ? 0 : (int32_t)ui->search_cursor;
    wayland_ime_set_surrounding(ui->wl, ui->search_buf ? ui->search_buf : "",
                                cursor, anchor, external_change);
}

static void set_focus_zone(UiState *ui, FocusZone zone) {
    if (!ui) return;
    FocusZone prev = ui->focus_zone;
    if (prev == FOCUS_SEARCH && zone != FOCUS_SEARCH) {
        ime_clear_preedit(ui);
        wayland_ime_set_enabled(ui->wl, false);
    }
    ui->focus_zone = zone;
    if (zone == FOCUS_SEARCH && !ui->show_help) {
        wayland_ime_set_enabled(ui->wl, true);
        ime_sync_surrounding(ui, true);
    }
}

UiState *ui_create(TuiTubeConfig *cfg, WaylandApp *wl, ThumbCache *thumbs,
                   FavoriteList *favs, HistoryList *history,
                   const UiCallbacks *cbs, void *user_ctx) {
    UiState *ui = calloc(1, sizeof(UiState));
    if (!ui) return NULL;
    ui->cfg = cfg;
    ui->wl = wl;
    ui->thumbs = thumbs;
    ui->favs = favs;
    ui->history = history;
    if (cbs) ui->cbs = *cbs;
    ui->user_ctx = user_ctx;

    ui->win_w = 1024;
    ui->win_h = 640;
    ui->selected_idx = -1;
    ui->hover_idx = -1;
    ui->ctx_menu.hover_idx = -1;
    ui->search_cap = 256;
    ui->search_buf = calloc(1, ui->search_cap);
    ui->focus_zone = FOCUS_SEARCH;
    media_list_init(&ui->list);
    snprintf(ui->status_text, sizeof(ui->status_text), "Ready");

    PangoFontMap *fm = pango_cairo_font_map_get_default();
    PangoContext *pctx = pango_font_map_create_context(fm);
    ui->measure_pl = pango_layout_new(pctx);
    g_object_unref(pctx);

    ui_reload_font(ui);
    ui_reload_mascot(ui);
    wayland_ime_set_enabled(ui->wl, true);
    ime_sync_surrounding(ui, false);
    if (ui->cfg) {
        wayland_set_opaque(ui->wl, ui->cfg->theme.base.bg.a == 255);
    }
    return ui;
}

void ui_destroy(UiState *ui) {
    if (!ui) return;
    if (ui->measure_pl) g_object_unref(ui->measure_pl);
    free(ui->font_family);
    if (ui->mascot_surf) cairo_surface_destroy(ui->mascot_surf);
    free(ui->search_buf);
    ime_clear_preedit(ui);
    media_list_free(&ui->list);
    media_item_free_fields(&ui->detail_item);
    str_list_free(&ui->inflight_keys, &ui->inflight_count, &ui->inflight_cap);
    str_list_free(&ui->failed_keys, &ui->failed_count, &ui->failed_cap);
    free(ui);
}

void ui_reload_font(UiState *ui) {
    if (!ui) return;
    free(ui->font_family);
    ui->font_family = NULL;

    if (ui->cfg->font && ui->cfg->font[0]) {
        char *resolved = config_resolve_asset_path(ui->cfg, ui->cfg->font);
        if (resolved && tt_file_exists(resolved)) {
            FcInit();
            FcConfigAppFontAddFile(NULL, (const FcChar8 *)resolved);
            int count = 0;
            FcPattern *pat = FcFreeTypeQuery((const FcChar8 *)resolved, 0, NULL, &count);
            if (pat) {
                FcChar8 *fam = NULL;
                if (FcPatternGetString(pat, FC_FAMILY, 0, &fam) == FcResultMatch && fam) {
                    ui->font_family = tt_strdup((const char *)fam);
                }
                FcPatternDestroy(pat);
            }
        }
        free(resolved);
    }
    if (!ui->font_family) ui->font_family = tt_strdup("Sans");
    if (ui->measure_pl) pango_layout_context_changed(ui->measure_pl);
}

void ui_reload_mascot(UiState *ui) {
    if (!ui) return;
    if (ui->mascot_surf) {
        cairo_surface_destroy(ui->mascot_surf);
        ui->mascot_surf = NULL;
    }
    if (ui->cfg->mascot && ui->cfg->mascot[0]) {
        char *resolved = config_resolve_asset_path(ui->cfg, ui->cfg->mascot);
        if (resolved) {
            ui->mascot_surf = image_load_cairo_file(resolved);
            free(resolved);
        }
    }
}

void ui_set_status(UiState *ui, const char *fmt, ...) {
    if (!ui || !fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->status_text, sizeof(ui->status_text), fmt, ap);
    va_end(ap);
    wayland_request_redraw(ui->wl);
}

static void sync_detail_from_selection(UiState *ui) {
    media_item_free_fields(&ui->detail_item);
    ui->has_detail_item = false;
    ui->desc_scroll_y = 0.0;
    if (ui->selected_idx >= 0 && (size_t)ui->selected_idx < ui->list.count) {
        media_item_copy(&ui->detail_item, &ui->list.items[ui->selected_idx]);
        ui->has_detail_item = true;
    }
}

static void mark_watched_from_history(UiState *ui, MediaItem *items, size_t count) {
    if (!ui || !ui->history) return;
    for (size_t i = 0; i < count; i++) {
        if (items[i].kind == ITEM_VIDEO && history_contains(ui->history, items[i].id)) {
            items[i].watched = true;
        }
    }
}

void ui_set_list(UiState *ui, MediaList *new_list, bool append) {
    if (!ui || !new_list) return;
    ui->loading_more = false;
    ui->ctx_menu.open = false;

    if (append) {
        for (size_t i = 0; i < new_list->count; i++) {
            MediaItem *it = &new_list->items[i];
            if (it->kind == ITEM_VIDEO && history_contains(ui->history, it->id)) {
                it->watched = true;
            }
            media_list_append_move(&ui->list, it);
        }
        free(ui->list.continuation_token);
        ui->list.continuation_token = tt_strdup(new_list->continuation_token);
        media_list_free(new_list);
    } else {
        media_list_free(&ui->list);
        ui->list = *new_list;
        memset(new_list, 0, sizeof(*new_list));
        mark_watched_from_history(ui, ui->list.items, ui->list.count);
        ui->list_scroll_y = 0.0;
        ui->selected_idx = ui->list.count > 0 ? 0 : -1;
        ui->hover_idx = -1;
        sync_detail_from_selection(ui);
    }
    wayland_request_redraw(ui->wl);
}

static void replace_if_nonempty(char **dst, const char *src) {
    if (src && src[0]) {
        free(*dst);
        *dst = tt_strdup(src);
    }
}

static void merge_media_details(MediaItem *dst, const MediaItem *src) {
    if (!dst || !src) return;
    replace_if_nonempty(&dst->title, src->title);
    replace_if_nonempty(&dst->author, src->author);
    replace_if_nonempty(&dst->author_id, src->author_id);
    replace_if_nonempty(&dst->view_count_text, src->view_count_text);
    replace_if_nonempty(&dst->published_text, src->published_text);
    replace_if_nonempty(&dst->description, src->description);
    if ((!dst->duration_text || !dst->duration_text[0]) && src->duration_text && src->duration_text[0]) {
        free(dst->duration_text);
        dst->duration_text = tt_strdup(src->duration_text);
    }
    if ((!dst->thumbnail_url || !dst->thumbnail_url[0]) && src->thumbnail_url && src->thumbnail_url[0]) {
        free(dst->thumbnail_url);
        dst->thumbnail_url = tt_strdup(src->thumbnail_url);
    }
    dst->is_live = dst->is_live || src->is_live;
    dst->is_members_only = dst->is_members_only || src->is_members_only;
    dst->details_loaded = true;
}

void ui_set_detail_item(UiState *ui, const MediaItem *detail) {
    if (!ui || !detail) return;
    bool sel_matches = (ui->selected_idx >= 0 && (size_t)ui->selected_idx < ui->list.count &&
                        strcmp(ui->list.items[ui->selected_idx].id, detail->id) == 0);
    if (ui->view_kind != UI_VIEW_NEXT && !sel_matches) {
        return;
    }

    if (sel_matches) {
        MediaItem *sel = &ui->list.items[ui->selected_idx];
        merge_media_details(sel, detail);
        if (!ui->has_detail_item || strcmp(ui->detail_item.id, detail->id) != 0) {
            media_item_free_fields(&ui->detail_item);
            media_item_copy(&ui->detail_item, sel);
            ui->has_detail_item = true;
        } else {
            merge_media_details(&ui->detail_item, detail);
        }
    } else {
        if (!ui->has_detail_item || strcmp(ui->detail_item.id, detail->id) != 0) {
            media_item_free_fields(&ui->detail_item);
            media_item_copy(&ui->detail_item, detail);
            ui->has_detail_item = true;
        } else {
            merge_media_details(&ui->detail_item, detail);
        }
        ui->detail_item.details_loaded = true;
    }
    ui->desc_scroll_y = 0.0;
    wayland_request_redraw(ui->wl);
}

void ui_mark_video_watched(UiState *ui, const char *video_id) {
    if (!ui || !video_id) return;
    for (size_t i = 0; i < ui->list.count; i++) {
        if (ui->list.items[i].kind == ITEM_VIDEO && strcmp(ui->list.items[i].id, video_id) == 0) {
            ui->list.items[i].watched = true;
        }
    }
    wayland_request_redraw(ui->wl);
}

void ui_on_thumb_loaded(UiState *ui, const char *key, cairo_surface_t *surf) {
    if (!ui || !key) return;
    str_list_remove(ui->inflight_keys, &ui->inflight_count, key);
    if (surf) {
        thumb_cache_put(ui->thumbs, key, surf);
    } else {
        str_list_add(&ui->failed_keys, &ui->failed_count, &ui->failed_cap, key);
    }
    wayland_request_redraw(ui->wl);
}

void ui_set_loading_more(UiState *ui, bool loading) {
    if (!ui) return;
    ui->loading_more = loading;
    wayland_request_redraw(ui->wl);
}

void ui_set_nav_state(UiState *ui, UiViewKind view_kind, bool can_go_back) {
    if (!ui) return;
    ui->view_kind = view_kind;
    ui->can_go_back = can_go_back;
    wayland_request_redraw(ui->wl);
}

void ui_set_channel_tabs(UiState *ui, bool show_tabs, bool live_tab) {
    if (!ui) return;
    ui->show_channel_tabs = show_tabs;
    ui->channel_live_tab = live_tab;
    wayland_request_redraw(ui->wl);
}

void ui_set_busy(UiState *ui, bool busy) {
    if (!ui) return;
    ui->busy = busy;
    wayland_request_redraw(ui->wl);
}

void ui_set_error(UiState *ui, const char *err_msg) {
    if (!ui) return;
    if (err_msg && err_msg[0]) {
        snprintf(ui->error_text, sizeof(ui->error_text), "%s", err_msg);
    } else {
        ui->error_text[0] = '\0';
    }
    wayland_request_redraw(ui->wl);
}

MediaList *ui_get_list(UiState *ui) { return ui ? &ui->list : NULL; }

const MediaItem *ui_get_selected_item(const UiState *ui) {
    if (!ui || ui->selected_idx < 0 || (size_t)ui->selected_idx >= ui->list.count) return NULL;
    return &ui->list.items[ui->selected_idx];
}

const MediaItem *ui_get_detail_item(const UiState *ui) {
    if (!ui) return NULL;
    if (ui->has_detail_item) return &ui->detail_item;
    return ui_get_selected_item(ui);
}

SearchFilter ui_get_filter(const UiState *ui) {
    return ui ? ui->filter : (SearchFilter){0};
}

void ui_set_search_text(UiState *ui, const char *text) {
    if (!ui) return;
    const char *s = text ? text : "";
    size_t len = strlen(s);
    if (len + 1 > ui->search_cap) {
        size_t nc = len + 64;
        char *nb = realloc(ui->search_buf, nc);
        if (!nb) return;
        ui->search_buf = nb;
        ui->search_cap = nc;
    }
    memcpy(ui->search_buf, s, len + 1);
    ui->search_len = len;
    ui->search_cursor = len;
    ui->search_sel_all = false;
    ime_clear_preedit(ui);
    ime_sync_surrounding(ui, true);
    wayland_request_redraw(ui->wl);
}

double ui_get_list_scroll(const UiState *ui) {
    return ui ? ui->list_scroll_y : 0.0;
}

ssize_t ui_get_selected_index(const UiState *ui) {
    return ui ? ui->selected_idx : -1;
}

static void clamp_list_scroll(UiState *ui, const Layout *L) {
    double total_h = (double)ui->list.count * row_height(ui);
    double max_s = total_h - L->list_area.h;
    if (max_s < 0.0) max_s = 0.0;
    if (ui->list_scroll_y < 0.0) ui->list_scroll_y = 0.0;
    if (ui->list_scroll_y > max_s) ui->list_scroll_y = max_s;
}

void ui_restore_list_state(UiState *ui, MediaList *restored, ssize_t selected_idx, double scroll_y) {
    if (!ui || !restored) return;
    ui->loading_more = false;
    ui->busy = false;
    ui->error_text[0] = '\0';
    ui->ctx_menu.open = false;

    media_list_free(&ui->list);
    ui->list = *restored;
    memset(restored, 0, sizeof(*restored));
    mark_watched_from_history(ui, ui->list.items, ui->list.count);

    if (ui->list.count == 0) {
        ui->selected_idx = -1;
    } else if (selected_idx < 0) {
        ui->selected_idx = 0;
    } else if ((size_t)selected_idx >= ui->list.count) {
        ui->selected_idx = (ssize_t)ui->list.count - 1;
    } else {
        ui->selected_idx = selected_idx;
    }

    ui->hover_idx = -1;
    ui->list_scroll_y = scroll_y < 0.0 ? 0.0 : scroll_y;
    Layout L;
    compute_layout(ui, &L);
    clamp_list_scroll(ui, &L);
    sync_detail_from_selection(ui);
    wayland_request_redraw(ui->wl);
}

static void ensure_selected_visible(UiState *ui, const Layout *L) {
    if (ui->selected_idx < 0 || L->list_area.h <= 0) return;
    double rh = row_height(ui);
    double top = (double)ui->selected_idx * rh;
    double bot = top + rh;
    if (top < ui->list_scroll_y) ui->list_scroll_y = top;
    else if (bot > ui->list_scroll_y + L->list_area.h) ui->list_scroll_y = bot - L->list_area.h;
    clamp_list_scroll(ui, L);
}

static Rect scrollbar_thumb_rect(const UiState *ui, const Layout *L) {
    double total_h = (double)ui->list.count * row_height(ui);
    if (total_h <= L->list_area.h || L->scrollbar.h <= 4) {
        return (Rect){L->scrollbar.x + 2, L->scrollbar.y + 2, L->scrollbar.w - 4, L->scrollbar.h - 4};
    }
    double ratio = L->list_area.h / total_h;
    double th = L->scrollbar.h * ratio;
    if (th < 24.0) th = 24.0;
    double max_s = total_h - L->list_area.h;
    double pos = (max_s > 0) ? (ui->list_scroll_y / max_s) : 0.0;
    double ty = L->scrollbar.y + pos * (L->scrollbar.h - th);
    return (Rect){L->scrollbar.x + 2, ty, L->scrollbar.w - 4, th};
}

static Rect desc_scrollbar_thumb_rect(const UiState *ui, Rect track) {
    double total_h = track.h + ui->desc_max_scroll;
    if (total_h <= track.h || ui->desc_max_scroll <= 0.0 || track.h <= 4.0) {
        return track;
    }
    double ratio = track.h / total_h;
    double th = track.h * ratio;
    if (th < 24.0) th = 24.0;
    double pos = ui->desc_scroll_y / ui->desc_max_scroll;
    double ty = track.y + pos * (track.h - th);
    return (Rect){track.x, ty, track.w, th};
}

static void compute_right_action_rects(const UiState *ui, const Layout *L, const MediaItem *item,
                                       double top_y, Rect *play_r, Rect *dl_r, Rect *chan_r,
                                       Rect *rec_r, Rect *fav_r, Rect *copy_r, double *out_next_y) {
    double rx = L->right_pane.x + SP_LG;
    double rw = L->right_pane.w - SP_LG * 2.0;
    double bh = fmax(30.0, align4(ui->cfg->font_size * 2.0));
    double y = top_y;
    double x = rx;
    double gap = SP_SM;

    const char *play_lbl = (item->kind == ITEM_VIDEO) ? "Play" : "Open";
    double w_play = measure_btn_width(ui, true, play_lbl, 76.0);
    *play_r = (Rect){x, y, w_play, bh};
    x += w_play + gap;

    if (item->kind == ITEM_VIDEO) {
        double dl_prog = -1.0;
        bool dl_active = player_get_download_progress(item->id, &dl_prog);
        double w_dl = 0.0;
        if (dl_active) {
            double icon_sz = fmax(13.0, ui->cfg->font_size * 1.05);
            double tw_dl = 0.0;
            measure_text_size(ui, "Downloading 100%", 0.92, PANGO_WEIGHT_BOLD, &tw_dl, NULL);
            w_dl = align4(18.0 + icon_sz + 6.0 + tw_dl);
            if (w_dl < 136.0) w_dl = 136.0;
        } else {
            w_dl = measure_btn_width(ui, true, "Download", 94.0);
        }
        if (x + w_dl > rx + rw) { x = rx; y += bh + gap; }
        *dl_r = (Rect){x, y, w_dl, bh};
        x += w_dl + gap;
    } else {
        *dl_r = (Rect){0};
    }

    if (item->kind == ITEM_CHANNEL || (item->author_id && item->author_id[0])) {
        double w_ch = measure_btn_width(ui, true, "Channel", 88.0);
        if (x + w_ch > rx + rw) { x = rx; y += bh + gap; }
        *chan_r = (Rect){x, y, w_ch, bh};
        x += w_ch + gap;
    } else {
        *chan_r = (Rect){0};
    }

    if (item->kind == ITEM_VIDEO) {
        double w_rec = measure_btn_width(ui, true, "Recommended", 118.0);
        if (x + w_rec > rx + rw) { x = rx; y += bh + gap; }
        *rec_r = (Rect){x, y, w_rec, bh};
        x += w_rec + gap;
    } else {
        *rec_r = (Rect){0};
    }

    if (item->kind == ITEM_CHANNEL || item->kind == ITEM_PLAYLIST ||
        (item->author_id && item->author_id[0])) {
        const char *fid = (item->kind == ITEM_VIDEO) ? item->author_id : item->id;
        bool is_fav = fid && favlist_contains(ui->favs, fid);
        const char *flbl = is_fav ? "Favorited" : "Fav";
        double w_fav = measure_btn_width(ui, true, flbl, 76.0);
        if (x + w_fav > rx + rw) { x = rx; y += bh + gap; }
        *fav_r = (Rect){x, y, w_fav, bh};
        x += w_fav + gap;
    } else {
        *fav_r = (Rect){0};
    }

    double w_copy = measure_btn_width(ui, true, "Copy URL", 94.0);
    if (x + w_copy > rx + rw) { x = rx; y += bh + gap; }
    *copy_r = (Rect){x, y, w_copy, bh};
    *out_next_y = y + bh + SP_MD;
}

static void draw_badge_pill(cairo_t *cr, const UiState *ui, double x, double y,
                            double max_w, const char *text,
                            double r, double g, double b, double a,
                            bool align_right_edge, double right_x) {
    if (!text || !text[0] || max_w < 14.0) return;
    double tw = 0.0, th = 0.0;
    measure_text_size(ui, text, 0.78, PANGO_WEIGHT_BOLD, &tw, &th);
    double bw = tw + 10.0;
    if (bw > max_w) bw = max_w;
    double bh = th + 4.0;
    double bx = align_right_edge ? (right_x - bw) : x;
    Rect br = {bx, y, bw, bh};
    draw_rounded_rect(cr, br, 4.0);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_fill(cr);
    draw_text_line(cr, ui, text, br.x + 4.0, br.y, br.w - 8.0, br.h,
                   (ColorRGBA){255, 255, 255, 255}, 1.0, 0.78, true,
                   PANGO_ALIGN_CENTER, true);
}

static void render_right_pane(UiState *ui, cairo_t *cr, Layout *L) {
    const Theme *t = &ui->cfg->theme;
    Rect rp = L->right_pane;
    L->detail_thumb = (Rect){0};
    L->detail_author = (Rect){0};
    L->act_play = (Rect){0};
    L->act_download = (Rect){0};
    L->act_channel = (Rect){0};
    L->act_recommended = (Rect){0};
    L->act_fav = (Rect){0};
    L->act_copy = (Rect){0};
    L->desc_area = (Rect){0};
    L->desc_scrollbar = (Rect){0};

    if (rp.w <= 2 || rp.h <= 2) return;

    cairo_save(cr);
    cairo_rectangle(cr, rp.x, rp.y, rp.w, rp.h);
    cairo_clip(cr);

    set_cairo_color(cr, t->text_normal.bg, 1.0);
    cairo_rectangle(cr, rp.x, rp.y, rp.w, rp.h);
    cairo_fill(cr);

    const MediaItem *item = ui_get_detail_item(ui);
    if (!item) {
        if (ui->mascot_surf) {
            double mw = rp.w * 0.55;
            double mh = rp.h * 0.55;
            if (mw > 240) mw = 240;
            if (mh > 240) mh = 240;
            draw_surface_fit(cr, ui->mascot_surf,
                             (Rect){rp.x + (rp.w - mw) * 0.5, rp.y + (rp.h - mh) * 0.5, mw, mh},
                             false, 8.0);
        } else {
            double isz = 36.0;
            draw_icon(cr, ICON_PLAY,
                      (Rect){rp.x + (rp.w - isz) * 0.5, rp.y + rp.h * 0.5 - isz - SP_SM, isz, isz},
                      t->base.fg, 0.25);
            draw_text_line(cr, ui, "Select a video or search YouTube",
                           rp.x + SP_LG, rp.y + rp.h * 0.5, rp.w - SP_LG * 2.0, ui->cfg->font_size * 2.0,
                           t->text_normal.fg, 0.60, 1.05, false, PANGO_ALIGN_CENTER, false);
        }
        cairo_restore(cr);
        return;
    }

    /* Mascot subtly at bottom-right behind description */
    if (ui->mascot_surf) {
        double msz = 140.0;
        if (rp.w < 320 || rp.h < 320) msz = 90.0;
        cairo_save(cr);
        cairo_rectangle(cr, rp.x + rp.w - msz - SP_MD, rp.y + rp.h - msz - SP_MD, msz, msz);
        cairo_clip(cr);
        int sw = cairo_image_surface_get_width(ui->mascot_surf);
        int sh = cairo_image_surface_get_height(ui->mascot_surf);
        if (sw > 0 && sh > 0) {
            double sc = fmin(msz / sw, msz / sh);
            double dw = sw * sc, dh = sh * sc;
            cairo_translate(cr, rp.x + rp.w - dw - SP_MD, rp.y + rp.h - dh - SP_MD);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, ui->mascot_surf, 0, 0);
            cairo_paint_with_alpha(cr, 0.85);
        }
        cairo_restore(cr);
    }

    double pad = SP_LG;
    double y = rp.y + pad;
    double cw = rp.w - pad * 2.0;

    /* 16:9 Hero Thumbnail with 8px rounded corners */
    double max_th = fmin(210.0, rp.h * 0.34);
    if (max_th < 90.0) max_th = 90.0;
    double tw = max_th * 16.0 / 9.0;
    if (tw > cw) {
        tw = cw;
        max_th = tw * 9.0 / 16.0;
    }
    Rect thumb_box = {rp.x + pad, y, tw, max_th};
    L->detail_thumb = thumb_box;

    draw_rounded_rect(cr, thumb_box, 8.0);
    set_cairo_color(cr, t->base.bg, 1.0);
    cairo_fill(cr);

    cairo_surface_t *thumb = acquire_thumb(ui, item);
    if (thumb) {
        draw_surface_fit(cr, thumb, thumb_box, true, 8.0);
    } else {
        draw_thumb_placeholder(cr, t, thumb_box, item->kind);
    }

    if (item->kind == ITEM_VIDEO) {
        bool thumb_hot = (ui->hover_id == HIT_DETAIL_THUMB || ui->pressed_id == HIT_DETAIL_THUMB);
        double diam = thumb_hot ? 44.0 : 36.0;
        double cx = thumb_box.x + thumb_box.w * 0.5;
        double cy = thumb_box.y + thumb_box.h * 0.5;
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, diam * 0.5, 0.0, 2.0 * M_PI);
        cairo_set_source_rgba(cr, 0.05, 0.06, 0.09, thumb_hot ? 0.86 : 0.72);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, thumb_hot ? 0.75 : 0.35);
        cairo_set_line_width(cr, thumb_hot ? 1.5 : 1.0);
        cairo_stroke(cr);

        double isz = diam * 0.44;
        draw_icon(cr, ICON_PLAY, (Rect){cx - isz * 0.44, cy - isz * 0.5, isz, isz},
                  (ColorRGBA){255, 255, 255, 255}, thumb_hot ? 1.0 : 0.92);
    }

    draw_rounded_rect(cr, thumb_box, 8.0);
    set_cairo_color(cr, ui->hover_id == HIT_DETAIL_THUMB ? t->elem_selected.bg : t->base.fg,
                    ui->hover_id == HIT_DETAIL_THUMB ? 0.85 : 0.25);
    cairo_set_line_width(cr, ui->hover_id == HIT_DETAIL_THUMB ? 1.5 : 1.0);
    cairo_stroke(cr);
    y += max_th + SP_MD;

    /* Title (wrapped, 1.22x bold) */
    if (item->title && item->title[0]) {
        PangoLayout *tl = pango_cairo_create_layout(cr);
        PangoFontDescription *td = make_font_desc(ui, 1.22, true);
        pango_layout_set_font_description(tl, td);
        pango_font_description_free(td);
        pango_layout_set_text(tl, item->title, -1);
        pango_layout_set_width(tl, (int)(cw * PANGO_SCALE));
        pango_layout_set_wrap(tl, PANGO_WRAP_WORD_CHAR);
        set_cairo_color(cr, t->text_normal.fg, 1.0);
        cairo_move_to(cr, rp.x + pad, y);
        pango_cairo_show_layout(cr, tl);
        cairo_new_path(cr);
        int pw = 0, ph = 0;
        pango_layout_get_pixel_size(tl, &pw, &ph);
        g_object_unref(tl);
        y += ph + SP_SM;
    }

    /* Clickable Author pill/link + metadata line */
    double meta_x = rp.x + pad;
    double line_h = ui->cfg->font_size * 1.6;
    bool has_author_link = (item->author && item->author[0]) &&
                           ((item->author_id && item->author_id[0]) || item->kind == ITEM_CHANNEL);
    if (item->author && item->author[0]) {
        double aw = 0.0, ah = 0.0;
        measure_text_size(ui, item->author, 0.95, PANGO_WEIGHT_SEMIBOLD, &aw, &ah);
        double pill_w = fmin(cw, aw + (has_author_link ? 14.0 : 0.0));
        double pill_h = fmax(line_h, ah + 4.0);
        if (has_author_link) {
            Rect ar = {meta_x, y, pill_w, pill_h};
            L->detail_author = ar;
            bool ahot = (ui->hover_id == HIT_DETAIL_AUTHOR || ui->pressed_id == HIT_DETAIL_AUTHOR);
            draw_rounded_rect(cr, ar, 4.0);
            set_cairo_color(cr, ahot ? t->elem_hover.bg : t->elem_normal.bg, ahot ? 1.0 : 0.85);
            cairo_fill(cr);
            draw_text_line(cr, ui, item->author, ar.x + 7.0, ar.y, ar.w - 14.0, ar.h,
                           ahot ? t->elem_hover.fg : t->elem_normal.fg, 1.0, 0.95,
                           true, PANGO_ALIGN_LEFT, true);
            if (ahot) {
                set_cairo_color(cr, t->elem_selected.bg, 0.9);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, ar.x + 7.0, ar.y + ar.h - 2.0);
                cairo_line_to(cr, ar.x + ar.w - 7.0, ar.y + ar.h - 2.0);
                cairo_stroke(cr);
            }
        } else {
            draw_text_line(cr, ui, item->author, meta_x, y, pill_w, pill_h,
                           t->text_normal.fg, 0.88, 0.95, true, PANGO_ALIGN_LEFT, true);
        }
        meta_x += pill_w + SP_SM;
        line_h = pill_h;
    }

    StrBuf meta;
    sb_init(&meta);
    if (item->view_count_text && item->view_count_text[0]) sb_append(&meta, item->view_count_text);
    if (item->published_text && item->published_text[0]) {
        if (meta.len > 0) sb_append(&meta, "  •  ");
        sb_append(&meta, item->published_text);
    }
    if (item->duration_text && item->duration_text[0]) {
        if (meta.len > 0) sb_append(&meta, "  •  ");
        sb_append(&meta, item->duration_text);
    }
    if (meta.len > 0) {
        double rem_w = (rp.x + pad + cw) - meta_x;
        if (rem_w < 120.0 && item->author && item->author[0]) {
            y += line_h + SP_XS;
            meta_x = rp.x + pad;
            rem_w = cw;
        }
        draw_text_line(cr, ui, meta.data, meta_x, y, rem_w, line_h,
                       t->text_normal.fg, 0.78, 0.92, false, PANGO_ALIGN_LEFT, true);
        y += line_h + SP_SM;
    } else if (item->author && item->author[0]) {
        y += line_h + SP_SM;
    }
    sb_free(&meta);

    /* Action buttons */
    double desc_y = y;
    compute_right_action_rects(ui, L, item, y,
                               &L->act_play, &L->act_download, &L->act_channel,
                               &L->act_recommended, &L->act_fav,
                               &L->act_copy, &desc_y);

    draw_button_ex(cr, ui, L->act_play, ICON_PLAY,
                   item->kind == ITEM_VIDEO ? "Play" : "Open",
                   HIT_ACT_PLAY, false, true, true);
    if (L->act_download.w > 0) {
        double dl_prog = -1.0;
        bool dl_active = player_get_download_progress(item->id, &dl_prog);
        if (dl_active) {
            char dl_lbl[32];
            if (dl_prog >= 0.0) {
                int pct = (int)lround(dl_prog * 100.0);
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                snprintf(dl_lbl, sizeof(dl_lbl), "Downloading %d%%", pct);
            } else {
                snprintf(dl_lbl, sizeof(dl_lbl), "Downloading...");
            }
            draw_download_progress_button(cr, ui, L->act_download, dl_lbl, dl_prog);
        } else {
            draw_button_ex(cr, ui, L->act_download, ICON_DOWNLOAD,
                           "Download", HIT_ACT_DOWNLOAD, false, true, false);
        }
    }
    if (L->act_channel.w > 0) {
        draw_button_ex(cr, ui, L->act_channel, ICON_CHANNEL, "Channel",
                       HIT_ACT_CHANNEL, false, true, false);
    }
    if (L->act_recommended.w > 0) {
        draw_button_ex(cr, ui, L->act_recommended, ICON_SPARKLE, "Recommended",
                       HIT_ACT_RECOMMENDED, false, true, false);
    }
    if (L->act_fav.w > 0) {
        const char *fid = (item->kind == ITEM_VIDEO) ? item->author_id : item->id;
        bool is_fav = fid && favlist_contains(ui->favs, fid);
        draw_button_ex(cr, ui, L->act_fav,
                       is_fav ? ICON_STAR_FILLED : ICON_STAR,
                       is_fav ? "Favorited" : "Fav",
                       HIT_ACT_FAVORITE, is_fav, true, false);
    }
    draw_button_ex(cr, ui, L->act_copy, ICON_COPY, "Copy URL",
                   HIT_ACT_COPY_URL, false, true, false);

    /* Scrollable description */
    double desc_h = (rp.y + rp.h - SP_SM) - desc_y;
    L->desc_area = (Rect){rp.x + pad, desc_y, cw, desc_h > 0 ? desc_h : 0};
    if (desc_h > 10.0) {
        if (ui->focus_zone == FOCUS_DETAIL) {
            Rect fr = {L->desc_area.x - 4.0, L->desc_area.y - 2.0, L->desc_area.w + 8.0, L->desc_area.h + 4.0};
            draw_rounded_rect(cr, fr, 6.0);
            set_cairo_color(cr, t->elem_selected.bg, 0.9);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
        }

        if (item->description && item->description[0]) {
            double text_w = cw - 12.0;
            if (text_w < 40.0) text_w = cw;

            PangoLayout *dl = pango_cairo_create_layout(cr);
            PangoFontDescription *dd = make_font_desc(ui, 0.95, false);
            pango_layout_set_font_description(dl, dd);
            pango_font_description_free(dd);
            pango_layout_set_text(dl, item->description, -1);
            pango_layout_set_width(dl, (int)(text_w * PANGO_SCALE));
            pango_layout_set_wrap(dl, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_spacing(dl, (int)(ui->cfg->font_size * 0.35 * PANGO_SCALE));

            int dw = 0, dh = 0;
            pango_layout_get_pixel_size(dl, &dw, &dh);
            ui->desc_max_scroll = (double)dh - desc_h;
            if (ui->desc_max_scroll < 0.0) ui->desc_max_scroll = 0.0;
            if (ui->desc_scroll_y > ui->desc_max_scroll) ui->desc_scroll_y = ui->desc_max_scroll;
            if (ui->desc_scroll_y < 0.0) ui->desc_scroll_y = 0.0;

            cairo_save(cr);
            cairo_rectangle(cr, L->desc_area.x, L->desc_area.y, text_w, L->desc_area.h);
            cairo_clip(cr);
            set_cairo_color(cr, t->text_normal.fg, 0.92);
            cairo_move_to(cr, L->desc_area.x, L->desc_area.y - ui->desc_scroll_y);
            pango_cairo_show_layout(cr, dl);
            cairo_new_path(cr);
            cairo_restore(cr);
            g_object_unref(dl);

            if (ui->desc_max_scroll > 0.0) {
                Rect track = {L->desc_area.x + L->desc_area.w - 6.0, L->desc_area.y, 6.0, L->desc_area.h};
                L->desc_scrollbar = track;
                draw_rounded_rect(cr, track, 3.0);
                set_cairo_color(cr, t->scrollbar_normal.bg, 1.0);
                cairo_fill(cr);

                Rect thumb_r = desc_scrollbar_thumb_rect(ui, track);
                ThemePair sbp = t->scrollbar_normal;
                if (ui->dragging_desc_scrollbar || ui->pressed_id == HIT_DESC_SCROLLBAR) sbp = t->scrollbar_pressed;
                else if (ui->hover_id == HIT_DESC_SCROLLBAR) sbp = t->scrollbar_hover;
                draw_rounded_rect(cr, thumb_r, 3.0);
                set_cairo_color(cr, sbp.fg, 1.0);
                cairo_fill(cr);
            }
        } else {
            ui->desc_max_scroll = 0.0;
        }
    } else {
        ui->desc_max_scroll = 0.0;
    }

    cairo_restore(cr);
}

static void render_search_box(UiState *ui, cairo_t *cr, const Layout *L) {
    const Theme *t = &ui->cfg->theme;
    Rect sb = L->search_box;
    bool focused = (ui->focus_zone == FOCUS_SEARCH);
    bool has_text = (ui->search_len > 0 || ui->ime_preedit_len > 0);

    draw_rounded_rect(cr, sb, 6.0);
    set_cairo_color(cr, t->text_normal.bg, 1.0);
    cairo_fill_preserve(cr);
    set_cairo_color(cr, focused ? t->elem_selected.bg : t->base.fg, focused ? 1.0 : 0.35);
    cairo_set_line_width(cr, focused ? 2.0 : 1.0);
    cairo_stroke(cr);

    double icon_sz = fmin(sb.h - 12.0, fmax(13.0, ui->cfg->font_size * 1.0));
    draw_icon(cr, ICON_SEARCH,
              (Rect){sb.x + 10.0, sb.y + (sb.h - icon_sz) * 0.5, icon_sz, icon_sz},
              t->base.fg, focused ? 0.9 : 0.6);

    double text_left = sb.x + 10.0 + icon_sz + 8.0;
    double text_right = has_text ? (L->btn_search_clear.x - 4.0) : (sb.x + sb.w - 8.0);
    double inner_w = text_right - text_left;
    if (inner_w < 10.0) inner_w = 10.0;

    cairo_save(cr);
    cairo_rectangle(cr, text_left, sb.y + 2.0, inner_w, sb.h - 4.0);
    cairo_clip(cr);

    StrBuf composed;
    sb_init(&composed);
    const char *disp = "Search YouTube or paste URL...";
    bool is_placeholder = !has_text;
    size_t preedit_start = ui->search_cursor;
    size_t preedit_end = ui->search_cursor;
    size_t caret_byte = ui->search_cursor;
    bool hide_caret = false;
    size_t preedit_sel_a = 0;
    size_t preedit_sel_b = 0;

    if (has_text) {
        if (ui->ime_preedit_len > 0 && ui->ime_preedit) {
            sb_append_n(&composed, ui->search_buf, ui->search_cursor);
            preedit_start = composed.len;
            sb_append_n(&composed, ui->ime_preedit, ui->ime_preedit_len);
            preedit_end = composed.len;
            sb_append_n(&composed, ui->search_buf + ui->search_cursor,
                        ui->search_len - ui->search_cursor);
            disp = composed.data ? composed.data : "";

            if (ui->ime_cursor_begin < 0 && ui->ime_cursor_end < 0) {
                hide_caret = true;
                caret_byte = preedit_end;
            } else {
                size_t cb = ui->ime_cursor_begin < 0 ? 0 : (size_t)ui->ime_cursor_begin;
                size_t ce = ui->ime_cursor_end < 0 ? cb : (size_t)ui->ime_cursor_end;
                if (cb > ui->ime_preedit_len) cb = ui->ime_preedit_len;
                if (ce > ui->ime_preedit_len) ce = ui->ime_preedit_len;
                cb = utf8_clamp_pos(ui->ime_preedit, cb, ui->ime_preedit_len);
                ce = utf8_clamp_pos(ui->ime_preedit, ce, ui->ime_preedit_len);
                caret_byte = preedit_start + cb;
                if (cb != ce) {
                    preedit_sel_a = preedit_start + (cb < ce ? cb : ce);
                    preedit_sel_b = preedit_start + (cb < ce ? ce : cb);
                }
            }
        } else {
            disp = ui->search_buf;
            caret_byte = ui->search_cursor;
        }
    }

    PangoLayout *pl = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = make_font_desc(ui, 1.0, false);
    pango_layout_set_font_description(pl, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(pl, disp, -1);

    int tw = 0, th = 0;
    pango_layout_get_pixel_size(pl, &tw, &th);
    double ty = sb.y + (sb.h - th) * 0.5;
    double tx = text_left;

    int cursor_x_px = 0;
    if (!is_placeholder) {
        PangoRectangle pos;
        pango_layout_get_cursor_pos(pl, (int)caret_byte, &pos, NULL);
        cursor_x_px = pos.x / PANGO_SCALE;
        if (cursor_x_px > inner_w - 2.0) {
            tx -= (cursor_x_px - (inner_w - 2.0));
        }
    }
    ui->last_search_tx = tx;

    if (!is_placeholder && ui->search_sel_all && focused && ui->ime_preedit_len == 0) {
        set_cairo_color(cr, t->text_selected.bg, 0.35);
        cairo_rectangle(cr, tx, sb.y + 4.0, tw, sb.h - 8.0);
        cairo_fill(cr);
    }

    if (ui->ime_preedit_len > 0 && preedit_sel_b > preedit_sel_a) {
        PangoRectangle pa, pb;
        pango_layout_get_cursor_pos(pl, (int)preedit_sel_a, &pa, NULL);
        pango_layout_get_cursor_pos(pl, (int)preedit_sel_b, &pb, NULL);
        double sx = tx + (double)pa.x / PANGO_SCALE;
        double sw = (double)(pb.x - pa.x) / PANGO_SCALE;
        if (sw > 0.5) {
            set_cairo_color(cr, t->text_selected.bg, 0.30);
            cairo_rectangle(cr, sx, sb.y + 4.0, sw, sb.h - 8.0);
            cairo_fill(cr);
        }
    }

    set_cairo_color(cr, t->text_normal.fg, is_placeholder ? 0.50 : 1.0);
    cairo_move_to(cr, tx, ty);
    pango_cairo_show_layout(cr, pl);
    cairo_new_path(cr);

    if (ui->ime_preedit_len > 0 && preedit_end > preedit_start) {
        PangoRectangle p1, p2;
        pango_layout_get_cursor_pos(pl, (int)preedit_start, &p1, NULL);
        pango_layout_get_cursor_pos(pl, (int)preedit_end, &p2, NULL);
        double ux1 = tx + (double)p1.x / PANGO_SCALE;
        double ux2 = tx + (double)p2.x / PANGO_SCALE;
        if (ux2 > ux1 + 0.5) {
            double uy = ty + th + 1.0;
            if (uy > sb.y + sb.h - 3.0) uy = sb.y + sb.h - 3.0;
            set_cairo_color(cr, t->elem_selected.bg, 0.95);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, ux1, uy);
            cairo_line_to(cr, ux2, uy);
            cairo_stroke(cr);
        }
    }

    g_object_unref(pl);
    sb_free(&composed);

    double cx = tx + (is_placeholder ? 0.0 : (double)cursor_x_px);
    if (focused && !hide_caret) {
        set_cairo_color(cr, t->text_normal.fg, 0.92);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, cx, sb.y + 6.0);
        cairo_line_to(cr, cx, sb.y + sb.h - 6.0);
        cairo_stroke(cr);
    }

    if (focused) {
        double sc = config_effective_ui_scale(ui->cfg);
        double clamped_cx = cx;
        if (clamped_cx < text_left) clamped_cx = text_left;
        if (clamped_cx > text_right) clamped_cx = text_right;
        wayland_ime_set_cursor_rect(
            ui->wl,
            (int32_t)lround(clamped_cx * sc),
            (int32_t)lround(sb.y * sc),
            1,
            (int32_t)lround(sb.h * sc));
    }

    cairo_restore(cr);

    if (has_text) {
        Rect cr_btn = L->btn_search_clear;
        bool hot = (ui->hover_id == HIT_BTN_SEARCH_CLEAR || ui->pressed_id == HIT_BTN_SEARCH_CLEAR);
        if (hot) {
            draw_rounded_rect(cr, cr_btn, 4.0);
            set_cairo_color(cr, ui->pressed_id == HIT_BTN_SEARCH_CLEAR ? t->elem_pressed.bg : t->elem_hover.bg, 1.0);
            cairo_fill(cr);
        }
        double csz = fmin(cr_btn.w, cr_btn.h) * 0.58;
        draw_icon(cr, ICON_CLEAR,
                  (Rect){cr_btn.x + (cr_btn.w - csz) * 0.5, cr_btn.y + (cr_btn.h - csz) * 0.5, csz, csz},
                  hot ? t->elem_hover.fg : t->base.fg, hot ? 1.0 : 0.75);
    }
}

static void render_empty_or_error_list(UiState *ui, cairo_t *cr, const Layout *L) {
    const Theme *t = &ui->cfg->theme;
    Rect la = L->list_area;
    if (la.w <= 20.0 || la.h <= 20.0) return;

    if (ui->error_text[0] != '\0') {
        double cy = la.y + la.h * 0.5 - 32.0;
        draw_text_line(cr, ui, "Unable to Load Content", la.x + SP_LG, cy - 22.0,
                       la.w - SP_LG * 2.0, ui->cfg->font_size * 1.8,
                       t->text_normal.fg, 0.95, 1.05, true, PANGO_ALIGN_CENTER, false);

        PangoLayout *el = pango_cairo_create_layout(cr);
        PangoFontDescription *ed = make_font_desc(ui, 0.92, false);
        pango_layout_set_font_description(el, ed);
        pango_font_description_free(ed);
        pango_layout_set_text(el, ui->error_text, -1);
        pango_layout_set_width(el, (int)((la.w - SP_XL * 2.0) * PANGO_SCALE));
        pango_layout_set_wrap(el, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_alignment(el, PANGO_ALIGN_CENTER);
        set_cairo_color(cr, t->base.fg, 0.9);
        cairo_move_to(cr, la.x + SP_XL, cy + 4.0);
        pango_cairo_show_layout(cr, el);
        cairo_new_path(cr);
        g_object_unref(el);

        draw_button_ex(cr, ui, L->btn_retry, ICON_RETRY, "Retry",
                       HIT_BTN_RETRY, false, true, true);
        return;
    }

    if (ui->busy) {
        draw_text_line(cr, ui, "Loading...", la.x + SP_LG, la.y,
                       la.w - SP_LG * 2.0, la.h,
                       t->base.fg, 0.8, 1.0, false, PANGO_ALIGN_CENTER, true);
        return;
    }

    const char *title = "Welcome to TuiTube";
    const char *sub = "Type a query or paste a YouTube URL above to begin.";
    char tip1[96] = {0}, tip2[96] = {0}, tip3[96] = {0}, tip4[96] = {0};

    if (ui->view_kind == UI_VIEW_FAVORITES) {
        title = "No Favorites Yet";
        sub = "Select a channel or video and click Fav to bookmark it here.";
    } else if (ui->view_kind == UI_VIEW_HISTORY) {
        title = "Watch History is Empty";
        sub = "Videos you play will be recorded here automatically.";
    } else if (ui->view_kind == UI_VIEW_SEARCH) {
        title = "No Results Found";
        sub = "Try different keywords or adjust your Search Filters.";
    } else if (ui->view_kind == UI_VIEW_FEED) {
        title = "No Recent Feed Videos";
        sub = "Add favorite channels or check max_feed_age in config.";
    } else {
        char k_search[24], k_feed[24], k_fav[24], k_help[24];
        if (!config_format_action_shortcut(ui->cfg, ACT_FOCUS_SEARCH_INPUT, k_search, sizeof(k_search)))
            snprintf(k_search, sizeof(k_search), "/");
        if (!config_format_action_shortcut(ui->cfg, ACT_SHOW_FEED, k_feed, sizeof(k_feed)))
            snprintf(k_feed, sizeof(k_feed), "Ctrl+R");
        if (!config_format_action_shortcut(ui->cfg, ACT_SHOW_FAVORITES, k_fav, sizeof(k_fav)))
            snprintf(k_fav, sizeof(k_fav), "Ctrl+F");
        if (!config_format_action_shortcut(ui->cfg, ACT_TOGGLE_HELP, k_help, sizeof(k_help)))
            snprintf(k_help, sizeof(k_help), "F1");
        snprintf(tip1, sizeof(tip1), "%s  Focus Search", k_search);
        snprintf(tip2, sizeof(tip2), "%s  Subscription Feed", k_feed);
        snprintf(tip3, sizeof(tip3), "%s  Favorites", k_fav);
        snprintf(tip4, sizeof(tip4), "%s  All Shortcuts", k_help);
    }

    double cy = la.y + la.h * 0.36;
    draw_text_line(cr, ui, title, la.x + SP_LG, cy,
                   la.w - SP_LG * 2.0, ui->cfg->font_size * 1.8,
                   t->text_normal.fg, 0.95, 1.12, true, PANGO_ALIGN_CENTER, false);
    draw_text_line(cr, ui, sub, la.x + SP_LG, cy + ui->cfg->font_size * 2.0,
                   la.w - SP_LG * 2.0, ui->cfg->font_size * 1.6,
                   t->base.fg, 0.90, 0.92, false, PANGO_ALIGN_CENTER, false);

    if (tip1[0]) {
        double ty = cy + ui->cfg->font_size * 4.4;
        const char *tips[4] = {tip1, tip2, tip3, tip4};
        for (int i = 0; i < 4; i++) {
            draw_text_line(cr, ui, tips[i], la.x + SP_LG, ty,
                           la.w - SP_LG * 2.0, ui->cfg->font_size * 1.6,
                           t->base.fg, 0.85, 0.88, false, PANGO_ALIGN_CENTER, false);
            ty += ui->cfg->font_size * 1.7;
        }
    }
}

static void build_ctx_menu(UiState *ui, const MediaItem *item, double x, double y) {
    CtxMenu *m = &ui->ctx_menu;
    memset(m, 0, sizeof(*m));
    m->hover_idx = -1;
    if (!item) return;

    size_t c = 0;
    m->actions[c] = CTX_ACT_PLAY;
    snprintf(m->labels[c], sizeof(m->labels[c]), "%s",
             item->kind == ITEM_VIDEO ? "Play Video" : "Open");
    m->icons[c++] = ICON_PLAY;

    if (item->kind == ITEM_VIDEO) {
        m->actions[c] = CTX_ACT_DOWNLOAD;
        snprintf(m->labels[c], sizeof(m->labels[c]), "Download Video");
        m->icons[c++] = ICON_DOWNLOAD;
    }

    if (item->kind == ITEM_CHANNEL || (item->author_id && item->author_id[0])) {
        m->actions[c] = CTX_ACT_CHANNEL;
        snprintf(m->labels[c], sizeof(m->labels[c]), "View Channel");
        m->icons[c++] = ICON_CHANNEL;
    }

    if (item->kind == ITEM_VIDEO) {
        m->actions[c] = CTX_ACT_RECOMMENDED;
        snprintf(m->labels[c], sizeof(m->labels[c]), "Recommended Videos");
        m->icons[c++] = ICON_SPARKLE;
    }

    if (item->kind == ITEM_CHANNEL || item->kind == ITEM_PLAYLIST ||
        (item->author_id && item->author_id[0])) {
        const char *fid = (item->kind == ITEM_VIDEO) ? item->author_id : item->id;
        bool is_fav = fid && favlist_contains(ui->favs, fid);
        m->actions[c] = CTX_ACT_FAVORITE;
        snprintf(m->labels[c], sizeof(m->labels[c]), "%s",
                 is_fav ? "Remove Favorite" : "Add to Favorites");
        m->icons[c++] = is_fav ? ICON_STAR_FILLED : ICON_STAR;
    }

    m->actions[c] = CTX_ACT_COPY_URL;
    snprintf(m->labels[c], sizeof(m->labels[c]), "Copy URL");
    m->icons[c++] = ICON_COPY;

    if (item->title && item->title[0]) {
        m->actions[c] = CTX_ACT_COPY_TITLE;
        snprintf(m->labels[c], sizeof(m->labels[c]), "Copy Title");
        m->icons[c++] = ICON_COPY;
    }

    if (item->description && item->description[0]) {
        m->actions[c] = CTX_ACT_COPY_DESC;
        snprintf(m->labels[c], sizeof(m->labels[c]), "Copy Description");
        m->icons[c++] = ICON_COPY;
    }

    double W = 1024.0, H = 640.0;
    logical_viewport(ui, &W, &H);
    double row_h = fmax(28.0, align4(ui->cfg->font_size * 2.0));
    double max_tw = 160.0;
    for (size_t i = 0; i < c; i++) {
        double tw = 0.0;
        measure_text_size(ui, m->labels[i], 0.92, PANGO_WEIGHT_NORMAL, &tw, NULL);
        if (tw + 48.0 > max_tw) max_tw = tw + 48.0;
    }
    double mw = ceil(max_tw);
    double mh = (double)c * row_h + SP_SM;

    double mx = x;
    double my = y;
    if (mx + mw > W - SP_SM) mx = W - SP_SM - mw;
    if (my + mh > H - SP_SM) my = H - SP_SM - mh;
    if (mx < SP_SM) mx = SP_SM;
    if (my < SP_SM) my = SP_SM;

    m->box = (Rect){mx, my, mw, mh};
    for (size_t i = 0; i < c; i++) {
        m->item_rects[i] = (Rect){mx + SP_XS, my + SP_XS + (double)i * row_h, mw - SP_SM, row_h};
    }
    m->count = c;
    m->x = mx;
    m->y = my;
    m->open = true;
}

static void render_ctx_menu(const UiState *ui, cairo_t *cr) {
    const CtxMenu *m = &ui->ctx_menu;
    if (!m->open || m->count == 0) return;
    const Theme *t = &ui->cfg->theme;

    /* Soft drop shadow */
    draw_rounded_rect(cr, (Rect){m->box.x + 2.0, m->box.y + 4.0, m->box.w, m->box.h}, 8.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.28);
    cairo_fill(cr);

    draw_rounded_rect(cr, m->box, 8.0);
    set_cairo_color(cr, t->elem_normal.bg, 1.0);
    cairo_fill_preserve(cr);
    set_cairo_color(cr, t->base.fg, 0.30);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    double isz = fmax(13.0, ui->cfg->font_size * 0.95);
    for (size_t i = 0; i < m->count; i++) {
        Rect r = m->item_rects[i];
        bool hot = ((ssize_t)i == m->hover_idx);
        ThemePair tp = hot ? t->elem_hover : t->elem_normal;
        if (hot) {
            draw_rounded_rect(cr, r, 5.0);
            set_cairo_color(cr, tp.bg, 1.0);
            cairo_fill(cr);
        }
        draw_icon(cr, m->icons[i],
                  (Rect){r.x + SP_SM, r.y + (r.h - isz) * 0.5, isz, isz},
                  tp.fg, hot ? 1.0 : 0.85);
        draw_text_line(cr, ui, m->labels[i], r.x + SP_SM + isz + SP_SM, r.y,
                       r.w - (SP_SM * 2.0 + isz + SP_XS), r.h,
                       tp.fg, 1.0, 0.92, false, PANGO_ALIGN_LEFT, true);
    }
}

static void render_help_modal(const UiState *ui, cairo_t *cr, const Layout *L) {
    if (!ui->show_help) return;
    const Theme *t = &ui->cfg->theme;
    double W = 1024.0, H = 640.0;
    logical_viewport(ui, &W, &H);

    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);

    Rect box = L->help_modal_box;
    draw_rounded_rect(cr, box, 10.0);
    set_cairo_color(cr, t->text_normal.bg, 1.0);
    cairo_fill_preserve(cr);
    set_cairo_color(cr, t->base.fg, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    draw_text_line(cr, ui, "Keyboard & Mouse Shortcuts",
                   box.x + SP_LG, box.y + SP_MD, box.w - 60.0, 26.0,
                   t->text_normal.fg, 1.0, 1.12, true, PANGO_ALIGN_LEFT, true);

    Rect cb = L->help_close_btn;
    bool chot = (ui->hover_id == HIT_HELP_CLOSE || ui->pressed_id == HIT_HELP_CLOSE);
    if (chot) {
        draw_rounded_rect(cr, cb, 5.0);
        set_cairo_color(cr, t->elem_hover.bg, 1.0);
        cairo_fill(cr);
    }
    draw_icon(cr, ICON_CLEAR, (Rect){cb.x + 5.0, cb.y + 5.0, cb.w - 10.0, cb.h - 10.0},
              chot ? t->elem_hover.fg : t->base.fg, 1.0);

    cairo_save(cr);
    cairo_rectangle(cr, box.x + SP_LG, box.y + 46.0, box.w - SP_LG * 2.0, box.h - 58.0);
    cairo_clip(cr);

    struct HelpRow {
        KeyAction act;
        const char *fallback_key;
        const char *desc;
    };
    static const struct HelpRow rows[] = {
        {ACT_FOCUS_SEARCH_INPUT, "/", "Focus search bar"},
        {ACT_PLAY_VIDEO, "Enter", "Play video / Open channel or playlist"},
        {ACT_DOWNLOAD_VIDEO, "Ctrl+S", "Download selected video"},
        {ACT_NAV_BACK, "Alt+Left", "Navigate back in history"},
        {ACT_SHOW_FEED, "Ctrl+R", "Open Subscription Feed"},
        {ACT_SHOW_FAVORITES, "Ctrl+F", "Open Favorites"},
        {ACT_SHOW_HISTORY, "Ctrl+H", "Open Watch History"},
        {ACT_ADD_FAVORITES, "Ctrl+D", "Toggle Favorite channel / playlist"},
        {ACT_VIEW_USER_VIDEOS, "", "Open item's Channel"},
        {ACT_VIEW_RECOMMENDED_VIDEOS, "", "Load Recommended videos"},
        {ACT_COPY_VIDEO_URL, "Ctrl+Y", "Copy item URL"},
        {ACT_NEXT_PAGE, "Ctrl+N", "Load next page"},
        {ACT_TOGGLE_HELP, "F1 / ?", "Toggle this shortcut guide"},
        {ACT_QUIT, "Ctrl+Q", "Quit TuiTube"},
        {ACT_NONE, "j / k / Up / Down", "Move list selection (or scroll details)"},
        {ACT_NONE, "Tab / Shift+Tab", "Cycle focus: Search / List / Detail"},
        {ACT_NONE, "Shift+Up / Down", "Scroll description while in List"},
        {ACT_NONE, "Right-Click", "Open item context menu"},
        {ACT_NONE, "Escape", "Close overlay or leave search box"},
    };

    double ry = box.y + 48.0;
    double row_h = fmax(22.0, ui->cfg->font_size * 1.65);
    double key_col_w = fmin(165.0, (box.w - SP_LG * 2.0) * 0.40);
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        if (ry + row_h > box.y + box.h - SP_SM) break;
        char kbuf[48] = {0};
        if (rows[i].act != ACT_NONE) {
            if (!config_format_action_shortcut(ui->cfg, rows[i].act, kbuf, sizeof(kbuf))) {
                if (rows[i].fallback_key[0]) snprintf(kbuf, sizeof(kbuf), "%s", rows[i].fallback_key);
                else snprintf(kbuf, sizeof(kbuf), "(unbound)");
            }
        } else {
            snprintf(kbuf, sizeof(kbuf), "%s", rows[i].fallback_key);
        }

        draw_rounded_rect(cr, (Rect){box.x + SP_LG, ry + 2.0, key_col_w - SP_SM, row_h - 4.0}, 4.0);
        set_cairo_color(cr, t->elem_normal.bg, 1.0);
        cairo_fill(cr);
        draw_text_line(cr, ui, kbuf, box.x + SP_LG + 6.0, ry, key_col_w - SP_SM - 12.0, row_h,
                       t->elem_normal.fg, 1.0, 0.85, true, PANGO_ALIGN_LEFT, true);
        draw_text_line(cr, ui, rows[i].desc, box.x + SP_LG + key_col_w, ry,
                       box.w - SP_LG * 2.0 - key_col_w, row_h,
                       t->text_normal.fg, 0.92, 0.90, false, PANGO_ALIGN_LEFT, true);
        ry += row_h;
    }
    cairo_restore(cr);
}

void ui_on_render(void *ctx, cairo_t *cr, int width, int height) {
    UiState *ui = ctx;
    ui->win_w = width;
    ui->win_h = height;

    double sc = config_effective_ui_scale(ui->cfg);
    double W = 1024.0, H = 640.0;
    logical_viewport(ui, &W, &H);

    cairo_save(cr);
    cairo_scale(cr, sc, sc);

    Layout L;
    compute_layout(ui, &L);
    clamp_list_scroll(ui, &L);

    const Theme *t = &ui->cfg->theme;
    set_cairo_color(cr, t->base.bg, 1.0);
    cairo_paint(cr);

    /* CSD titlebar */
    if (L.has_csd) {
        set_cairo_color(cr, t->elem_normal.bg, 1.0);
        cairo_rectangle(cr, L.csd_bar.x, L.csd_bar.y, L.csd_bar.w, L.csd_bar.h);
        cairo_fill(cr);
        draw_text_line(cr, ui, "TuiTube", SP_MD, L.csd_bar.y, L.csd_bar.w - 120.0, L.csd_bar.h,
                       t->elem_normal.fg, 1.0, 0.95, true, PANGO_ALIGN_LEFT, true);
        draw_button_ex(cr, ui, L.csd_min, ICON_NONE, "-", HIT_CSD_MIN, false, true, false);
        draw_button_ex(cr, ui, L.csd_max, ICON_NONE,
                       wayland_is_maximized(ui->wl) ? "o" : "+", HIT_CSD_MAX, false, true, false);
        draw_button_ex(cr, ui, L.csd_close, ICON_CLEAR, NULL, HIT_CSD_CLOSE, false, true, false);
    }

    /* Toolbar */
    set_cairo_color(cr, t->base.bg, 1.0);
    cairo_rectangle(cr, L.toolbar.x, L.toolbar.y, L.toolbar.w, L.toolbar.h);
    cairo_fill(cr);

    draw_button_ex(cr, ui, L.btn_back, ICON_BACK, NULL, HIT_BTN_BACK,
                   false, ui->can_go_back, false);
    render_search_box(ui, cr, &L);
    draw_button_ex(cr, ui, L.btn_go, L.compact_toolbar ? ICON_SEARCH : ICON_NONE,
                   L.compact_toolbar ? NULL : "Search", HIT_BTN_GO, false, true, true);
    draw_button_ex(cr, ui, L.btn_filters, ICON_SLIDERS,
                   L.compact_toolbar ? NULL : "Filters", HIT_BTN_FILTERS,
                   ui->show_filters, true, false);

    bool filters_active = (ui->filter.sort != SORT_RELEVANCE ||
                           ui->filter.type != TYPE_ALL ||
                           ui->filter.live_only);
    if (!ui->show_filters && filters_active) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, L.btn_filters.x + L.btn_filters.w - 6.0, L.btn_filters.y + 6.0,
                  2.5, 0.0, 2.0 * M_PI);
        set_cairo_color(cr, t->elem_selected.bg, 1.0);
        cairo_fill(cr);
    }

    set_cairo_color(cr, t->base.fg, 0.22);
    cairo_rectangle(cr, L.toolbar_sep.x, L.toolbar_sep.y, L.toolbar_sep.w, L.toolbar_sep.h);
    cairo_fill(cr);

    draw_button_ex(cr, ui, L.btn_feed, ICON_RSS,
                   L.compact_toolbar ? NULL : "Feed", HIT_BTN_FEED,
                   ui->view_kind == UI_VIEW_FEED, true, false);
    draw_button_ex(cr, ui, L.btn_favs, ICON_STAR,
                   L.compact_toolbar ? NULL : "Favorites", HIT_BTN_FAVS,
                   ui->view_kind == UI_VIEW_FAVORITES, true, false);
    draw_button_ex(cr, ui, L.btn_history, ICON_HISTORY,
                   L.compact_toolbar ? NULL : "History", HIT_BTN_HISTORY,
                   ui->view_kind == UI_VIEW_HISTORY, true, false);
    draw_button_ex(cr, ui, L.btn_help, ICON_HELP, NULL, HIT_BTN_HELP,
                   ui->show_help, true, false);

    /* Filter bar */
    if (ui->show_filters) {
        draw_button_ex(cr, ui, L.btn_sort, ICON_NONE, sort_label(ui->filter.sort),
                       HIT_BTN_SORT, ui->filter.sort != SORT_RELEVANCE, true, false);
        draw_button_ex(cr, ui, L.btn_type, ICON_NONE, type_label(ui->filter.type),
                       HIT_BTN_TYPE, ui->filter.type != TYPE_ALL, true, false);
        draw_button_ex(cr, ui, L.btn_live, ICON_NONE,
                       ui->filter.live_only ? "Live: On" : "Live: Off",
                       HIT_BTN_LIVE, ui->filter.live_only, true, false);
    }

    /* Left pane header */
    set_cairo_color(cr, t->elem_normal.bg, 1.0);
    cairo_rectangle(cr, L.left_header.x, L.left_header.y, L.left_header.w, L.left_header.h);
    cairo_fill(cr);

    const char *hdr = (ui->list.heading && ui->list.heading[0]) ? ui->list.heading : "Results";
    double hdr_right_edge = L.left_header.x + L.left_header.w - SP_SM;
    if (L.btn_chan_videos.w > 0) {
        hdr_right_edge = L.btn_chan_videos.x - SP_SM;
    } else if (L.btn_next_page.w > 0) {
        hdr_right_edge = L.btn_next_page.x - SP_SM;
    }
    double hdr_text_w = hdr_right_edge - (L.left_header.x + SP_SM);
    if (hdr_text_w < 0.0) hdr_text_w = 0.0;
    draw_text_line(cr, ui, hdr, L.left_header.x + SP_SM, L.left_header.y, hdr_text_w, L.left_header.h,
                   t->elem_normal.fg, 1.0, 0.95, true, PANGO_ALIGN_LEFT, true);
    if (L.btn_chan_videos.w > 0) {
        draw_button_ex(cr, ui, L.btn_chan_videos, ICON_NONE, "Videos",
                       HIT_BTN_CHAN_VIDEOS, !ui->channel_live_tab, true, false);
        draw_button_ex(cr, ui, L.btn_chan_live, ICON_NONE, "Live",
                       HIT_BTN_CHAN_LIVE, ui->channel_live_tab, true, false);
    }
    if (L.btn_next_page.w > 0) {
        draw_button_ex(cr, ui, L.btn_next_page, ICON_NONE,
                       ui->loading_more ? "Loading..." : "Next Page",
                       HIT_BTN_NEXT_PAGE, ui->loading_more, !ui->loading_more, false);
    }
    if (ui->busy || ui->loading_more) {
        set_cairo_color(cr, t->elem_selected.bg, 1.0);
        cairo_rectangle(cr, L.left_header.x, L.left_header.y + L.left_header.h - 2.5,
                        L.left_header.w, 2.5);
        cairo_fill(cr);
    }

    /* Left list (virtualized cards) */
    cairo_save(cr);
    cairo_rectangle(cr, L.list_area.x, L.list_area.y, L.list_area.w, L.list_area.h);
    cairo_clip(cr);

    set_cairo_color(cr, t->text_normal.bg, 1.0);
    cairo_rectangle(cr, L.list_area.x, L.list_area.y, L.list_area.w, L.list_area.h);
    cairo_fill(cr);

    double rh = row_height(ui);
    if (ui->list.count > 0) {
        size_t first = (size_t)floor(ui->list_scroll_y / rh);
        size_t last = (size_t)ceil((ui->list_scroll_y + L.list_area.h) / rh);
        if (last >= ui->list.count) last = ui->list.count - 1;

        int th = ui->cfg->thumb_size;
        int tw = th * 16 / 9;

        for (size_t i = first; i <= last; i++) {
            const MediaItem *it = &ui->list.items[i];
            double ry = L.list_area.y + (double)i * rh - ui->list_scroll_y;
            bool selected = ((ssize_t)i == ui->selected_idx);
            bool hovered = (!selected && (ssize_t)i == ui->hover_idx);

            ThemePair rp = selected ? t->elem_selected : (hovered ? t->text_hover : t->text_normal);
            Rect card = {L.list_area.x + 6.0, ry + 2.0, L.list_area.w - 12.0, rh - 4.0};

            if (selected || hovered) {
                draw_rounded_rect(cr, card, 6.0);
                set_cairo_color(cr, rp.bg, 1.0);
                cairo_fill(cr);
            }
            if (selected) {
                Rect bar = {card.x + 2.0, card.y + 4.0, 3.0, card.h - 8.0};
                draw_rounded_rect(cr, bar, 1.5);
                set_cairo_color(cr, t->elem_selected.fg, 0.85);
                cairo_fill(cr);
            }

            double dim = (!selected && ui->cfg->dim_watched_videos && it->watched) ? 0.56 : 1.0;

            Rect tb = {card.x + 8.0, ry + (rh - th) * 0.5, (double)tw, (double)th};
            draw_rounded_rect(cr, tb, 6.0);
            set_cairo_color(cr, t->base.bg, dim);
            cairo_fill(cr);

            cairo_surface_t *surf = acquire_thumb(ui, it);
            if (surf) {
                draw_surface_fit(cr, surf, tb, true, 6.0);
                if (dim < 1.0) {
                    draw_rounded_rect(cr, tb, 6.0);
                    set_cairo_color(cr, t->text_normal.bg, 1.0 - dim);
                    cairo_fill(cr);
                }
            } else {
                draw_thumb_placeholder(cr, t, tb, it->kind);
            }

            /* Watched pill (top-left) */
            if (it->watched && ui->cfg->dim_watched_videos) {
                draw_badge_pill(cr, ui, tb.x + 4.0, tb.y + 4.0, tb.w - 8.0,
                                "WATCHED", 0.06, 0.06, 0.08, 0.80, false, 0.0);
            }

            /* Bottom-right Duration / LIVE / Kind badge */
            const char *badge = it->is_live ? "LIVE"
                              : (it->duration_text && it->duration_text[0]) ? it->duration_text
                              : (it->kind == ITEM_CHANNEL) ? "CHANNEL"
                              : (it->kind == ITEM_PLAYLIST) ? "PLAYLIST" : NULL;
            if (badge && badge[0]) {
                double bh_est = ui->cfg->font_size * 0.95 + 4.0;
                if (it->is_live) {
                    draw_badge_pill(cr, ui, tb.x + 4.0, tb.y + tb.h - bh_est - 4.0, tb.w - 8.0,
                                    badge, 0.85, 0.16, 0.16, 0.94, true, tb.x + tb.w - 4.0);
                } else {
                    draw_badge_pill(cr, ui, tb.x + 4.0, tb.y + tb.h - bh_est - 4.0, tb.w - 8.0,
                                    badge, 0.06, 0.06, 0.08, 0.84, true, tb.x + tb.w - 4.0);
                }
            }

            /* Title (constant SEMIBOLD weight for zero reflow jitter) */
            double tx = tb.x + tb.w + SP_SM;
            double avail_w = (card.x + card.w) - tx - SP_SM;
            if (avail_w > 10.0) {
                double ty = ry + 6.0;
                if (it->title && it->title[0]) {
                    PangoLayout *tl = pango_cairo_create_layout(cr);
                    PangoFontDescription *td = make_font_desc_w(ui, 0.96, PANGO_WEIGHT_SEMIBOLD);
                    pango_layout_set_font_description(tl, td);
                    pango_font_description_free(td);
                    pango_layout_set_text(tl, it->title, -1);
                    pango_layout_set_width(tl, (int)(avail_w * PANGO_SCALE));
                    pango_layout_set_wrap(tl, PANGO_WRAP_WORD_CHAR);
                    pango_layout_set_ellipsize(tl, PANGO_ELLIPSIZE_END);
                    pango_layout_set_height(tl, -2);

                    set_cairo_color(cr, rp.fg, dim);
                    cairo_move_to(cr, tx, ty);
                    pango_cairo_show_layout(cr, tl);
                    cairo_new_path(cr);
                    int pw = 0, ph = 0;
                    pango_layout_get_pixel_size(tl, &pw, &ph);
                    g_object_unref(tl);
                    ty += ph + 3.0;
                }

                if (it->author && it->author[0]) {
                    draw_text_line(cr, ui, it->author, tx, ty, avail_w, ui->cfg->font_size * 1.35,
                                   rp.fg, dim * 0.80, 0.85, false, PANGO_ALIGN_LEFT, false);
                    ty += ui->cfg->font_size * 1.25;
                }

                if (ui->cfg->show_extra_metadata &&
                    ((it->view_count_text && it->view_count_text[0]) ||
                     (it->published_text && it->published_text[0]))) {
                    char extra[192] = {0};
                    if (it->view_count_text && it->published_text) {
                        snprintf(extra, sizeof(extra), "%s • %s", it->view_count_text, it->published_text);
                    } else if (it->view_count_text) {
                        snprintf(extra, sizeof(extra), "%s", it->view_count_text);
                    } else {
                        snprintf(extra, sizeof(extra), "%s", it->published_text);
                    }
                    draw_text_line(cr, ui, extra, tx, ty, avail_w, ui->cfg->font_size * 1.3,
                                   rp.fg, dim * 0.70, 0.80, false, PANGO_ALIGN_LEFT, false);
                }
            }

            if (!selected && !hovered) {
                set_cairo_color(cr, t->base.fg, 0.12);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, card.x + 6.0, ry + rh - 0.5);
                cairo_line_to(cr, card.x + card.w - 6.0, ry + rh - 0.5);
                cairo_stroke(cr);
            }
        }
    } else {
        render_empty_or_error_list(ui, cr, &L);
    }

    if (ui->focus_zone == FOCUS_LIST) {
        Rect fr = {L.list_area.x + 1.0, L.list_area.y + 1.0,
                   L.list_area.w - 2.0, L.list_area.h - 2.0};
        set_cairo_color(cr, t->elem_selected.bg, 0.90);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, fr.x, fr.y, fr.w, fr.h);
        cairo_stroke(cr);
    }
    cairo_restore(cr);

    /* List Scrollbar */
    set_cairo_color(cr, t->scrollbar_normal.bg, 1.0);
    cairo_rectangle(cr, L.scrollbar.x, L.scrollbar.y, L.scrollbar.w, L.scrollbar.h);
    cairo_fill(cr);
    Rect thumb_r = scrollbar_thumb_rect(ui, &L);
    ThemePair sbp = t->scrollbar_normal;
    if (ui->dragging_scrollbar || ui->pressed_id == HIT_SCROLLBAR) sbp = t->scrollbar_pressed;
    else if (ui->hover_id == HIT_SCROLLBAR) sbp = t->scrollbar_hover;
    draw_rounded_rect(cr, thumb_r, 4.0);
    set_cairo_color(cr, sbp.fg, 1.0);
    cairo_fill(cr);

    /* Splitter */
    set_cairo_color(cr, t->base.fg,
                    ui->dragging_splitter || ui->hover_id == HIT_SPLITTER ? 0.6 : 0.25);
    cairo_rectangle(cr, L.splitter.x, L.splitter.y, L.splitter.w, L.splitter.h);
    cairo_fill(cr);

    /* Right pane */
    render_right_pane(ui, cr, &L);

    /* Status bar */
    Rect st = L.status_bar;
    set_cairo_color(cr, t->elem_normal.bg, 1.0);
    cairo_rectangle(cr, st.x, st.y, st.w, st.h);
    cairo_fill(cr);

    char right_info[160] = {0};
    if (W >= 780.0) {
        if (ui->list.count > 0) {
            snprintf(right_info, sizeof(right_info),
                     "%zu items  •  / Search  •  Enter Play  •  F1 Help", ui->list.count);
        } else {
            snprintf(right_info, sizeof(right_info), "/ Search  •  F1 Help");
        }
    } else if (ui->list.count > 0) {
        snprintf(right_info, sizeof(right_info), "%zu items", ui->list.count);
    }

    double rw_info = 0.0;
    if (right_info[0]) {
        measure_text_size(ui, right_info, 0.85, PANGO_WEIGHT_NORMAL, &rw_info, NULL);
        rw_info += SP_MD;
        draw_text_line(cr, ui, right_info, st.w - rw_info - SP_SM, st.y, rw_info, st.h,
                       t->elem_normal.fg, 0.78, 0.85, false, PANGO_ALIGN_RIGHT, true);
    }

    double dl_prog = -1.0;
    size_t dl_cnt = player_get_active_download_summary(&dl_prog, NULL, 0);
    double dl_block_w = 0.0;
    if (dl_cnt > 0 && st.w - rw_info > 260.0) {
        char dl_txt[48];
        if (dl_prog >= 0.0) {
            int pct = (int)lround(dl_prog * 100.0);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            if (dl_cnt > 1) snprintf(dl_txt, sizeof(dl_txt), "DL %d%% (%zu)", pct, dl_cnt);
            else snprintf(dl_txt, sizeof(dl_txt), "DL %d%%", pct);
        } else {
            if (dl_cnt > 1) snprintf(dl_txt, sizeof(dl_txt), "DL... (%zu)", dl_cnt);
            else snprintf(dl_txt, sizeof(dl_txt), "DL...");
        }
        double tw_dl = 0.0;
        measure_text_size(ui, dl_txt, 0.85, PANGO_WEIGHT_SEMIBOLD, &tw_dl, NULL);
        double bar_w = 88.0;
        double bar_h = 8.0;
        dl_block_w = bar_w + SP_XS + tw_dl + SP_MD;
        double dx = st.w - rw_info - SP_SM - dl_block_w;
        Rect bar_r = {dx, st.y + (st.h - bar_h) * 0.5, bar_w, bar_h};
        draw_rounded_rect(cr, bar_r, 4.0);
        set_cairo_color(cr, t->base.bg, 1.0);
        cairo_fill_preserve(cr);
        set_cairo_color(cr, t->base.fg, 0.25);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        if (dl_prog >= 0.0 && dl_prog <= 1.0) {
            Rect fill_r = {bar_r.x, bar_r.y, bar_r.w * dl_prog, bar_r.h};
            if (fill_r.w > 1.0) {
                draw_rounded_rect(cr, fill_r, 4.0);
                set_cairo_color(cr, t->elem_selected.bg, 1.0);
                cairo_fill(cr);
            }
        }
        draw_text_line(cr, ui, dl_txt, bar_r.x + bar_w + SP_XS, st.y,
                       tw_dl + SP_XS, st.h, t->elem_normal.fg, 0.92, 0.85,
                       true, PANGO_ALIGN_LEFT, true);
    }

    double status_w = st.w - rw_info - dl_block_w - SP_LG - SP_SM;
    if (status_w > 40.0) {
        draw_text_line(cr, ui, ui->status_text, SP_MD, st.y, status_w, st.h,
                       t->elem_normal.fg, 0.92, 0.88, false, PANGO_ALIGN_LEFT, true);
    }

    /* Floating overlays */
    render_ctx_menu(ui, cr);
    render_help_modal(ui, cr, &L);

    ui->last_layout = L;
    ui->has_last_layout = true;

    cairo_restore(cr);
}

void ui_on_resize(void *ctx, int width, int height) {
    UiState *ui = ctx;
    ui->win_w = width;
    ui->win_h = height;
    ui->ctx_menu.open = false;
    wayland_request_redraw(ui->wl);
}

static HitId hit_test(UiState *ui, const Layout *L, double x, double y) {
    if (ui->show_help) {
        if (pt_in_rect(x, y, L->help_close_btn)) return HIT_HELP_CLOSE;
        if (pt_in_rect(x, y, L->help_modal_box)) return HIT_HELP_MODAL;
        return HIT_HELP_BACKDROP;
    }

    if (ui->ctx_menu.open) {
        ui->ctx_menu.hover_idx = -1;
        for (size_t i = 0; i < ui->ctx_menu.count; i++) {
            if (pt_in_rect(x, y, ui->ctx_menu.item_rects[i])) {
                ui->ctx_menu.hover_idx = (ssize_t)i;
                return HIT_CTX_ITEM;
            }
        }
        if (pt_in_rect(x, y, ui->ctx_menu.box)) return HIT_CTX_MENU;
    }

    if (L->has_csd) {
        if (pt_in_rect(x, y, L->csd_close)) return HIT_CSD_CLOSE;
        if (pt_in_rect(x, y, L->csd_max)) return HIT_CSD_MAX;
        if (pt_in_rect(x, y, L->csd_min)) return HIT_CSD_MIN;
        if (pt_in_rect(x, y, L->csd_bar)) return HIT_CSD_TITLEBAR;
    }
    if (pt_in_rect(x, y, L->btn_back)) return HIT_BTN_BACK;
    if ((ui->search_len > 0 || ui->ime_preedit_len > 0) && pt_in_rect(x, y, L->btn_search_clear)) {
        return HIT_BTN_SEARCH_CLEAR;
    }
    if (pt_in_rect(x, y, L->search_box)) return HIT_SEARCH_BOX;
    if (pt_in_rect(x, y, L->btn_go)) return HIT_BTN_GO;
    if (pt_in_rect(x, y, L->btn_filters)) return HIT_BTN_FILTERS;
    if (pt_in_rect(x, y, L->btn_feed)) return HIT_BTN_FEED;
    if (pt_in_rect(x, y, L->btn_favs)) return HIT_BTN_FAVS;
    if (pt_in_rect(x, y, L->btn_history)) return HIT_BTN_HISTORY;
    if (pt_in_rect(x, y, L->btn_help)) return HIT_BTN_HELP;
    if (ui->show_filters) {
        if (pt_in_rect(x, y, L->btn_sort)) return HIT_BTN_SORT;
        if (pt_in_rect(x, y, L->btn_type)) return HIT_BTN_TYPE;
        if (pt_in_rect(x, y, L->btn_live)) return HIT_BTN_LIVE;
    }
    if (pt_in_rect(x, y, L->btn_chan_videos)) return HIT_BTN_CHAN_VIDEOS;
    if (pt_in_rect(x, y, L->btn_chan_live)) return HIT_BTN_CHAN_LIVE;
    if (pt_in_rect(x, y, L->btn_next_page)) return HIT_BTN_NEXT_PAGE;
    if (pt_in_rect(x, y, L->btn_retry)) return HIT_BTN_RETRY;
    if (pt_in_rect(x, y, L->splitter)) return HIT_SPLITTER;
    if (pt_in_rect(x, y, L->scrollbar)) return HIT_SCROLLBAR;
    if (pt_in_rect(x, y, L->list_area)) {
        double rh = row_height(ui);
        double rel_y = y - L->list_area.y + ui->list_scroll_y;
        if (rel_y >= 0.0) {
            size_t idx = (size_t)(rel_y / rh);
            if (idx < ui->list.count) {
                ui->hover_idx = (ssize_t)idx;
                return HIT_LIST_ITEM;
            }
        }
        return HIT_LIST_AREA;
    }

    if (pt_in_rect(x, y, L->right_pane)) {
        if (pt_in_rect(x, y, L->detail_thumb)) return HIT_DETAIL_THUMB;
        if (pt_in_rect(x, y, L->detail_author)) return HIT_DETAIL_AUTHOR;
        if (pt_in_rect(x, y, L->act_play)) return HIT_ACT_PLAY;
        if (pt_in_rect(x, y, L->act_download)) return HIT_ACT_DOWNLOAD;
        if (pt_in_rect(x, y, L->act_channel)) return HIT_ACT_CHANNEL;
        if (pt_in_rect(x, y, L->act_recommended)) return HIT_ACT_RECOMMENDED;
        if (pt_in_rect(x, y, L->act_fav)) return HIT_ACT_FAVORITE;
        if (pt_in_rect(x, y, L->act_copy)) return HIT_ACT_COPY_URL;
        if (ui->desc_max_scroll > 0.0 && pt_in_rect(x, y, L->desc_scrollbar)) {
            return HIT_DESC_SCROLLBAR;
        }
        return HIT_RIGHT_PANE;
    }
    return HIT_NONE;
}

static uint32_t csd_edge_at(const UiState *ui, double lx, double ly) {
    if (!wayland_needs_csd(ui->wl) || wayland_is_maximized(ui->wl)) return TT_RESIZE_NONE;
    double W = 1024.0, H = 640.0;
    logical_viewport(ui, &W, &H);
    double m = 5.0;
    bool left = lx < m;
    bool right = lx >= W - m;
    bool top = ly < m;
    bool bottom = ly >= H - m;
    if (top && left) return TT_RESIZE_TOP_LEFT;
    if (top && right) return TT_RESIZE_TOP_RIGHT;
    if (bottom && left) return TT_RESIZE_BOTTOM_LEFT;
    if (bottom && right) return TT_RESIZE_BOTTOM_RIGHT;
    if (top) return TT_RESIZE_TOP;
    if (bottom) return TT_RESIZE_BOTTOM;
    if (left) return TT_RESIZE_LEFT;
    if (right) return TT_RESIZE_RIGHT;
    return TT_RESIZE_NONE;
}

static void maybe_infinite_scroll(UiState *ui, const Layout *L) {
    if (!ui->cfg->infinite_scroll || ui->loading_more || !ui->list.continuation_token) return;
    double total_h = (double)ui->list.count * row_height(ui);
    if (ui->list_scroll_y + L->list_area.h >= total_h - row_height(ui) * 2.0) {
        if (ui->cbs.on_next_page) {
            ui->loading_more = true;
            ui->cbs.on_next_page(ui->user_ctx);
        }
    }
}

static TtCursorShape cursor_for_hit(const UiState *ui, HitId h) {
    switch (h) {
        case HIT_SEARCH_BOX:
            return TT_CURSOR_TEXT;
        case HIT_SPLITTER:
            return TT_CURSOR_COL_RESIZE;
        case HIT_BTN_BACK:
            return ui->can_go_back ? TT_CURSOR_POINTER : TT_CURSOR_DEFAULT;
        case HIT_CSD_MIN:
        case HIT_CSD_MAX:
        case HIT_CSD_CLOSE:
        case HIT_BTN_SEARCH_CLEAR:
        case HIT_BTN_GO:
        case HIT_BTN_FILTERS:
        case HIT_BTN_FEED:
        case HIT_BTN_FAVS:
        case HIT_BTN_HISTORY:
        case HIT_BTN_HELP:
        case HIT_BTN_SORT:
        case HIT_BTN_TYPE:
        case HIT_BTN_LIVE:
        case HIT_BTN_CHAN_VIDEOS:
        case HIT_BTN_CHAN_LIVE:
        case HIT_BTN_NEXT_PAGE:
        case HIT_BTN_RETRY:
        case HIT_LIST_ITEM:
        case HIT_DETAIL_THUMB:
        case HIT_DETAIL_AUTHOR:
        case HIT_ACT_PLAY:
        case HIT_ACT_CHANNEL:
        case HIT_ACT_RECOMMENDED:
        case HIT_ACT_FAVORITE:
        case HIT_ACT_COPY_URL:
        case HIT_CTX_ITEM:
        case HIT_HELP_CLOSE:
            return TT_CURSOR_POINTER;
        case HIT_ACT_DOWNLOAD: {
            const MediaItem *it = ui_get_detail_item(ui);
            bool dl = (it && it->kind == ITEM_VIDEO && player_is_downloading(it->id));
            return dl ? TT_CURSOR_DEFAULT : TT_CURSOR_POINTER;
        }
        default:
            return TT_CURSOR_DEFAULT;
    }
}

void ui_on_pointer_motion(void *ctx, double x, double y) {
    UiState *ui = ctx;
    double sc = config_effective_ui_scale(ui->cfg);
    double lx = x / sc;
    double ly = y / sc;
    ui->ptr_x = lx;
    ui->ptr_y = ly;

    Layout L;
    compute_layout(ui, &L);

    if (ui->dragging_splitter) {
        double W = 1024.0, H = 640.0;
        logical_viewport(ui, &W, &H);
        int nw = (int)(lx - ui->splitter_drag_offset);
        double max_w = W - 240.0;
        if (W < 720.0) {
            double lim = W * 0.52;
            if (lim < max_w) max_w = lim;
        }
        if (max_w < 180.0) max_w = W * 0.5;
        if (nw < 180) nw = 180;
        if (nw > (int)max_w) nw = (int)max_w;
        if (nw != ui->cfg->playlist_width) {
            ui->cfg->playlist_width = nw;
            wayland_request_redraw(ui->wl);
        }
        return;
    }

    if (ui->dragging_scrollbar) {
        double total_h = (double)ui->list.count * row_height(ui);
        if (total_h > L.list_area.h) {
            Rect tr = scrollbar_thumb_rect(ui, &L);
            double avail = L.scrollbar.h - tr.h;
            if (avail > 0) {
                double rel = (ly - ui->scrollbar_drag_offset - L.scrollbar.y) / avail;
                if (rel < 0.0) rel = 0.0;
                if (rel > 1.0) rel = 1.0;
                ui->list_scroll_y = rel * (total_h - L.list_area.h);
                maybe_infinite_scroll(ui, &L);
                wayland_request_redraw(ui->wl);
            }
        }
        return;
    }

    if (ui->dragging_desc_scrollbar && ui->desc_max_scroll > 0.0 && L.desc_scrollbar.h > 4.0) {
        Rect tr = desc_scrollbar_thumb_rect(ui, L.desc_scrollbar);
        double avail = L.desc_scrollbar.h - tr.h;
        if (avail > 0.0) {
            double rel = (ly - ui->desc_sb_drag_offset - L.desc_scrollbar.y) / avail;
            if (rel < 0.0) rel = 0.0;
            if (rel > 1.0) rel = 1.0;
            ui->desc_scroll_y = rel * ui->desc_max_scroll;
            wayland_request_redraw(ui->wl);
        }
        return;
    }

    uint32_t edge = csd_edge_at(ui, lx, ly);
    if (edge != TT_RESIZE_NONE) {
        TtCursorShape sh = TT_CURSOR_DEFAULT;
        if (edge == TT_RESIZE_TOP_LEFT) sh = TT_CURSOR_NW_RESIZE;
        else if (edge == TT_RESIZE_TOP_RIGHT) sh = TT_CURSOR_NE_RESIZE;
        else if (edge == TT_RESIZE_TOP) sh = TT_CURSOR_N_RESIZE;
        else if (edge == TT_RESIZE_BOTTOM) sh = TT_CURSOR_S_RESIZE;
        else if (edge == TT_RESIZE_LEFT) sh = TT_CURSOR_W_RESIZE;
        else if (edge == TT_RESIZE_RIGHT) sh = TT_CURSOR_E_RESIZE;
        wayland_set_cursor(ui->wl, sh);
        return;
    }

    ssize_t prev_hover_idx = ui->hover_idx;
    ssize_t prev_ctx_hover = ui->ctx_menu.hover_idx;
    ui->hover_idx = -1;
    HitId h = hit_test(ui, &L, lx, ly);
    if (h != ui->hover_id || ui->hover_idx != prev_hover_idx ||
        ui->ctx_menu.hover_idx != prev_ctx_hover) {
        ui->hover_id = h;
        wayland_request_redraw(ui->wl);
    }
    wayland_set_cursor(ui->wl, cursor_for_hit(ui, h));
}

void ui_on_pointer_leave(void *ctx) {
    UiState *ui = ctx;
    ui->hover_id = HIT_NONE;
    ui->hover_idx = -1;
    ui->ctx_menu.hover_idx = -1;
    wayland_request_redraw(ui->wl);
}

static void copy_item_field(UiState *ui, int which) {
    const MediaItem *it = ui_get_detail_item(ui);
    if (!it) return;
    if (which == 0) {
        char url[160];
        if (it->kind == ITEM_VIDEO) snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", it->id);
        else if (it->kind == ITEM_CHANNEL) snprintf(url, sizeof(url), "https://www.youtube.com/channel/%s", it->id);
        else snprintf(url, sizeof(url), "https://www.youtube.com/playlist?list=%s", it->id);
        wayland_clipboard_copy(ui->wl, url);
        ui_set_status(ui, "Copied URL to clipboard");
    } else if (which == 1 && it->title) {
        wayland_clipboard_copy(ui->wl, it->title);
        ui_set_status(ui, "Copied title to clipboard");
    } else if (which == 2 && it->description) {
        wayland_clipboard_copy(ui->wl, it->description);
        ui_set_status(ui, "Copied description to clipboard");
    }
}

static void select_list_row(UiState *ui, size_t idx) {
    if (idx >= ui->list.count) return;
    bool changed = ((ssize_t)idx != ui->selected_idx);
    ui->selected_idx = (ssize_t)idx;
    const MediaItem *it = &ui->list.items[idx];
    if (changed) {
        sync_detail_from_selection(ui);
    }
    if ((changed || (it->kind == ITEM_VIDEO && !it->details_loaded)) &&
        ui->cbs.on_select_item) {
        ui->cbs.on_select_item(ui->user_ctx, idx);
    }
}

static void execute_ctx_action(UiState *ui, CtxAction act) {
    switch (act) {
        case CTX_ACT_PLAY:
            if (ui->selected_idx >= 0 && ui->cbs.on_activate_item) {
                ui->cbs.on_activate_item(ui->user_ctx, (size_t)ui->selected_idx);
            }
            break;
        case CTX_ACT_DOWNLOAD:
            if (ui->cbs.on_download_video) ui->cbs.on_download_video(ui->user_ctx);
            break;
        case CTX_ACT_CHANNEL:
            if (ui->cbs.on_view_channel) ui->cbs.on_view_channel(ui->user_ctx);
            break;
        case CTX_ACT_RECOMMENDED:
            if (ui->cbs.on_view_recommended) ui->cbs.on_view_recommended(ui->user_ctx);
            break;
        case CTX_ACT_FAVORITE:
            if (ui->cbs.on_toggle_favorite) ui->cbs.on_toggle_favorite(ui->user_ctx);
            break;
        case CTX_ACT_COPY_URL:
            copy_item_field(ui, 0);
            break;
        case CTX_ACT_COPY_TITLE:
            copy_item_field(ui, 1);
            break;
        case CTX_ACT_COPY_DESC:
            copy_item_field(ui, 2);
            break;
    }
}

static void trigger_filter_search_if_active(UiState *ui) {
    if (ui->search_len == 0 || !ui->cbs.on_search) return;
    ParsedUrl pu;
    if (innertube_parse_input_url(ui->search_buf, &pu)) return;
    ui->cbs.on_search(ui->user_ctx, ui->search_buf, ui->filter);
}

static size_t search_cursor_from_click(const UiState *ui, double lx) {
    if (!ui->measure_pl || ui->search_len == 0) return 0;
    PangoFontDescription *fd = make_font_desc(ui, 1.0, false);
    pango_layout_set_font_description(ui->measure_pl, fd);
    pango_font_description_free(fd);
    pango_layout_set_width(ui->measure_pl, -1);
    pango_layout_set_single_paragraph_mode(ui->measure_pl, TRUE);
    pango_layout_set_text(ui->measure_pl, ui->search_buf, -1);

    int rel_x = (int)((lx - ui->last_search_tx) * PANGO_SCALE);
    if (rel_x < 0) return 0;
    int idx = 0, trailing = 0;
    pango_layout_xy_to_index(ui->measure_pl, rel_x, 0, &idx, &trailing);
    size_t pos = utf8_clamp_pos(ui->search_buf, (size_t)(idx < 0 ? 0 : idx), ui->search_len);
    for (int i = 0; i < trailing && pos < ui->search_len; i++) {
        pos = utf8_next_char(ui->search_buf, pos, ui->search_len);
    }
    return pos;
}

static void search_insert_utf8(UiState *ui, const char *utf8);

static void ime_commit_preedit_locally(UiState *ui) {
    if (!ui || !ui->ime_preedit || ui->ime_preedit_len == 0) return;
    char *tmp = ui->ime_preedit;
    ui->ime_preedit = NULL;
    ui->ime_preedit_len = 0;
    ui->ime_cursor_begin = 0;
    ui->ime_cursor_end = 0;
    search_insert_utf8(ui, tmp);
    free(tmp);
}

static void toggle_help_modal(UiState *ui, bool open) {
    ui->show_help = open;
    if (open) {
        ui->ctx_menu.open = false;
        ime_clear_preedit(ui);
        wayland_ime_set_enabled(ui->wl, false);
    } else if (ui->focus_zone == FOCUS_SEARCH) {
        wayland_ime_set_enabled(ui->wl, true);
        ime_sync_surrounding(ui, true);
    }
}

void ui_on_pointer_button(void *ctx, TtMouseButton btn, bool pressed,
                          bool double_click, double x, double y) {
    UiState *ui = ctx;
    double sc = config_effective_ui_scale(ui->cfg);
    double lx = x / sc;
    double ly = y / sc;
    ui->ptr_x = lx;
    ui->ptr_y = ly;

    if (btn == TT_BTN_BACK) {
        if (pressed && ui->cbs.on_navigate_back) {
            ui->ctx_menu.open = false;
            ui->cbs.on_navigate_back(ui->user_ctx);
        }
        return;
    }

    if (btn == TT_BTN_RIGHT) {
        if (!pressed) return;
        if (ui->show_help) {
            toggle_help_modal(ui, false);
            wayland_request_redraw(ui->wl);
            return;
        }
        Layout L;
        compute_layout(ui, &L);
        if (pt_in_rect(lx, ly, L.list_area)) {
            double rh = row_height(ui);
            double rel_y = ly - L.list_area.y + ui->list_scroll_y;
            size_t idx = rel_y >= 0.0 ? (size_t)(rel_y / rh) : ui->list.count;
            if (idx < ui->list.count) {
                set_focus_zone(ui, FOCUS_LIST);
                select_list_row(ui, idx);
                build_ctx_menu(ui, ui_get_detail_item(ui), lx, ly);
            } else {
                ui->ctx_menu.open = false;
            }
        } else if (pt_in_rect(lx, ly, L.right_pane)) {
            const MediaItem *item = ui_get_detail_item(ui);
            if (item) build_ctx_menu(ui, item, lx, ly);
            else ui->ctx_menu.open = false;
        } else {
            ui->ctx_menu.open = false;
        }
        wayland_request_redraw(ui->wl);
        return;
    }

    if (btn != TT_BTN_LEFT) return;

    if (!pressed) {
        ui->dragging_splitter = false;
        ui->dragging_scrollbar = false;
        ui->dragging_desc_scrollbar = false;
        if (ui->pressed_id != HIT_NONE) {
            ui->pressed_id = HIT_NONE;
            wayland_request_redraw(ui->wl);
        }
        return;
    }

    if (!ui->show_help && !ui->ctx_menu.open) {
        uint32_t edge = csd_edge_at(ui, lx, ly);
        if (edge != 0) {
            wayland_start_resize(ui->wl, edge);
            return;
        }
    }

    Layout L;
    compute_layout(ui, &L);
    HitId h = hit_test(ui, &L, lx, ly);

    if (ui->ctx_menu.open && h != HIT_CTX_ITEM && h != HIT_CTX_MENU) {
        ui->ctx_menu.open = false;
    }

    ui->pressed_id = h;

    switch (h) {
        case HIT_HELP_CLOSE:
        case HIT_HELP_BACKDROP:
            toggle_help_modal(ui, false);
            break;
        case HIT_HELP_MODAL:
            break;
        case HIT_CTX_ITEM:
            if (ui->ctx_menu.hover_idx >= 0 &&
                (size_t)ui->ctx_menu.hover_idx < ui->ctx_menu.count) {
                CtxAction act = ui->ctx_menu.actions[ui->ctx_menu.hover_idx];
                ui->ctx_menu.open = false;
                execute_ctx_action(ui, act);
            }
            break;
        case HIT_CTX_MENU:
            break;
        case HIT_CSD_CLOSE:
            wayland_request_quit(ui->wl);
            break;
        case HIT_CSD_MAX:
            wayland_toggle_maximize(ui->wl);
            break;
        case HIT_CSD_MIN:
            wayland_minimize(ui->wl);
            break;
        case HIT_CSD_TITLEBAR:
            wayland_start_move(ui->wl);
            break;
        case HIT_BTN_BACK:
            if (ui->can_go_back && ui->cbs.on_navigate_back) {
                ui->cbs.on_navigate_back(ui->user_ctx);
            }
            break;
        case HIT_BTN_SEARCH_CLEAR:
            ime_clear_preedit(ui);
            ui->search_len = 0;
            ui->search_cursor = 0;
            ui->search_buf[0] = '\0';
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_SEARCH);
            ime_sync_surrounding(ui, true);
            break;
        case HIT_SEARCH_BOX:
            ime_commit_preedit_locally(ui);
            if (double_click) {
                ui->search_sel_all = true;
                ui->search_cursor = ui->search_len;
            } else {
                ui->search_sel_all = false;
                ui->search_cursor = search_cursor_from_click(ui, lx);
            }
            set_focus_zone(ui, FOCUS_SEARCH);
            break;
        case HIT_BTN_GO:
            ime_commit_preedit_locally(ui);
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_search) ui->cbs.on_search(ui->user_ctx, ui->search_buf, ui->filter);
            break;
        case HIT_BTN_FILTERS:
            ui->show_filters = !ui->show_filters;
            break;
        case HIT_BTN_FEED:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_show_feed) ui->cbs.on_show_feed(ui->user_ctx);
            break;
        case HIT_BTN_FAVS:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_show_favorites) ui->cbs.on_show_favorites(ui->user_ctx);
            break;
        case HIT_BTN_HISTORY:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_show_history) ui->cbs.on_show_history(ui->user_ctx);
            break;
        case HIT_BTN_HELP:
            toggle_help_modal(ui, !ui->show_help);
            break;
        case HIT_BTN_SORT:
            ui->filter.sort = (SearchSort)(((int)ui->filter.sort + 1) % 4);
            trigger_filter_search_if_active(ui);
            break;
        case HIT_BTN_TYPE:
            ui->filter.type = (SearchType)(((int)ui->filter.type + 1) % 4);
            trigger_filter_search_if_active(ui);
            break;
        case HIT_BTN_LIVE:
            ui->filter.live_only = !ui->filter.live_only;
            trigger_filter_search_if_active(ui);
            break;
        case HIT_BTN_CHAN_VIDEOS:
            if (ui->cbs.on_switch_channel_tab) ui->cbs.on_switch_channel_tab(ui->user_ctx, false);
            break;
        case HIT_BTN_CHAN_LIVE:
            if (ui->cbs.on_switch_channel_tab) ui->cbs.on_switch_channel_tab(ui->user_ctx, true);
            break;
        case HIT_BTN_NEXT_PAGE:
            if (!ui->loading_more && ui->cbs.on_next_page) {
                ui->loading_more = true;
                ui->cbs.on_next_page(ui->user_ctx);
            }
            break;
        case HIT_BTN_RETRY:
            if (ui->cbs.on_retry) ui->cbs.on_retry(ui->user_ctx);
            break;
        case HIT_SPLITTER:
            ui->dragging_splitter = true;
            ui->splitter_drag_offset = lx - L.splitter.x;
            break;
        case HIT_SCROLLBAR: {
            Rect tr = scrollbar_thumb_rect(ui, &L);
            ui->dragging_scrollbar = true;
            ui->scrollbar_drag_offset = pt_in_rect(lx, ly, tr) ? (ly - tr.y) : (tr.h * 0.5);
            break;
        }
        case HIT_DESC_SCROLLBAR: {
            Rect tr = desc_scrollbar_thumb_rect(ui, L.desc_scrollbar);
            ui->dragging_desc_scrollbar = true;
            ui->desc_sb_drag_offset = pt_in_rect(lx, ly, tr) ? (ly - tr.y) : (tr.h * 0.5);
            set_focus_zone(ui, FOCUS_DETAIL);
            break;
        }
        case HIT_LIST_ITEM: {
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            double rh = row_height(ui);
            size_t idx = (size_t)((ly - L.list_area.y + ui->list_scroll_y) / rh);
            if (idx < ui->list.count) {
                select_list_row(ui, idx);
                if ((double_click || ui->cfg->single_click_select) && ui->cbs.on_activate_item) {
                    ui->cbs.on_activate_item(ui->user_ctx, idx);
                }
            }
            break;
        }
        case HIT_LIST_AREA:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            break;
        case HIT_DETAIL_THUMB:
        case HIT_ACT_PLAY:
            if (ui->selected_idx >= 0 && ui->cbs.on_activate_item) {
                ui->cbs.on_activate_item(ui->user_ctx, (size_t)ui->selected_idx);
            }
            break;
        case HIT_ACT_DOWNLOAD:
            if (ui->cbs.on_download_video) ui->cbs.on_download_video(ui->user_ctx);
            break;
        case HIT_DETAIL_AUTHOR:
        case HIT_ACT_CHANNEL:
            if (ui->cbs.on_view_channel) ui->cbs.on_view_channel(ui->user_ctx);
            break;
        case HIT_ACT_RECOMMENDED:
            if (ui->cbs.on_view_recommended) ui->cbs.on_view_recommended(ui->user_ctx);
            break;
        case HIT_ACT_FAVORITE:
            if (ui->cbs.on_toggle_favorite) ui->cbs.on_toggle_favorite(ui->user_ctx);
            break;
        case HIT_ACT_COPY_URL:
            copy_item_field(ui, 0);
            break;
        case HIT_RIGHT_PANE:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_DETAIL);
            break;
        default:
            break;
    }

    wayland_request_redraw(ui->wl);
}

void ui_on_pointer_axis(void *ctx, double dx, double dy) {
    (void)dx;
    UiState *ui = ctx;
    if (ui->show_help) return;
    if (ui->ctx_menu.open) {
        ui->ctx_menu.open = false;
        wayland_request_redraw(ui->wl);
    }

    Layout L;
    compute_layout(ui, &L);

    double step = dy * 4.5;
    if (pt_in_rect(ui->ptr_x, ui->ptr_y, L.right_pane)) {
        ui->desc_scroll_y += step;
        if (ui->desc_scroll_y < 0.0) ui->desc_scroll_y = 0.0;
        if (ui->desc_scroll_y > ui->desc_max_scroll) ui->desc_scroll_y = ui->desc_max_scroll;
        wayland_request_redraw(ui->wl);
    } else if (pt_in_rect(ui->ptr_x, ui->ptr_y, L.left_pane)) {
        ui->list_scroll_y += step;
        clamp_list_scroll(ui, &L);
        maybe_infinite_scroll(ui, &L);
        wayland_request_redraw(ui->wl);
    }
}

static void search_insert_utf8(UiState *ui, const char *utf8) {
    if (!utf8 || !utf8[0]) return;
    if (ui->search_sel_all) {
        ui->search_len = 0;
        ui->search_cursor = 0;
        ui->search_buf[0] = '\0';
        ui->search_sel_all = false;
    }
    size_t add = strlen(utf8);
    if (ui->search_len + add + 1 > ui->search_cap) {
        size_t nc = (ui->search_len + add + 1) * 2;
        char *nb = realloc(ui->search_buf, nc);
        if (!nb) return;
        ui->search_buf = nb;
        ui->search_cap = nc;
    }
    memmove(ui->search_buf + ui->search_cursor + add,
            ui->search_buf + ui->search_cursor,
            ui->search_len - ui->search_cursor + 1);
    memcpy(ui->search_buf + ui->search_cursor, utf8, add);
    ui->search_len += add;
    ui->search_cursor += add;
}

static size_t search_prev_word_pos(const UiState *ui) {
    size_t pos = ui->search_cursor;
    while (pos > 0 && isspace((unsigned char)ui->search_buf[pos - 1])) {
        pos = utf8_prev_char(ui->search_buf, pos);
    }
    while (pos > 0 && !isspace((unsigned char)ui->search_buf[pos - 1])) {
        pos = utf8_prev_char(ui->search_buf, pos);
    }
    return pos;
}

static size_t search_next_word_pos(const UiState *ui) {
    size_t pos = ui->search_cursor;
    while (pos < ui->search_len && isspace((unsigned char)ui->search_buf[pos])) {
        pos = utf8_next_char(ui->search_buf, pos, ui->search_len);
    }
    while (pos < ui->search_len && !isspace((unsigned char)ui->search_buf[pos])) {
        pos = utf8_next_char(ui->search_buf, pos, ui->search_len);
    }
    return pos;
}

static void search_delete_prev_word(UiState *ui) {
    if (ui->search_sel_all) {
        ui->search_len = 0;
        ui->search_cursor = 0;
        ui->search_buf[0] = '\0';
        ui->search_sel_all = false;
        return;
    }
    if (ui->search_cursor == 0) return;
    size_t pos = search_prev_word_pos(ui);
    memmove(ui->search_buf + pos, ui->search_buf + ui->search_cursor,
            ui->search_len - ui->search_cursor + 1);
    ui->search_len -= (ui->search_cursor - pos);
    ui->search_cursor = pos;
}

static void search_delete_next_word(UiState *ui) {
    if (ui->search_sel_all) {
        ui->search_len = 0;
        ui->search_cursor = 0;
        ui->search_buf[0] = '\0';
        ui->search_sel_all = false;
        return;
    }
    if (ui->search_cursor >= ui->search_len) return;
    size_t pos = search_next_word_pos(ui);
    memmove(ui->search_buf + ui->search_cursor, ui->search_buf + pos,
            ui->search_len - pos + 1);
    ui->search_len -= (pos - ui->search_cursor);
}

static void execute_action(UiState *ui, KeyAction act) {
    switch (act) {
        case ACT_QUIT:
            wayland_request_quit(ui->wl);
            break;
        case ACT_PLAY_VIDEO:
            if (ui->selected_idx >= 0 && ui->cbs.on_activate_item) {
                ui->cbs.on_activate_item(ui->user_ctx, (size_t)ui->selected_idx);
            }
            break;
        case ACT_DOWNLOAD_VIDEO:
            if (ui->cbs.on_download_video) ui->cbs.on_download_video(ui->user_ctx);
            break;
        case ACT_VIEW_USER_VIDEOS:
            if (ui->cbs.on_view_channel) ui->cbs.on_view_channel(ui->user_ctx);
            break;
        case ACT_VIEW_RECOMMENDED_VIDEOS:
            if (ui->cbs.on_view_recommended) ui->cbs.on_view_recommended(ui->user_ctx);
            break;
        case ACT_NEXT_PAGE:
            if (!ui->loading_more && ui->list.continuation_token && ui->cbs.on_next_page) {
                ui->loading_more = true;
                ui->cbs.on_next_page(ui->user_ctx);
            }
            break;
        case ACT_COPY_VIDEO_URL:
            copy_item_field(ui, 0);
            break;
        case ACT_COPY_VIDEO_TITLE:
            copy_item_field(ui, 1);
            break;
        case ACT_COPY_VIDEO_DESCRIPTION:
            copy_item_field(ui, 2);
            break;
        case ACT_SHOW_FAVORITES:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_show_favorites) ui->cbs.on_show_favorites(ui->user_ctx);
            break;
        case ACT_SHOW_FEED:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_show_feed) ui->cbs.on_show_feed(ui->user_ctx);
            break;
        case ACT_SHOW_HISTORY:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_show_history) ui->cbs.on_show_history(ui->user_ctx);
            break;
        case ACT_ADD_FAVORITES:
            if (ui->cbs.on_toggle_favorite) ui->cbs.on_toggle_favorite(ui->user_ctx);
            break;
        case ACT_SET_DARK_THEME:
            config_apply_theme(ui->cfg, "dark");
            wayland_set_opaque(ui->wl, ui->cfg->theme.base.bg.a == 255);
            ui_set_status(ui, "Theme: dark");
            break;
        case ACT_SET_LIGHT_THEME:
            config_apply_theme(ui->cfg, "light");
            wayland_set_opaque(ui->wl, ui->cfg->theme.base.bg.a == 255);
            ui_set_status(ui, "Theme: light");
            break;
        case ACT_SET_YUUKA_THEME:
            config_apply_theme(ui->cfg, "yuuka");
            wayland_set_opaque(ui->wl, ui->cfg->theme.base.bg.a == 255);
            ui_set_status(ui, "Theme: yuuka");
            break;
        case ACT_SET_CUSTOM_THEME:
            if (config_apply_theme(ui->cfg, "custom")) {
                wayland_set_opaque(ui->wl, ui->cfg->theme.base.bg.a == 255);
                ui_set_status(ui, "Theme: custom");
            } else {
                ui_set_status(ui, "Custom theme not configured or invalid");
            }
            break;
        case ACT_CLEAR_THUMBNAIL_CACHE:
            if (ui->cbs.on_clear_cache) ui->cbs.on_clear_cache(ui->user_ctx);
            break;
        case ACT_INC_LIST_SIZE:
            ui->cfg->playlist_width += 24;
            if (ui->cfg->playlist_width > 1200) ui->cfg->playlist_width = 1200;
            break;
        case ACT_DEC_LIST_SIZE:
            ui->cfg->playlist_width -= 24;
            if (ui->cfg->playlist_width < 200) ui->cfg->playlist_width = 200;
            break;
        case ACT_FOCUS_SEARCH_INPUT:
            ui->search_sel_all = (ui->search_len > 0);
            ui->search_cursor = ui->search_len;
            set_focus_zone(ui, FOCUS_SEARCH);
            break;
        case ACT_FOCUS_PLAYLIST:
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            break;
        case ACT_NAV_BACK:
            if (ui->cbs.on_navigate_back) ui->cbs.on_navigate_back(ui->user_ctx);
            break;
        case ACT_TOGGLE_HELP:
            toggle_help_modal(ui, !ui->show_help);
            break;
        default:
            break;
    }
    wayland_request_redraw(ui->wl);
}

static void move_selection(UiState *ui, ssize_t delta) {
    if (ui->list.count == 0) return;
    ssize_t next = ui->selected_idx + delta;
    if (next < 0) next = 0;
    if ((size_t)next >= ui->list.count) next = (ssize_t)ui->list.count - 1;
    if (next != ui->selected_idx) {
        select_list_row(ui, (size_t)next);
    }
    Layout L;
    compute_layout(ui, &L);
    ensure_selected_visible(ui, &L);
    maybe_infinite_scroll(ui, &L);
    wayland_request_redraw(ui->wl);
}

static void scroll_desc(UiState *ui, double delta) {
    ui->desc_scroll_y += delta;
    if (ui->desc_scroll_y < 0.0) ui->desc_scroll_y = 0.0;
    if (ui->desc_scroll_y > ui->desc_max_scroll) ui->desc_scroll_y = ui->desc_max_scroll;
    wayland_request_redraw(ui->wl);
}

void ui_on_ime(void *ctx, const WaylandImeEvent *ev) {
    UiState *ui = ctx;
    if (!ui || !ev) return;

    if (ev->reset) {
        ime_clear_preedit(ui);
        wayland_request_redraw(ui->wl);
        return;
    }

    if (ui->focus_zone != FOCUS_SEARCH || ui->show_help) {
        ime_clear_preedit(ui);
        return;
    }

    /* 1. Clear existing preedit string */
    ime_clear_preedit(ui);

    bool text_modified = false;
    bool has_commit = (ev->commit_text && ev->commit_text[0]);
    bool has_preedit = (ev->preedit_text && ev->preedit_text[0]);

    /* 2. Delete requested surrounding text (UTF-8 byte counts around cursor) */
    if (ev->delete_before > 0 || ev->delete_after > 0) {
        if (ui->search_sel_all) {
            ui->search_len = 0;
            ui->search_cursor = 0;
            ui->search_buf[0] = '\0';
            ui->search_sel_all = false;
            text_modified = true;
        }
        size_t before = ev->delete_before > ui->search_cursor ? ui->search_cursor : ev->delete_before;
        size_t after = ev->delete_after > (ui->search_len - ui->search_cursor)
                       ? (ui->search_len - ui->search_cursor) : ev->delete_after;
        size_t del_start = utf8_clamp_pos(ui->search_buf, ui->search_cursor - before, ui->search_len);
        size_t del_end = ui->search_cursor + after;
        while (del_end < ui->search_len && ((unsigned char)ui->search_buf[del_end] & 0xC0u) == 0x80u) {
            del_end++;
        }
        if (del_end > del_start) {
            memmove(ui->search_buf + del_start, ui->search_buf + del_end,
                    ui->search_len - del_end + 1);
            ui->search_len -= (del_end - del_start);
            ui->search_cursor = del_start;
            text_modified = true;
        }
    }

    /* 3. Insert committed UTF-8 string */
    if (has_commit) {
        char *clean = tt_strdup(ev->commit_text);
        if (clean) {
            for (char *p = clean; *p; p++) {
                if (*p == '\r' || *p == '\n') *p = ' ';
            }
            search_insert_utf8(ui, clean);
            free(clean);
            text_modified = true;
        }
    }

    /* 4. Sync updated surrounding text back to IME */
    if (text_modified) {
        ime_sync_surrounding(ui, false);
    }

    /* 5. Set new inline preedit string */
    if (has_preedit) {
        if (ui->search_sel_all) {
            ui->search_len = 0;
            ui->search_cursor = 0;
            ui->search_buf[0] = '\0';
            ui->search_sel_all = false;
            ime_sync_surrounding(ui, false);
        }
        ui->ime_preedit = tt_strdup(ev->preedit_text);
        if (ui->ime_preedit) {
            ui->ime_preedit_len = strlen(ui->ime_preedit);
            ui->ime_cursor_begin = ev->preedit_cursor_begin;
            ui->ime_cursor_end = ev->preedit_cursor_end;
        }
    }

    wayland_request_redraw(ui->wl);
}

static bool is_modifier_keysym(xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_Shift_L:
        case XKB_KEY_Shift_R:
        case XKB_KEY_Control_L:
        case XKB_KEY_Control_R:
        case XKB_KEY_Alt_L:
        case XKB_KEY_Alt_R:
        case XKB_KEY_Super_L:
        case XKB_KEY_Super_R:
        case XKB_KEY_Meta_L:
        case XKB_KEY_Meta_R:
        case XKB_KEY_ISO_Level3_Shift:
        case XKB_KEY_Caps_Lock:
        case XKB_KEY_Num_Lock:
            return true;
        default:
            return false;
    }
}

void ui_on_key(void *ctx, uint32_t mods, xkb_keysym_t sym,
               const char *utf8, bool pressed) {
    if (!pressed) return;
    UiState *ui = ctx;

    if (ui->show_help) {
        if (sym == XKB_KEY_Escape ||
            config_match_keybinding(ui->cfg, mods, sym) == ACT_TOGGLE_HELP) {
            toggle_help_modal(ui, false);
            wayland_request_redraw(ui->wl);
        }
        return;
    }

    if (ui->ctx_menu.open) {
        if (sym == XKB_KEY_Escape) {
            ui->ctx_menu.open = false;
            wayland_request_redraw(ui->wl);
            return;
        }
        if (!is_modifier_keysym(sym)) {
            ui->ctx_menu.open = false;
        }
    }

    /* Cycle FocusZone on Tab / Shift+Tab */
    if ((sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) &&
        (mods & ~(TT_MOD_SHIFT)) == 0) {
        bool rev = (sym == XKB_KEY_ISO_Left_Tab) || ((mods & TT_MOD_SHIFT) != 0);
        ui->search_sel_all = false;
        FocusZone next_z = rev ? (FocusZone)(((int)ui->focus_zone + 2) % 3)
                               : (FocusZone)(((int)ui->focus_zone + 1) % 3);
        set_focus_zone(ui, next_z);
        wayland_request_redraw(ui->wl);
        return;
    }

    if (ui->focus_zone == FOCUS_SEARCH) {
        if (sym == XKB_KEY_Escape) {
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            ime_commit_preedit_locally(ui);
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->cbs.on_search) ui->cbs.on_search(ui->user_ctx, ui->search_buf, ui->filter);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_Down && (mods == 0) && ui->list.count > 0) {
            ui->search_sel_all = false;
            set_focus_zone(ui, FOCUS_LIST);
            if (ui->selected_idx < 0) select_list_row(ui, 0);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_a || sym == XKB_KEY_A)) {
            ime_commit_preedit_locally(ui);
            ui->search_sel_all = (ui->search_len > 0);
            ui->search_cursor = ui->search_len;
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_c || sym == XKB_KEY_C)) {
            if (ui->search_len > 0) wayland_clipboard_copy(ui->wl, ui->search_buf);
            return;
        }
        if ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_x || sym == XKB_KEY_X)) {
            ime_commit_preedit_locally(ui);
            if (ui->search_len > 0) {
                wayland_clipboard_copy(ui->wl, ui->search_buf);
                ui->search_len = 0;
                ui->search_cursor = 0;
                ui->search_buf[0] = '\0';
                ui->search_sel_all = false;
                ime_sync_surrounding(ui, true);
                wayland_request_redraw(ui->wl);
            }
            return;
        }
        if ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_v || sym == XKB_KEY_V)) {
            ime_clear_preedit(ui);
            char *pasted = wayland_clipboard_paste(ui->wl);
            if (pasted) {
                for (char *p = pasted; *p; p++) {
                    if (*p == '\r' || *p == '\n') *p = ' ';
                }
                search_insert_utf8(ui, pasted);
                free(pasted);
                ime_sync_surrounding(ui, true);
                wayland_request_redraw(ui->wl);
            }
            return;
        }
        if ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
            ime_clear_preedit(ui);
            if (ui->search_sel_all) {
                ui->search_len = 0;
                ui->search_cursor = 0;
                ui->search_buf[0] = '\0';
                ui->search_sel_all = false;
            } else if (ui->search_cursor > 0) {
                memmove(ui->search_buf, ui->search_buf + ui->search_cursor,
                        ui->search_len - ui->search_cursor + 1);
                ui->search_len -= ui->search_cursor;
                ui->search_cursor = 0;
            }
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_k || sym == XKB_KEY_K)) {
            ime_clear_preedit(ui);
            if (ui->search_sel_all) {
                ui->search_len = 0;
                ui->search_cursor = 0;
                ui->search_buf[0] = '\0';
                ui->search_sel_all = false;
            } else if (ui->search_cursor < ui->search_len) {
                ui->search_len = ui->search_cursor;
                ui->search_buf[ui->search_len] = '\0';
            }
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (((mods & TT_MOD_CTRL) && sym == XKB_KEY_BackSpace) ||
            ((mods & TT_MOD_CTRL) && (sym == XKB_KEY_w || sym == XKB_KEY_W))) {
            ime_clear_preedit(ui);
            search_delete_prev_word(ui);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods & TT_MOD_CTRL) && sym == XKB_KEY_Delete) {
            ime_clear_preedit(ui);
            search_delete_next_word(ui);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods & TT_MOD_CTRL) && sym == XKB_KEY_Left) {
            ime_commit_preedit_locally(ui);
            ui->search_sel_all = false;
            ui->search_cursor = search_prev_word_pos(ui);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods & TT_MOD_CTRL) && sym == XKB_KEY_Right) {
            ime_commit_preedit_locally(ui);
            ui->search_sel_all = false;
            ui->search_cursor = search_next_word_pos(ui);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if ((mods != 0 && (mods & ~TT_MOD_SHIFT) != 0) ||
            (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)) {
            KeyAction act = config_match_keybinding(ui->cfg, mods, sym);
            if (act != ACT_NONE) {
                execute_action(ui, act);
                return;
            }
        }
        if (sym == XKB_KEY_BackSpace) {
            if (ui->search_sel_all) {
                ui->search_len = 0;
                ui->search_cursor = 0;
                ui->search_buf[0] = '\0';
                ui->search_sel_all = false;
            } else if (ui->search_cursor > 0) {
                size_t prev = utf8_prev_char(ui->search_buf, ui->search_cursor);
                memmove(ui->search_buf + prev, ui->search_buf + ui->search_cursor,
                        ui->search_len - ui->search_cursor + 1);
                ui->search_len -= (ui->search_cursor - prev);
                ui->search_cursor = prev;
            }
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_Delete) {
            if (ui->search_sel_all) {
                ui->search_len = 0;
                ui->search_cursor = 0;
                ui->search_buf[0] = '\0';
                ui->search_sel_all = false;
            } else if (ui->search_cursor < ui->search_len) {
                size_t next = utf8_next_char(ui->search_buf, ui->search_cursor, ui->search_len);
                memmove(ui->search_buf + ui->search_cursor, ui->search_buf + next,
                        ui->search_len - next + 1);
                ui->search_len -= (next - ui->search_cursor);
            }
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_Left) {
            ui->search_sel_all = false;
            ui->search_cursor = utf8_prev_char(ui->search_buf, ui->search_cursor);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_Right) {
            ui->search_sel_all = false;
            ui->search_cursor = utf8_next_char(ui->search_buf, ui->search_cursor, ui->search_len);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_Home) {
            ui->search_sel_all = false;
            ui->search_cursor = 0;
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (sym == XKB_KEY_End) {
            ui->search_sel_all = false;
            ui->search_cursor = ui->search_len;
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        if (utf8 && utf8[0] && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f &&
            (mods & (TT_MOD_CTRL | TT_MOD_ALT | TT_MOD_SUPER)) == 0) {
            search_insert_utf8(ui, utf8);
            ime_sync_surrounding(ui, true);
            wayland_request_redraw(ui->wl);
            return;
        }
        return;
    }

    KeyAction act = config_match_keybinding(ui->cfg, mods, sym);
    if (act != ACT_NONE) {
        execute_action(ui, act);
        return;
    }

    if (ui->focus_zone == FOCUS_DETAIL) {
        Layout L;
        compute_layout(ui, &L);
        double page_step = L.desc_area.h > 40.0 ? L.desc_area.h * 0.85 : 160.0;
        double line_step = ui->cfg->font_size * 2.5;
        if (sym == XKB_KEY_Up || sym == XKB_KEY_k || sym == XKB_KEY_K) scroll_desc(ui, -line_step);
        else if (sym == XKB_KEY_Down || sym == XKB_KEY_j || sym == XKB_KEY_J) scroll_desc(ui, line_step);
        else if (sym == XKB_KEY_Page_Up) scroll_desc(ui, -page_step);
        else if (sym == XKB_KEY_Page_Down) scroll_desc(ui, page_step);
        else if (sym == XKB_KEY_Home) scroll_desc(ui, -ui->desc_max_scroll - 1.0);
        else if (sym == XKB_KEY_End) scroll_desc(ui, ui->desc_max_scroll + 1.0);
        return;
    }

    if ((mods & TT_MOD_SHIFT) && (sym == XKB_KEY_Up || sym == XKB_KEY_K || sym == XKB_KEY_k)) {
        scroll_desc(ui, -ui->cfg->font_size * 3.0);
    } else if ((mods & TT_MOD_SHIFT) && (sym == XKB_KEY_Down || sym == XKB_KEY_J || sym == XKB_KEY_j)) {
        scroll_desc(ui, ui->cfg->font_size * 3.0);
    } else if (sym == XKB_KEY_Up || sym == XKB_KEY_k || sym == XKB_KEY_K) {
        move_selection(ui, -1);
    } else if (sym == XKB_KEY_Down || sym == XKB_KEY_j || sym == XKB_KEY_J) {
        move_selection(ui, 1);
    } else if (sym == XKB_KEY_Page_Up) {
        move_selection(ui, -8);
    } else if (sym == XKB_KEY_Page_Down) {
        move_selection(ui, 8);
    } else if (sym == XKB_KEY_Home) {
        move_selection(ui, -(ssize_t)ui->list.count);
    } else if (sym == XKB_KEY_End) {
        move_selection(ui, (ssize_t)ui->list.count);
    }
}

bool ui_can_escape_internally(const UiState *ui) {
    if (!ui) return false;
    if (ui->show_help || ui->ctx_menu.open) return true;
    if (ui->focus_zone == FOCUS_SEARCH &&
        (ui->search_len > 0 || ui->ime_preedit_len > 0 || ui->list.count > 0)) {
        return true;
    }
    return false;
}

void ui_release_hidden_caches(UiState *ui) {
    if (!ui) return;
    thumb_cache_flush_ram(ui->thumbs);
    str_list_free(&ui->inflight_keys, &ui->inflight_count, &ui->inflight_cap);
    str_list_free(&ui->failed_keys, &ui->failed_count, &ui->failed_cap);
}
