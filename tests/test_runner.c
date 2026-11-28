#include "config.h"
#include "http.h"
#include "image.h"
#include "innertube.h"
#include "model.h"
#include "player.h"
#include "util.h"

#include <assert.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static int g_tests_run = 0;

static void test_temp_base(char *out, size_t cap) {
#ifdef _WIN32
    wchar_t wtmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, wtmp);
    if (n > 0 && n < MAX_PATH) {
        char *utf8 = tt_wide_to_utf8(wtmp);
        if (utf8) {
            size_t len = strlen(utf8);
            while (len > 1 && (utf8[len - 1] == '\\' || utf8[len - 1] == '/')) {
                if (len == 3 && utf8[1] == ':') break;
                utf8[--len] = '\0';
            }
            snprintf(out, cap, "%s", utf8);
            free(utf8);
            return;
        }
    }
    snprintf(out, cap, ".");
#else
    snprintf(out, cap, "/tmp");
#endif
}

static void test_make_temp_dir(const char *prefix, char *out, size_t cap) {
#ifdef _WIN32
    static unsigned long seq = 0;
    char base[PATH_MAX];
    test_temp_base(base, sizeof(base));
    snprintf(out, cap, "%s/%s_%lu_%lu", base, prefix,
             (unsigned long)GetCurrentProcessId(), ++seq);
    assert(tt_mkdir_p(out, 0755));
#else
    snprintf(out, cap, "/tmp/%s_XXXXXX", prefix);
    assert(mkdtemp(out) != NULL);
#endif
}

static void test_make_temp_file(const char *name, char *out, size_t cap) {
    char base[PATH_MAX];
    test_temp_base(base, sizeof(base));
    snprintf(out, cap, "%s/%s", base, name);
}

static void test_unlink_file(const char *path) {
#ifdef _WIN32
    wchar_t *w = tt_utf8_to_wide(path);
    if (w) {
        DeleteFileW(w);
        free(w);
    }
#else
    unlink(path);
#endif
}

static void test_rmdir(const char *path) {
#ifdef _WIN32
    wchar_t *w = tt_utf8_to_wide(path);
    if (w) {
        RemoveDirectoryW(w);
        free(w);
    }
#else
    rmdir(path);
#endif
}

static bool test_same_file_path(const char *a, const char *b) {
    if (!a || !b) return false;
#ifdef _WIN32
    wchar_t *wa = tt_utf8_to_wide(a);
    wchar_t *wb = tt_utf8_to_wide(b);
    if (!wa || !wb) {
        free(wa);
        free(wb);
        return false;
    }
    HANDLE ha = CreateFileW(wa, 0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hb = CreateFileW(wb, 0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wa);
    free(wb);
    if (ha == INVALID_HANDLE_VALUE || hb == INVALID_HANDLE_VALUE) {
        if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
        if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION ia, ib;
    bool ok = GetFileInformationByHandle(ha, &ia) &&
              GetFileInformationByHandle(hb, &ib) &&
              ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
              ia.nFileIndexHigh == ib.nFileIndexHigh &&
              ia.nFileIndexLow == ib.nFileIndexLow;
    CloseHandle(ha);
    CloseHandle(hb);
    return ok;
#else
    return strcmp(a, b) == 0;
#endif
}

#define RUN_TEST(fn) do { \
    printf("Running %-42s ... ", #fn); \
    fflush(stdout); \
    fn(); \
    g_tests_run++; \
    printf("PASS\n"); \
} while (0)

static void test_util_utf8_and_entities(void) {
    /* UTF-8 boundary stepping: "A" (1B) + "é" (2B) + "あ" (3B) + "🎵" (4B) */
    const char *s = "A\xC3\xA9\xE3\x81\x82\xF0\x9F\x8E\xB5";
    size_t len = strlen(s);
    assert(len == 10);

    size_t p = 0;
    p = utf8_next_char(s, p, len); assert(p == 1);
    p = utf8_next_char(s, p, len); assert(p == 3);
    p = utf8_next_char(s, p, len); assert(p == 6);
    p = utf8_next_char(s, p, len); assert(p == 10);

    p = utf8_prev_char(s, p); assert(p == 6);
    p = utf8_prev_char(s, p); assert(p == 3);
    p = utf8_prev_char(s, p); assert(p == 1);
    p = utf8_prev_char(s, p); assert(p == 0);

    assert(utf8_clamp_pos(s, 8, len) == 6);

    /* Vietnamese multi-byte UTF-8 ("Tiếng Việt"):
       'T'(1) 'i'(1) 'ế'(3) 'n'(1) 'g'(1) ' '(1) 'V'(1) 'i'(1) 'ệ'(3) 't'(1) = 14 bytes */
    const char *vi = "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t";
    size_t vi_len = strlen(vi);
    assert(vi_len == 14);
    assert(utf8_next_char(vi, 2, vi_len) == 5);
    assert(utf8_prev_char(vi, 5) == 2);
    assert(utf8_clamp_pos(vi, 4, vi_len) == 2);
    assert(utf8_clamp_pos(vi, 12, vi_len) == 10);

    char *unesc = tt_xml_unescape("Rock &amp; Roll &lt;3 &quot;Hi&quot; &#39;ok&#39; &#x2605;");
    assert(unesc != NULL);
    assert(strcmp(unesc, "Rock & Roll <3 \"Hi\" 'ok' \xE2\x98\x85") == 0);
    free(unesc);

    int64_t sec = 0;
    assert(tt_parse_age_seconds("2 hours", &sec) && sec == 7200);
    assert(tt_parse_age_seconds("1 week", &sec) && sec == 7 * 86400);
    assert(tt_parse_age_seconds("3 months", &sec) && sec == 90 * 86400);
    assert(tt_parse_age_seconds("1 year", &sec) && sec == 365 * 86400);
    assert(!tt_parse_age_seconds("invalid", &sec));

    int64_t ep = 0;
    assert(tt_parse_iso8601_utc("2026-07-08T12:30:00+00:00", &ep));
    int64_t ep_z = 0;
    assert(tt_parse_iso8601_utc("2026-07-08T12:30:00Z", &ep_z));
    assert(ep == ep_z && ep > 0);
}

static void test_search_params_encoding(void) {
    char buf[64];

    /* [0x08, 0x00] -> "CAA=" */
    assert(innertube_encode_search_params((SearchFilter){SORT_RELEVANCE, TYPE_ALL, false}, buf, sizeof(buf)));
    assert(strcmp(buf, "CAA=") == 0);

    /* [0x08, 0x02, 0x12, 0x02, 0x10, 0x01] -> "CAISAhAB" */
    assert(innertube_encode_search_params((SearchFilter){SORT_UPLOAD_DATE, TYPE_VIDEO, false}, buf, sizeof(buf)));
    assert(strcmp(buf, "CAISAhAB") == 0);

    /* [0x08, 0x03, 0x12, 0x02, 0x40, 0x01] -> "CAMSAnAB"? Let's verify:
       0x08 0x03 0x12 -> 00001000 00000011 00010010 -> CA M S
       0x02 0x40 0x01 -> 00000010 01000000 00000001 -> A k A B */
    assert(innertube_encode_search_params((SearchFilter){SORT_VIEW_COUNT, TYPE_ALL, true}, buf, sizeof(buf)));
    assert(strcmp(buf, "CAMSAkAB") == 0);

    /* [0x08, 0x01, 0x12, 0x04, 0x10, 0x02, 0x40, 0x01] */
    assert(innertube_encode_search_params((SearchFilter){SORT_RATING, TYPE_CHANNEL, true}, buf, sizeof(buf)));
    assert(strlen(buf) > 4);

    /* SORT_RELEVANCE with live_only omits 0x08,0x00 -> "EgJAAQ==" */
    assert(innertube_encode_search_params((SearchFilter){SORT_RELEVANCE, TYPE_ALL, true}, buf, sizeof(buf)));
    assert(strcmp(buf, "EgJAAQ==") == 0);

    /* SORT_RELEVANCE with TYPE_VIDEO + live_only -> "EgQQAUAB" */
    assert(innertube_encode_search_params((SearchFilter){SORT_RELEVANCE, TYPE_VIDEO, true}, buf, sizeof(buf)));
    assert(strcmp(buf, "EgQQAUAB") == 0);
}

static void test_url_parsing(void) {
    ParsedUrl u;

    assert(innertube_parse_input_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42", &u));
    assert(u.kind == URL_VIDEO && strcmp(u.id, "dQw4w9WgXcQ") == 0);

    assert(innertube_parse_input_url("https://youtu.be/abc123XYZ_-?si=foo", &u));
    assert(u.kind == URL_VIDEO && strcmp(u.id, "abc123XYZ_-") == 0);

    assert(innertube_parse_input_url("https://youtube.com/shorts/shortVid123", &u));
    assert(u.kind == URL_VIDEO && strcmp(u.id, "shortVid123") == 0);

    assert(innertube_parse_input_url("https://m.youtube.com/live/liveVid4567", &u));
    assert(u.kind == URL_VIDEO && strcmp(u.id, "liveVid4567") == 0);

    assert(innertube_parse_input_url("https://www.youtube.com/playlist?list=PL1234567890", &u));
    assert(u.kind == URL_PLAYLIST && strcmp(u.id, "PL1234567890") == 0);

    assert(innertube_parse_input_url("https://www.youtube.com/channel/UCabcdef123456", &u));
    assert(u.kind == URL_CHANNEL && strcmp(u.id, "UCabcdef123456") == 0);
    assert(!u.prefer_live_tab);

    assert(innertube_parse_input_url("https://www.youtube.com/channel/UCabcdef123456/streams", &u));
    assert(u.kind == URL_CHANNEL && strcmp(u.id, "UCabcdef123456") == 0);
    assert(u.prefer_live_tab);

    assert(innertube_parse_input_url("https://www.youtube.com/channel/UCabcdef123456/live", &u));
    assert(u.kind == URL_CHANNEL && strcmp(u.id, "UCabcdef123456") == 0);
    assert(u.prefer_live_tab);

    assert(innertube_parse_input_url("https://www.youtube.com/@BocchiTheRock", &u));
    assert(u.kind == URL_HANDLE && strcmp(u.id, "@BocchiTheRock") == 0);
    assert(!u.prefer_live_tab);
    assert(strcmp(u.resolved_url, "https://www.youtube.com/@BocchiTheRock") == 0);

    assert(innertube_parse_input_url("https://www.youtube.com/@BocchiTheRock/streams", &u));
    assert(u.kind == URL_HANDLE && strcmp(u.id, "@BocchiTheRock") == 0);
    assert(u.prefer_live_tab);
    assert(strcmp(u.resolved_url, "https://www.youtube.com/@BocchiTheRock") == 0);

    assert(innertube_parse_input_url("https://www.youtube.com/@BocchiTheRock/live", &u));
    assert(u.kind == URL_HANDLE && strcmp(u.id, "@BocchiTheRock") == 0);
    assert(u.prefer_live_tab);
    assert(strcmp(u.resolved_url, "https://www.youtube.com/@BocchiTheRock/live") == 0);

    const char *res_json =
        "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCstreamChan\",\"params\":\"EgdzdHJlYW1z8gYECgJ6AA==\"}}}";
    assert(innertube_parse_resolve_url_json(res_json, strlen(res_json), &u));
    assert(u.kind == URL_CHANNEL && strcmp(u.id, "UCstreamChan") == 0);
    assert(u.prefer_live_tab);

    assert(!innertube_parse_input_url("c programming wayland tutorial", &u));
    assert(u.kind == URL_NONE);
}

static void test_innertube_search_browse_next_json(void) {
    const char *search_json =
        "{"
        "  \"contents\": {\"twoColumnSearchResultsRenderer\": {\"primaryContents\": {\"sectionListRenderer\": {\"contents\": ["
        "    {\"itemSectionRenderer\": {\"contents\": ["
        "      {\"videoRenderer\": {"
        "        \"videoId\": \"vid00000001\","
        "        \"title\": {\"runs\": [{\"text\": \"Legacy Video\"}]},"
        "        \"ownerText\": {\"runs\": [{\"text\": \"Ch One\", \"navigationEndpoint\": {\"browseEndpoint\": {\"browseId\": \"UC111\"}}}]},"
        "        \"lengthText\": {\"simpleText\": \"12:34\"},"
        "        \"shortViewCountText\": {\"simpleText\": \"10K views\"},"
        "        \"publishedTimeText\": {\"simpleText\": \"2 days ago\"},"
        "        \"badges\": [{\"metadataBadgeRenderer\": {\"style\": \"BADGE_STYLE_TYPE_LIVE_NOW\"}}]"
        "      }},"
        "      {\"channelRenderer\": {"
        "        \"channelId\": \"UC2222222222\","
        "        \"title\": {\"simpleText\": \"Channel Two\"},"
        "        \"subscriberCountText\": {\"simpleText\": \"50K subscribers\"}"
        "      }},"
        "      {\"lockupViewModel\": {"
        "        \"contentId\": \"PL999888\","
        "        \"contentType\": \"LOCKUP_CONTENT_TYPE_PLAYLIST\","
        "        \"metadata\": {\"lockupMetadataViewModel\": {"
        "          \"title\": {\"content\": \"Modern Playlist Lockup\"},"
        "          \"metadata\": {\"contentMetadataViewModel\": {\"metadataRows\": ["
        "            {\"metadataParts\": [{\"text\": {\"content\": \"Creator X\"}}, {\"text\": {\"content\": \"25 videos\"}}]}"
        "          ]}}"
        "        }},"
        "        \"contentImage\": {\"collectionThumbnailViewModel\": {\"primaryThumbnail\": {\"thumbnailViewModel\": {"
        "          \"image\": {\"sources\": [{\"url\": \"//i.ytimg.com/vi/abc/mqdefault.jpg\"}]},"
        "          \"overlays\": [{\"thumbnailOverlayBadgeViewModel\": {\"thumbnailBadges\": [{\"thumbnailBadgeViewModel\": {\"text\": \"25\"}}]}}]"
        "        }}}}"
        "      }}"
        "    ]}},"
        "    {\"continuationItemRenderer\": {\"continuationEndpoint\": {\"continuationCommand\": {\"token\": \"CONT_TOK_1\"}}}}"
        "  ]}}}}"
        "}";

    MediaList ml;
    media_list_init(&ml);
    char err[128] = {0};
    assert(innertube_parse_search_json(search_json, strlen(search_json), &ml, err, sizeof(err)));
    assert(ml.count == 3);
    assert(ml.items[0].kind == ITEM_VIDEO && strcmp(ml.items[0].id, "vid00000001") == 0);
    assert(strcmp(ml.items[0].title, "Legacy Video") == 0);
    assert(strcmp(ml.items[0].author_id, "UC111") == 0);
    assert(ml.items[0].is_live == true);

    assert(ml.items[1].kind == ITEM_CHANNEL && strcmp(ml.items[1].id, "UC2222222222") == 0);
    assert(ml.items[2].kind == ITEM_PLAYLIST && strcmp(ml.items[2].id, "PL999888") == 0);
    assert(strcmp(ml.items[2].title, "Modern Playlist Lockup") == 0);
    assert(strcmp(ml.items[2].thumbnail_url, "https://i.ytimg.com/vi/abc/mqdefault.jpg") == 0);
    assert(ml.continuation_token && strcmp(ml.continuation_token, "CONT_TOK_1") == 0);
    media_list_free(&ml);

    /* Continuation search JSON */
    const char *cont_json =
        "{\"onResponseReceivedCommands\": [{\"appendContinuationItemsAction\": {\"continuationItems\": ["
        "  {\"videoRenderer\": {\"videoId\": \"vid00000002\", \"title\": {\"simpleText\": \"Page 2 Video\"}}}"
        "]}}]}";
    media_list_init(&ml);
    assert(innertube_parse_search_json(cont_json, strlen(cont_json), &ml, err, sizeof(err)));
    assert(ml.count == 1 && strcmp(ml.items[0].id, "vid00000002") == 0);
    media_list_free(&ml);

    /* Browse playlist JSON */
    const char *browse_json =
        "{"
        "  \"header\": {\"playlistHeaderRenderer\": {\"title\": {\"simpleText\": \"My Fav Mix\"}}},"
        "  \"contents\": {\"twoColumnBrowseResultsRenderer\": {\"tabs\": [{\"tabRenderer\": {\"selected\": true, \"content\": {"
        "    \"sectionListRenderer\": {\"contents\": [{\"itemSectionRenderer\": {\"contents\": [{\"playlistVideoListRenderer\": {\"contents\": ["
        "      {\"playlistVideoRenderer\": {\"videoId\": \"pv111\", \"title\": {\"runs\": [{\"text\": \"Track 1\"}]}, \"lengthText\": {\"simpleText\": \"3:45\"}}}"
        "    ]}} ]}} ]}"
        "  }}}]}}"
        "}";
    media_list_init(&ml);
    assert(innertube_parse_browse_json(browse_json, strlen(browse_json), &ml, err, sizeof(err)));
    assert(ml.heading && strcmp(ml.heading, "My Fav Mix") == 0);
    assert(ml.count == 1 && strcmp(ml.items[0].id, "pv111") == 0);
    media_list_free(&ml);

    /* Next JSON */
    const char *next_json =
        "{"
        "  \"contents\": {\"twoColumnWatchNextResults\": {"
        "    \"results\": {\"results\": {\"contents\": ["
        "      {\"videoPrimaryInfoRenderer\": {"
        "        \"title\": {\"runs\": [{\"text\": \"Main Title\"}]},"
        "        \"viewCount\": {\"videoViewCountRenderer\": {\"viewCount\": {\"simpleText\": \"1,234 views\"}}},"
        "        \"dateText\": {\"simpleText\": \"Jul 8, 2026\"}"
        "      }},"
        "      {\"videoSecondaryInfoRenderer\": {"
        "        \"owner\": {\"videoOwnerRenderer\": {\"title\": {\"runs\": [{\"text\": \"OwnerName\", \"navigationEndpoint\": {\"browseEndpoint\": {\"browseId\": \"UCOWNER\"}}}]}}},"
        "        \"attributedDescription\": {\"content\": \"Full description text here.\"}"
        "      }}"
        "    ]}},"
        "    \"secondaryResults\": {\"secondaryResults\": {\"results\": ["
        "      {\"compactVideoRenderer\": {\"videoId\": \"rec01\", \"title\": {\"simpleText\": \"Rec One\"}}},"
        "      {\"lockupViewModel\": {\"contentId\": \"rec02\", \"contentType\": \"LOCKUP_CONTENT_TYPE_VIDEO\", \"metadata\": {\"lockupMetadataViewModel\": {\"title\": {\"content\": \"Rec Two\"}}}}}"
        "    ]}}"
        "  }}"
        "}";
    MediaItem det = {0};
    media_list_init(&ml);
    assert(innertube_parse_next_json(next_json, strlen(next_json), "main01", &det, &ml, err, sizeof(err)));
    assert(strcmp(det.title, "Main Title") == 0);
    assert(strcmp(det.author, "OwnerName") == 0);
    assert(strcmp(det.author_id, "UCOWNER") == 0);
    assert(strcmp(det.description, "Full description text here.") == 0);
    assert(ml.count == 2);
    assert(strcmp(ml.items[0].id, "rec01") == 0);
    assert(strcmp(ml.items[1].id, "rec02") == 0);
    media_item_free_fields(&det);
    media_list_free(&ml);
}

static void test_rss_and_feed_filtering(void) {
    const char *xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<feed>"
        "  <entry>"
        "    <yt:videoId>newVid1</yt:videoId>"
        "    <title>New &amp; Shiny &lt;1&gt;</title>"
        "    <author><name>Ch&#39;s Name</name></author>"
        "    <yt:channelId>UC_A</yt:channelId>"
        "    <published>2026-07-08T10:00:00+00:00</published>"
        "    <media:group>"
        "      <media:description>Desc &quot;A&quot;</media:description>"
        "      <media:thumbnail url=\"https://i.ytimg.com/vi/newVid1/hqdefault.jpg\"/>"
        "      <media:community><media:statistics views=\"4200\"/></media:community>"
        "    </media:group>"
        "  </entry>"
        "  <entry>"
        "    <yt:videoId>oldVid2</yt:videoId>"
        "    <title>Old Video</title>"
        "    <author><name>Ch B</name></author>"
        "    <yt:channelId>UC_B</yt:channelId>"
        "    <published>2026-01-01T00:00:00+00:00</published>"
        "  </entry>"
        "  <entry>"
        "    <yt:videoId>newerVid3</yt:videoId>"
        "    <title>Newer Video</title>"
        "    <author><name>Ch C</name></author>"
        "    <yt:channelId>UC_C</yt:channelId>"
        "    <published>2026-07-09T10:00:00+00:00</published>"
        "  </entry>"
        "  <entry>"
        "    <yt:videoId>newVid1</yt:videoId>"
        "    <title>Duplicate</title>"
        "    <published>2026-07-08T10:00:00+00:00</published>"
        "  </entry>"
        "</feed>";

    MediaList ml;
    media_list_init(&ml);
    assert(innertube_parse_rss_xml(xml, strlen(xml), &ml, 15));
    assert(ml.count == 4);
    assert(strcmp(ml.items[0].title, "New & Shiny <1>") == 0);
    assert(strcmp(ml.items[0].author, "Ch's Name") == 0);
    assert(strcmp(ml.items[0].description, "Desc \"A\"") == 0);
    assert(strcmp(ml.items[0].view_count_text, "4200 views") == 0);

    int64_t now_ep = 0;
    assert(tt_parse_iso8601_utc("2026-07-10T00:00:00Z", &now_ep));
    int64_t max_age = 0;
    assert(tt_parse_age_seconds("1 month", &max_age));

    innertube_filter_and_sort_feed(&ml, now_ep, max_age);
    assert(ml.count == 2);
    assert(strcmp(ml.items[0].id, "newerVid3") == 0);
    assert(strcmp(ml.items[1].id, "newVid1") == 0);
    media_list_free(&ml);
}

static void test_config_theme_and_keybindings(void) {
    /* Load sample custom theme directly (has comments & trailing comma!) */
    Theme custom_theme;
    theme_init_builtin(&custom_theme, "yuuka");
    assert(theme_load_file(&custom_theme, "examples/sample_theme.json"));
    assert(custom_theme.base.fg.r == 156 && custom_theme.base.fg.g == 178 && custom_theme.base.fg.b == 168);
    assert(custom_theme.base.bg.r == 16 && custom_theme.base.bg.g == 24 && custom_theme.base.bg.b == 21);
    assert(theme_contrast_ratio(custom_theme.text_normal.fg, custom_theme.text_normal.bg) >= 4.5);

    /* Temporary config directory */
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_cfg", tmpdir, sizeof(tmpdir));

    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);
    const char *cfg_json =
        "{\n"
        "  // comment allowed\n"
        "  \"font_size\": 99,\n"
        "  \"thumb_size\": 108,\n"
        "  \"playlist_width\": 420,\n"
        "  \"theme\": \"dark\",\n"
        "  \"max_feed_age\": \"2 weeks\",\n"
        "  \"key_bindings\": [\n"
        "    {\"action\": \"set_light_theme\", \"mod\": \"\", \"key\": \"F2\"},\n"
        "    {\"action\": \"clear_thumbnail_cache\", \"mod\": \"CTRL-ALT\", \"key\": \"C\"},\n"
        "  ],\n"
        "}\n";
    assert(tt_write_file_atomic(cfg_file, cfg_json, strlen(cfg_json)));

    TuiTubeConfig cfg;
    assert(config_load(&cfg, tmpdir));
    assert(cfg.font_size == 48); /* clamped */
    assert(cfg.thumb_size == 108);
    assert(cfg.playlist_width == 420);
    assert(strcmp(cfg.theme_name, "dark") == 0);
    assert(cfg.max_feed_age_sec == 14 * 86400);

    assert(config_match_keybinding(&cfg, 0, XKB_KEY_F2) == ACT_SET_LIGHT_THEME);
    assert(config_match_keybinding(&cfg, TT_MOD_CTRL | TT_MOD_ALT, XKB_KEY_c) == ACT_CLEAR_THUMBNAIL_CACHE);
    assert(config_match_keybinding(&cfg, TT_MOD_CTRL, XKB_KEY_q) == ACT_QUIT);

    config_free(&cfg);
    test_unlink_file(cfg_file);
    test_rmdir(tmpdir);
}

static void test_player_format_mapping(void) {
    char out[512];

    assert(player_map_video_format("1080p60", false, false, out, sizeof(out)));
    assert(strstr(out, "height<=1080") != NULL);
    assert(strstr(out, "fps<=60") != NULL);
    assert(strstr(out, "[dynamic_range!=HDR]") != NULL);
    assert(strstr(out, "[vcodec^=avc1]") != NULL);

    assert(player_map_video_format("720p_vp9", true, false, out, sizeof(out)));
    assert(strstr(out, "height<=720") != NULL);
    assert(strstr(out, "fps<=30") != NULL);
    assert(strstr(out, "dynamic_range") == NULL);
    assert(strstr(out, "[vcodec^=vp9]") != NULL);

    assert(player_map_video_format("2160p60_av1", false, true, out, sizeof(out)));
    assert(strstr(out, "height<=2160") != NULL);
    assert(strstr(out, "vcodec") == NULL);

    assert(player_map_video_format("bestvideo[height<=480]/best", false, false, out, sizeof(out)));
    assert(strcmp(out, "bestvideo[height<=480]/best") == 0);
}

static void test_model_favlist_and_history_persistence(void) {
    /* Load favlist containing [null] entries cleanly */
    char null_fav_path[PATH_MAX];
    test_make_temp_file("tuitube_test_fav_null.json", null_fav_path, sizeof(null_fav_path));
    const char *null_json = "[\n  null\n]\n";
    assert(tt_write_file_atomic(null_fav_path, null_json, strlen(null_json)));
    FavoriteList fl;
    favlist_init(&fl);
    assert(favlist_load(&fl, null_fav_path));
    assert(fl.count == 0);
    test_unlink_file(null_fav_path);

    FavoriteItem f1 = {.kind = ITEM_CHANNEL, .title = "Ch One", .thumbnail_url = "https://x/1.jpg"};
    snprintf(f1.id, sizeof(f1.id), "UC123");
    assert(favlist_add(&fl, &f1));
    assert(favlist_contains(&fl, "UC123"));

    char fav_path[PATH_MAX];
    test_make_temp_file("tuitube_test_fav.json", fav_path, sizeof(fav_path));
    assert(favlist_save(&fl, fav_path));
    favlist_free(&fl);

    favlist_init(&fl);
    assert(favlist_load(&fl, fav_path));
    assert(fl.count == 1 && strcmp(fl.items[0].id, "UC123") == 0);
    assert(favlist_remove(&fl, "UC123"));
    assert(!favlist_contains(&fl, "UC123"));
    favlist_free(&fl);
    test_unlink_file(fav_path);

    /* History deduplication & round-trip */
    HistoryList hl;
    history_init(&hl);
    MediaItem v1 = {.kind = ITEM_VIDEO, .title = "First"};
    snprintf(v1.id, sizeof(v1.id), "vidA");
    MediaItem v2 = {.kind = ITEM_VIDEO, .title = "Second"};
    snprintf(v2.id, sizeof(v2.id), "vidB");

    assert(history_record(&hl, &v1, 100));
    assert(history_record(&hl, &v2, 200));
    assert(history_record(&hl, &v1, 300)); /* moves vidA to front */
    assert(hl.count == 2);
    assert(strcmp(hl.items[0].video_id, "vidA") == 0);
    assert(hl.items[0].watched_at == 300);
    assert(history_contains(&hl, "vidB"));

    char hist_path[PATH_MAX];
    test_make_temp_file("tuitube_test_hist.json", hist_path, sizeof(hist_path));
    assert(history_save(&hl, hist_path));
    history_free(&hl);

    history_init(&hl);
    assert(history_load(&hl, hist_path));
    assert(hl.count == 2 && strcmp(hl.items[0].video_id, "vidA") == 0);
    history_free(&hl);
    test_unlink_file(hist_path);
}

static void test_image_decoding_and_mascot(void) {
    /* Corrupt bytes must return false without aborting */
    const uint8_t garbage[] = {0xFF, 0xD8, 0x00, 0x01, 0x02, 0x03};
    ImageBuffer buf;
    assert(!image_decode_bytes(garbage, sizeof(garbage), &buf));

    const uint8_t png_bad[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00};
    assert(!image_decode_bytes(png_bad, sizeof(png_bad), &buf));

    /* Generate a valid 2x2 PNG via Cairo and verify round-trip decoding */
    char png_path[PATH_MAX];
    test_make_temp_file("tuitube_test_icon.png", png_path, sizeof(png_path));
    cairo_surface_t *gen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2, 2);
    assert(cairo_surface_write_to_png(gen, png_path) == CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(gen);

    cairo_surface_t *surf = image_load_cairo_file(png_path);
    assert(surf != NULL);
    assert(cairo_image_surface_get_width(surf) == 2);
    assert(cairo_image_surface_get_height(surf) == 2);
    cairo_surface_destroy(surf);
    test_unlink_file(png_path);
}

static void assert_theme_wcag_aa(const char *name) {
    Theme th;
    theme_init_builtin(&th, name);
    assert(theme_contrast_ratio(th.base.fg, th.base.bg) >= 4.5);
    assert(theme_contrast_ratio(th.text_normal.fg, th.text_normal.bg) >= 4.5);
    assert(theme_contrast_ratio(th.text_hover.fg, th.text_hover.bg) >= 4.5);
    assert(theme_contrast_ratio(th.text_pressed.fg, th.text_pressed.bg) >= 4.5);
    assert(theme_contrast_ratio(th.text_selected.fg, th.text_selected.bg) >= 4.5);
    assert(theme_contrast_ratio(th.elem_normal.fg, th.elem_normal.bg) >= 4.5);
    assert(theme_contrast_ratio(th.elem_hover.fg, th.elem_hover.bg) >= 4.5);
    assert(theme_contrast_ratio(th.elem_pressed.fg, th.elem_pressed.bg) >= 4.5);
    assert(theme_contrast_ratio(th.elem_selected.fg, th.elem_selected.bg) >= 4.5);
    assert(theme_contrast_ratio(th.scrollbar_normal.fg, th.scrollbar_normal.bg) >= 3.0);
    assert(theme_contrast_ratio(th.scrollbar_hover.fg, th.scrollbar_hover.bg) >= 3.0);
    assert(theme_contrast_ratio(th.scrollbar_pressed.fg, th.scrollbar_pressed.bg) >= 3.0);
    assert(theme_contrast_ratio(th.scrollbar_selected.fg, th.scrollbar_selected.bg) >= 3.0);
}

static void test_wcag_themes_and_default_dark(void) {
    assert_theme_wcag_aa("dark");
    assert_theme_wcag_aa("light");
    assert_theme_wcag_aa("yuuka");

    /* Missing config directory defaults to "dark" */
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_def", tmpdir, sizeof(tmpdir));
    TuiTubeConfig cfg;
    assert(config_load(&cfg, tmpdir));
    assert(strcmp(cfg.theme_name, "dark") == 0);
    assert(cfg.theme.base.bg.r == 15 && cfg.theme.base.bg.g == 17 && cfg.theme.base.bg.b == 23);

    /* UI scale clamping */
    assert(config_effective_ui_scale(NULL) == 1.0);
    cfg.scale = 0.0;  assert(config_effective_ui_scale(&cfg) == 1.0);
    cfg.scale = 0.2;  assert(config_effective_ui_scale(&cfg) == 0.5);
    cfg.scale = 1.25; assert(config_effective_ui_scale(&cfg) == 1.25);
    cfg.scale = 5.0;  assert(config_effective_ui_scale(&cfg) == 3.0);

    config_free(&cfg);
    test_rmdir(tmpdir);
}

static void test_new_keybindings_and_formatting(void) {
    TuiTubeConfig cfg;
    config_init_defaults(&cfg);

    xkb_keysym_t qsym = XKB_KEY_NoSymbol;
    assert(config_key_from_string("?", &qsym) && qsym == XKB_KEY_question);
    assert(config_action_from_string("navigate_back") == ACT_NAV_BACK);
    assert(config_action_from_string("toggle_help") == ACT_TOGGLE_HELP);

    assert(config_match_keybinding(&cfg, TT_MOD_ALT, XKB_KEY_Left) == ACT_NAV_BACK);
    assert(config_match_keybinding(&cfg, 0, XKB_KEY_BackSpace) == ACT_NAV_BACK);
    assert(config_match_keybinding(&cfg, 0, XKB_KEY_F1) == ACT_TOGGLE_HELP);
    assert(config_match_keybinding(&cfg, 0, XKB_KEY_question) == ACT_TOGGLE_HELP);
    assert(config_match_keybinding(&cfg, TT_MOD_SHIFT, XKB_KEY_question) == ACT_TOGGLE_HELP);

    char out[64];
    assert(config_format_action_shortcut(&cfg, ACT_SHOW_FAVORITES, out, sizeof(out)));
    assert(strcmp(out, "Ctrl+F") == 0);
    assert(config_format_action_shortcut(&cfg, ACT_NAV_BACK, out, sizeof(out)));
    assert(strcmp(out, "Alt+Left") == 0);
    assert(config_format_action_shortcut(&cfg, ACT_TOGGLE_HELP, out, sizeof(out)));
    assert(strcmp(out, "F1") == 0);
    assert(config_format_action_shortcut(&cfg, ACT_FOCUS_SEARCH_INPUT, out, sizeof(out)));
    assert(strcmp(out, "/") == 0);
    assert(config_format_action_shortcut(&cfg, ACT_PLAY_VIDEO, out, sizeof(out)));
    assert(strcmp(out, "Enter") == 0);
    assert(!config_format_action_shortcut(&cfg, ACT_VIEW_USER_VIDEOS, out, sizeof(out)));
    assert(out[0] == '\0');
    assert(!config_format_action_shortcut(&cfg, ACT_NONE, out, sizeof(out)));

    config_free(&cfg);
}

static void test_nav_stack_push_pop_and_eviction(void) {
    NavStack ns;
    nav_stack_init(&ns);
    assert(!nav_stack_can_pop(&ns));

    for (int i = 0; i < NAV_STACK_MAX + 2; i++) {
        NavEntry snap = {0};
        media_list_init(&snap.list);
        snap.list.heading = tt_asprintf("Heading %d", i);
        snap.list.continuation_token = tt_asprintf("tok_%d", i);

        MediaItem it = {0};
        it.kind = ITEM_VIDEO;
        snprintf(it.id, sizeof(it.id), "vid_%d", i);
        it.title = tt_asprintf("Title %d", i);
        media_list_append_move(&snap.list, &it);

        snap.selected_idx = i;
        snap.scroll_y = (double)i * 12.5;
        snap.view_mode = i % 3;
        snap.query = tt_asprintf("query_%d", i);
        snap.heading = tt_asprintf("Heading %d", i);

        assert(nav_stack_push(&ns, &snap));
        nav_entry_free_fields(&snap);
    }

    assert(ns.count == NAV_STACK_MAX);
    assert(nav_stack_can_pop(&ns));

    /* Entries 0 and 1 were evicted; popping yields 17 down to 2 */
    for (int expected = NAV_STACK_MAX + 1; expected >= 2; expected--) {
        NavEntry popped = {0};
        assert(nav_stack_pop(&ns, &popped));
        assert(popped.selected_idx == expected);
        assert(popped.list.count == 1);
        char exp_id[32];
        snprintf(exp_id, sizeof(exp_id), "vid_%d", expected);
        assert(strcmp(popped.list.items[0].id, exp_id) == 0);
        nav_entry_free_fields(&popped);
    }

    assert(!nav_stack_can_pop(&ns));
    assert(ns.count == 0);
    nav_stack_free(&ns);
}

typedef struct {
    int id;
    int *ran_flag;
} PoolTestArg;

static void *pool_test_work(CURL *curl, void *arg, uint64_t gen, HttpPool *pool) {
    (void)curl;
    (void)gen;
    (void)pool;
    PoolTestArg *a = arg;
    if (a->ran_flag) *a->ran_flag = 1;
    int *out = malloc(sizeof(int));
    *out = a->id;
    return out;
}

static void test_live_shelf_lockup_engagement_and_members_only(void) {
    /* Shelves, richSection/richShelf, thumbnailOverlayTimeStatusRenderer, lockupViewModel live & channel-tab */
    const char *search_json =
        "{"
        "  \"contents\": {\"twoColumnSearchResultsRenderer\": {\"primaryContents\": {\"sectionListRenderer\": {\"contents\": ["
        "    {\"itemSectionRenderer\": {\"contents\": ["
        "      {\"shelfRenderer\": {\"content\": {\"verticalListRenderer\": {\"items\": ["
        "        {\"videoRenderer\": {"
        "          \"videoId\": \"liveShelf01\","
        "          \"title\": {\"simpleText\": \"Live In Shelf\"},"
        "          \"ownerText\": {\"runs\": [{\"text\": \"Streamer A\", \"navigationEndpoint\": {\"browseEndpoint\": {\"browseId\": \"UCSTREAM1\"}}}]},"
        "          \"shortViewCountText\": {\"simpleText\": \"4.2K watching\"},"
        "          \"descriptionSnippet\": {\"runs\": [{\"text\": \"Snippet preview\"}]},"
        "          \"thumbnailOverlays\": [{\"thumbnailOverlayTimeStatusRenderer\": {\"style\": \"LIVE\", \"text\": {\"simpleText\": \"LIVE\"}}}],"
        "          \"badges\": [{\"metadataBadgeRenderer\": {\"style\": \"BADGE_STYLE_TYPE_MEMBERS_ONLY\"}}]"
        "        }}"
        "      ]}}}},"
        "      {\"lockupViewModel\": {"
        "        \"contentId\": \"liveLockup02\","
        "        \"contentType\": \"LOCKUP_CONTENT_TYPE_VIDEO\","
        "        \"metadata\": {\"lockupMetadataViewModel\": {"
        "          \"title\": {\"content\": \"Modern Live Lockup\"},"
        "          \"metadata\": {\"contentMetadataViewModel\": {\"metadataRows\": ["
        "            {\"metadataParts\": [{\"text\": {\"content\": \"Ch Lockup\", \"commandRuns\": [{\"onTap\": {\"innertubeCommand\": {\"browseEndpoint\": {\"browseId\": \"UCLOCKUP1\"}}}}]}}]},"
        "            {\"metadataParts\": [{\"text\": {\"content\": \"1.5K watching\"}}]}"
        "          ]}}"
        "        }},"
        "        \"contentImage\": {\"thumbnailViewModel\": {"
        "          \"overlays\": [{\"thumbnailBottomOverlayViewModel\": {\"badges\": [{\"thumbnailBadgeViewModel\": {\"badgeStyle\": \"THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE\", \"text\": \"LIVE\"}}]}}]"
        "        }}"
        "      }}"
        "    ]}}"
        "  ]}}}}"
        "}";

    MediaList ml;
    media_list_init(&ml);
    char err[128] = {0};
    assert(innertube_parse_search_json(search_json, strlen(search_json), &ml, err, sizeof(err)));
    assert(ml.count == 2);

    assert(strcmp(ml.items[0].id, "liveShelf01") == 0);
    assert(ml.items[0].is_live == true);
    assert(ml.items[0].is_members_only == true);
    assert(ml.items[0].details_loaded == false);
    assert(ml.items[0].description && strcmp(ml.items[0].description, "Snippet preview") == 0);
    assert(ml.items[0].duration_text && strcmp(ml.items[0].duration_text, "LIVE") == 0);

    assert(strcmp(ml.items[1].id, "liveLockup02") == 0);
    assert(ml.items[1].is_live == true);
    assert(ml.items[1].author && strcmp(ml.items[1].author, "Ch Lockup") == 0);
    assert(ml.items[1].author_id && strcmp(ml.items[1].author_id, "UCLOCKUP1") == 0);
    assert(ml.items[1].duration_text && strcmp(ml.items[1].duration_text, "LIVE") == 0);

    media_list_filter_members_only(&ml);
    assert(ml.count == 1);
    assert(strcmp(ml.items[0].id, "liveLockup02") == 0);
    media_list_free(&ml);

    /* Browse JSON: pageHeaderViewModel heading, richSectionRenderer/richShelfRenderer,
       channel-tab lockupViewModel without author row, and continuationContents */
    const char *browse_json =
        "{"
        "  \"header\": {\"pageHeaderRenderer\": {\"content\": {\"pageHeaderViewModel\": {\"title\": {\"dynamicTextViewModel\": {\"text\": {\"content\": \"Stream Channel\"}}}}}}},"
        "  \"contents\": {\"twoColumnBrowseResultsRenderer\": {\"tabs\": [{\"tabRenderer\": {\"selected\": true, \"content\": {"
        "    \"richGridRenderer\": {\"contents\": ["
        "      {\"richSectionRenderer\": {\"content\": {\"richShelfRenderer\": {\"contents\": ["
        "        {\"richItemRenderer\": {\"content\": {\"lockupViewModel\": {"
        "          \"contentId\": \"chanStream01\","
        "          \"contentType\": \"LOCKUP_CONTENT_TYPE_VIDEO\","
        "          \"metadata\": {\"lockupMetadataViewModel\": {"
        "            \"title\": {\"content\": \"Channel Tab Stream\"},"
        "            \"badges\": [{\"badgeViewModel\": {\"badgeStyle\": \"BADGE_MEMBERS_ONLY\", \"badgeText\": \"Members only\"}}],"
        "            \"metadata\": {\"contentMetadataViewModel\": {\"metadataRows\": ["
        "              {\"metadataParts\": [{\"text\": {\"content\": \"4.2K watching\"}}, {\"text\": {\"content\": \"Streamed 2 days ago\"}}]}"
        "            ]}}"
        "          }},"
        "          \"contentImage\": {\"thumbnailViewModel\": {"
        "            \"overlays\": [{\"thumbnailBottomOverlayViewModel\": {\"badges\": [{\"thumbnailBadgeViewModel\": {\"badgeStyle\": \"THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE\", \"text\": \"LIVE\"}}]}}]"
        "          }}"
        "        }}}}"
        "      ]}}}}"
        "    ]}"
        "  }}}]}},"
        "  \"continuationContents\": {\"richGridContinuation\": {\"contents\": ["
        "    {\"richItemRenderer\": {\"content\": {\"videoRenderer\": {\"videoId\": \"contVid01\", \"title\": {\"simpleText\": \"Cont Video\"}}}}}"
        "  ], \"continuations\": [{\"nextContinuationData\": {\"continuation\": \"LEGACY_CONT_2\"}}]}}"
        "}";
    media_list_init(&ml);
    assert(innertube_parse_browse_json(browse_json, strlen(browse_json), &ml, err, sizeof(err)));
    assert(ml.heading && strcmp(ml.heading, "Stream Channel") == 0);
    assert(ml.count == 2);
    assert(strcmp(ml.items[0].id, "chanStream01") == 0);
    assert(ml.items[0].author == NULL);
    assert(ml.items[0].view_count_text && strcmp(ml.items[0].view_count_text, "4.2K watching") == 0);
    assert(ml.items[0].is_live == true);
    assert(ml.items[0].is_members_only == true);
    assert(strcmp(ml.items[1].id, "contVid01") == 0);
    assert(ml.continuation_token && strcmp(ml.continuation_token, "LEGACY_CONT_2") == 0);
    media_list_free(&ml);

    /* engagementPanels structured description extraction */
    const char *next_eng_json =
        "{"
        "  \"contents\": {\"twoColumnWatchNextResults\": {\"results\": {\"results\": {\"contents\": ["
        "    {\"videoPrimaryInfoRenderer\": {\"title\": {\"simpleText\": \"Eng Title\"}}},"
        "    {\"videoSecondaryInfoRenderer\": {\"attributedDescription\": {\"content\": \"\"}}}"
        "  ]}}}},"
        "  \"engagementPanels\": [{\"engagementPanelSectionListRenderer\": {\"content\": {\"structuredDescriptionContentRenderer\": {\"items\": ["
        "    {\"videoDescriptionHeaderRenderer\": {"
        "      \"channel\": {\"simpleText\": \"Eng Channel\"},"
        "      \"channelNavigationEndpoint\": {\"browseEndpoint\": {\"browseId\": \"UCENG999\"}},"
        "      \"publishDate\": {\"simpleText\": \"Nov 26, 2026\"},"
        "      \"views\": {\"simpleText\": \"999 views\"}"
        "    }},"
        "    {\"expandableVideoDescriptionBodyRenderer\": {\"attributedDescriptionBodyText\": {\"content\": \"Structured description body\"}}}"
        "  ]}}}}]"
        "}";
    MediaItem det = {0};
    assert(innertube_parse_next_json(next_eng_json, strlen(next_eng_json), "eng01", &det, NULL, err, sizeof(err)));
    assert(det.details_loaded == true);
    assert(det.description && strcmp(det.description, "Structured description body") == 0);
    assert(det.author && strcmp(det.author, "Eng Channel") == 0);
    assert(det.author_id && strcmp(det.author_id, "UCENG999") == 0);
    assert(det.published_text && strcmp(det.published_text, "Nov 26, 2026") == 0);
    assert(det.view_count_text && strcmp(det.view_count_text, "999 views") == 0);
    media_item_free_fields(&det);

    /* yt-dlp subscriber_only & is_live */
    const char *ytdlp_json =
        "{\"entries\": ["
        "  {\"id\": \"yd1\", \"title\": \"Members\", \"availability\": \"subscriber_only\"},"
        "  {\"id\": \"yd2\", \"title\": \"Live\", \"live_status\": \"is_live\"}"
        "]}";
    media_list_init(&ml);
    assert(innertube_parse_ytdlp_json(ytdlp_json, strlen(ytdlp_json), &ml));
    assert(ml.count == 2);
    assert(ml.items[0].is_members_only == true);
    assert(ml.items[1].is_members_only == false && ml.items[1].is_live == true);
    assert(ml.items[1].duration_text && strcmp(ml.items[1].duration_text, "LIVE") == 0);
    media_list_free(&ml);
}

static void test_http_pool_detail_gen_independence(void) {
    HttpPool *pool = http_pool_create(1);
    assert(pool != NULL);

    http_pool_set_active_gen(pool, 10);
    http_pool_set_active_detail_gen(pool, 1);

    int ran_stale_search = 0;
    int ran_stale_detail = 0;
    int ran_valid_detail = 0;

    PoolTestArg *a1 = calloc(1, sizeof(PoolTestArg));
    a1->id = 101;
    a1->ran_flag = &ran_stale_search;
    PoolTestArg *a2 = calloc(1, sizeof(PoolTestArg));
    a2->id = 102;
    a2->ran_flag = &ran_stale_detail;
    PoolTestArg *a3 = calloc(1, sizeof(PoolTestArg));
    a3->id = 103;
    a3->ran_flag = &ran_valid_detail;

    assert(http_pool_submit(pool, JOB_SEARCH, 9, a1, pool_test_work, free, free));
    assert(http_pool_submit(pool, JOB_DETAIL, 0, a2, pool_test_work, free, free));
    assert(http_pool_submit(pool, JOB_DETAIL, 1, a3, pool_test_work, free, free));

#ifdef _WIN32
    HANDLE nh = (HANDLE)http_pool_notify_handle(pool);
    assert(nh != NULL);
    bool got_valid = false;
    for (int iter = 0; iter < 50 && !got_valid; iter++) {
        WaitForSingleObject(nh, 100);
        http_pool_drain_notify(pool);
#else
    int nfd = http_pool_notify_fd(pool);
    bool got_valid = false;
    for (int iter = 0; iter < 50 && !got_valid; iter++) {
        struct pollfd pfd = {.fd = nfd, .events = POLLIN};
        poll(&pfd, 1, 100);
        http_pool_drain_notify(pool);
#endif
        HttpResult res;
        while (http_pool_pop_result(pool, &res)) {
            if (res.kind == RES_JOB_DONE && res.job_type == JOB_DETAIL && res.request_gen == 1) {
                assert(res.data != NULL && *(int *)res.data == 103);
                got_valid = true;
            }
            http_result_free(&res);
        }
    }
    assert(got_valid);
    assert(ran_valid_detail == 1);
    assert(ran_stale_search == 0);
    assert(ran_stale_detail == 0);

    http_pool_destroy(pool);
}

static void test_config_members_only_and_download(void) {
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_dl", tmpdir, sizeof(tmpdir));

    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);
    const char *cfg_json =
        "{\n"
        "  \"hide_members_only\": true,\n"
        "  \"download_dir\": \"~/MyVideos\"\n"
        "}\n";
    assert(tt_write_file_atomic(cfg_file, cfg_json, strlen(cfg_json)));

    TuiTubeConfig cfg;
    assert(config_load(&cfg, tmpdir));
    assert(cfg.hide_members_only == true);
    assert(cfg.download_dir && strcmp(cfg.download_dir, "~/MyVideos") == 0);

    char *resolved = config_resolve_download_dir(&cfg);
    assert(resolved != NULL);
    assert(strstr(resolved, "/MyVideos") != NULL);
    free(resolved);

    assert(config_action_from_string("download_video") == ACT_DOWNLOAD_VIDEO);
    assert(config_match_keybinding(&cfg, TT_MOD_CTRL, XKB_KEY_s) == ACT_DOWNLOAD_VIDEO);

    char shortcut[32];
    assert(config_format_action_shortcut(&cfg, ACT_DOWNLOAD_VIDEO, shortcut, sizeof(shortcut)));
    assert(strcmp(shortcut, "Ctrl+S") == 0);

    config_free(&cfg);

    /* Alias "hide_member_videos" + relative download_dir */
    const char *alias_json =
        "{\n"
        "  \"hide_member_videos\": true,\n"
        "  \"download_dir\": \"saved\"\n"
        "}\n";
    assert(tt_write_file_atomic(cfg_file, alias_json, strlen(alias_json)));
    assert(config_load(&cfg, tmpdir));
    assert(cfg.hide_members_only == true);
    resolved = config_resolve_download_dir(&cfg);
    char expected_rel[PATH_MAX + 16];
    snprintf(expected_rel, sizeof(expected_rel), "%s/saved", cfg.config_dir);
    assert(resolved && strcmp(resolved, expected_rel) == 0);
    free(resolved);
    config_free(&cfg);

    test_unlink_file(cfg_file);
    test_rmdir(tmpdir);

    /* Download progress template parsing */
    double p = -1.0;
    assert(player_parse_ytdlp_progress_line("TT_DL:524288:1048576:NA: 50.0%", &p));
    assert(p > 0.49 && p < 0.51);
    assert(player_parse_ytdlp_progress_line("TT_DL:250:NA:1000: 25.0%", &p));
    assert(p > 0.24 && p < 0.26);
    assert(player_parse_ytdlp_progress_line("TT_DL:NA:NA:NA: 73.5%", &p));
    assert(p > 0.73 && p < 0.74);
    assert(!player_parse_ytdlp_progress_line("other line", &p));
}

static void test_wayland_shm_sizing_and_scaling(void) {
    int pw = 0, ph = 0;

    /* 4K @ 125% (scale_120 = 150) -> exact 3840 x 2160 */
    assert(tt_shm_target_dims(3072, 1728, 150u, 2, &pw, &ph));
    assert(pw == 3840 && ph == 2160);

    /* 1440p @ 125% (scale_120 = 150) -> exact 2560 x 1440 */
    assert(tt_shm_target_dims(2048, 1152, 150u, 2, &pw, &ph));
    assert(pw == 2560 && ph == 1440);

    /* 1440p @ 150% (scale_120 = 180) and 175% (scale_120 = 210) */
    assert(tt_shm_target_dims(1707, 960, 180u, 2, &pw, &ph));
    assert(pw == 2561 && ph == 1440);
    assert(tt_shm_target_dims(1463, 823, 210u, 2, &pw, &ph));
    assert(pw == 2560 && ph == 1440);

    /* Halfway-away-from-zero rounding: 102*1.25 = 127.5 -> 128, 50*1.25 = 62.5 -> 63 */
    assert(tt_shm_target_dims(102, 50, 150u, 1, &pw, &ph));
    assert(pw == 128 && ph == 63);

    /* Scale_120 clamping: [60, 480] */
    assert(tt_shm_target_dims(100, 100, 10u, 2, &pw, &ph));
    assert(pw == 50 && ph == 50);
    assert(tt_shm_target_dims(100, 100, 999u, 1, &pw, &ph));
    assert(pw == 400 && ph == 400);

    /* Integer fallback (scale_120 == 0) with int_scale clamped to [1, 4] */
    assert(tt_shm_target_dims(1024, 640, 0u, 0, &pw, &ph));
    assert(pw == 1024 && ph == 640);
    assert(tt_shm_target_dims(1024, 640, 0u, 2, &pw, &ph));
    assert(pw == 2048 && ph == 1280);
    assert(tt_shm_target_dims(500, 300, 0u, 8, &pw, &ph));
    assert(pw == 2000 && ph == 1200);

    /* Invalid & INT32_MAX overflow bounds */
    assert(!tt_shm_target_dims(0, 640, 120u, 1, &pw, &ph));
    assert(!tt_shm_target_dims(640, -1, 120u, 1, &pw, &ph));
    assert(!tt_shm_target_dims(640, 480, 120u, 1, NULL, &ph));
    assert(!tt_shm_target_dims(640, 480, 120u, 1, &pw, NULL));
    assert(!tt_shm_target_dims(32768, 100, 0u, 1, &pw, &ph));
    assert(!tt_shm_target_dims(25000, 25000, 0u, 1, &pw, &ph));

    /* 256 KiB alignment without power-of-two bloat */
    assert(tt_shm_pool_capacity(0) == 0);
    assert(tt_shm_pool_capacity((size_t)INT32_MAX + 1u) == 0);
    assert(tt_shm_pool_capacity(1) == TT_SHM_ALIGN_BYTES);
    assert(tt_shm_pool_capacity(TT_SHM_ALIGN_BYTES) == TT_SHM_ALIGN_BYTES);
    assert(tt_shm_pool_capacity(TT_SHM_ALIGN_BYTES + 1u) == 2u * TT_SHM_ALIGN_BYTES);
    size_t uhd_bytes = 3840u * 2160u * 4u; /* 33,177,600 bytes */
    size_t uhd_cap = tt_shm_pool_capacity(uhd_bytes);
    assert(uhd_cap >= uhd_bytes);
    assert(uhd_cap % TT_SHM_ALIGN_BYTES == 0);
    assert(uhd_cap - uhd_bytes < TT_SHM_ALIGN_BYTES);
    assert(tt_shm_pool_capacity((size_t)INT32_MAX) == (size_t)INT32_MAX);

    /* 512 KiB shrink threshold */
    assert(!tt_shm_should_shrink(0, 1024u * 1024u));
    assert(!tt_shm_should_shrink(1024u * 1024u, 1024u * 1024u));
    assert(!tt_shm_should_shrink(512u * 1024u + 1u, 1024u * 1024u));
    assert(tt_shm_should_shrink(512u * 1024u, 1024u * 1024u));
    assert(tt_shm_should_shrink(1024u * 640u * 4u, uhd_cap));
}

static void test_mpv_osd_dims_and_script_opts(void) {
    int pw = 0, ph = 0, lw = 0, lh = 0;

    /* 1080p @ 1.0x */
    assert(tt_mpv_osd_dims(1920, 1080, 1.0, &pw, &ph, &lw, &lh));
    assert(pw == 1920 && ph == 1080 && lw == 1920 && lh == 1080);

    /* 4K @ 1.5x -> logical 2560x1440 */
    assert(tt_mpv_osd_dims(3840, 2160, 1.5, &pw, &ph, &lw, &lh));
    assert(pw == 3840 && ph == 2160 && lw == 2560 && lh == 1440);

    /* Clamping [0.5, 4.0] and NaN/Inf/non-positive fallback to 1.0 */
    assert(tt_mpv_osd_dims(1000, 500, 0.1, &pw, &ph, &lw, &lh));
    assert(lw == 2000 && lh == 1000);
    assert(tt_mpv_osd_dims(1000, 500, 10.0, &pw, &ph, &lw, &lh));
    assert(lw == 250 && lh == 125);
    assert(tt_mpv_osd_dims(1000, 500, -2.0, &pw, &ph, &lw, &lh));
    assert(lw == 1000 && lh == 500);
    assert(tt_mpv_osd_dims(1000, 500, NAN, &pw, &ph, &lw, &lh));
    assert(lw == 1000 && lh == 500);
    assert(tt_mpv_osd_dims(1000, 500, INFINITY, &pw, &ph, &lw, &lh));
    assert(lw == 1000 && lh == 500);

    /* Bounds rejections */
    assert(!tt_mpv_osd_dims(0, 720, 1.0, &pw, &ph, &lw, &lh));
    assert(!tt_mpv_osd_dims(1280, -1, 1.0, &pw, &ph, &lw, &lh));
    assert(!tt_mpv_osd_dims(32768, 100, 1.0, &pw, &ph, &lw, &lh));
    assert(!tt_mpv_osd_dims(25000, 25000, 1.0, &pw, &ph, &lw, &lh));
    assert(!tt_mpv_osd_dims(1280, 720, 1.0, NULL, &ph, &lw, &lh));

    /* script-opts parsing */
    char dir[PATH_MAX];
    assert(!tt_mpv_parse_script_opt_config_dir(NULL, dir, sizeof(dir)));
    assert(!tt_mpv_parse_script_opt_config_dir("uosc-color=fg,other=1", dir, sizeof(dir)));
    assert(tt_mpv_parse_script_opt_config_dir("uosc-color=1,tuitube-config_dir=/tmp/tt_cfg,other=2", dir, sizeof(dir)));
    assert(strcmp(dir, "/tmp/tt_cfg") == 0);

    /* Length-prefixed %<len>% value with embedded comma */
    assert(tt_mpv_parse_script_opt_config_dir("foo=bar,tuitube-config_dir=%12%/a,b/tuitube,baz=9", dir, sizeof(dir)));
    assert(strcmp(dir, "/a,b/tuitube") == 0);

    /* Underscore key & ~ expansion */
    assert(tt_mpv_parse_script_opt_config_dir("tuitube_config_dir=~/my_tuitube", dir, sizeof(dir)));
    assert(strstr(dir, "/my_tuitube") != NULL && tt_is_abs_path(dir));

    /* Buffer too small */
    char tiny[8];
    assert(!tt_mpv_parse_script_opt_config_dir("tuitube-config_dir=/very/long/path", tiny, sizeof(tiny)));
    assert(tiny[0] == '\0');
}

static void test_mpv_key_mouse_and_unicode_parsing(void) {
    TtMpvKeyEvent ev;

    /* Invalid states / args */
    assert(!tt_mpv_parse_key_event(NULL, "ESC", "", NULL, &ev));
    assert(!tt_mpv_parse_key_event("x-m", "ESC", "", NULL, &ev));
    assert(!tt_mpv_parse_key_event("d-m", "", "", NULL, &ev));

    /* Named key & canceled state */
    assert(tt_mpv_parse_key_event("d-m", "ESC", "", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && ev.pressed && !ev.repeat && ev.keysym == XKB_KEY_Escape && ev.mods == 0);

    assert(tt_mpv_parse_key_event("d-c", "ESC", "", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && !ev.pressed);

    /* Modifiers & shifted uppercase letter */
    assert(tt_mpv_parse_key_event("d-m", "Ctrl+Alt+f", "", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && ev.pressed);
    assert(ev.mods == (TT_MOD_CTRL | TT_MOD_ALT) && ev.keysym == 'f' && ev.utf8[0] == '\0');

    assert(tt_mpv_parse_key_event("r-m", "J", "J", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && ev.pressed && ev.repeat);
    assert((ev.mods & TT_MOD_SHIFT) != 0 && ev.keysym == 'J' && strcmp(ev.utf8, "J") == 0);

    assert(tt_mpv_parse_key_event("d-m", "Meta+Left", "", NULL, &ev));
    assert(ev.mods == TT_MOD_SUPER && ev.keysym == XKB_KEY_Left);

    assert(tt_mpv_parse_key_event("d-m", "SHARP", "", NULL, &ev));
    assert(ev.keysym == '#' && strcmp(ev.utf8, "#") == 0);

    assert(tt_mpv_parse_key_event("d-m", "KP_DIVIDE", "", NULL, &ev));
    assert(ev.keysym == XKB_KEY_slash && strcmp(ev.utf8, "/") == 0);

    /* Mouse buttons & press pulse */
    assert(tt_mpv_parse_key_event("p-m", "MBTN_LEFT", "", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_BUTTON && ev.mouse_btn == 1 && ev.pressed && ev.is_press_pulse && !ev.double_click);

    assert(tt_mpv_parse_key_event("d-m", "MBTN_LEFT_DBL", "", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_BUTTON && ev.mouse_btn == 1 && ev.double_click);
    assert(!tt_mpv_parse_key_event("u-m", "MBTN_LEFT_DBL", "", NULL, &ev));

    assert(tt_mpv_parse_key_event("d-m", "MBTN_BACK", "", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_BUTTON && ev.mouse_btn == 4);

    /* Wheel direction & scale */
    assert(tt_mpv_parse_key_event("d-m", "WHEEL_DOWN", "", "2.0", &ev));
    assert(ev.kind == TT_MPV_INPUT_WHEEL && ev.wheel_dy > 0.0 && ev.wheel_dy == 30.0);
    assert(tt_mpv_parse_key_event("d-m", "WHEEL_UP", "", "1.0", &ev));
    assert(ev.kind == TT_MPV_INPUT_WHEEL && ev.wheel_dy < 0.0 && ev.wheel_dy == -15.0);
    assert(!tt_mpv_parse_key_event("u-m", "WHEEL_DOWN", "", "1.0", &ev));

    /* ANY_UNICODE: single ASCII shortcut vs multi-byte Vietnamese UTF-8 */
    assert(tt_mpv_parse_key_event("d-m", "ANY_UNICODE", "?", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && ev.keysym == XKB_KEY_question && strcmp(ev.utf8, "?") == 0);

    assert(tt_mpv_parse_key_event("d-m", "ANY_UNICODE", "\xE1\xBA\xBF", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && ev.keysym == XKB_KEY_NoSymbol);
    assert(strcmp(ev.utf8, "\xE1\xBA\xBF") == 0);

    assert(tt_mpv_parse_key_event("d-m", "ANY_UNICODE", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t", NULL, &ev));
    assert(ev.kind == TT_MPV_INPUT_KEY && ev.keysym == XKB_KEY_NoSymbol);
    assert(strcmp(ev.utf8, "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t") == 0);
}

static void test_config_explicit_format_and_slang_flags(void) {
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_mpvcfg", tmpdir, sizeof(tmpdir));

    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);

    /* Absent -> has_video_format == false, has_sub_language == false */
    const char *empty_cfg = "{ \"theme\": \"dark\" }\n";
    assert(tt_write_file_atomic(cfg_file, empty_cfg, strlen(empty_cfg)));
    TuiTubeConfig cfg;
    assert(config_load(&cfg, tmpdir));
    assert(!cfg.has_video_format);
    assert(!cfg.has_sub_language);
    config_free(&cfg);

    /* Empty strings -> still false */
    const char *blank_cfg = "{ \"video_format\": \"\", \"sub_language\": \"\" }\n";
    assert(tt_write_file_atomic(cfg_file, blank_cfg, strlen(blank_cfg)));
    assert(config_load(&cfg, tmpdir));
    assert(!cfg.has_video_format);
    assert(!cfg.has_sub_language);
    config_free(&cfg);

    /* Explicit non-empty strings -> true */
    const char *set_cfg = "{ \"video_format\": \"720p\", \"sub_language\": \"ja,en\" }\n";
    assert(tt_write_file_atomic(cfg_file, set_cfg, strlen(set_cfg)));
    assert(config_load(&cfg, tmpdir));
    assert(cfg.has_video_format);
    assert(cfg.has_sub_language);
    assert(strcmp(cfg.video_format, "720p") == 0);
    assert(strcmp(cfg.sub_language, "ja,en") == 0);
    config_free(&cfg);

    test_unlink_file(cfg_file);
    test_rmdir(tmpdir);
}

static void test_thumb_cache_flush_ram_and_drop_jobs(void) {
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_tc", tmpdir, sizeof(tmpdir));

    ThumbCache *tc = thumb_cache_create(tmpdir);
    assert(tc != NULL);

    const uint8_t dummy[] = {1, 2, 3, 4};
    assert(thumb_disk_save(tmpdir, "k1", dummy, sizeof(dummy)));

    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
    thumb_cache_put(tc, "k1", s);
    cairo_surface_destroy(s);
    assert(thumb_cache_get(tc, "k1") != NULL);

    /* Flush RAM only: in-memory surface evicted, disk cache preserved */
    thumb_cache_flush_ram(tc);
    assert(thumb_cache_get(tc, "k1") == NULL);

    size_t dlen = 0;
    uint8_t *disk = thumb_disk_load(tmpdir, "k1", &dlen);
    assert(disk != NULL && dlen == sizeof(dummy));
    free(disk);

    thumb_cache_clear(tc);
    thumb_cache_destroy(tc);
    test_rmdir(tmpdir);

    /* Drop queued thumbnail jobs cleanly */
    HttpPool *pool = http_pool_create(1);
    assert(pool != NULL);
    http_pool_drop_thumbnails(pool);
    http_pool_destroy(pool);
}

static void test_win32_cmdline_quoting(void) {
    StrBuf sb;
    sb_init(&sb);

    assert(tt_win32_quote_arg("yt-dlp", &sb));
    assert(strcmp(sb.data, "yt-dlp") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("", &sb));
    assert(strcmp(sb.data, "\"\"") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("hello world", &sb));
    assert(strcmp(sb.data, "\"hello world\"") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("say \"hi\"", &sb));
    assert(strcmp(sb.data, "\"say \\\"hi\\\"\"") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("C:\\Users\\test\\file.mp4", &sb));
    assert(strcmp(sb.data, "C:\\Users\\test\\file.mp4") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("C:\\Program Files\\mpv\\mpv.exe", &sb));
    assert(strcmp(sb.data, "\"C:\\Program Files\\mpv\\mpv.exe\"") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("a\\\"b", &sb));
    assert(strcmp(sb.data, "\"a\\\\\\\"b\"") == 0);

    sb_clear(&sb);
    assert(tt_win32_quote_arg("C:\\my dir\\", &sb));
    assert(strcmp(sb.data, "\"C:\\my dir\\\\\"") == 0);
    sb_free(&sb);

    const char *argv[] = {
        "C:\\Program Files\\mpv\\mpv.exe",
        "--force-media-title=a \"b\"\\",
        "",
        "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
        NULL
    };
    char *cmd = tt_win32_build_cmdline(argv);
    assert(cmd != NULL);
    assert(strcmp(cmd,
                  "\"C:\\Program Files\\mpv\\mpv.exe\" "
                  "\"--force-media-title=a \\\"b\\\"\\\\\" "
                  "\"\" "
                  "https://www.youtube.com/watch?v=dQw4w9WgXcQ") == 0);
    free(cmd);
}

static void test_windows_paths_and_script_opts(void) {
    assert(tt_is_abs_path("/usr/local/bin"));
    assert(tt_is_abs_path("C:/Users/alice"));
    assert(tt_is_abs_path("D:\\Media\\yt"));
    assert(tt_is_abs_path("C:"));
    assert(tt_is_abs_path("\\\\server\\share\\folder"));
    assert(!tt_is_abs_path(""));
    assert(!tt_is_abs_path("relative/path"));
    assert(!tt_is_abs_path("C:relative"));

    char *j1 = tt_path_join("C:\\Users\\alice", "tuitube.json");
    assert(j1 && strcmp(j1, "C:\\Users\\alice/tuitube.json") == 0);
    free(j1);

    char *j2 = tt_path_join("C:\\", "tuitube.json");
    assert(j2 && strcmp(j2, "C:\\tuitube.json") == 0);
    free(j2);

    char *j3 = tt_path_join("C:\\Users\\alice", "D:\\override\\cfg.json");
    assert(j3 && strcmp(j3, "D:\\override\\cfg.json") == 0);
    free(j3);

    char dir[PATH_MAX];
    assert(tt_mpv_parse_script_opt_config_dir(
        "foo=1,tuitube-config_dir=C:\\Users\\alice\\tuitube,bar=2", dir, sizeof(dir)));
    assert(strcmp(dir, "C:\\Users\\alice\\tuitube") == 0);

    assert(tt_mpv_parse_script_opt_config_dir(
        "tuitube_config_dir=~\\win_cfg", dir, sizeof(dir)));
    assert(strstr(dir, "\\win_cfg") != NULL && tt_is_abs_path(dir));
}

static void write_u16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void write_u32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static void test_ttf_family_name_parser(void) {
    char fam[128];

    /* Invalid / corrupt / truncated inputs must fail safely */
    assert(!tt_ttf_read_family_name(NULL, 100, fam, sizeof(fam)));
    const uint8_t tiny[8] = {0};
    assert(!tt_ttf_read_family_name(tiny, sizeof(tiny), fam, sizeof(fam)));
    const uint8_t bad_magic[16] = {'B', 'A', 'D', '!'};
    assert(!tt_ttf_read_family_name(bad_magic, sizeof(bad_magic), fam, sizeof(fam)));

    /* Minimal SFNT with nameID=1 (Mac Roman) + nameID=16 (Win UTF-16BE English) */
    uint8_t sfnt[256];
    memset(sfnt, 0, sizeof(sfnt));
    write_u32be(sfnt + 0, 0x00010000u);
    write_u16be(sfnt + 4, 1); /* numTables */
    /* Table record at offset 12: 'name' */
    write_u32be(sfnt + 12, 0x6E616D65u);
    write_u32be(sfnt + 20, 28);  /* offset */
    write_u32be(sfnt + 24, 128); /* length */

    /* 'name' header at 28 */
    uint8_t *nt = sfnt + 28;
    write_u16be(nt + 0, 0);
    write_u16be(nt + 2, 2);  /* count */
    write_u16be(nt + 4, 30); /* stringOffset = 6 + 2*12 */

    /* Record 0: Mac Roman nameID=1 ("OldFam") */
    write_u16be(nt + 6 + 0, 1);  /* platformID */
    write_u16be(nt + 6 + 2, 0);  /* encodingID */
    write_u16be(nt + 6 + 4, 0);  /* languageID */
    write_u16be(nt + 6 + 6, 1);  /* nameID */
    write_u16be(nt + 6 + 8, 6);  /* length */
    write_u16be(nt + 6 + 10, 0); /* offset */
    memcpy(nt + 30, "OldFam", 6);

    /* Record 1: Windows Unicode BMP English nameID=16 ("JetBrains") */
    const char *pref = "JetBrains";
    size_t plen = strlen(pref);
    write_u16be(nt + 18 + 0, 3);
    write_u16be(nt + 18 + 2, 1);
    write_u16be(nt + 18 + 4, 0x0409u);
    write_u16be(nt + 18 + 6, 16);
    write_u16be(nt + 18 + 8, (uint16_t)(plen * 2u));
    write_u16be(nt + 18 + 10, 6);
    for (size_t i = 0; i < plen; i++) {
        write_u16be(nt + 30 + 6 + i * 2u, (uint16_t)(unsigned char)pref[i]);
    }

    assert(tt_ttf_read_family_name(sfnt, 28 + 128, fam, sizeof(fam)));
    assert(strcmp(fam, "JetBrains") == 0);

    /* Corrupt name table length past buffer -> must fail safely */
    write_u32be(sfnt + 24, 99999u);
    assert(!tt_ttf_read_family_name(sfnt, 28 + 128, fam, sizeof(fam)));
    write_u32be(sfnt + 24, 128);

    /* TTC ('ttcf') container pointing to an OTTO font with surrogate pair + UTF-8 */
    uint8_t ttc[320];
    memset(ttc, 0, sizeof(ttc));
    write_u32be(ttc + 0, 0x74746366u); /* 'ttcf' */
    write_u32be(ttc + 4, 0x00010000u);
    write_u32be(ttc + 8, 1);  /* numFonts */
    write_u32be(ttc + 12, 32); /* offset to font 0 */

    uint8_t *f0 = ttc + 32;
    write_u32be(f0 + 0, 0x4F54544Fu); /* 'OTTO' */
    write_u16be(f0 + 4, 1);
    write_u32be(f0 + 12, 0x6E616D65u);
    write_u32be(f0 + 20, 32 + 28);
    write_u32be(f0 + 24, 64);

    uint8_t *nt2 = f0 + 28;
    write_u16be(nt2 + 2, 1);
    write_u16be(nt2 + 4, 18);
    write_u16be(nt2 + 6 + 0, 3);
    write_u16be(nt2 + 6 + 2, 10);
    write_u16be(nt2 + 6 + 4, 0x0409u);
    write_u16be(nt2 + 6 + 6, 1);
    write_u16be(nt2 + 6 + 8, 6); /* 3 UTF-16 code units: U+1F3B5 + 'A' */
    write_u16be(nt2 + 6 + 10, 0);
    write_u16be(nt2 + 18 + 0, 0xD83Cu);
    write_u16be(nt2 + 18 + 2, 0xDFB5u);
    write_u16be(nt2 + 18 + 4, 'A');

    assert(tt_ttf_read_family_name(ttc, sizeof(ttc), fam, sizeof(fam)));
    assert(strcmp(fam, "\xF0\x9F\x8E\xB5" "A") == 0);
}

static void test_mpv_overlay_section_close_win_and_bindings(void) {
    TuiTubeConfig cfg;
    config_init_defaults(&cfg);

    StrBuf sb;
    sb_init(&sb);

    /* 1. Default config & CLOSE_WIN fallback */
    assert(config_build_mpv_overlay_section(&cfg, "tuitube", NULL, &sb));
    assert(sb.data != NULL);
    assert(strncmp(sb.data, "CLOSE_WIN quit\n", 15) == 0 ||
           strstr(sb.data, "\nCLOSE_WIN quit\n") != NULL);

    /* Sanitization fallbacks */
    static const char *const invalid_cmds[] = {
        "",
        "   \t  ",
        "ignore",
        "IGNORE",
        "# comment",
        "quit\nset pause yes",
        "quit\r\nset pause yes",
        "noop",
        "script-binding tuitube/noop"
    };
    for (size_t i = 0; i < sizeof(invalid_cmds) / sizeof(invalid_cmds[0]); i++) {
        assert(config_build_mpv_overlay_section(&cfg, "tuitube", invalid_cmds[i], &sb));
        assert(strncmp(sb.data, "CLOSE_WIN quit\n", 15) == 0 ||
               strstr(sb.data, "\nCLOSE_WIN quit\n") != NULL);
    }

    /* CLOSE_WIN appears before UNMAPPED, and UNMAPPED is the final line */
    const char *close_pos = strstr(sb.data, "CLOSE_WIN quit\n");
    const char *unmapped_line = "UNMAPPED repeatable script-binding tuitube/noop\n";
    const char *unmapped_pos = strstr(sb.data, unmapped_line);
    assert(close_pos != NULL && unmapped_pos != NULL);
    assert(close_pos < unmapped_pos);
    assert(unmapped_pos + strlen(unmapped_line) == sb.data + sb.len);

    /* No WM-specific shortcut hardcoded under default config */
    assert(strstr(sb.data, "Meta+q") == NULL);
    assert(strstr(sb.data, "Meta+Q") == NULL);
    assert(strstr(sb.data, "Super+q") == NULL);
    assert(strstr(sb.data, "Super+Q") == NULL);

    /* Core overlay bindings remain present */
    assert(strstr(sb.data, "F12 script-binding tuitube/toggle\n") != NULL);
    assert(strstr(sb.data, "Ctrl+q script-binding tuitube/input\n") != NULL);
    assert(strstr(sb.data, "ANY_UNICODE repeatable script-binding tuitube/input\n") != NULL);

    /* 2. Custom host CLOSE_WIN command preservation */
    assert(config_build_mpv_overlay_section(&cfg, "tuitube", "  quit-watch-later  ", &sb));
    assert(strstr(sb.data, "CLOSE_WIN quit-watch-later\n") != NULL);

    assert(config_build_mpv_overlay_section(&cfg, "tuitube", "quit 4", &sb));
    assert(strstr(sb.data, "CLOSE_WIN quit 4\n") != NULL);

    /* 3. config_mpv_key_name() & custom tuitube.json modifier keybindings */
    char kname[16];
    assert(config_mpv_key_name(XKB_KEY_Q, kname, sizeof(kname)) && strcmp(kname, "q") == 0);
    assert(config_mpv_key_name(XKB_KEY_F4, kname, sizeof(kname)) && strcmp(kname, "F4") == 0);
    assert(config_mpv_key_name(XKB_KEY_Return, kname, sizeof(kname)) && strcmp(kname, "ENTER") == 0);
    assert(config_mpv_key_name(XKB_KEY_BackSpace, kname, sizeof(kname)) && strcmp(kname, "BS") == 0);
    assert(config_mpv_key_name(XKB_KEY_bracketright, kname, sizeof(kname)) && strcmp(kname, "]") == 0);
    assert(!config_mpv_key_name(XKB_KEY_NoSymbol, kname, sizeof(kname)));

    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_ovsec", tmpdir, sizeof(tmpdir));
    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);
    const char *json_cfg =
        "{ \"key_bindings\": [ { \"action\": \"show_feed\", \"mod\": \"SUPER\", \"key\": \"R\" } ] }\n";
    assert(tt_write_file_atomic(cfg_file, json_cfg, strlen(json_cfg)));

    TuiTubeConfig cfg2;
    assert(config_load(&cfg2, tmpdir));
    assert(config_build_mpv_overlay_section(&cfg2, "custom_client", NULL, &sb));
    assert(strstr(sb.data, "Meta+r script-binding custom_client/input\n") != NULL);
    assert(strstr(sb.data, "CLOSE_WIN quit\n") != NULL);

    config_free(&cfg2);
    test_unlink_file(cfg_file);
    test_rmdir(tmpdir);
    sb_free(&sb);
    config_free(&cfg);
}

static void test_player_launch_and_exe_resolution(void) {
    TuiTubeConfig cfg;
    config_init_defaults(&cfg);

    MediaItem vid = {0};
    vid.kind = ITEM_VIDEO;
    snprintf(vid.id, sizeof(vid.id), "dQw4w9WgXcQ");
    vid.title = tt_strdup("Launch Test");

    char err[256] = {0};

    /* Nonexistent player must fail with a clear diagnostic */
    free(cfg.player_path);
    cfg.player_path = tt_strdup("tuitube_definitely_missing_player_99999");
    assert(!player_launch(&cfg, &vid, err, sizeof(err)));
    assert(strstr(err, "Failed to launch player:") != NULL);

    /* Real executable must spawn cleanly (catches invalid CreateProcessW flags on Windows) */
    free(cfg.player_path);
#ifdef _WIN32
    char *cmd_exe = tt_win32_resolve_exe("cmd");
    assert(cmd_exe != NULL && tt_file_exists(cmd_exe));
    free(cmd_exe);
    cfg.player_path = tt_strdup("where");
#else
    cfg.player_path = tt_strdup("true");
#endif
    err[0] = '\0';
    assert(player_launch(&cfg, &vid, err, sizeof(err)));

    media_item_free_fields(&vid);
    config_free(&cfg);
}

static void test_locale_parsing_and_innertube_headers(void) {
    char hl[16], gl[8];

    assert(tt_parse_locale_tag("vi_VN.UTF-8", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "vi") == 0 && strcmp(gl, "VN") == 0);

    assert(tt_parse_locale_tag("vi-VN", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "vi") == 0 && strcmp(gl, "VN") == 0);

    assert(tt_parse_locale_tag("vi", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "vi") == 0 && gl[0] == '\0');

    assert(tt_parse_locale_tag("en_US.utf8@modern", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "en") == 0 && strcmp(gl, "US") == 0);

    assert(tt_parse_locale_tag("en-GB", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "en-GB") == 0 && strcmp(gl, "GB") == 0);

    assert(tt_parse_locale_tag("zh_TW.UTF-8", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "zh-TW") == 0 && strcmp(gl, "TW") == 0);

    assert(tt_parse_locale_tag("zh-Hant-HK", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "zh-HK") == 0 && strcmp(gl, "HK") == 0);

    assert(tt_parse_locale_tag("zh-Hans-CN", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "zh-CN") == 0 && strcmp(gl, "CN") == 0);

    assert(tt_parse_locale_tag("pt_BR", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "pt-BR") == 0 && strcmp(gl, "BR") == 0);

    assert(tt_parse_locale_tag("es-419", hl, sizeof(hl), gl, sizeof(gl)));
    assert(strcmp(hl, "es-419") == 0 && gl[0] == '\0');

    static const char *const invalid_locales[] = {
        "", "C", "C.UTF-8", "POSIX", "auto",
        "vi\r\nX-Injected: 1", "en;q=0.9", "123", "toolongsubtagvalue"
    };
    for (size_t i = 0; i < sizeof(invalid_locales) / sizeof(invalid_locales[0]); i++) {
        assert(!tt_parse_locale_tag(invalid_locales[i], hl, sizeof(hl), gl, sizeof(gl)));
    }

    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_loc", tmpdir, sizeof(tmpdir));
    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);

    const char *vi_cfg = "{ \"language\": \"vi\" }\n";
    assert(tt_write_file_atomic(cfg_file, vi_cfg, strlen(vi_cfg)));
    TuiTubeConfig cfg;
    assert(config_load(&cfg, tmpdir));
    assert(strcmp(cfg.resolved_hl, "vi") == 0);
    assert(strcmp(cfg.resolved_gl, "VN") == 0);
    config_free(&cfg);

    const char *vi_us_cfg = "{ \"language\": \"vi\", \"region\": \"us\" }\n";
    assert(tt_write_file_atomic(cfg_file, vi_us_cfg, strlen(vi_us_cfg)));
    assert(config_load(&cfg, tmpdir));
    assert(strcmp(cfg.resolved_hl, "vi") == 0);
    assert(strcmp(cfg.resolved_gl, "US") == 0);
    config_free(&cfg);

    /* Load examples/tuitube.json via temp copy */
    size_t ex_len = 0;
    char *ex_raw = tt_read_file("examples/tuitube.json", &ex_len);
    assert(ex_raw != NULL && ex_len > 0);
    assert(tt_write_file_atomic(cfg_file, ex_raw, ex_len));
    free(ex_raw);
    assert(config_load(&cfg, tmpdir));
    assert(!cfg.has_video_format);
    assert(!cfg.has_sub_language);
    assert(!cfg.has_ytdlp_path);
    assert(cfg.ytdlp_path && cfg.ytdlp_path[0] == '\0');
    assert(cfg.language && strcmp(cfg.language, "auto") == 0);
    config_free(&cfg);

    test_unlink_file(cfg_file);
    test_rmdir(tmpdir);

    innertube_set_locale("vi", "VN");
    innertube_get_locale(hl, sizeof(hl), gl, sizeof(gl));
    assert(strcmp(hl, "vi") == 0 && strcmp(gl, "VN") == 0);

    char hdr[64];
    assert(innertube_format_accept_language("vi", hdr, sizeof(hdr)));
    assert(strcmp(hdr, "Accept-Language: vi,en;q=0.8") == 0);
    assert(innertube_format_accept_language("en", hdr, sizeof(hdr)));
    assert(strcmp(hdr, "Accept-Language: en-US,en;q=0.9") == 0);

    innertube_set_locale("en", "US");
}

static void test_original_titles(void) {
    /* (a) Config parsing: true / false / omitted / non-bool */
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_orig_cfg", tmpdir, sizeof(tmpdir));
    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);

    const char *cfg_true = "{ \"original_titles\": true }\n";
    assert(tt_write_file_atomic(cfg_file, cfg_true, strlen(cfg_true)));
    TuiTubeConfig cfg;
    assert(config_load(&cfg, tmpdir));
    assert(cfg.original_titles == true);
    config_free(&cfg);

    const char *cfg_false = "{ \"original_titles\": false }\n";
    assert(tt_write_file_atomic(cfg_file, cfg_false, strlen(cfg_false)));
    assert(config_load(&cfg, tmpdir));
    assert(cfg.original_titles == false);
    config_free(&cfg);

    const char *cfg_omitted = "{ \"language\": \"vi\" }\n";
    assert(tt_write_file_atomic(cfg_file, cfg_omitted, strlen(cfg_omitted)));
    assert(config_load(&cfg, tmpdir));
    assert(cfg.original_titles == true);
    config_free(&cfg);

    const char *cfg_nonbool = "{ \"original_titles\": \"yes\" }\n";
    assert(tt_write_file_atomic(cfg_file, cfg_nonbool, strlen(cfg_nonbool)));
    assert(config_load(&cfg, tmpdir));
    assert(cfg.original_titles == true);
    config_free(&cfg);

    assert(tt_write_file_atomic(cfg_file, "{}\n", 3));
    assert(config_load(&cfg, tmpdir));
    assert(cfg.original_titles == true);
    config_free(&cfg);

    test_unlink_file(cfg_file);
    test_rmdir(tmpdir);

    /* (b) display helper prefers original_title, copies/frees it */
    MediaItem src = {0};
    src.kind = ITEM_VIDEO;
    snprintf(src.id, sizeof(src.id), "abc123XYZ");
    src.title = tt_strdup("Vietnamese title");
    src.original_title = tt_strdup("Original English Title");

    assert(strcmp(media_item_display_title(&src), "Original English Title") == 0);

    MediaItem copy;
    assert(media_item_copy(&copy, &src));
    assert(copy.original_title && strcmp(copy.original_title, "Original English Title") == 0);
    assert(strcmp(media_item_display_title(&copy), "Original English Title") == 0);
    media_item_free_fields(&copy);
    assert(copy.title == NULL && copy.original_title == NULL);

    /* Empty original_title falls back to the localized title */
    MediaItem empty_orig = {0};
    empty_orig.title = tt_strdup("Localized");
    empty_orig.original_title = tt_strdup("");
    assert(strcmp(media_item_display_title(&empty_orig), "Localized") == 0);
    media_item_free_fields(&empty_orig);

    /* NULL original_title falls back too, and NULL item is safe */
    MediaItem no_orig = {0};
    no_orig.title = tt_strdup("Only Title");
    assert(strcmp(media_item_display_title(&no_orig), "Only Title") == 0);
    media_item_free_fields(&no_orig);
    assert(media_item_display_title(NULL) == NULL);
    media_item_free_fields(&src);

    /* (c) oEmbed JSON title extraction */
    char out[128];
    const char *valid =
        "{\"title\":\"I Survived 100 Days in a Cave\","
        "\"author_name\":\"MrBeast\",\"type\":\"video\"}";
    assert(innertube_parse_oembed_json(valid, strlen(valid), out, sizeof(out)));
    assert(strcmp(out, "I Survived 100 Days in a Cave") == 0);

    const char *empty_obj = "{}";
    out[0] = 'x';
    assert(!innertube_parse_oembed_json(empty_obj, strlen(empty_obj), out, sizeof(out)));
    assert(out[0] == '\0');

    const char *empty_title = "{\"title\":\"\"}";
    assert(!innertube_parse_oembed_json(empty_title, strlen(empty_title), out, sizeof(out)));
    assert(out[0] == '\0');

    const char *malformed = "{\"title\": ";
    assert(!innertube_parse_oembed_json(malformed, strlen(malformed), out, sizeof(out)));
    assert(out[0] == '\0');

    const char *non_string_title = "{\"title\": 42}";
    assert(!innertube_parse_oembed_json(non_string_title, strlen(non_string_title), out, sizeof(out)));

    /* (d) player-endpoint fallback JSON title extraction */
    const char *player_valid =
        "{\"videoDetails\":{\"videoId\":\"abc123XYZ\","
        "\"title\":\"Uploader Original Title\"},\"playabilityStatus\":{\"status\":\"OK\"}}";
    assert(innertube_parse_player_title_json(player_valid, strlen(player_valid), out, sizeof(out)));
    assert(strcmp(out, "Uploader Original Title") == 0);

    const char *player_no_details = "{\"playabilityStatus\":{\"status\":\"OK\"}}";
    out[0] = 'x';
    assert(!innertube_parse_player_title_json(player_no_details, strlen(player_no_details),
                                              out, sizeof(out)));
    assert(out[0] == '\0');

    const char *player_no_title = "{\"videoDetails\":{\"videoId\":\"abc123XYZ\"}}";
    assert(!innertube_parse_player_title_json(player_no_title, strlen(player_no_title),
                                              out, sizeof(out)));
    assert(out[0] == '\0');

    const char *player_empty_title = "{\"videoDetails\":{\"title\":\"\"}}";
    assert(!innertube_parse_player_title_json(player_empty_title, strlen(player_empty_title),
                                              out, sizeof(out)));
    assert(out[0] == '\0');

    const char *player_non_string = "{\"videoDetails\":{\"title\":42}}";
    assert(!innertube_parse_player_title_json(player_non_string, strlen(player_non_string),
                                              out, sizeof(out)));
    assert(out[0] == '\0');

    const char *player_malformed = "{\"videoDetails\":{\"title\": ";
    assert(!innertube_parse_player_title_json(player_malformed, strlen(player_malformed),
                                              out, sizeof(out)));
    assert(out[0] == '\0');

    const char *oembed_missing_title = "{\"author_name\":\"Nobody\"}";
    assert(!innertube_parse_oembed_json(oembed_missing_title, strlen(oembed_missing_title),
                                        out, sizeof(out)));

    /* oEmbed failure triggers the player fallback; oEmbed success wins. */
    assert(innertube_parse_original_title_fallback(NULL, 0, false,
                                                   player_valid, strlen(player_valid), true,
                                                   out, sizeof(out)));
    assert(strcmp(out, "Uploader Original Title") == 0);
    assert(innertube_parse_original_title_fallback(valid, strlen(valid), true,
                                                   player_valid, strlen(player_valid), true,
                                                   out, sizeof(out)));
    assert(strcmp(out, "I Survived 100 Days in a Cave") == 0);
    out[0] = 'x';
    assert(!innertube_parse_original_title_fallback(NULL, 0, false,
                                                    player_malformed, strlen(player_malformed), true,
                                                    out, sizeof(out)));
    assert(out[0] == '\0');
    assert(innertube_parse_original_title_fallback(oembed_missing_title,
                                                   strlen(oembed_missing_title), true,
                                                   player_valid, strlen(player_valid), true,
                                                   out, sizeof(out)));
    assert(strcmp(out, "Uploader Original Title") == 0);

    /* (e) Cache reset is callable and idempotent */
    innertube_original_title_cache_reset();
    innertube_original_title_cache_reset();
    innertube_original_title_cache_reset();
}

static void test_original_title_detail_merge(void) {
    MediaItem selected = {0};
    selected.kind = ITEM_VIDEO;
    snprintf(selected.id, sizeof(selected.id), "mergeVid1");
    selected.title = tt_strdup("Localized listed title");
    selected.original_title = tt_strdup("Resolved original title");

    MediaItem detail_item;
    assert(media_item_copy(&detail_item, &selected));

    MediaItem localized_detail = {0};
    localized_detail.kind = ITEM_VIDEO;
    snprintf(localized_detail.id, sizeof(localized_detail.id), "mergeVid1");
    localized_detail.title = tt_strdup("Localized detail title");
    localized_detail.author = tt_strdup("Channel");
    localized_detail.description = tt_strdup("Description");
    localized_detail.details_loaded = true;

    assert(media_item_apply_detail_merge(&selected, &detail_item, true, &localized_detail));
    assert(strcmp(selected.title, "Localized detail title") == 0);
    assert(selected.original_title && strcmp(selected.original_title, "Resolved original title") == 0);
    assert(detail_item.original_title &&
           strcmp(detail_item.original_title, "Resolved original title") == 0);
    assert(strcmp(media_item_display_title(&detail_item), "Resolved original title") == 0);

    MediaItem fresh_original = {0};
    fresh_original.kind = ITEM_VIDEO;
    snprintf(fresh_original.id, sizeof(fresh_original.id), "mergeVid1");
    fresh_original.title = tt_strdup("Localized detail title");
    fresh_original.original_title = tt_strdup("New original from detail");
    assert(media_item_apply_detail_merge(&selected, &detail_item, true, &fresh_original));
    assert(strcmp(media_item_display_title(&selected), "New original from detail") == 0);
    assert(strcmp(media_item_display_title(&detail_item), "New original from detail") == 0);

    /* Detail refresh without an original must not discard the resolved one. */
    MediaItem localized_refresh = {0};
    localized_refresh.kind = ITEM_VIDEO;
    snprintf(localized_refresh.id, sizeof(localized_refresh.id), "mergeVid1");
    localized_refresh.title = tt_strdup("Localized again");
    assert(media_item_apply_detail_merge(&selected, &detail_item, true, &localized_refresh));
    assert(strcmp(media_item_display_title(&selected), "New original from detail") == 0);
    assert(strcmp(media_item_display_title(&detail_item), "New original from detail") == 0);

    /* A resolved original already in the detail pane must reach the row. */
    MediaItem pane_only = {0};
    pane_only.kind = ITEM_VIDEO;
    snprintf(pane_only.id, sizeof(pane_only.id), "mergeVid2");
    pane_only.title = tt_strdup("Pane localized");
    pane_only.original_title = tt_strdup("Pane resolved original");

    MediaItem row_without_original = {0};
    row_without_original.kind = ITEM_VIDEO;
    snprintf(row_without_original.id, sizeof(row_without_original.id), "mergeVid2");
    row_without_original.title = tt_strdup("Row localized");

    MediaItem detail_without_original = {0};
    detail_without_original.kind = ITEM_VIDEO;
    snprintf(detail_without_original.id, sizeof(detail_without_original.id), "mergeVid2");
    detail_without_original.title = tt_strdup("Detail localized");
    assert(media_item_apply_detail_merge(&row_without_original, &pane_only, true,
                                         &detail_without_original));
    assert(strcmp(media_item_display_title(&row_without_original),
                  "Pane resolved original") == 0);
    assert(strcmp(media_item_display_title(&pane_only), "Pane resolved original") == 0);

    media_item_free_fields(&selected);
    media_item_free_fields(&detail_item);
    media_item_free_fields(&localized_detail);
    media_item_free_fields(&fresh_original);
    media_item_free_fields(&localized_refresh);
    media_item_free_fields(&pane_only);
    media_item_free_fields(&row_without_original);
    media_item_free_fields(&detail_without_original);
}

static void test_original_description_and_url_links(void) {
    char title[128];
    char *desc = NULL;
    const char *player =
        "{\"videoDetails\":{\"title\":\"Vinh Xo goc\","
        "\"shortDescription\":\"Dong 1\\nXem https://youtube.com/@VinhXo (nguon).\"}}";
    assert(innertube_parse_player_metadata_json(player, strlen(player), title, sizeof(title), &desc));
    assert(strcmp(title, "Vinh Xo goc") == 0);
    assert(desc && strcmp(desc, "Dong 1\nXem https://youtube.com/@VinhXo (nguon).") == 0);
    free(desc);
    desc = NULL;

    const char *desc_only =
        "{\"videoDetails\":{\"title\":\"\",\"shortDescription\":\"Only description\"}}";
    assert(innertube_parse_player_metadata_json(desc_only, strlen(desc_only), title,
                                                sizeof(title), &desc));
    assert(title[0] == '\0');
    assert(desc && strcmp(desc, "Only description") == 0);
    free(desc);
    desc = NULL;

    const char *no_details = "{\"playabilityStatus\":{\"status\":\"OK\"}}";
    assert(!innertube_parse_player_metadata_json(no_details, strlen(no_details), title,
                                                 sizeof(title), &desc));
    assert(desc == NULL);
    const char *empty_meta = "{\"videoDetails\":{\"title\":\"\",\"shortDescription\":\"\"}}";
    assert(!innertube_parse_player_metadata_json(empty_meta, strlen(empty_meta), title,
                                                 sizeof(title), &desc));
    assert(desc == NULL);
    const char *malformed = "{\"videoDetails\":";
    assert(!innertube_parse_player_metadata_json(malformed, strlen(malformed), title,
                                                 sizeof(title), &desc));
    assert(desc == NULL);

    MediaItem item = {0};
    item.kind = ITEM_VIDEO;
    snprintf(item.id, sizeof(item.id), "descVid01");
    item.title = tt_strdup("Localized title");
    item.description = tt_strdup("Localized description");
    item.original_description = tt_strdup("Original description");
    assert(strcmp(media_item_display_description(&item), "Original description") == 0);

    MediaItem copy;
    assert(media_item_copy(&copy, &item));
    assert(copy.original_description &&
           strcmp(copy.original_description, "Original description") == 0);
    assert(strcmp(media_item_display_description(&copy), "Original description") == 0);
    media_item_free_fields(&copy);
    assert(copy.original_description == NULL);

    MediaItem localized = {0};
    localized.kind = ITEM_VIDEO;
    snprintf(localized.id, sizeof(localized.id), "descVid01");
    localized.description = tt_strdup("Localized refresh");
    MediaItem pane;
    assert(media_item_copy(&pane, &item));
    assert(media_item_apply_detail_merge(&item, &pane, true, &localized));
    assert(strcmp(media_item_display_description(&item), "Original description") == 0);
    assert(strcmp(media_item_display_description(&pane), "Original description") == 0);

    item.original_description[0] = '\0';
    assert(strcmp(media_item_display_description(&item), "Localized refresh") == 0);
    media_item_free_fields(&item);
    media_item_free_fields(&pane);
    media_item_free_fields(&localized);

    const char *text =
        "Read https://Example.com/a_(b), www.youtube.com/@chan and youtu.be/x. "
        "Wiki en.wikipedia.org/wiki/Test. Not ftp://example.com or a@b.com or https://x.com/...";
    TtTextSpan spans[8];
    size_t n = tt_find_url_spans(text, spans, 8);
    assert(n == 4);
    char url[256];
    assert(tt_url_from_span(text, spans[0], url, sizeof(url)));
    assert(strcmp(url, "https://Example.com/a_(b)") == 0);
    assert(tt_url_from_span(text, spans[1], url, sizeof(url)));
    assert(strcmp(url, "https://www.youtube.com/@chan") == 0);
    assert(tt_url_from_span(text, spans[2], url, sizeof(url)));
    assert(strcmp(url, "https://youtu.be/x") == 0);
    assert(tt_url_from_span(text, spans[3], url, sizeof(url)));
    assert(strcmp(url, "https://en.wikipedia.org/wiki/Test") == 0);

    const char *shortener = "Mirror: bit.ly/abc123 and t.co/x";
    n = tt_find_url_spans(shortener, spans, 8);
    assert(n == 2);
    assert(tt_url_from_span(shortener, spans[0], url, sizeof(url)));
    assert(strcmp(url, "https://bit.ly/abc123") == 0);
    assert(tt_url_from_span(shortener, spans[1], url, sizeof(url)));
    assert(strcmp(url, "https://t.co/x") == 0);

    TtTextSpan none[4];
    assert(tt_find_url_spans("mail me at a@b.com or ftp://x", none, 4) == 0);
    const char *caps = "HTTP://Example.COM/path";
    assert(tt_find_url_spans(caps, spans, 8) == 1);
    assert(tt_url_from_span(caps, spans[0], url, sizeof(url)));
    assert(strcmp(url, "HTTP://Example.COM/path") == 0);

    char err[64] = {0};
    assert(!tt_open_url("ftp://example.com", err, sizeof(err)));
    assert(err[0] != '\0');
    assert(!tt_open_url("javascript:alert(1)", err, sizeof(err)));
    assert(!tt_open_url("https://exa mple.com", err, sizeof(err)));
    assert(!tt_open_url("", err, sizeof(err)));
}

static void test_url_link_rect_geometry(void) {
    const char *text =
        "Line one with a link https://example.com/path here and more wrapping words to force "
        "a second visual line with www.youtube.com/@chan too.";
    TtTextSpan spans[8];
    size_t span_count = tt_find_url_spans(text, spans, 8);
    assert(span_count == 2);

    /* Use a private font map/context so Pango's global default map (and its
       fontconfig caches) is never created inside the sanitizer run. */
    PangoFontMap *fm = pango_cairo_font_map_new();
    assert(fm != NULL);
    PangoContext *ctx = pango_font_map_create_context(fm);
    assert(ctx != NULL);
    PangoLayout *layout = pango_layout_new(ctx);
    assert(layout != NULL);

    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, "Sans");
    pango_font_description_set_absolute_size(fd, 13.0 * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, (int)(420 * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

    TtLinkRect rects[16];
    size_t rect_count = tt_layout_link_rects(layout, spans, span_count, rects, 16);
    assert(rect_count > 0);

    /* Every rectangle must be a plausible, positive-size hit target. */
    for (size_t i = 0; i < rect_count; i++) {
        assert(rects[i].link < span_count);
        assert(rects[i].rect.w > 0.5);
        assert(rects[i].rect.h > 0.5);
    }

    /* Both links must produce at least one rectangle. */
    bool seen[2] = {false, false};
    for (size_t i = 0; i < rect_count; i++) seen[rects[i].link] = true;
    assert(seen[0] && seen[1]);

    /*
     * Regression guard against passing line-relative byte indexes to Pango.
     * That bug is invisible on the first visual line (line_start == 0), so the
     * assertion must target a link that begins on a later line and wraps.
     * Cross-check every rectangle against Pango's own index-to-position
     * oracle for the first byte of its link.
     */
    for (size_t i = 0; i < rect_count; i++) {
        const TtTextSpan *span = &spans[rects[i].link];
        PangoRectangle pos;
        pango_layout_index_to_pos(layout, (int)span->start, &pos);
        double want_x = (double)pos.x / PANGO_SCALE;
        double want_y = (double)pos.y / PANGO_SCALE;
        /* Only the first rectangle of each link starts at the link's first byte. */
        bool first_of_link = true;
        for (size_t j = 0; j < i; j++) {
            if (rects[j].link == rects[i].link) first_of_link = false;
        }
        if (first_of_link) {
            assert(fabs(rects[i].rect.x - want_x) < 2.0);
            assert(fabs(rects[i].rect.y - want_y) < 2.0);
        }
    }

    /* The second link begins on a later line; a line-relative index bug moves
       it away from the oracle position measured above. */
    size_t link_one_rects = 0;
    for (size_t i = 0; i < rect_count; i++) {
        if (rects[i].link == 1) link_one_rects++;
    }
    assert(link_one_rects >= 2);

    g_object_unref(layout);
    g_object_unref(ctx);
    g_object_unref(fm);
}

static void test_history_original_title_roundtrip(void) {
    HistoryList hl;
    history_init(&hl);

    MediaItem v = {0};
    v.kind = ITEM_VIDEO;
    snprintf(v.id, sizeof(v.id), "histOrig01");
    v.title = tt_strdup("Localized history title");
    v.original_title = tt_strdup("Original history title");
    v.author = tt_strdup("History Channel");
    assert(history_record(&hl, &v, 1234));

    char hist_path[PATH_MAX];
    test_make_temp_file("tuitube_test_hist_orig.json", hist_path, sizeof(hist_path));
    assert(history_save(&hl, hist_path));
    history_free(&hl);

    history_init(&hl);
    assert(history_load(&hl, hist_path));
    assert(hl.count == 1);
    assert(strcmp(hl.items[0].title, "Localized history title") == 0);
    assert(hl.items[0].original_title &&
           strcmp(hl.items[0].original_title, "Original history title") == 0);
    history_free(&hl);
    test_unlink_file(hist_path);

    /* Backward compatibility: files without original_title still load. */
    const char *legacy =
        "[\n"
        "  {\"video_id\":\"legacy01\",\"title\":\"Legacy title\",\"author\":\"Old\"}\n"
        "]\n";
    assert(tt_write_file_atomic(hist_path, legacy, strlen(legacy)));
    history_init(&hl);
    assert(history_load(&hl, hist_path));
    assert(hl.count == 1 && strcmp(hl.items[0].title, "Legacy title") == 0);
    assert(hl.items[0].original_title && hl.items[0].original_title[0] == '\0');
    history_free(&hl);

    /* A later play without a resolved original keeps the persisted one. */
    history_free(&hl);
    history_init(&hl);
    MediaItem with_orig = {0};
    with_orig.kind = ITEM_VIDEO;
    snprintf(with_orig.id, sizeof(with_orig.id), "keepOrig01");
    with_orig.title = tt_strdup("Keep localized");
    with_orig.original_title = tt_strdup("Keep original");
    assert(history_record(&hl, &with_orig, 1000));
    assert(history_save(&hl, hist_path));
    history_free(&hl);

    history_init(&hl);
    assert(history_load(&hl, hist_path));
    MediaItem localized_only = {0};
    localized_only.kind = ITEM_VIDEO;
    snprintf(localized_only.id, sizeof(localized_only.id), "keepOrig01");
    localized_only.title = tt_strdup("Keep localized again");
    assert(history_record(&hl, &localized_only, 2000));
    assert(hl.items[0].original_title &&
           strcmp(hl.items[0].original_title, "Keep original") == 0);
    assert(strcmp(hl.items[0].title, "Keep localized again") == 0);
    history_free(&hl);
    test_unlink_file(hist_path);
    media_item_free_fields(&v);
    media_item_free_fields(&with_orig);
    media_item_free_fields(&localized_only);
}

static void test_player_original_title_selection(void) {
    MediaItem video = {0};
    video.kind = ITEM_VIDEO;
    snprintf(video.id, sizeof(video.id), "titleVid01");
    video.title = tt_strdup("Localized player title");

    assert(strcmp(player_video_title(&video), "Localized player title") == 0);

    video.original_title = tt_strdup("Original player title");
    assert(strcmp(player_video_title(&video), "Original player title") == 0);

    char *mpv_arg = NULL;
    char *vlc_arg = NULL;
    assert(player_build_title_args(&video, &mpv_arg, &vlc_arg));
    assert(strcmp(mpv_arg, "--force-media-title=Original player title") == 0);
    assert(strcmp(vlc_arg, "--meta-title=Original player title") == 0);
    free(mpv_arg);
    free(vlc_arg);

    /* Missing titles fall back to the video id. */
    media_item_free_fields(&video);
    video.kind = ITEM_VIDEO;
    snprintf(video.id, sizeof(video.id), "titleVid01");
    assert(strcmp(player_video_title(&video), "titleVid01") == 0);
    assert(player_build_title_args(&video, &mpv_arg, &vlc_arg));
    assert(strcmp(mpv_arg, "--force-media-title=titleVid01") == 0);
    assert(strcmp(vlc_arg, "--meta-title=titleVid01") == 0);
    free(mpv_arg);
    free(vlc_arg);
    assert(player_video_title(NULL) == NULL);
    media_item_free_fields(&video);
}

static void test_non_english_lockup_and_badges(void) {
    const char *vi_browse =
        "{"
        "  \"header\": {\"pageHeaderRenderer\": {\"content\": {\"pageHeaderViewModel\": {\"title\": {\"dynamicTextViewModel\": {\"text\": {\"content\": \"Kenh Viet\"}}}}}}},"
        "  \"contents\": {\"twoColumnBrowseResultsRenderer\": {\"tabs\": [{\"tabRenderer\": {\"selected\": true, \"content\": {"
        "    \"richGridRenderer\": {\"contents\": ["
        "      {\"richItemRenderer\": {\"content\": {\"lockupViewModel\": {"
        "        \"contentId\": \"viMem01\","
        "        \"contentType\": \"LOCKUP_CONTENT_TYPE_VIDEO\","
        "        \"metadata\": {\"lockupMetadataViewModel\": {"
        "          \"title\": {\"content\": \"Video Hoi Vien\"},"
        "          \"badges\": [{\"badgeViewModel\": {\"badgeText\": \"D\\u00e0nh ri\\u00eang cho h\\u1ed9i vi\\u00ean\"}}],"
        "          \"metadata\": {\"contentMetadataViewModel\": {\"metadataRows\": ["
        "            {\"metadataParts\": ["
        "              {\"text\": {\"content\": \"12 N l\\u01b0\\u1ee3t xem\"}},"
        "              {\"text\": {\"content\": \"3 ng\\u00e0y tr\\u01b0\\u1edbc\"}}"
        "            ]}"
        "          ]}}"
        "        }}"
        "      }}}},"
        "      {\"richItemRenderer\": {\"content\": {\"lockupViewModel\": {"
        "        \"contentId\": \"viLive02\","
        "        \"contentType\": \"LOCKUP_CONTENT_TYPE_VIDEO\","
        "        \"metadata\": {\"lockupMetadataViewModel\": {"
        "          \"title\": {\"content\": \"Phat Truc Tiep\"},"
        "          \"badges\": [{\"badgeViewModel\": {\"badgeText\": \"TR\\u1ef0C TI\\u1ebeP\"}}],"
        "          \"metadata\": {\"contentMetadataViewModel\": {\"metadataRows\": ["
        "            {\"metadataParts\": ["
        "              {\"text\": {\"content\": \"1,5 N ng\\u01b0\\u1eddi \\u0111ang xem\"}}"
        "            ]}"
        "          ]}}"
        "        }}"
        "      }}}}"
        "    ]}"
        "  }}}]}}"
        "}";

    MediaList ml;
    media_list_init(&ml);
    char err[128] = {0};
    assert(innertube_parse_browse_json(vi_browse, strlen(vi_browse), &ml, err, sizeof(err)));
    assert(ml.count == 2);

    assert(strcmp(ml.items[0].id, "viMem01") == 0);
    assert(ml.items[0].author == NULL);
    assert(ml.items[0].view_count_text &&
           strcmp(ml.items[0].view_count_text, "12 N l\xC6\xB0\xE1\xBB\xA3t xem") == 0);
    assert(ml.items[0].published_text &&
           strcmp(ml.items[0].published_text, "3 ng\xC3\xA0y tr\xC6\xB0\xE1\xBB\x9B" "c") == 0);
    assert(ml.items[0].is_members_only == true);

    assert(strcmp(ml.items[1].id, "viLive02") == 0);
    assert(ml.items[1].author == NULL);
    assert(ml.items[1].is_live == true);

    media_list_free(&ml);
}

static void write_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void write_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static void test_pe_subsystem_and_runtime_dep_checks(void) {
    uint8_t pe[160];
    memset(pe, 0, sizeof(pe));
    pe[0] = 'M';
    pe[1] = 'Z';
    write_u32le(pe + 0x3C, 64u);
    pe[64] = 'P';
    pe[65] = 'E';
    pe[66] = 0;
    pe[67] = 0;

    /* PE32+ GUI (Subsystem = 2) -> true; CUI (Subsystem = 3) -> false */
    write_u16le(pe + 64u + 24u, 0x020Bu);
    write_u16le(pe + 64u + 24u + 68u, 2u);
    assert(tt_pe_is_gui_subsystem(pe, sizeof(pe)));

    write_u16le(pe + 64u + 24u + 68u, 3u);
    assert(!tt_pe_is_gui_subsystem(pe, sizeof(pe)));

    /* PE32 GUI -> true */
    write_u16le(pe + 64u + 24u, 0x010Bu);
    write_u16le(pe + 64u + 24u + 68u, 2u);
    assert(tt_pe_is_gui_subsystem(pe, sizeof(pe)));

    /* Truncated / corrupt headers -> false */
    assert(!tt_pe_is_gui_subsystem(pe, 150u));
    assert(!tt_pe_is_gui_subsystem(NULL, sizeof(pe)));
    pe[64] = 'X';
    assert(!tt_pe_is_gui_subsystem(pe, sizeof(pe)));
    pe[64] = 'P';
    pe[0] = 'X';
    assert(!tt_pe_is_gui_subsystem(pe, sizeof(pe)));

    /* Runtime dependency checks */
    TuiTubeConfig cfg;
    config_init_defaults(&cfg);

    bool has_player = true;
    bool has_ytdlp = false;
    free(cfg.player_path);
    cfg.player_path = tt_strdup("tuitube_definitely_missing_player_99999");
    player_check_deps(&cfg, &has_player, &has_ytdlp);
    assert(!has_player);

    free(cfg.player_path);
#ifdef _WIN32
    tt_win32_refresh_path();
    char *cmd_exe = tt_win32_resolve_exe("cmd");
    assert(cmd_exe != NULL && tt_file_exists(cmd_exe));
    free(cmd_exe);
    cfg.player_path = tt_strdup("where");
#else
    cfg.player_path = tt_strdup("true");
#endif
    has_player = false;
    player_check_deps(&cfg, &has_player, NULL);
    assert(has_player);

    config_free(&cfg);
}

static void test_ytdlp_path_resolution_and_mpv_opts(void) {
    char tmpdir[PATH_MAX];
    test_make_temp_dir("tuitube_test_ytdlp", tmpdir, sizeof(tmpdir));

    char subdir[PATH_MAX + 32];
    snprintf(subdir, sizeof(subdir), "%s/sub", tmpdir);
    assert(tt_mkdir_p(subdir, 0755));

    char missing_path[PATH_MAX + 64];
    snprintf(missing_path, sizeof(missing_path), "%s/definitely_missing_ytdlp_99999", tmpdir);

    /* 1. tt_dir_exists() checks */
    assert(tt_dir_exists(tmpdir));
    assert(tt_dir_exists(subdir));
    assert(!tt_dir_exists(missing_path));

    char dummy_file[PATH_MAX + 64];
    snprintf(dummy_file, sizeof(dummy_file), "%s/regular.txt", tmpdir);
    assert(tt_write_file_atomic(dummy_file, "hello", 5));
    assert(tt_file_exists(dummy_file));
    assert(!tt_dir_exists(dummy_file));
    test_unlink_file(dummy_file);

    /* 2. Config JSON parsing & trimming of ytdlp_path */
    char cfg_file[PATH_MAX + 32];
    snprintf(cfg_file, sizeof(cfg_file), "%s/tuitube.json", tmpdir);
    const char *json_cfg = "{ \"ytdlp_path\": \"  \\\"/tmp/custom-ytdlp\\\"  \" }\n";
    assert(tt_write_file_atomic(cfg_file, json_cfg, strlen(json_cfg)));
    TuiTubeConfig loaded;
    assert(config_load(&loaded, tmpdir));
    assert(loaded.has_ytdlp_path);
    assert(loaded.ytdlp_path && strcmp(loaded.ytdlp_path, "/tmp/custom-ytdlp") == 0);
    config_free(&loaded);

    /* 2b. Override config file (e.g., mpv/script-opts/tuitube.json) & relative asset resolution */
    char override_cfg[PATH_MAX + 64];
    snprintf(override_cfg, sizeof(override_cfg), "%s/tuitube.json", subdir);
    char override_theme[PATH_MAX + 64];
    snprintf(override_theme, sizeof(override_theme), "%s/mpv_theme.json", subdir);
    const char *ov_json = "{ \"language\": \"ja\", \"theme_path\": \"mpv_theme.json\" }\n";
    assert(tt_write_file_atomic(override_cfg, ov_json, strlen(ov_json)));
    assert(tt_write_file_atomic(override_theme, "{}\n", 3));
    assert(config_load_from(&loaded, tmpdir, override_cfg));
    assert(strcmp(loaded.config_dir, tmpdir) == 0);
    assert(strcmp(loaded.config_file_dir, subdir) == 0);
    assert(strcmp(loaded.resolved_hl, "ja") == 0);
    char *resolved_asset = config_resolve_asset_path(&loaded, "mpv_theme.json");
    assert(resolved_asset != NULL && test_same_file_path(resolved_asset, override_theme));
    free(resolved_asset);
    config_free(&loaded);
    test_unlink_file(override_theme);
    test_unlink_file(override_cfg);
    test_unlink_file(cfg_file);

    TuiTubeConfig cfg;
    config_init_defaults(&cfg);
    snprintf(cfg.config_dir, sizeof(cfg.config_dir), "%s", tmpdir);

    /* 3. Auto <config_dir> discovery (ytdlp_path: "") */
    char cfg_bin[PATH_MAX + 64];
#ifdef _WIN32
    snprintf(cfg_bin, sizeof(cfg_bin), "%s/yt-dlp.exe", tmpdir);
    assert(tt_write_file_atomic(cfg_bin, "MZ", 2));
#else
    snprintf(cfg_bin, sizeof(cfg_bin), "%s/yt-dlp", tmpdir);
    assert(tt_write_file_atomic(cfg_bin, "#!/bin/sh\nexit 0\n", 17));
    assert(chmod(cfg_bin, 0755) == 0);
#endif

    bool needs_opt = false;
    char *resolved = player_resolve_ytdlp_path(&cfg, &needs_opt);
    assert(resolved != NULL);
    assert(needs_opt);
    assert(test_same_file_path(resolved, cfg_bin));

    bool has_ytdlp = false;
    player_check_deps(&cfg, NULL, &has_ytdlp);
    assert(has_ytdlp);

    char opt[PATH_MAX + 64];
    assert(player_build_mpv_ytdl_script_opt(&cfg, opt, sizeof(opt)));
    char expected_opt[PATH_MAX + 128];
    snprintf(expected_opt, sizeof(expected_opt), "ytdl_hook-ytdl_path=%s", resolved);
    assert(strcmp(opt, expected_opt) == 0);
    free(resolved);
    test_unlink_file(cfg_bin);

    /* 4. Explicit ytdlp_path file (with whitespace) & directory expansion */
    char sub_custom[PATH_MAX + 64];
#ifdef _WIN32
    snprintf(sub_custom, sizeof(sub_custom), "%s/my-ytdlp.exe", subdir);
    assert(tt_write_file_atomic(sub_custom, "MZ", 2));
#else
    snprintf(sub_custom, sizeof(sub_custom), "%s/my-ytdlp", subdir);
    assert(tt_write_file_atomic(sub_custom, "#!/bin/sh\nexit 0\n", 17));
    assert(chmod(sub_custom, 0755) == 0);
#endif
    free(cfg.ytdlp_path);
    cfg.ytdlp_path = tt_asprintf("  %s  ", sub_custom);
    needs_opt = false;
    resolved = player_resolve_ytdlp_path(&cfg, &needs_opt);
    assert(resolved != NULL && needs_opt);
    assert(test_same_file_path(resolved, sub_custom));
    assert(player_build_mpv_ytdl_script_opt(&cfg, opt, sizeof(opt)));
    snprintf(expected_opt, sizeof(expected_opt), "ytdl_hook-ytdl_path=%s", resolved);
    assert(strcmp(opt, expected_opt) == 0);
    free(resolved);

    char sub_std[PATH_MAX + 64];
#ifdef _WIN32
    snprintf(sub_std, sizeof(sub_std), "%s/yt-dlp.exe", subdir);
    assert(tt_write_file_atomic(sub_std, "MZ", 2));
#else
    snprintf(sub_std, sizeof(sub_std), "%s/yt-dlp", subdir);
    assert(tt_write_file_atomic(sub_std, "#!/bin/sh\nexit 0\n", 17));
    assert(chmod(sub_std, 0755) == 0);
#endif
    free(cfg.ytdlp_path);
    cfg.ytdlp_path = tt_strdup(subdir);
    needs_opt = false;
    resolved = player_resolve_ytdlp_path(&cfg, &needs_opt);
    assert(resolved != NULL && needs_opt);
    assert(test_same_file_path(resolved, sub_std));
    free(resolved);

    /* 5. Relative ytdlp_path under <config_dir> */
    free(cfg.ytdlp_path);
#ifdef _WIN32
    cfg.ytdlp_path = tt_strdup("sub/my-ytdlp.exe");
#else
    cfg.ytdlp_path = tt_strdup("sub/my-ytdlp");
#endif
    needs_opt = false;
    resolved = player_resolve_ytdlp_path(&cfg, &needs_opt);
    assert(resolved != NULL && needs_opt);
    assert(test_same_file_path(resolved, sub_custom));
    free(resolved);

    test_unlink_file(sub_custom);
    test_unlink_file(sub_std);

    /* 6. Missing explicit ytdlp_path (strict no-fallback) */
    MediaItem vid = {0};
    vid.kind = ITEM_VIDEO;
    snprintf(vid.id, sizeof(vid.id), "dQw4w9WgXcQ");
    vid.title = tt_strdup("Test Video");

    free(cfg.ytdlp_path);
    cfg.ytdlp_path = tt_strdup(missing_path);
    assert(player_resolve_ytdlp_path(&cfg, NULL) == NULL);
    has_ytdlp = true;
    player_check_deps(&cfg, NULL, &has_ytdlp);
    assert(!has_ytdlp);

    char err[256] = {0};
    assert(!player_download_start(&cfg, &vid, NULL, 0, err, sizeof(err)));
    assert(strstr(err, "ytdlp_path") != NULL);

#ifndef _WIN32
    /* 7. POSIX non-executable ytdlp_path */
    char nonexec[PATH_MAX + 64];
    snprintf(nonexec, sizeof(nonexec), "%s/nonexec_ytdlp", tmpdir);
    assert(tt_write_file_atomic(nonexec, "#!/bin/sh\nexit 0\n", 17));
    assert(chmod(nonexec, 0644) == 0);
    free(cfg.ytdlp_path);
    cfg.ytdlp_path = tt_strdup(nonexec);
    assert(player_resolve_ytdlp_path(&cfg, NULL) == NULL);
    memset(err, 0, sizeof(err));
    assert(!player_download_start(&cfg, &vid, NULL, 0, err, sizeof(err)));
    assert(strstr(err, "not executable") != NULL);
    test_unlink_file(nonexec);
#endif

    /* 8. Delimiter / newline sanitization in player_build_mpv_ytdl_script_opt() */
    free(cfg.ytdlp_path);
    cfg.ytdlp_path = tt_strdup("bad\npath");
    assert(!player_build_mpv_ytdl_script_opt(&cfg, opt, sizeof(opt)));

    free(cfg.ytdlp_path);
#ifdef _WIN32
    cfg.ytdlp_path = tt_strdup("C:\\Tools\\a;b\\yt-dlp.exe");
#else
    cfg.ytdlp_path = tt_strdup("/tmp/a:b/yt-dlp");
#endif
    assert(!player_build_mpv_ytdl_script_opt(&cfg, opt, sizeof(opt)));

    /* 9. InnerTube ytdlp_path setter / getter */
    char it_buf[PATH_MAX];
    innertube_set_ytdlp_path("/custom/yt-dlp");
    innertube_get_ytdlp_path(it_buf, sizeof(it_buf));
    assert(strcmp(it_buf, "/custom/yt-dlp") == 0);

    innertube_set_ytdlp_path("bad\npath");
    innertube_get_ytdlp_path(it_buf, sizeof(it_buf));
    assert(strcmp(it_buf, "yt-dlp") == 0);

    innertube_set_ytdlp_path(NULL);
    innertube_get_ytdlp_path(it_buf, sizeof(it_buf));
    assert(strcmp(it_buf, "yt-dlp") == 0);

    media_item_free_fields(&vid);
    config_free(&cfg);
    test_rmdir(subdir);
    test_rmdir(tmpdir);
}

int main(void) {
    RUN_TEST(test_util_utf8_and_entities);
    RUN_TEST(test_search_params_encoding);
    RUN_TEST(test_url_parsing);
    RUN_TEST(test_innertube_search_browse_next_json);
    RUN_TEST(test_rss_and_feed_filtering);
    RUN_TEST(test_config_theme_and_keybindings);
    RUN_TEST(test_player_format_mapping);
    RUN_TEST(test_model_favlist_and_history_persistence);
    RUN_TEST(test_image_decoding_and_mascot);
    RUN_TEST(test_wcag_themes_and_default_dark);
    RUN_TEST(test_new_keybindings_and_formatting);
    RUN_TEST(test_nav_stack_push_pop_and_eviction);
    RUN_TEST(test_live_shelf_lockup_engagement_and_members_only);
    RUN_TEST(test_http_pool_detail_gen_independence);
    RUN_TEST(test_config_members_only_and_download);
    RUN_TEST(test_wayland_shm_sizing_and_scaling);
    RUN_TEST(test_mpv_osd_dims_and_script_opts);
    RUN_TEST(test_mpv_key_mouse_and_unicode_parsing);
    RUN_TEST(test_config_explicit_format_and_slang_flags);
    RUN_TEST(test_thumb_cache_flush_ram_and_drop_jobs);
    RUN_TEST(test_win32_cmdline_quoting);
    RUN_TEST(test_windows_paths_and_script_opts);
    RUN_TEST(test_ttf_family_name_parser);
    RUN_TEST(test_mpv_overlay_section_close_win_and_bindings);
    RUN_TEST(test_player_launch_and_exe_resolution);
    RUN_TEST(test_locale_parsing_and_innertube_headers);
    RUN_TEST(test_original_titles);
    RUN_TEST(test_original_title_detail_merge);
    RUN_TEST(test_original_description_and_url_links);
    RUN_TEST(test_url_link_rect_geometry);
    RUN_TEST(test_history_original_title_roundtrip);
    RUN_TEST(test_player_original_title_selection);
    RUN_TEST(test_non_english_lockup_and_badges);
    RUN_TEST(test_pe_subsystem_and_runtime_dep_checks);
    RUN_TEST(test_ytdlp_path_resolution_and_mpv_opts);

    printf("\nAll %d tests passed!\n", g_tests_run);
    return 0;
}
