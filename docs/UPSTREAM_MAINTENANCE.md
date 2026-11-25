# Upstream Maintenance Guide

Because TuiTube communicates directly with YouTube's undocumented InnerTube API and delegates stream playback to `mpv` + `yt-dlp`, upstream changes happen periodically. This guide tracks every external dependency and where to patch TuiTube if upstream behavior changes.

---

## 1. YouTube InnerTube API (`src/innertube.c`)

### Symptoms
- Searches, channel browses, or video detail loads return HTTP 400 / 403 / 429 or `0 items`.
- Specific fields (title, author, view count, duration, description, continuation token) start showing up blank.

### Where to Fix
1. **Client Context (`INNERTUBE_CLIENT_VER`, `add_innertube_context`):**
   - YouTube occasionally retires old `WEB` `clientVersion` strings. Update `INNERTUBE_CLIENT_VER` (format `"2.YYYYMMDD.00.00"`) in `src/innertube.c`.
   - Headers (`Origin`, `Referer`, `Content-Type`) are defined in `INNERTUBE_HEADERS`.
2. **Search Filter Protobuf (`innertube_encode_search_params`):**
   - If Sort / Type / Live filters stop applying, inspect the base64 `"params"` value sent by YouTube web search and update the protobuf field tags in `innertube_encode_search_params`.
3. **Renderer Schema Drift (`parse_*` functions):**
   - Search items: `parse_video_renderer`, `parse_channel_renderer`, `parse_playlist_renderer`, and `parse_lockup_view_model`.
   - Channel/playlist items: `innertube_parse_browse_json` (`richGridRenderer`, `playlistVideoListRenderer`).
   - Video details & recommended: `innertube_parse_next_json` (`videoPrimaryInfoRenderer`, `videoSecondaryInfoRenderer`, `secondaryResults`).
   - Pagination: `extract_continuation_token` and `parse_continuation_array`.
4. **Search Fallback (`search_ytdlp_fallback`):**
   - If InnerTube search is temporarily blocked, TuiTube automatically falls back to `yt-dlp --flat-playlist --dump-single-json "ytsearch20:<query>"`. Updating `yt-dlp` on the host often restores search immediately without recompiling.

---

## 2. YouTube Atom RSS Feed (`src/innertube.c`)

### Symptoms
- Subscription Feed (`Feed` button / `Ctrl+R`) returns empty or drops timestamps/titles.

### Where to Fix
- `innertube_parse_rss_xml()` and `innertube_fetch_feed()` in `src/innertube.c`.
- Temporary user workaround without code changes: set `"feed_use_rss": false` in `tuitube.json` to switch the Feed from Atom XML to InnerTube `browse`.

---

## 3. Thumbnails (`src/image.c`)

### Symptoms
- Video or channel thumbnails show empty themed placeholders.

### Where to Fix
- `image_resolve_thumb_url()` in `src/image.c` (defaults videos to `https://i.ytimg.com/vi/<id>/mqdefault.jpg` and normalizes protocol-relative `//` URLs).
- `image_decode_bytes()` in `src/image.c` supports JPEG and PNG. If YouTube ever forces WebP on `mqdefault.jpg` paths, add a `libwebp` decoder branch or adjust the URL template.

---

## 4. `yt-dlp` & Media Players (`src/player.c`)

### Symptoms
- Video playback fails to start or picks an unexpected resolution/codec.

### Where to Fix
- `player_map_video_format()` in `src/player.c` translates `video_format` (e.g. `1080p60`), `allow_hdr`, and `all_codecs` into `--ytdl-format=...` strings.
- `player_launch()` in `src/player.c` builds `argv` for `mpv` and `vlc`.
- Note: Most YouTube playback failures ("Sign in to confirm", HTTP 403 on video streams, SABR/n-sig errors) are resolved by upgrading `yt-dlp` rather than changing TuiTube.

---

## 5. Wayland Protocols (`Makefile`, `src/wayland.c`)

### Symptoms
- Compilation fails on `wayland-scanner` or compositor behavior changes (decorations, fractional scaling, clipboard).

### Where to Fix
- Protocol XML paths (`XDG_SHELL_XML`, `XDG_DECO_XML`) in `Makefile`.
- Compositor protocol binding, `wl_shm` buffers, `xdg_toplevel_decoration_v1`, `wl_data_device`, and `xkbcommon` handling in `src/wayland.c`.
