#include "config.h"
#include "http.h"
#include "image.h"
#include "innertube.h"
#include "model.h"
#include "player.h"
#include "util.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
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

    printf("\nAll %d tests passed!\n", g_tests_run);
    return 0;
}
