#include "innertube.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define INNERTUBE_CLIENT_VER "2.20260708.00.00"
#define INNERTUBE_MAX_JSON (16u * 1024u * 1024u)

static const char *const INNERTUBE_HEADERS[] = {
    "Content-Type: application/json",
    "Origin: https://www.youtube.com",
    "Referer: https://www.youtube.com/"
};

bool innertube_encode_search_params(SearchFilter filter, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return false;
    uint8_t buf[16];
    size_t n = 0;

    uint8_t sort_val = 0;
    switch (filter.sort) {
        case SORT_RELEVANCE:   sort_val = 0; break;
        case SORT_RATING:      sort_val = 1; break;
        case SORT_UPLOAD_DATE: sort_val = 2; break;
        case SORT_VIEW_COUNT:  sort_val = 3; break;
    }
    if (filter.sort != SORT_RELEVANCE || (filter.type == TYPE_ALL && !filter.live_only)) {
        buf[n++] = 0x08; /* field 1, varint */
        buf[n++] = sort_val;
    }

    if (filter.type != TYPE_ALL || filter.live_only) {
        uint8_t sub[8];
        size_t sn = 0;
        if (filter.type != TYPE_ALL) {
            uint8_t tv = 1;
            if (filter.type == TYPE_CHANNEL) tv = 2;
            else if (filter.type == TYPE_PLAYLIST) tv = 3;
            sub[sn++] = 0x10; /* sub-field 2, varint */
            sub[sn++] = tv;
        }
        if (filter.live_only) {
            sub[sn++] = 0x40; /* sub-field 8, varint */
            sub[sn++] = 0x01;
        }
        buf[n++] = 0x12; /* field 2, length-delimited */
        buf[n++] = (uint8_t)sn;
        memcpy(buf + n, sub, sn);
        n += sn;
    }

    return tt_base64_encode(buf, n, out, out_cap) > 0;
}

static void copy_id_token(const char *src, char *dst, size_t cap) {
    size_t i = 0;
    while (src[i] && src[i] != '&' && src[i] != '#' && src[i] != '?' && src[i] != '/' &&
           !isspace((unsigned char)src[i]) && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool has_path_seg(const char *p, const char *seg) {
    if (!p || !seg) return false;
    size_t slen = strlen(seg);
    if (strncmp(p, seg, slen) != 0) return false;
    char next = p[slen];
    return next == '\0' || next == '/' || next == '?' || next == '#' ||
           isspace((unsigned char)next);
}

bool innertube_parse_input_url(const char *input, ParsedUrl *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!input) return false;

    while (*input && isspace((unsigned char)*input)) input++;
    if (!*input) return false;

    const char *p = input;
    if (tt_strncasecmp(p, "https://", 8) == 0) p += 8;
    else if (tt_strncasecmp(p, "http://", 7) == 0) p += 7;

    if (tt_strncasecmp(p, "www.", 4) == 0) p += 4;
    else if (tt_strncasecmp(p, "m.", 2) == 0) p += 2;
    else if (tt_strncasecmp(p, "music.", 6) == 0) p += 6;

    if (tt_strncasecmp(p, "youtu.be/", 9) == 0) {
        p += 9;
        copy_id_token(p, out->id, sizeof(out->id));
        if (out->id[0]) {
            out->kind = URL_VIDEO;
            return true;
        }
        return false;
    }

    if (tt_strncasecmp(p, "youtube.com/", 12) != 0) {
        return false;
    }
    p += 12;

    if (strncmp(p, "watch", 5) == 0 && (p[5] == '?' || p[5] == '/')) {
        const char *q = strchr(p, '?');
        if (q) {
            const char *v = strstr(q, "v=");
            if (v && (v == q + 1 || v[-1] == '&')) {
                copy_id_token(v + 2, out->id, sizeof(out->id));
                if (out->id[0]) {
                    out->kind = URL_VIDEO;
                    return true;
                }
            }
        }
        return false;
    }

    if (strncmp(p, "shorts/", 7) == 0) {
        copy_id_token(p + 7, out->id, sizeof(out->id));
        if (out->id[0]) { out->kind = URL_VIDEO; return true; }
        return false;
    }
    if (strncmp(p, "live/", 5) == 0) {
        copy_id_token(p + 5, out->id, sizeof(out->id));
        if (out->id[0]) { out->kind = URL_VIDEO; return true; }
        return false;
    }
    if (strncmp(p, "embed/", 6) == 0) {
        copy_id_token(p + 6, out->id, sizeof(out->id));
        if (out->id[0]) { out->kind = URL_VIDEO; return true; }
        return false;
    }

    if (strncmp(p, "playlist", 8) == 0) {
        const char *q = strchr(p, '?');
        if (q) {
            const char *l = strstr(q, "list=");
            if (l && (l == q + 1 || l[-1] == '&')) {
                copy_id_token(l + 5, out->id, sizeof(out->id));
                if (out->id[0]) {
                    out->kind = URL_PLAYLIST;
                    return true;
                }
            }
        }
        return false;
    }

    if (strncmp(p, "channel/", 8) == 0) {
        copy_id_token(p + 8, out->id, sizeof(out->id));
        if (out->id[0]) {
            const char *after = p + 8 + strlen(out->id);
            if (has_path_seg(after, "/streams") || has_path_seg(after, "/live")) {
                out->prefer_live_tab = true;
            }
            out->kind = URL_CHANNEL;
            return true;
        }
        return false;
    }

    if (p[0] == '@') {
        copy_id_token(p, out->id, sizeof(out->id));
        if (out->id[1] != '\0') {
            const char *after = p + strlen(out->id);
            out->kind = URL_HANDLE;
            if (has_path_seg(after, "/streams")) {
                out->prefer_live_tab = true;
                snprintf(out->resolved_url, sizeof(out->resolved_url),
                         "https://www.youtube.com/%s", out->id);
            } else if (has_path_seg(after, "/live")) {
                out->prefer_live_tab = true;
                snprintf(out->resolved_url, sizeof(out->resolved_url),
                         "https://www.youtube.com/%s/live", out->id);
            } else {
                snprintf(out->resolved_url, sizeof(out->resolved_url),
                         "https://www.youtube.com/%s", out->id);
            }
            return true;
        }
        return false;
    }

    return false;
}

static char *extract_text(yyjson_val *v) {
    if (!v) return NULL;
    if (yyjson_is_str(v)) return tt_strdup(yyjson_get_str(v));
    if (!yyjson_is_obj(v)) return NULL;

    yyjson_val *simple = yyjson_obj_get(v, "simpleText");
    if (simple && yyjson_is_str(simple)) {
        return tt_strdup(yyjson_get_str(simple));
    }

    yyjson_val *content = yyjson_obj_get(v, "content");
    if (content && yyjson_is_str(content)) {
        return tt_strdup(yyjson_get_str(content));
    }

    yyjson_val *runs = yyjson_obj_get(v, "runs");
    if (runs && yyjson_is_arr(runs)) {
        StrBuf sb;
        sb_init(&sb);
        size_t idx, max;
        yyjson_val *r;
        yyjson_arr_foreach(runs, idx, max, r) {
            yyjson_val *t = yyjson_obj_get(r, "text");
            if (t && yyjson_is_str(t)) {
                sb_append(&sb, yyjson_get_str(t));
            }
        }
        if (sb.len > 0) return sb_take(&sb);
        sb_free(&sb);
    }
    return NULL;
}

static char *normalize_thumb_url(const char *raw) {
    if (!raw || !raw[0]) return NULL;
    if (strncmp(raw, "//", 2) == 0) {
        return tt_asprintf("https:%s", raw);
    }
    return tt_strdup(raw);
}

static char *extract_thumb(yyjson_val *obj) {
    if (!obj || !yyjson_is_obj(obj)) return NULL;
    yyjson_val *arr = yyjson_obj_get(obj, "thumbnails");
    if (!arr) arr = yyjson_obj_get(obj, "sources");
    if (!arr || !yyjson_is_arr(arr)) return NULL;
    size_t n = yyjson_arr_size(arr);
    if (n == 0) return NULL;
    /* Pick last (or medium) entry */
    size_t pick = (n >= 2) ? 1 : 0;
    yyjson_val *entry = yyjson_arr_get(arr, pick);
    yyjson_val *url = entry ? yyjson_obj_get(entry, "url") : NULL;
    if (!url || !yyjson_is_str(url)) {
        entry = yyjson_arr_get(arr, n - 1);
        url = entry ? yyjson_obj_get(entry, "url") : NULL;
    }
    if (url && yyjson_is_str(url)) {
        return normalize_thumb_url(yyjson_get_str(url));
    }
    return NULL;
}

static bool check_live_badge_obj(yyjson_val *obj) {
    if (!obj || !yyjson_is_obj(obj)) return false;
    yyjson_val *style = yyjson_obj_get(obj, "style");
    if (!style) style = yyjson_obj_get(obj, "badgeStyle");
    if (style && yyjson_is_str(style) &&
        strstr(yyjson_get_str(style), "LIVE") != NULL) {
        return true;
    }
    static const char *const txt_keys[] = {"label", "text", "badgeText"};
    for (size_t i = 0; i < 3; i++) {
        yyjson_val *tv = yyjson_obj_get(obj, txt_keys[i]);
        if (tv) {
            char *s = extract_text(tv);
            bool match = (s && tt_strcasecmp(s, "LIVE") == 0);
            free(s);
            if (match) return true;
        }
    }
    return false;
}

static bool has_live_badge(yyjson_val *badges) {
    if (!badges || !yyjson_is_arr(badges)) return false;
    static const char *const keys[] = {
        "metadataBadgeRenderer",
        "liveBadgeRenderer",
        "metadataBadgeViewModel",
        "badgeViewModel"
    };
    size_t idx, max;
    yyjson_val *b;
    yyjson_arr_foreach(badges, idx, max, b) {
        if (!b || !yyjson_is_obj(b)) continue;
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
            if (check_live_badge_obj(yyjson_obj_get(b, keys[k]))) return true;
        }
    }
    return false;
}

static bool check_members_only_badge_obj(yyjson_val *obj) {
    if (!obj || !yyjson_is_obj(obj)) return false;
    yyjson_val *style = yyjson_obj_get(obj, "style");
    if (!style) style = yyjson_obj_get(obj, "badgeStyle");
    if (style && yyjson_is_str(style) &&
        strstr(yyjson_get_str(style), "MEMBERS_ONLY") != NULL) {
        return true;
    }
    static const char *const txt_keys[] = {
        "label", "text", "badgeText", "tooltip", "iconTooltip"
    };
    for (size_t i = 0; i < sizeof(txt_keys) / sizeof(txt_keys[0]); i++) {
        yyjson_val *tv = yyjson_obj_get(obj, txt_keys[i]);
        if (tv) {
            char *s = extract_text(tv);
            bool match = (s && tt_strcasestr(s, "members only") != NULL);
            free(s);
            if (match) return true;
        }
    }
    yyjson_val *acc = yyjson_obj_get(obj, "accessibilityData");
    if (!acc) {
        yyjson_val *outer_acc = yyjson_obj_get(obj, "accessibility");
        acc = outer_acc ? yyjson_obj_get(outer_acc, "accessibilityData") : NULL;
    }
    yyjson_val *albl = acc ? yyjson_obj_get(acc, "label") : NULL;
    if (albl && yyjson_is_str(albl) &&
        tt_strcasestr(yyjson_get_str(albl), "members only") != NULL) {
        return true;
    }
    return false;
}

static bool has_members_only_badge(yyjson_val *badges) {
    if (!badges || !yyjson_is_arr(badges)) return false;
    static const char *const keys[] = {
        "metadataBadgeRenderer",
        "metadataBadgeViewModel",
        "badgeViewModel"
    };
    size_t idx, max;
    yyjson_val *b;
    yyjson_arr_foreach(badges, idx, max, b) {
        if (!b || !yyjson_is_obj(b)) continue;
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
            if (check_members_only_badge_obj(yyjson_obj_get(b, keys[k]))) return true;
        }
    }
    return false;
}

static void parse_thumbnail_overlays(yyjson_val *overlays, MediaItem *it) {
    if (!overlays || !yyjson_is_arr(overlays)) return;
    size_t idx, max;
    yyjson_val *ov;
    yyjson_arr_foreach(overlays, idx, max, ov) {
        yyjson_val *tsr = yyjson_obj_get(ov, "thumbnailOverlayTimeStatusRenderer");
        if (!tsr || !yyjson_is_obj(tsr)) continue;
        yyjson_val *st = yyjson_obj_get(tsr, "style");
        const char *st_str = (st && yyjson_is_str(st)) ? yyjson_get_str(st) : NULL;
        if (st_str && strcmp(st_str, "LIVE") == 0) {
            it->is_live = true;
        }
        if (!it->duration_text) {
            char *t = extract_text(yyjson_obj_get(tsr, "text"));
            if (t && t[0]) {
                it->duration_text = t;
            } else {
                free(t);
                if (st_str && strcmp(st_str, "UPCOMING") == 0) {
                    it->duration_text = tt_strdup("UPCOMING");
                }
            }
        }
    }
}

static void parse_video_renderer(yyjson_val *vr, MediaList *out_list) {
    if (!vr || !yyjson_is_obj(vr)) return;
    yyjson_val *vid = yyjson_obj_get(vr, "videoId");
    if (!vid || !yyjson_is_str(vid)) return;

    MediaItem it = {0};
    it.kind = ITEM_VIDEO;
    snprintf(it.id, sizeof(it.id), "%s", yyjson_get_str(vid));
    it.title = extract_text(yyjson_obj_get(vr, "title"));

    yyjson_val *owner = yyjson_obj_get(vr, "ownerText");
    if (!owner) owner = yyjson_obj_get(vr, "longBylineText");
    if (!owner) owner = yyjson_obj_get(vr, "shortBylineText");
    it.author = extract_text(owner);

    if (owner && yyjson_is_obj(owner)) {
        yyjson_val *runs = yyjson_obj_get(owner, "runs");
        yyjson_val *r0 = runs ? yyjson_arr_get(runs, 0) : NULL;
        yyjson_val *nav = r0 ? yyjson_obj_get(r0, "navigationEndpoint") : NULL;
        yyjson_val *be = nav ? yyjson_obj_get(nav, "browseEndpoint") : NULL;
        yyjson_val *bid = be ? yyjson_obj_get(be, "browseId") : NULL;
        if (bid && yyjson_is_str(bid)) {
            it.author_id = tt_strdup(yyjson_get_str(bid));
        }
    }

    it.duration_text = extract_text(yyjson_obj_get(vr, "lengthText"));
    yyjson_val *vc = yyjson_obj_get(vr, "shortViewCountText");
    if (!vc) vc = yyjson_obj_get(vr, "viewCountText");
    it.view_count_text = extract_text(vc);
    it.published_text = extract_text(yyjson_obj_get(vr, "publishedTimeText"));
    yyjson_val *vbadges = yyjson_obj_get(vr, "badges");
    it.is_live = has_live_badge(vbadges);
    it.is_members_only = has_members_only_badge(vbadges);
    parse_thumbnail_overlays(yyjson_obj_get(vr, "thumbnailOverlays"), &it);
    if (!it.duration_text && it.view_count_text &&
        tt_strcasestr(it.view_count_text, "watching") != NULL) {
        it.is_live = true;
    }
    it.description = extract_text(yyjson_obj_get(vr, "descriptionSnippet"));
    if (!it.description) {
        yyjson_val *dms = yyjson_obj_get(vr, "detailedMetadataSnippets");
        yyjson_val *d0 = (dms && yyjson_is_arr(dms)) ? yyjson_arr_get(dms, 0) : NULL;
        if (d0) {
            it.description = extract_text(yyjson_obj_get(d0, "snippetText"));
        }
    }
    if (it.is_live && (!it.duration_text || !it.duration_text[0])) {
        free(it.duration_text);
        it.duration_text = tt_strdup("LIVE");
    }
    it.thumbnail_url = extract_thumb(yyjson_obj_get(vr, "thumbnail"));
    if (!it.thumbnail_url && it.id[0]) {
        it.thumbnail_url = tt_asprintf("https://i.ytimg.com/vi/%s/mqdefault.jpg", it.id);
    }

    if (!media_list_append_move(out_list, &it)) {
        media_item_free_fields(&it);
    }
}

static void parse_channel_renderer(yyjson_val *cr, MediaList *out_list) {
    if (!cr || !yyjson_is_obj(cr)) return;
    yyjson_val *cid = yyjson_obj_get(cr, "channelId");
    if (!cid || !yyjson_is_str(cid)) return;

    MediaItem it = {0};
    it.kind = ITEM_CHANNEL;
    snprintf(it.id, sizeof(it.id), "%s", yyjson_get_str(cid));
    it.title = extract_text(yyjson_obj_get(cr, "title"));
    it.author = tt_strdup(it.title);
    it.author_id = tt_strdup(it.id);
    it.view_count_text = extract_text(yyjson_obj_get(cr, "subscriberCountText"));
    it.description = extract_text(yyjson_obj_get(cr, "descriptionSnippet"));
    it.thumbnail_url = extract_thumb(yyjson_obj_get(cr, "thumbnail"));

    if (!media_list_append_move(out_list, &it)) {
        media_item_free_fields(&it);
    }
}

static void parse_playlist_renderer(yyjson_val *pr, MediaList *out_list) {
    if (!pr || !yyjson_is_obj(pr)) return;
    yyjson_val *pid = yyjson_obj_get(pr, "playlistId");
    if (!pid || !yyjson_is_str(pid)) return;

    MediaItem it = {0};
    it.kind = ITEM_PLAYLIST;
    snprintf(it.id, sizeof(it.id), "%s", yyjson_get_str(pid));
    it.title = extract_text(yyjson_obj_get(pr, "title"));

    yyjson_val *by = yyjson_obj_get(pr, "longBylineText");
    if (!by) by = yyjson_obj_get(pr, "shortBylineText");
    it.author = extract_text(by);
    it.duration_text = extract_text(yyjson_obj_get(pr, "videoCountText"));

    yyjson_val *thumbs = yyjson_obj_get(pr, "thumbnails");
    if (thumbs && yyjson_is_arr(thumbs)) {
        it.thumbnail_url = extract_thumb(yyjson_arr_get(thumbs, 0));
    }

    if (!media_list_append_move(out_list, &it)) {
        media_item_free_fields(&it);
    }
}

static void parse_playlist_video_renderer(yyjson_val *pvr, MediaList *out_list) {
    if (!pvr || !yyjson_is_obj(pvr)) return;
    yyjson_val *vid = yyjson_obj_get(pvr, "videoId");
    if (!vid || !yyjson_is_str(vid)) return;

    MediaItem it = {0};
    it.kind = ITEM_VIDEO;
    snprintf(it.id, sizeof(it.id), "%s", yyjson_get_str(vid));
    it.title = extract_text(yyjson_obj_get(pvr, "title"));

    yyjson_val *owner = yyjson_obj_get(pvr, "shortBylineText");
    it.author = extract_text(owner);
    if (owner && yyjson_is_obj(owner)) {
        yyjson_val *runs = yyjson_obj_get(owner, "runs");
        yyjson_val *r0 = runs ? yyjson_arr_get(runs, 0) : NULL;
        yyjson_val *nav = r0 ? yyjson_obj_get(r0, "navigationEndpoint") : NULL;
        yyjson_val *be = nav ? yyjson_obj_get(nav, "browseEndpoint") : NULL;
        yyjson_val *bid = be ? yyjson_obj_get(be, "browseId") : NULL;
        if (bid && yyjson_is_str(bid)) it.author_id = tt_strdup(yyjson_get_str(bid));
    }

    it.duration_text = extract_text(yyjson_obj_get(pvr, "lengthText"));
    yyjson_val *pbadges = yyjson_obj_get(pvr, "badges");
    it.is_live = has_live_badge(pbadges);
    it.is_members_only = has_members_only_badge(pbadges);
    parse_thumbnail_overlays(yyjson_obj_get(pvr, "thumbnailOverlays"), &it);
    if (it.is_live && (!it.duration_text || !it.duration_text[0])) {
        free(it.duration_text);
        it.duration_text = tt_strdup("LIVE");
    }
    it.thumbnail_url = extract_thumb(yyjson_obj_get(pvr, "thumbnail"));
    if (!it.thumbnail_url && it.id[0]) {
        it.thumbnail_url = tt_asprintf("https://i.ytimg.com/vi/%s/mqdefault.jpg", it.id);
    }

    if (!media_list_append_move(out_list, &it)) {
        media_item_free_fields(&it);
    }
}

static void process_lockup_overlay_badges(yyjson_val *badges, MediaItem *it) {
    if (!badges || !yyjson_is_arr(badges)) return;
    size_t bidx, bmax;
    yyjson_val *belem;
    yyjson_arr_foreach(badges, bidx, bmax, belem) {
        yyjson_val *tbvm = yyjson_obj_get(belem, "thumbnailBadgeViewModel");
        if (!tbvm || !yyjson_is_obj(tbvm)) continue;
        yyjson_val *bs = yyjson_obj_get(tbvm, "badgeStyle");
        if (bs && yyjson_is_str(bs)) {
            const char *bss = yyjson_get_str(bs);
            if (strstr(bss, "LIVE") != NULL) it->is_live = true;
            if (strstr(bss, "MEMBERS_ONLY") != NULL) it->is_members_only = true;
        }
        char *btxt = extract_text(yyjson_obj_get(tbvm, "text"));
        if (btxt) {
            if (tt_strcasecmp(btxt, "LIVE") == 0) {
                it->is_live = true;
                if (!it->duration_text) it->duration_text = btxt;
                else free(btxt);
            } else if (tt_strcasestr(btxt, "members only") != NULL) {
                it->is_members_only = true;
                free(btxt);
            } else if (!it->duration_text) {
                it->duration_text = btxt;
            } else {
                free(btxt);
            }
        }
    }
}

static void parse_lockup_view_model(yyjson_val *lvm, MediaList *out_list) {
    if (!lvm || !yyjson_is_obj(lvm)) return;

    yyjson_val *ctype = yyjson_obj_get(lvm, "contentType");
    const char *ct = (ctype && yyjson_is_str(ctype)) ? yyjson_get_str(ctype) : "";

    yyjson_val *cid_v = yyjson_obj_get(lvm, "contentId");
    const char *cid = (cid_v && yyjson_is_str(cid_v)) ? yyjson_get_str(cid_v) : "";

    yyjson_val *rc = yyjson_obj_get(lvm, "rendererContext");
    yyjson_val *cc = rc ? yyjson_obj_get(rc, "commandContext") : NULL;
    yyjson_val *ontap = cc ? yyjson_obj_get(cc, "onTap") : NULL;
    yyjson_val *ic = ontap ? yyjson_obj_get(ontap, "innertubeCommand") : NULL;
    yyjson_val *we = ic ? yyjson_obj_get(ic, "watchEndpoint") : NULL;
    yyjson_val *be = ic ? yyjson_obj_get(ic, "browseEndpoint") : NULL;

    MediaItem it = {0};
    if (strcmp(ct, "LOCKUP_CONTENT_TYPE_PLAYLIST") == 0 ||
        strcmp(ct, "LOCKUP_CONTENT_TYPE_PODCAST") == 0) {
        it.kind = ITEM_PLAYLIST;
    } else if (strcmp(ct, "LOCKUP_CONTENT_TYPE_CHANNEL") == 0) {
        it.kind = ITEM_CHANNEL;
    } else if (we) {
        it.kind = ITEM_VIDEO;
    } else if (be) {
        yyjson_val *bid = yyjson_obj_get(be, "browseId");
        const char *bstr = (bid && yyjson_is_str(bid)) ? yyjson_get_str(bid) : "";
        if (strncmp(bstr, "VL", 2) == 0) it.kind = ITEM_PLAYLIST;
        else it.kind = ITEM_CHANNEL;
    } else {
        it.kind = ITEM_VIDEO;
    }

    if (it.kind == ITEM_VIDEO) {
        yyjson_val *wvid = we ? yyjson_obj_get(we, "videoId") : NULL;
        const char *vid = (wvid && yyjson_is_str(wvid)) ? yyjson_get_str(wvid) : cid;
        if (!vid || !vid[0]) return;
        snprintf(it.id, sizeof(it.id), "%s", vid);
    } else {
        const char *id_src = cid;
        if ((!id_src || !id_src[0]) && be) {
            yyjson_val *bid = yyjson_obj_get(be, "browseId");
            if (bid && yyjson_is_str(bid)) {
                id_src = yyjson_get_str(bid);
                if (it.kind == ITEM_PLAYLIST && strncmp(id_src, "VL", 2) == 0) {
                    id_src += 2;
                }
            }
        }
        if (!id_src || !id_src[0]) return;
        snprintf(it.id, sizeof(it.id), "%s", id_src);
    }

    yyjson_val *meta = yyjson_obj_get(lvm, "metadata");
    yyjson_val *lm = meta ? yyjson_obj_get(meta, "lockupMetadataViewModel") : NULL;
    if (lm) {
        it.title = extract_text(yyjson_obj_get(lm, "title"));

        yyjson_val *lmb = yyjson_obj_get(lm, "badges");
        if (has_live_badge(lmb)) it.is_live = true;
        if (has_members_only_badge(lmb)) it.is_members_only = true;

        yyjson_val *limg = yyjson_obj_get(lm, "image");
        yyjson_val *davm = limg ? yyjson_obj_get(limg, "decoratedAvatarViewModel") : NULL;
        yyjson_val *drc = davm ? yyjson_obj_get(davm, "rendererContext") : NULL;
        yyjson_val *dcc = drc ? yyjson_obj_get(drc, "commandContext") : NULL;
        yyjson_val *dontap = dcc ? yyjson_obj_get(dcc, "onTap") : NULL;
        yyjson_val *dic = dontap ? yyjson_obj_get(dontap, "innertubeCommand") : NULL;
        yyjson_val *dbe = dic ? yyjson_obj_get(dic, "browseEndpoint") : NULL;
        yyjson_val *dbid = dbe ? yyjson_obj_get(dbe, "browseId") : NULL;
        if (dbid && yyjson_is_str(dbid) && strncmp(yyjson_get_str(dbid), "UC", 2) == 0) {
            it.author_id = tt_strdup(yyjson_get_str(dbid));
        }

        yyjson_val *sub_meta = yyjson_obj_get(lm, "metadata");
        yyjson_val *cmeta = sub_meta ? yyjson_obj_get(sub_meta, "contentMetadataViewModel") : NULL;
        yyjson_val *rows = cmeta ? yyjson_obj_get(cmeta, "metadataRows") : NULL;
        if (rows && yyjson_is_arr(rows)) {
            size_t ridx, rmax;
            yyjson_val *row;
            yyjson_arr_foreach(rows, ridx, rmax, row) {
                yyjson_val *rbadges = yyjson_obj_get(row, "badges");
                if (has_live_badge(rbadges)) it.is_live = true;
                if (has_members_only_badge(rbadges)) it.is_members_only = true;

                yyjson_val *parts = yyjson_obj_get(row, "metadataParts");
                if (!parts || !yyjson_is_arr(parts)) continue;
                size_t pidx, pmax;
                yyjson_val *part;
                yyjson_arr_foreach(parts, pidx, pmax, part) {
                    yyjson_val *ptxt_obj = yyjson_obj_get(part, "text");
                    char *txt = extract_text(ptxt_obj);
                    if (!txt) continue;

                    const char *part_bid = NULL;
                    if (ptxt_obj && yyjson_is_obj(ptxt_obj)) {
                        yyjson_val *cruns = yyjson_obj_get(ptxt_obj, "commandRuns");
                        yyjson_val *cr0 = (cruns && yyjson_is_arr(cruns)) ? yyjson_arr_get(cruns, 0) : NULL;
                        yyjson_val *pontap = cr0 ? yyjson_obj_get(cr0, "onTap") : NULL;
                        yyjson_val *pic = pontap ? yyjson_obj_get(pontap, "innertubeCommand") : NULL;
                        yyjson_val *pbe = pic ? yyjson_obj_get(pic, "browseEndpoint") : NULL;
                        yyjson_val *pbid = pbe ? yyjson_obj_get(pbe, "browseId") : NULL;
                        if (pbid && yyjson_is_str(pbid) && strncmp(yyjson_get_str(pbid), "UC", 2) == 0) {
                            part_bid = yyjson_get_str(pbid);
                        }
                    }

                    if (part_bid) {
                        free(it.author);
                        it.author = txt;
                        free(it.author_id);
                        it.author_id = tt_strdup(part_bid);
                    } else {
                        bool looks_metric =
                            tt_strcasestr(txt, "views") != NULL ||
                            tt_strcasestr(txt, "watching") != NULL ||
                            tt_strcasestr(txt, "waiting") != NULL ||
                            tt_strcasestr(txt, "ago") != NULL ||
                            tt_strcasestr(txt, "streamed") != NULL ||
                            tt_strcasestr(txt, "scheduled") != NULL ||
                            tt_strcasestr(txt, "premiere") != NULL;
                        if (!it.author && !looks_metric) {
                            it.author = txt;
                        } else if (!it.view_count_text) {
                            it.view_count_text = txt;
                            if (tt_strcasestr(txt, "watching") != NULL && !it.duration_text) {
                                it.is_live = true;
                            }
                        } else if (!it.published_text) {
                            it.published_text = txt;
                        } else {
                            free(txt);
                        }
                    }
                }
            }
        }
    }

    yyjson_val *cimg = yyjson_obj_get(lvm, "contentImage");
    if (cimg) {
        yyjson_val *tvm = yyjson_obj_get(cimg, "thumbnailViewModel");
        if (!tvm) {
            yyjson_val *col = yyjson_obj_get(cimg, "collectionThumbnailViewModel");
            yyjson_val *prim = col ? yyjson_obj_get(col, "primaryThumbnail") : NULL;
            tvm = prim ? yyjson_obj_get(prim, "thumbnailViewModel") : NULL;
        }
        if (tvm) {
            it.thumbnail_url = extract_thumb(yyjson_obj_get(tvm, "image"));
            yyjson_val *overlays = yyjson_obj_get(tvm, "overlays");
            if (overlays && yyjson_is_arr(overlays)) {
                size_t oidx, omax;
                yyjson_val *ov;
                yyjson_arr_foreach(overlays, oidx, omax, ov) {
                    yyjson_val *tbovm = yyjson_obj_get(ov, "thumbnailBottomOverlayViewModel");
                    if (tbovm) {
                        process_lockup_overlay_badges(yyjson_obj_get(tbovm, "badges"), &it);
                    }
                    yyjson_val *bvm = yyjson_obj_get(ov, "thumbnailOverlayBadgeViewModel");
                    if (bvm) {
                        process_lockup_overlay_badges(yyjson_obj_get(bvm, "thumbnailBadges"), &it);
                    }
                }
            }
        }
    }

    if (it.is_live && (!it.duration_text || !it.duration_text[0])) {
        free(it.duration_text);
        it.duration_text = tt_strdup("LIVE");
    }

    if (!it.thumbnail_url && it.kind == ITEM_VIDEO && it.id[0]) {
        it.thumbnail_url = tt_asprintf("https://i.ytimg.com/vi/%s/mqdefault.jpg", it.id);
    }

    if (!media_list_append_move(out_list, &it)) {
        media_item_free_fields(&it);
    }
}

static void extract_continuation_token(yyjson_val *cir, MediaList *out_list) {
    if (!cir || !yyjson_is_obj(cir) || out_list->continuation_token) return;
    yyjson_val *ep = yyjson_obj_get(cir, "continuationEndpoint");
    yyjson_val *cmd = ep ? yyjson_obj_get(ep, "continuationCommand") : NULL;
    yyjson_val *tok = cmd ? yyjson_obj_get(cmd, "token") : NULL;
    if (tok && yyjson_is_str(tok)) {
        out_list->continuation_token = tt_strdup(yyjson_get_str(tok));
    }
}

static void parse_item_container(yyjson_val *item, MediaList *out_list);

static void parse_container_array(yyjson_val *arr, MediaList *out_list) {
    if (!arr || !yyjson_is_arr(arr)) return;
    size_t idx, max;
    yyjson_val *sub;
    yyjson_arr_foreach(arr, idx, max, sub) {
        parse_item_container(sub, out_list);
    }
}

static void parse_item_container(yyjson_val *item, MediaList *out_list) {
    if (!item || !yyjson_is_obj(item)) return;

    yyjson_val *vr = yyjson_obj_get(item, "videoRenderer");
    if (vr) { parse_video_renderer(vr, out_list); return; }

    yyjson_val *gvr = yyjson_obj_get(item, "gridVideoRenderer");
    if (gvr) { parse_video_renderer(gvr, out_list); return; }

    yyjson_val *cvr = yyjson_obj_get(item, "compactVideoRenderer");
    if (cvr) { parse_video_renderer(cvr, out_list); return; }

    yyjson_val *pvr = yyjson_obj_get(item, "playlistVideoRenderer");
    if (pvr) { parse_playlist_video_renderer(pvr, out_list); return; }

    yyjson_val *cr = yyjson_obj_get(item, "channelRenderer");
    if (cr) { parse_channel_renderer(cr, out_list); return; }

    yyjson_val *pr = yyjson_obj_get(item, "playlistRenderer");
    if (pr) { parse_playlist_renderer(pr, out_list); return; }

    yyjson_val *lvm = yyjson_obj_get(item, "lockupViewModel");
    if (lvm) { parse_lockup_view_model(lvm, out_list); return; }

    yyjson_val *rir = yyjson_obj_get(item, "richItemRenderer");
    if (rir) {
        parse_item_container(yyjson_obj_get(rir, "content"), out_list);
        return;
    }

    yyjson_val *cir = yyjson_obj_get(item, "continuationItemRenderer");
    if (cir) {
        extract_continuation_token(cir, out_list);
        return;
    }

    yyjson_val *rsec = yyjson_obj_get(item, "richSectionRenderer");
    if (rsec) {
        parse_item_container(yyjson_obj_get(rsec, "content"), out_list);
        return;
    }

    yyjson_val *sr = yyjson_obj_get(item, "shelfRenderer");
    if (sr) {
        parse_item_container(yyjson_obj_get(sr, "content"), out_list);
        return;
    }

    static const struct { const char *obj_key; const char *arr_key; } list_containers[] = {
        {"itemSectionRenderer", "contents"},
        {"playlistVideoListRenderer", "contents"},
        {"richShelfRenderer", "contents"},
        {"gridRenderer", "items"},
        {"verticalListRenderer", "items"},
        {"expandedShelfContentsRenderer", "items"},
        {"horizontalListRenderer", "items"},
    };
    for (size_t i = 0; i < sizeof(list_containers) / sizeof(list_containers[0]); i++) {
        yyjson_val *sub = yyjson_obj_get(item, list_containers[i].obj_key);
        if (sub) {
            parse_container_array(yyjson_obj_get(sub, list_containers[i].arr_key), out_list);
            return;
        }
    }
}

static void extract_legacy_continuation(yyjson_val *obj, MediaList *out_list) {
    if (!obj || !yyjson_is_obj(obj) || out_list->continuation_token) return;
    yyjson_val *conts = yyjson_obj_get(obj, "continuations");
    if (!conts || !yyjson_is_arr(conts)) return;
    yyjson_val *c0 = yyjson_arr_get(conts, 0);
    yyjson_val *ncd = c0 ? yyjson_obj_get(c0, "nextContinuationData") : NULL;
    yyjson_val *tok = ncd ? yyjson_obj_get(ncd, "continuation") : NULL;
    if (tok && yyjson_is_str(tok)) {
        out_list->continuation_token = tt_strdup(yyjson_get_str(tok));
    }
}

static void parse_continuation_contents(yyjson_val *cc, MediaList *out_list) {
    if (!cc || !yyjson_is_obj(cc)) return;
    static const struct { const char *key; const char *arr_key; } cont_map[] = {
        {"sectionListContinuation", "contents"},
        {"itemSectionContinuation", "contents"},
        {"playlistVideoListContinuation", "contents"},
        {"richGridContinuation", "contents"},
        {"gridContinuation", "items"},
    };
    for (size_t i = 0; i < sizeof(cont_map) / sizeof(cont_map[0]); i++) {
        yyjson_val *sub = yyjson_obj_get(cc, cont_map[i].key);
        if (sub && yyjson_is_obj(sub)) {
            parse_container_array(yyjson_obj_get(sub, cont_map[i].arr_key), out_list);
            extract_legacy_continuation(sub, out_list);
        }
    }
}

static void parse_continuation_array(yyjson_val *actions, MediaList *out_list) {
    if (!actions || !yyjson_is_arr(actions)) return;
    size_t idx, max;
    yyjson_val *act;
    yyjson_arr_foreach(actions, idx, max, act) {
        yyjson_val *acia = yyjson_obj_get(act, "appendContinuationItemsAction");
        if (!acia) continue;
        yyjson_val *items = yyjson_obj_get(acia, "continuationItems");
        if (!items || !yyjson_is_arr(items)) continue;
        size_t i2, m2;
        yyjson_val *it;
        yyjson_arr_foreach(items, i2, m2, it) {
            parse_item_container(it, out_list);
        }
    }
}

bool innertube_parse_search_json(const char *json, size_t len, MediaList *out_list,
                                 char *err_buf, size_t err_cap) {
    if (!json || !out_list) return false;
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Invalid JSON from YouTube search");
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *contents = yyjson_obj_get(root, "contents");
    yyjson_val *two = contents ? yyjson_obj_get(contents, "twoColumnSearchResultsRenderer") : NULL;
    yyjson_val *prim = two ? yyjson_obj_get(two, "primaryContents") : NULL;
    yyjson_val *slr = prim ? yyjson_obj_get(prim, "sectionListRenderer") : NULL;
    yyjson_val *sec_contents = slr ? yyjson_obj_get(slr, "contents") : NULL;

    if (sec_contents && yyjson_is_arr(sec_contents)) {
        size_t idx, max;
        yyjson_val *sec;
        yyjson_arr_foreach(sec_contents, idx, max, sec) {
            parse_item_container(sec, out_list);
        }
    }

    parse_continuation_contents(yyjson_obj_get(root, "continuationContents"), out_list);
    parse_continuation_array(yyjson_obj_get(root, "onResponseReceivedCommands"), out_list);
    yyjson_doc_free(doc);
    return true;
}

bool innertube_parse_browse_json(const char *json, size_t len, MediaList *out_list,
                                 char *err_buf, size_t err_cap) {
    if (!json || !out_list) return false;
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Invalid JSON from YouTube browse");
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);

    /* Extract heading from header or metadata */
    if (!out_list->heading) {
        yyjson_val *header = yyjson_obj_get(root, "header");
        if (header) {
            yyjson_val *c4 = yyjson_obj_get(header, "c4TabbedHeaderRenderer");
            yyjson_val *ph = yyjson_obj_get(header, "pageHeaderRenderer");
            yyjson_val *plh = yyjson_obj_get(header, "playlistHeaderRenderer");
            if (c4) out_list->heading = extract_text(yyjson_obj_get(c4, "title"));
            else if (ph) {
                out_list->heading = extract_text(yyjson_obj_get(ph, "pageTitle"));
                if (!out_list->heading) {
                    yyjson_val *phc = yyjson_obj_get(ph, "content");
                    yyjson_val *phvm = phc ? yyjson_obj_get(phc, "pageHeaderViewModel") : NULL;
                    yyjson_val *pht = phvm ? yyjson_obj_get(phvm, "title") : NULL;
                    yyjson_val *dtvm = pht ? yyjson_obj_get(pht, "dynamicTextViewModel") : NULL;
                    if (dtvm) out_list->heading = extract_text(yyjson_obj_get(dtvm, "text"));
                }
            }
            else if (plh) out_list->heading = extract_text(yyjson_obj_get(plh, "title"));
        }
        if (!out_list->heading) {
            yyjson_val *meta = yyjson_obj_get(root, "metadata");
            yyjson_val *cmr = meta ? yyjson_obj_get(meta, "channelMetadataRenderer") : NULL;
            yyjson_val *pmr = meta ? yyjson_obj_get(meta, "playlistMetadataRenderer") : NULL;
            if (cmr) out_list->heading = extract_text(yyjson_obj_get(cmr, "title"));
            else if (pmr) out_list->heading = extract_text(yyjson_obj_get(pmr, "title"));
        }
    }

    yyjson_val *contents = yyjson_obj_get(root, "contents");
    yyjson_val *two = contents ? yyjson_obj_get(contents, "twoColumnBrowseResultsRenderer") : NULL;
    yyjson_val *tabs = two ? yyjson_obj_get(two, "tabs") : NULL;
    if (tabs && yyjson_is_arr(tabs)) {
        size_t tidx, tmax;
        yyjson_val *tab;
        yyjson_arr_foreach(tabs, tidx, tmax, tab) {
            yyjson_val *tr = yyjson_obj_get(tab, "tabRenderer");
            if (!tr) continue;
            yyjson_val *sel = yyjson_obj_get(tr, "selected");
            if (yyjson_arr_size(tabs) > 1 && (!sel || !yyjson_get_bool(sel))) {
                continue;
            }
            yyjson_val *tc = yyjson_obj_get(tr, "content");
            if (!tc) continue;

            yyjson_val *rgr = yyjson_obj_get(tc, "richGridRenderer");
            if (rgr) {
                yyjson_val *rc = yyjson_obj_get(rgr, "contents");
                if (rc && yyjson_is_arr(rc)) {
                    size_t idx, max;
                    yyjson_val *sub;
                    yyjson_arr_foreach(rc, idx, max, sub) {
                        parse_item_container(sub, out_list);
                    }
                }
            }

            yyjson_val *slr = yyjson_obj_get(tc, "sectionListRenderer");
            if (slr) {
                yyjson_val *sc = yyjson_obj_get(slr, "contents");
                if (sc && yyjson_is_arr(sc)) {
                    size_t idx, max;
                    yyjson_val *sub;
                    yyjson_arr_foreach(sc, idx, max, sub) {
                        parse_item_container(sub, out_list);
                    }
                }
            }
        }
    }

    parse_continuation_array(yyjson_obj_get(root, "onResponseReceivedActions"), out_list);
    parse_continuation_contents(yyjson_obj_get(root, "continuationContents"), out_list);
    parse_continuation_array(yyjson_obj_get(root, "onResponseReceivedCommands"), out_list);
    yyjson_doc_free(doc);
    return true;
}

bool innertube_parse_next_json(const char *json, size_t len, const char *video_id,
                               MediaItem *out_details, MediaList *out_recommended,
                               char *err_buf, size_t err_cap) {
    if (!json) return false;
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Invalid JSON from YouTube next");
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *contents = yyjson_obj_get(root, "contents");
    yyjson_val *watch = contents ? yyjson_obj_get(contents, "twoColumnWatchNextResults") : NULL;

    if (out_details) {
        memset(out_details, 0, sizeof(*out_details));
        out_details->kind = ITEM_VIDEO;
        out_details->details_loaded = true;
        if (video_id) snprintf(out_details->id, sizeof(out_details->id), "%s", video_id);

        yyjson_val *res = watch ? yyjson_obj_get(watch, "results") : NULL;
        yyjson_val *res2 = res ? yyjson_obj_get(res, "results") : NULL;
        yyjson_val *rc = res2 ? yyjson_obj_get(res2, "contents") : NULL;
        if (rc && yyjson_is_arr(rc)) {
            size_t idx, max;
            yyjson_val *elem;
            yyjson_arr_foreach(rc, idx, max, elem) {
                yyjson_val *vpi = yyjson_obj_get(elem, "videoPrimaryInfoRenderer");
                if (vpi) {
                    if (!out_details->title) {
                        out_details->title = extract_text(yyjson_obj_get(vpi, "title"));
                    }
                    yyjson_val *vc = yyjson_obj_get(vpi, "viewCount");
                    yyjson_val *vvr = vc ? yyjson_obj_get(vc, "videoViewCountRenderer") : NULL;
                    if (vvr) {
                        out_details->view_count_text = extract_text(yyjson_obj_get(vvr, "viewCount"));
                        if (!out_details->view_count_text) {
                            out_details->view_count_text = extract_text(yyjson_obj_get(vvr, "extraShortViewCount"));
                        }
                        yyjson_val *isl = yyjson_obj_get(vvr, "isLive");
                        if (isl && yyjson_is_bool(isl)) out_details->is_live = yyjson_get_bool(isl);
                    }
                    out_details->published_text = extract_text(yyjson_obj_get(vpi, "dateText"));
                }

                yyjson_val *vsi = yyjson_obj_get(elem, "videoSecondaryInfoRenderer");
                if (vsi) {
                    yyjson_val *ad = yyjson_obj_get(vsi, "attributedDescription");
                    out_details->description = extract_text(ad);
                    if (out_details->description && !out_details->description[0]) {
                        free(out_details->description);
                        out_details->description = NULL;
                    }
                    if (!out_details->description) {
                        out_details->description = extract_text(yyjson_obj_get(vsi, "description"));
                        if (out_details->description && !out_details->description[0]) {
                            free(out_details->description);
                            out_details->description = NULL;
                        }
                    }
                    yyjson_val *owner = yyjson_obj_get(vsi, "owner");
                    yyjson_val *vor = owner ? yyjson_obj_get(owner, "videoOwnerRenderer") : NULL;
                    if (vor) {
                        yyjson_val *otitle = yyjson_obj_get(vor, "title");
                        out_details->author = extract_text(otitle);
                        yyjson_val *runs = otitle ? yyjson_obj_get(otitle, "runs") : NULL;
                        yyjson_val *r0 = runs ? yyjson_arr_get(runs, 0) : NULL;
                        yyjson_val *nav = r0 ? yyjson_obj_get(r0, "navigationEndpoint") : NULL;
                        yyjson_val *be = nav ? yyjson_obj_get(nav, "browseEndpoint") : NULL;
                        yyjson_val *bid = be ? yyjson_obj_get(be, "browseId") : NULL;
                        if (bid && yyjson_is_str(bid)) {
                            out_details->author_id = tt_strdup(yyjson_get_str(bid));
                        }
                    }
                }
            }
        }

        if (out_details->description && !out_details->description[0]) {
            free(out_details->description);
            out_details->description = NULL;
        }

        yyjson_val *eps = yyjson_obj_get(root, "engagementPanels");
        if (eps && yyjson_is_arr(eps)) {
            size_t pidx, pmax;
            yyjson_val *ep;
            yyjson_arr_foreach(eps, pidx, pmax, ep) {
                yyjson_val *epslr = yyjson_obj_get(ep, "engagementPanelSectionListRenderer");
                yyjson_val *epc = epslr ? yyjson_obj_get(epslr, "content") : NULL;
                yyjson_val *sdcr = epc ? yyjson_obj_get(epc, "structuredDescriptionContentRenderer") : NULL;
                yyjson_val *sitems = sdcr ? yyjson_obj_get(sdcr, "items") : NULL;
                if (!sitems || !yyjson_is_arr(sitems)) continue;
                size_t iidx, imax;
                yyjson_val *sitem;
                yyjson_arr_foreach(sitems, iidx, imax, sitem) {
                    yyjson_val *evdb = yyjson_obj_get(sitem, "expandableVideoDescriptionBodyRenderer");
                    if (evdb && (!out_details->description || !out_details->description[0])) {
                        free(out_details->description);
                        out_details->description =
                            extract_text(yyjson_obj_get(evdb, "attributedDescriptionBodyText"));
                        if (!out_details->description || !out_details->description[0]) {
                            free(out_details->description);
                            out_details->description =
                                extract_text(yyjson_obj_get(evdb, "descriptionBodyText"));
                        }
                        if (out_details->description && !out_details->description[0]) {
                            free(out_details->description);
                            out_details->description = NULL;
                        }
                    }
                    yyjson_val *vdhr = yyjson_obj_get(sitem, "videoDescriptionHeaderRenderer");
                    if (vdhr) {
                        if (!out_details->title || !out_details->title[0]) {
                            free(out_details->title);
                            out_details->title = extract_text(yyjson_obj_get(vdhr, "title"));
                        }
                        if (!out_details->author || !out_details->author[0]) {
                            free(out_details->author);
                            out_details->author = extract_text(yyjson_obj_get(vdhr, "channel"));
                        }
                        if (!out_details->author_id || !out_details->author_id[0]) {
                            yyjson_val *cnav = yyjson_obj_get(vdhr, "channelNavigationEndpoint");
                            yyjson_val *cbe = cnav ? yyjson_obj_get(cnav, "browseEndpoint") : NULL;
                            yyjson_val *cbid = cbe ? yyjson_obj_get(cbe, "browseId") : NULL;
                            if (cbid && yyjson_is_str(cbid)) {
                                free(out_details->author_id);
                                out_details->author_id = tt_strdup(yyjson_get_str(cbid));
                            }
                        }
                        if (!out_details->published_text || !out_details->published_text[0]) {
                            free(out_details->published_text);
                            out_details->published_text =
                                extract_text(yyjson_obj_get(vdhr, "publishDate"));
                        }
                        if (!out_details->view_count_text || !out_details->view_count_text[0]) {
                            free(out_details->view_count_text);
                            out_details->view_count_text =
                                extract_text(yyjson_obj_get(vdhr, "views"));
                        }
                    }
                }
            }
        }
        if (out_details->id[0] && !out_details->thumbnail_url) {
            out_details->thumbnail_url =
                tt_asprintf("https://i.ytimg.com/vi/%s/mqdefault.jpg", out_details->id);
        }
    }

    if (out_recommended) {
        yyjson_val *sr = watch ? yyjson_obj_get(watch, "secondaryResults") : NULL;
        yyjson_val *sr2 = sr ? yyjson_obj_get(sr, "secondaryResults") : NULL;
        yyjson_val *results = sr2 ? yyjson_obj_get(sr2, "results") : NULL;
        if (results && yyjson_is_arr(results)) {
            size_t idx, max;
            yyjson_val *elem;
            yyjson_arr_foreach(results, idx, max, elem) {
                parse_item_container(elem, out_recommended);
            }
        }
        parse_continuation_array(yyjson_obj_get(root, "onResponseReceivedEndpoints"), out_recommended);
    }

    yyjson_doc_free(doc);
    return true;
}

bool innertube_parse_resolve_url_json(const char *json, size_t len, ParsedUrl *out) {
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *ep = yyjson_obj_get(root, "endpoint");
    yyjson_val *be = ep ? yyjson_obj_get(ep, "browseEndpoint") : NULL;
    yyjson_val *we = ep ? yyjson_obj_get(ep, "watchEndpoint") : NULL;

    bool ok = false;
    if (be) {
        yyjson_val *bid = yyjson_obj_get(be, "browseId");
        if (bid && yyjson_is_str(bid)) {
            const char *s = yyjson_get_str(bid);
            if (strncmp(s, "VL", 2) == 0) {
                out->kind = URL_PLAYLIST;
                snprintf(out->id, sizeof(out->id), "%s", s + 2);
            } else {
                out->kind = URL_CHANNEL;
                snprintf(out->id, sizeof(out->id), "%s", s);
                yyjson_val *bp = yyjson_obj_get(be, "params");
                if (bp && yyjson_is_str(bp) &&
                    strncmp(yyjson_get_str(bp), "EgdzdHJlYW1z", 12) == 0) {
                    out->prefer_live_tab = true;
                }
            }
            ok = true;
        }
    } else if (we) {
        yyjson_val *vid = yyjson_obj_get(we, "videoId");
        if (vid && yyjson_is_str(vid)) {
            out->kind = URL_VIDEO;
            snprintf(out->id, sizeof(out->id), "%s", yyjson_get_str(vid));
            ok = true;
        }
    }
    yyjson_doc_free(doc);
    return ok;
}

bool innertube_parse_ytdlp_json(const char *json, size_t len, MediaList *out_list) {
    if (!json || !out_list) return false;
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *entries = yyjson_obj_get(root, "entries");
    if (!entries || !yyjson_is_arr(entries)) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t idx, max;
    yyjson_val *e;
    yyjson_arr_foreach(entries, idx, max, e) {
        if (!yyjson_is_obj(e)) continue;
        yyjson_val *vid = yyjson_obj_get(e, "id");
        if (!vid || !yyjson_is_str(vid)) continue;

        MediaItem it = {0};
        it.kind = ITEM_VIDEO;
        snprintf(it.id, sizeof(it.id), "%s", yyjson_get_str(vid));

        yyjson_val *t = yyjson_obj_get(e, "title");
        it.title = tt_strdup((t && yyjson_is_str(t)) ? yyjson_get_str(t) : it.id);

        yyjson_val *up = yyjson_obj_get(e, "uploader");
        if (!up || !yyjson_is_str(up)) up = yyjson_obj_get(e, "channel");
        it.author = (up && yyjson_is_str(up)) ? tt_strdup(yyjson_get_str(up)) : NULL;

        yyjson_val *cid = yyjson_obj_get(e, "channel_id");
        it.author_id = (cid && yyjson_is_str(cid)) ? tt_strdup(yyjson_get_str(cid)) : NULL;

        yyjson_val *ds = yyjson_obj_get(e, "duration_string");
        if (ds && yyjson_is_str(ds)) {
            it.duration_text = tt_strdup(yyjson_get_str(ds));
        } else {
            yyjson_val *d = yyjson_obj_get(e, "duration");
            if (d && yyjson_is_num(d)) {
                int64_t secs = (int64_t)yyjson_get_num(d);
                if (secs >= 3600) {
                    it.duration_text = tt_asprintf("%lld:%02lld:%02lld",
                        (long long)(secs / 3600), (long long)((secs % 3600) / 60),
                        (long long)(secs % 60));
                } else {
                    it.duration_text = tt_asprintf("%lld:%02lld",
                        (long long)(secs / 60), (long long)(secs % 60));
                }
            }
        }

        yyjson_val *vc = yyjson_obj_get(e, "view_count");
        if (vc && yyjson_is_num(vc)) {
            it.view_count_text = tt_asprintf("%lld views", (long long)yyjson_get_sint(vc));
        }

        yyjson_val *avail = yyjson_obj_get(e, "availability");
        if (avail && yyjson_is_str(avail) &&
            strcmp(yyjson_get_str(avail), "subscriber_only") == 0) {
            it.is_members_only = true;
        }

        yyjson_val *ls = yyjson_obj_get(e, "live_status");
        yyjson_val *il = yyjson_obj_get(e, "is_live");
        if ((ls && yyjson_is_str(ls) && strcmp(yyjson_get_str(ls), "is_live") == 0) ||
            (il && yyjson_is_bool(il) && yyjson_get_bool(il))) {
            it.is_live = true;
            if (!it.duration_text || !it.duration_text[0]) {
                free(it.duration_text);
                it.duration_text = tt_strdup("LIVE");
            }
        }

        it.thumbnail_url = tt_asprintf("https://i.ytimg.com/vi/%s/mqdefault.jpg", it.id);
        if (!media_list_append_move(out_list, &it)) {
            media_item_free_fields(&it);
        }
    }

    yyjson_doc_free(doc);
    return out_list->count > 0;
}

static char *extract_xml_tag_text(const char *block, const char *block_end, const char *tag) {
    char open_pat[64];
    char close_pat[64];
    snprintf(open_pat, sizeof(open_pat), "<%s", tag);
    snprintf(close_pat, sizeof(close_pat), "</%s>", tag);

    const char *p = block;
    while (p < block_end) {
        const char *hit = strstr(p, open_pat);
        if (!hit || hit >= block_end) return NULL;
        char next = hit[strlen(open_pat)];
        if (next != '>' && !isspace((unsigned char)next)) {
            p = hit + 1;
            continue;
        }
        const char *gt = strchr(hit, '>');
        if (!gt || gt >= block_end) return NULL;
        if (gt > hit && gt[-1] == '/') {
            p = gt + 1;
            continue;
        }
        const char *val_start = gt + 1;
        const char *close = strstr(val_start, close_pat);
        if (!close || close > block_end) return NULL;

        /* Strip CDATA if present */
        size_t raw_len = (size_t)(close - val_start);
        if (raw_len >= 12 && strncmp(val_start, "<![CDATA[", 9) == 0 &&
            strncmp(close - 3, "]]>", 3) == 0) {
            return tt_strndup(val_start + 9, raw_len - 12);
        }
        char *raw = tt_strndup(val_start, raw_len);
        if (!raw) return NULL;
        char *unesc = tt_xml_unescape(raw);
        free(raw);
        return unesc;
    }
    return NULL;
}

static char *extract_xml_attr(const char *block, const char *block_end,
                              const char *tag, const char *attr) {
    char open_pat[64];
    char attr_pat[64];
    snprintf(open_pat, sizeof(open_pat), "<%s", tag);
    snprintf(attr_pat, sizeof(attr_pat), "%s=\"", attr);

    const char *hit = strstr(block, open_pat);
    if (!hit || hit >= block_end) return NULL;
    const char *gt = strchr(hit, '>');
    if (!gt || gt >= block_end) return NULL;

    const char *ap = strstr(hit, attr_pat);
    if (!ap || ap >= gt) return NULL;
    const char *vs = ap + strlen(attr_pat);
    const char *ve = strchr(vs, '"');
    if (!ve || ve >= gt) return NULL;
    char *raw = tt_strndup(vs, (size_t)(ve - vs));
    if (!raw) return NULL;
    char *unesc = tt_xml_unescape(raw);
    free(raw);
    return unesc;
}

bool innertube_parse_rss_xml(const char *xml, size_t xml_len, MediaList *out_list, int max_items) {
    if (!xml || xml_len == 0 || !out_list) return false;
    const char *cursor = xml;
    int added = 0;

    while (max_items <= 0 || added < max_items) {
        const char *entry_start = strstr(cursor, "<entry>");
        if (!entry_start) break;
        const char *entry_end = strstr(entry_start, "</entry>");
        if (!entry_end) break;

        char *vid = extract_xml_tag_text(entry_start, entry_end, "yt:videoId");
        if (vid && vid[0]) {
            MediaItem it = {0};
            it.kind = ITEM_VIDEO;
            snprintf(it.id, sizeof(it.id), "%s", vid);
            it.title = extract_xml_tag_text(entry_start, entry_end, "title");
            it.author = extract_xml_tag_text(entry_start, entry_end, "name");
            it.author_id = extract_xml_tag_text(entry_start, entry_end, "yt:channelId");
            it.description = extract_xml_tag_text(entry_start, entry_end, "media:description");

            char *pub = extract_xml_tag_text(entry_start, entry_end, "published");
            if (pub) {
                tt_parse_iso8601_utc(pub, &it.published_epoch);
                /* Trim T..Z to readable YYYY-MM-DD HH:MM */
                if (strlen(pub) >= 16 && pub[10] == 'T') {
                    pub[10] = ' ';
                    pub[16] = '\0';
                }
                it.published_text = pub;
            }

            char *views = extract_xml_attr(entry_start, entry_end, "media:statistics", "views");
            if (views) {
                it.view_count_text = tt_asprintf("%s views", views);
                free(views);
            }

            it.thumbnail_url = extract_xml_attr(entry_start, entry_end, "media:thumbnail", "url");
            if (!it.thumbnail_url) {
                it.thumbnail_url = tt_asprintf("https://i.ytimg.com/vi/%s/mqdefault.jpg", it.id);
            }

            if (media_list_append_move(out_list, &it)) {
                added++;
            } else {
                media_item_free_fields(&it);
            }
        }
        free(vid);
        cursor = entry_end + 8;
    }
    return true;
}

static int cmp_media_epoch_desc(const void *a, const void *b) {
    const MediaItem *ia = a;
    const MediaItem *ib = b;
    if (ib->published_epoch > ia->published_epoch) return 1;
    if (ib->published_epoch < ia->published_epoch) return -1;
    return 0;
}

void innertube_filter_and_sort_feed(MediaList *list, int64_t now_epoch, int64_t max_age_sec) {
    if (!list || list->count == 0) return;

    size_t write_idx = 0;
    for (size_t i = 0; i < list->count; i++) {
        MediaItem *it = &list->items[i];
        bool drop = false;
        if (max_age_sec > 0 && it->published_epoch > 0 &&
            (now_epoch - it->published_epoch) > max_age_sec) {
            drop = true;
        }
        if (!drop) {
            for (size_t j = 0; j < write_idx; j++) {
                if (strcmp(list->items[j].id, it->id) == 0) {
                    drop = true;
                    break;
                }
            }
        }
        if (drop) {
            media_item_free_fields(it);
        } else {
            if (write_idx != i) {
                list->items[write_idx] = *it;
                memset(it, 0, sizeof(*it));
            }
            write_idx++;
        }
    }
    list->count = write_idx;
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(MediaItem), cmp_media_epoch_desc);
    }
}

static void add_innertube_context(yyjson_mut_doc *doc, yyjson_mut_val *root) {
    yyjson_mut_val *ctx = yyjson_mut_obj(doc);
    yyjson_mut_val *cli = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, cli, "hl", "en");
    yyjson_mut_obj_add_str(doc, cli, "gl", "US");
    yyjson_mut_obj_add_str(doc, cli, "clientName", "WEB");
    yyjson_mut_obj_add_str(doc, cli, "clientVersion", INNERTUBE_CLIENT_VER);
    yyjson_mut_obj_add_val(doc, ctx, "client", cli);
    yyjson_mut_obj_add_val(doc, root, "context", ctx);
}

static bool innertube_post(CURL *curl, const char *endpoint, yyjson_mut_doc *doc,
                           HttpResponse *resp, char *err_buf, size_t err_cap) {
    char *body = yyjson_mut_write(doc, 0, NULL);
    if (!body) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Out of memory building request");
        return false;
    }
    char url[256];
    snprintf(url, sizeof(url), "https://www.youtube.com/youtubei/v1/%s?prettyPrint=false", endpoint);

    bool ok = http_post_json(curl, url, body, INNERTUBE_HEADERS, 3, INNERTUBE_MAX_JSON, resp);
    free(body);
    if (!ok) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Network error: %s", resp->error);
        http_response_free(resp);
        return false;
    }
    if (resp->status_code < 200 || resp->status_code >= 300) {
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "YouTube API HTTP %ld", resp->status_code);
        }
        http_response_free(resp);
        return false;
    }
    return true;
}

static bool search_ytdlp_fallback(const char *query, MediaList *out_list) {
    if (!query || !query[0]) return false;
    char *search_arg = tt_asprintf("ytsearch20:%s", query);
    if (!search_arg) return false;

    const char *argv[] = {
        "yt-dlp", "--flat-playlist", "--dump-single-json", search_arg, NULL
    };
    StrBuf sb;
    sb_init(&sb);
    bool spawn_ok = tt_spawn_capture_stdout(argv, INNERTUBE_MAX_JSON, &sb);
    free(search_arg);

    bool ok = false;
    if (spawn_ok && sb.len > 0) {
        ok = innertube_parse_ytdlp_json(sb.data, sb.len, out_list);
    }
    sb_free(&sb);
    return ok;
}

bool innertube_search(CURL *curl, const char *query, SearchFilter filter,
                      const char *continuation, MediaList *out_list,
                      char *err_buf, size_t err_cap) {
    if (!out_list) return false;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    add_innertube_context(doc, root);

    if (continuation && continuation[0]) {
        yyjson_mut_obj_add_str(doc, root, "continuation", continuation);
    } else {
        yyjson_mut_obj_add_str(doc, root, "query", query ? query : "");
        if (filter.sort != SORT_RELEVANCE || filter.type != TYPE_ALL || filter.live_only) {
            char params[64];
            if (innertube_encode_search_params(filter, params, sizeof(params))) {
                yyjson_mut_obj_add_str(doc, root, "params", params);
            }
        }
    }

    HttpResponse resp;
    bool posted = innertube_post(curl, "search", doc, &resp, err_buf, err_cap);
    yyjson_mut_doc_free(doc);

    bool ok = false;
    if (posted) {
        ok = innertube_parse_search_json(resp.data, resp.len, out_list, err_buf, err_cap);
        http_response_free(&resp);
    }

    if ((!continuation || !continuation[0]) && query && query[0] &&
        (!ok || (out_list->count == 0 && filter.sort == SORT_RELEVANCE &&
                 filter.type == TYPE_ALL && !filter.live_only))) {
        if (search_ytdlp_fallback(query, out_list)) {
            if (err_buf && err_cap) err_buf[0] = '\0';
            return true;
        }
    }
    return ok;
}

bool innertube_browse(CURL *curl, const char *browse_id, const char *params,
                      const char *continuation, MediaList *out_list,
                      char *err_buf, size_t err_cap) {
    if (!out_list) return false;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    add_innertube_context(doc, root);

    if (continuation && continuation[0]) {
        yyjson_mut_obj_add_str(doc, root, "continuation", continuation);
    } else {
        yyjson_mut_obj_add_str(doc, root, "browseId", browse_id ? browse_id : "");
        if (params && params[0]) {
            yyjson_mut_obj_add_str(doc, root, "params", params);
        }
    }

    HttpResponse resp;
    bool posted = innertube_post(curl, "browse", doc, &resp, err_buf, err_cap);
    yyjson_mut_doc_free(doc);
    if (!posted) return false;

    bool ok = innertube_parse_browse_json(resp.data, resp.len, out_list, err_buf, err_cap);
    http_response_free(&resp);

    if ((!continuation || !continuation[0]) && ok && out_list->count == 0 &&
        out_list->continuation_token && out_list->continuation_token[0]) {
        char *follow_tok = out_list->continuation_token;
        out_list->continuation_token = NULL;
        yyjson_mut_doc *fdoc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *froot = yyjson_mut_obj(fdoc);
        yyjson_mut_doc_set_root(fdoc, froot);
        add_innertube_context(fdoc, froot);
        yyjson_mut_obj_add_str(fdoc, froot, "continuation", follow_tok);
        free(follow_tok);

        HttpResponse fresp;
        if (innertube_post(curl, "browse", fdoc, &fresp, err_buf, err_cap)) {
            ok = innertube_parse_browse_json(fresp.data, fresp.len, out_list, err_buf, err_cap);
            http_response_free(&fresp);
        }
        yyjson_mut_doc_free(fdoc);
    }

    if (ok && browse_id && strncmp(browse_id, "UC", 2) == 0) {
        if (!out_list->source_id) {
            out_list->source_id = tt_strdup(browse_id);
        }
        for (size_t i = 0; i < out_list->count; i++) {
            MediaItem *it = &out_list->items[i];
            if (it->kind != ITEM_VIDEO) continue;
            if (!it->author_id || !it->author_id[0]) {
                free(it->author_id);
                it->author_id = tt_strdup(browse_id);
            }
            if ((!it->author || !it->author[0]) && out_list->heading && out_list->heading[0]) {
                free(it->author);
                it->author = tt_strdup(out_list->heading);
            }
        }
    }
    return ok;
}

bool innertube_browse_channel(CURL *curl, const char *channel_id, bool live_tab,
                              const char *continuation, bool hide_members_only,
                              MediaList *out_list, char *err_buf, size_t err_cap) {
    if (!out_list) return false;
    if ((continuation && continuation[0]) || live_tab) {
        const char *params = live_tab ? INNERTUBE_CHAN_STREAMS_PARAMS : INNERTUBE_CHAN_VIDEOS_PARAMS;
        bool ok = innertube_browse(curl, channel_id, params, continuation,
                                   out_list, err_buf, err_cap);
        if (ok && hide_members_only) {
            media_list_filter_members_only(out_list);
        }
        return ok;
    }

    MediaList vid_list, stm_list;
    media_list_init(&vid_list);
    media_list_init(&stm_list);

    char stm_err[256] = {0};
    bool vid_ok = innertube_browse(curl, channel_id, INNERTUBE_CHAN_VIDEOS_PARAMS,
                                   NULL, &vid_list, err_buf, err_cap);
    bool stm_ok = innertube_browse(curl, channel_id, INNERTUBE_CHAN_STREAMS_PARAMS,
                                   NULL, &stm_list, stm_err, sizeof(stm_err));

    if (!vid_ok && !stm_ok) {
        if (err_buf && err_cap && !err_buf[0] && stm_err[0]) {
            snprintf(err_buf, err_cap, "%s", stm_err);
        }
        media_list_free(&vid_list);
        media_list_free(&stm_list);
        return false;
    }

    if (hide_members_only) {
        media_list_filter_members_only(&vid_list);
        media_list_filter_members_only(&stm_list);
    }

    if (vid_list.count == 0 && stm_list.count > 0) {
        char *saved_heading = out_list->heading;
        out_list->heading = NULL;
        free(out_list->continuation_token);
        free(out_list->source_id);
        *out_list = stm_list;
        memset(&stm_list, 0, sizeof(stm_list));
        if (!out_list->heading && saved_heading) {
            out_list->heading = saved_heading;
        } else {
            free(saved_heading);
        }
        media_list_free(&vid_list);
        if (err_buf && err_cap) err_buf[0] = '\0';
        return true;
    }

    if (!out_list->heading) {
        if (vid_list.heading && vid_list.heading[0]) {
            out_list->heading = tt_strdup(vid_list.heading);
        } else if (stm_list.heading && stm_list.heading[0]) {
            out_list->heading = tt_strdup(stm_list.heading);
        }
    }
    if (!out_list->continuation_token && vid_list.continuation_token) {
        out_list->continuation_token = tt_strdup(vid_list.continuation_token);
    }
    if (!out_list->source_id && channel_id && channel_id[0]) {
        out_list->source_id = tt_strdup(channel_id);
    }

    for (size_t i = 0; i < stm_list.count; i++) {
        if (stm_list.items[i].is_live) {
            media_list_append_copy(out_list, &stm_list.items[i]);
        }
    }
    for (size_t i = 0; i < vid_list.count; i++) {
        bool dup = false;
        for (size_t j = 0; j < out_list->count; j++) {
            if (strcmp(out_list->items[j].id, vid_list.items[i].id) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            media_list_append_copy(out_list, &vid_list.items[i]);
        }
    }

    media_list_free(&vid_list);
    media_list_free(&stm_list);
    if (err_buf && err_cap) err_buf[0] = '\0';
    return true;
}

bool innertube_next(CURL *curl, const char *video_id, const char *continuation,
                    MediaItem *out_details, MediaList *out_recommended,
                    char *err_buf, size_t err_cap) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    add_innertube_context(doc, root);

    if (continuation && continuation[0]) {
        yyjson_mut_obj_add_str(doc, root, "continuation", continuation);
    } else {
        yyjson_mut_obj_add_str(doc, root, "videoId", video_id ? video_id : "");
    }

    HttpResponse resp;
    bool posted = innertube_post(curl, "next", doc, &resp, err_buf, err_cap);
    yyjson_mut_doc_free(doc);
    if (!posted) return false;

    bool ok = innertube_parse_next_json(resp.data, resp.len, video_id,
                                        out_details, out_recommended, err_buf, err_cap);
    http_response_free(&resp);
    return ok;
}

bool innertube_resolve_url(CURL *curl, const char *url, ParsedUrl *out,
                           char *err_buf, size_t err_cap) {
    if (!url || !out) return false;
    ParsedUrl in_url;
    bool url_prefers_live = innertube_parse_input_url(url, &in_url) && in_url.prefer_live_tab;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    add_innertube_context(doc, root);
    yyjson_mut_obj_add_str(doc, root, "url", url);

    HttpResponse resp;
    bool posted = innertube_post(curl, "navigation/resolve_url", doc, &resp, err_buf, err_cap);
    yyjson_mut_doc_free(doc);
    if (!posted) return false;

    bool ok = innertube_parse_resolve_url_json(resp.data, resp.len, out);
    http_response_free(&resp);
    if (ok && url_prefers_live && out->kind == URL_CHANNEL) {
        out->prefer_live_tab = true;
    }
    if (!ok && err_buf && err_cap) {
        snprintf(err_buf, err_cap, "Could not resolve YouTube URL");
    }
    return ok;
}

bool innertube_fetch_feed(CURL *curl, const FavoriteList *favs, bool use_rss,
                          int max_per_channel, int64_t max_age_sec, int64_t now_epoch,
                          bool hide_members_only, MediaList *out_list,
                          HttpPool *pool, uint64_t gen,
                          char *err_buf, size_t err_cap) {
    if (!favs || !out_list) return false;
    out_list->heading = tt_strdup("Feed");

    for (size_t i = 0; i < favs->count; i++) {
        if (pool && http_pool_should_cancel(pool, gen)) {
            return false;
        }
        if (pool) {
            char msg[128];
            snprintf(msg, sizeof(msg), "loading feed... (%zu items left)", favs->count - i);
            http_pool_post_progress(pool, JOB_FEED, gen, msg);
        }

        const FavoriteItem *fav = &favs->items[i];
        if (use_rss) {
            char url[256];
            if (fav->kind == ITEM_CHANNEL && strncmp(fav->id, "UC", 2) == 0) {
                snprintf(url, sizeof(url),
                         "https://www.youtube.com/feeds/videos.xml?channel_id=%s", fav->id);
            } else {
                snprintf(url, sizeof(url),
                         "https://www.youtube.com/feeds/videos.xml?playlist_id=%s", fav->id);
            }
            HttpResponse resp;
            if (http_get(curl, url, INNERTUBE_MAX_JSON, &resp)) {
                if (resp.status_code >= 200 && resp.status_code < 300 && resp.data) {
                    int limit = (max_per_channel > 0 && max_per_channel <= 15) ? max_per_channel : 15;
                    innertube_parse_rss_xml(resp.data, resp.len, out_list, limit);
                }
                http_response_free(&resp);
            }
        } else {
            MediaList ch_list;
            media_list_init(&ch_list);
            if (fav->kind == ITEM_CHANNEL) {
                MediaList stm_list;
                media_list_init(&stm_list);
                char stm_err[256] = {0};
                bool vid_ok = innertube_browse(curl, fav->id, INNERTUBE_CHAN_VIDEOS_PARAMS,
                                               NULL, &ch_list, err_buf, err_cap);
                bool stm_ok = innertube_browse(curl, fav->id, INNERTUBE_CHAN_STREAMS_PARAMS,
                                               NULL, &stm_list, stm_err, sizeof(stm_err));
                if (hide_members_only) {
                    media_list_filter_members_only(&ch_list);
                    media_list_filter_members_only(&stm_list);
                }
                size_t limit = (max_per_channel > 0)
                                   ? (size_t)max_per_channel
                                   : (ch_list.count + stm_list.count);
                size_t added = 0;
                if (stm_ok) {
                    for (size_t j = 0; j < stm_list.count && added < limit; j++) {
                        if (stm_list.items[j].is_live) {
                            media_list_append_copy(out_list, &stm_list.items[j]);
                            added++;
                        }
                    }
                }
                if (vid_ok) {
                    for (size_t j = 0; j < ch_list.count && added < limit; j++) {
                        media_list_append_copy(out_list, &ch_list.items[j]);
                        added++;
                    }
                }
                if (stm_ok && added < limit) {
                    for (size_t j = 0; j < stm_list.count && added < limit; j++) {
                        if (!stm_list.items[j].is_live) {
                            media_list_append_copy(out_list, &stm_list.items[j]);
                            added++;
                        }
                    }
                }
                media_list_free(&stm_list);
            } else {
                char browse_id[72];
                if (strncmp(fav->id, "VL", 2) == 0) {
                    snprintf(browse_id, sizeof(browse_id), "%s", fav->id);
                } else {
                    snprintf(browse_id, sizeof(browse_id), "VL%s", fav->id);
                }
                if (innertube_browse(curl, browse_id, NULL, NULL, &ch_list, err_buf, err_cap)) {
                    if (hide_members_only) media_list_filter_members_only(&ch_list);
                    size_t limit = (max_per_channel > 0) ? (size_t)max_per_channel : ch_list.count;
                    if (limit > ch_list.count) limit = ch_list.count;
                    for (size_t j = 0; j < limit; j++) {
                        media_list_append_copy(out_list, &ch_list.items[j]);
                    }
                }
            }
            media_list_free(&ch_list);
        }
    }

    innertube_filter_and_sort_feed(out_list, now_epoch, max_age_sec);
    return true;
}
