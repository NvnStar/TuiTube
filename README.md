# TuiTube

A lightweight, zero-bloat native Wayland YouTube client written in pure C17.

No Electron, no GTK, no Qt — just `wayland-client`, `wl_shm` + Cairo/PangoCairo, `libcurl`, `yyjson`, `libjpeg`/`libpng`, and external playback via `mpv` (powered by `yt-dlp`).

## Features

- **Native Wayland GUI:** Double-buffered `wl_shm` rendering with `xdg-shell`, HiDPI scaling, server-side decorations (`xdg-decoration`) with automatic slim CSD fallback, `xkbcommon` keyboard + repeat, and Wayland clipboard copy/paste.
- **YouTube Search & Browsing:** Direct InnerTube v1 (`search`, `browse`, `next`, `navigation/resolve_url`) with sort/type/live filters, pagination (`Next Page` + optional `infinite_scroll`), direct URL / `@handle` paste resolution, and an automatic `yt-dlp --flat-playlist` fallback for search.
- **Subscriptions, Feed & History:** Channel/playlist favorites (`favlist.json`), chronological subscription Feed via YouTube Atom RSS (with InnerTube fallback, configurable `max_feed_videos_per_channel` and `max_feed_age`), and deduplicated watch history (`history.json`) with watched-video dimming.
- **Fast Thumbnail Pipeline:** Priority 4-thread `libcurl` worker pool, `libjpeg`/`libpng` decoding, disk cache + 256-entry LRU Cairo surface cache.
- **Customizable & Portable:** `ikatube`-compatible JSON config (`tuitube.json`), custom TTF/OTF fonts via Fontconfig, custom mascot image, built-in WCAG AA themes (`dark` default, `light`, `yuuka`) + custom theme JSON (supports comments & trailing commas), custom keybindings, and portable mode (`tuitube_portable_mode.txt`).

## Dependencies

### Build-time
- C17 compiler (`gcc` or `clang`) and `pkg-config`
- `wayland-scanner` and `wayland-protocols`
- Development headers for: `wayland-client`, `wayland-cursor`, `xkbcommon`, `cairo`, `pangocairo`, `fontconfig`, `libcurl`, `yyjson`, `libjpeg`, `libpng`

### Runtime
- A Wayland compositor
- `mpv` (or `vlc`) and `yt-dlp` on `$PATH`
- Optional: `wl-copy` (`wl-clipboard`) so copied URLs/titles persist after closing TuiTube

<details>
<summary>Install dependencies on Arch Linux / Debian / Ubuntu</summary>

**Arch Linux:**
```bash
sudo pacman -S gcc pkgconf wayland wayland-protocols libxkbcommon cairo pango fontconfig curl yyjson libjpeg-turbo libpng mpv yt-dlp wl-clipboard
```

**Debian / Ubuntu (24.04+):**
```bash
sudo apt install build-essential pkg-config wayland-scanner wayland-protocols \
  libwayland-dev libxkbcommon-dev libcairo2-dev libpango1.0-dev libfontconfig-dev \
  libcurl4-openssl-dev libyyjson-dev libjpeg-dev libpng-dev mpv yt-dlp wl-clipboard
```

</details>

## Building & Running

```bash
# Release binary (./tuitube)
make release

# Debug binary with ASan/UBSan (./tuitube-debug)
make debug

# Strict compiler lint (-Wall -Wextra -Wpedantic -Werror)
make lint

# Run offline unit tests under ASan/UBSan
make test

# Run
./tuitube
# Or override config directory:
./tuitube --config-dir ~/.config/tuitube
```

### Portable Mode
Place an empty file named `tuitube_portable_mode.txt` in the same directory as the `tuitube` executable to store `tuitube.json`, `favlist.json`, `history.json`, and `cache/thumbs/` alongside the binary.

## Configuration & Keybindings

See [examples/tuitube.json](examples/tuitube.json) and [examples/sample_theme.json](examples/sample_theme.json) for starter configuration and custom theme syntax.

Default keybindings (when search input is not focused, or with modifiers):
- `Ctrl+Q` — Quit
- `Ctrl+L` or `/` — Focus search input (`Escape` moves focus to list, `Ctrl+U` / `Ctrl+BackSpace` clear/delete word)
- `Tab` / `Shift+Tab` — Cycle focus across Search, List, and Detail panes
- `Enter` — Play selected video / open selected channel or playlist
- `Alt+Left` / `BackSpace` / Mouse Back — Navigate back in history
- `Up` / `Down` / `j` / `k` / `PageUp` / `PageDown` / `Home` / `End` — Navigate list (or scroll description when Detail pane is focused)
- `Shift+Up` / `Shift+Down` (`K` / `J`) — Scroll description while in list
- `Right-Click` — Open item context menu (Play, Channel, Recommended, Favorite, Copy URL/Title/Description)
- `F1` or `?` — Toggle keyboard & mouse shortcut overlay
- `Ctrl+F` — Show Favorites
- `Ctrl+R` — Show Subscription Feed
- `Ctrl+H` — Show Watch History
- `Ctrl+D` — Add/remove selected item/channel to Favorites
- `Ctrl+Y` — Copy video/item URL
- `Ctrl+N` — Load next page
- `Ctrl+]` / `Ctrl+[` — Widen / narrow left list pane

---

## Credits & Acknowledgments

If you enjoy TuiTube, please consider giving a star, thanks, or support to the upstream projects and authors that made it possible:

- **ikatube / ikatk** *(original UX inspiration)* — TuiTube's two-pane layout, portable mode convention, `tuitube.json` options, 8-value RGBA theme schema, and keybinding design are directly modeled on `ikatube` (`ikatube2-wl`) and its configuration reference. Thank you to the `ikatube` author for designing such a clean, no-nonsense YouTube desktop workflow!
- **[mpv](https://mpv.io/)** & **[yt-dlp](https://github.com/yt-dlp/yt-dlp)** — Do the heavy lifting of stream extraction, adaptive muxing, and hardware-accelerated video playback. Please star `yt-dlp` and support the `mpv` developers!
- **Core C Libraries:** [Wayland](https://wayland.freedesktop.org/), [libxkbcommon](https://xkbcommon.org/), [Cairo](https://www.cairographics.org/), [Pango](https://pango.gnome.org/), [Fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/), [libcurl](https://curl.se/libcurl/), [yyjson](https://github.com/ibireme/yyjson), [libjpeg-turbo](https://libjpeg-turbo.org/), and [libpng](http://www.libpng.org/pub/png/libpng.html).

*How to show appreciation:* Star the upstream repositories (`yt-dlp`, `mpv-player/mpv`, `ibireme/yyjson`, and `ikatube`), report reproducible issues with logs/fixtures, and keep `yt-dlp` updated when YouTube changes player signatures.

---

## Upstream Tracking & Where to Fix Breakage

YouTube and external media tools evolve frequently. Use this map to jump straight to the right file when something breaks upstream (see [docs/UPSTREAM_MAINTENANCE.md](docs/UPSTREAM_MAINTENANCE.md) for detailed instructions):

| Upstream Component | Symptom | Where to Fix in TuiTube |
| :--- | :--- | :--- |
| **YouTube InnerTube client version / context** | Search, Browse, or Next returns HTTP 400/403 | `INNERTUBE_CLIENT_VER` and `add_innertube_context()` in [src/innertube.c](src/innertube.c) |
| **YouTube Search / Browse / Next JSON schema** | Missing titles, durations, descriptions, or 0 results | `parse_video_renderer`, `parse_lockup_view_model`, `innertube_parse_*_json` in [src/innertube.c](src/innertube.c) |
| **YouTube Search protobuf filter params** | Sort/Type/Live filters stop filtering | `innertube_encode_search_params()` in [src/innertube.c](src/innertube.c) |
| **YouTube Atom RSS feeds** | Subscription Feed stops parsing entries or dates | `innertube_parse_rss_xml()` and `innertube_fetch_feed()` in [src/innertube.c](src/innertube.c) (or set `"feed_use_rss": false` in `tuitube.json`) |
| **YouTube thumbnail URLs / WebP changes** | Thumbnails fail to load or show placeholders | `image_resolve_thumb_url()` in [src/image.c](src/image.c) |
| **`yt-dlp` format selector or search fallback** | Playback fails on format selection or fallback search breaks | `player_map_video_format()` in [src/player.c](src/player.c) and `search_ytdlp_fallback()` in [src/innertube.c](src/innertube.c) |
| **`mpv` / `vlc` CLI options** | Player fails to launch with unknown flag error | `player_launch()` in [src/player.c](src/player.c) |
| **Wayland compositor / protocol updates** | Decorations, clipboard, or key repeat issues | `src/wayland.c` and protocol XML paths in [Makefile](Makefile) |
