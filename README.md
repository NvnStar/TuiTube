# TuiTube

A lightweight, pure-C17 YouTube client that builds as both a **standalone native Wayland app (`./tuitube`)** and a **native `mpv` C plugin (`./tuitube.so`)**.

No Electron, no GTK, no Qt — just Cairo/PangoCairo, `libcurl`, `yyjson`, `libjpeg`/`libpng`, and playback via `mpv` + `yt-dlp`.

---

## Features

- **Dual Native Targets:**
  - **Standalone Wayland (`./tuitube`):** `wl_shm` rendering with `xdg-shell`, fractional HiDPI scaling (`wp_fractional_scale_v1` / `wp_viewporter`), SSD/CSD negotiation, `text-input-v3` IME, and Wayland clipboard.
  - **`mpv` C Plugin (`./tuitube.so`):** Single-file OSD overlay (`overlay-add`) playing videos directly in the host `mpv` instance (respects your `mpv.conf` by default, zero `libmpv-dev` build dependency, zero Wayland linkage, flushes decoded thumbnail/framebuffer RAM when hidden).
- **YouTube Search & Browsing:** InnerTube v1 (`search`, `browse`, `next`, `navigation/resolve_url`) with sort/type/live filters, Channel **Videos** / **Live** tabs, pagination (`Next Page` + optional `infinite_scroll`), direct URL / `@handle` resolution (including `/streams` and `/live`), and `yt-dlp --flat-playlist` fallback.
- **Subscriptions, Feed & History:** Channel/playlist favorites (`favlist.json`), chronological Subscription Feed via Atom RSS (or InnerTube), and watch history (`history.json`) with watched-video dimming.
- **Background Downloads & Filtering:** Non-blocking `yt-dlp` video downloads (`Ctrl+S` or detail/context menu) and `"hide_members_only"` filtering (pair with `"feed_use_rss": false` to filter Subscription Feed items as well).
- **Fast Thumbnail Pipeline:** 4-thread priority `libcurl` worker pool, `libjpeg`/`libpng` decoding, disk cache (`~/.cache/tuitube/thumbs`), and LRU Cairo surface cache.
- **Customizable & Portable:** JSON config (`tuitube.json`), custom TTF/OTF fonts, custom mascot image, built-in WCAG AA themes (`dark`, `light`, `yuuka`) + custom theme JSON, custom keybindings, and portable mode (`tuitube_portable_mode.txt`).

---

## Dependencies

**Runtime:** `mpv` (or `vlc` for standalone), `yt-dlp`, and optionally `wl-copy` (`wl-clipboard`).  
**Build-time:** C17 compiler (`gcc`/`clang`), `pkg-config`, and dev headers for `xkbcommon`, `cairo`, `pangocairo`, `fontconfig`, `libcurl`, `yyjson`, `libjpeg`, `libpng` (plus `wayland-scanner`, `wayland-protocols`, `wayland-client`, `wayland-cursor` for standalone `./tuitube`; `libmpv-dev` is **not** required for `./tuitube.so`).

<details>
<summary>Install commands (Arch Linux / Debian / Ubuntu)</summary>

**Arch Linux:**
```bash
sudo pacman -S gcc pkgconf wayland wayland-protocols libxkbcommon cairo pango fontconfig curl yyjson libjpeg-turbo libpng mpv yt-dlp wl-clipboard
```

**Debian / Ubuntu (24.04+):**
```bash
sudo apt install build-essential pkg-config libwayland-bin wayland-protocols \
  libwayland-dev libxkbcommon-dev libcairo2-dev libpango1.0-dev libfontconfig-dev \
  libcurl4-openssl-dev libjpeg-dev libpng-dev mpv yt-dlp wl-clipboard
# On Debian 13+ / Ubuntu 25.04+, also install libyyjson-dev;
# on Ubuntu 24.04 LTS, build yyjson from https://github.com/ibireme/yyjson
```

</details>

---

## Build

```bash
make release   # Build both ./tuitube and ./tuitube.so
make plugin    # Build only ./tuitube.so (no Wayland or libmpv-dev needed)
make debug     # Build ./tuitube-debug with ASan + UBSan
make test      # Run offline ASan/UBSan unit tests + plugin symbol/linkage checks
make lint      # Strict -Wall -Wextra -Wpedantic -Werror syntax check
```

---

## Usage

### Standalone Wayland (`./tuitube`)

```bash
./tuitube
./tuitube --config-dir ~/.config/tuitube   # Override config directory
```

### Native `mpv` C Plugin (`./tuitube.so`)

```bash
# Install for automatic loading in mpv
mkdir -p ~/.config/mpv/scripts
cp ./tuitube.so ~/.config/mpv/scripts/tuitube.so
mpv --idle

# Or load directly without installing
mpv --idle --script=./tuitube.so
```

- **Toggle Overlay:** Press `F12` by default, or bind your own key in `~/.config/mpv/input.conf`:
  ```conf
  Tab script-message tuitube-toggle
  ```
  (`script-binding tuitube/toggle`, `script-message tuitube-open`, and `script-message tuitube-close` are also supported.)
- **Playback Behavior:** Videos play directly in the host `mpv` window via `loadfile`, respecting your `mpv.conf` unless `"video_format"` or `"sub_language"` are explicitly set in `tuitube.json`.
- **Override Config Dir:** Pass `--script-opts=tuitube-config_dir=/path/to/dir`.

### Shared State & Portable Mode

Both targets share `~/.config/tuitube/{tuitube.json,favlist.json,history.json}` and `~/.cache/tuitube/thumbs`.  
To run in **portable mode**, place an empty `tuitube_portable_mode.txt` beside `./tuitube` or `./tuitube.so`.

---

## Configuration & Keybindings

Starter files: [examples/tuitube.json](examples/tuitube.json) · [examples/sample_theme.json](examples/sample_theme.json)

| Key / Input | Action |
| :--- | :--- |
| `F12` *(plugin)* | Toggle TuiTube overlay inside `mpv` |
| `Ctrl+Q` | Quit (`./tuitube`) / Hide overlay (`tuitube.so`) |
| `Ctrl+L` or `/` | Focus search box (`Esc` returns to list, `Ctrl+U`/`Ctrl+BackSpace` edit) |
| `Tab` / `Shift+Tab` | Cycle focus across Search, List, and Detail panes |
| `Enter` / Double-Click | Play selected video or open channel/playlist |
| `Up`/`Down`, `j`/`k`, `PgUp`/`PgDn`, `Home`/`End` | Navigate list (or scroll description in Detail pane) |
| `Shift+Up` / `Shift+Down` (`K` / `J`) | Scroll description while in list |
| `Alt+Left` / `BackSpace` / Mouse Back | Navigate back |
| `Right-Click` | Context menu (Play, Download, Channel, Recommended, Favorite, Copy) |
| `Ctrl+S` | Download selected video via `yt-dlp` |
| `Ctrl+F` / `Ctrl+R` / `Ctrl+H` | Show Favorites / Subscription Feed / Watch History |
| `Ctrl+D` | Add/remove channel or playlist in Favorites |
| `Ctrl+Y` | Copy item URL |
| `Ctrl+N` | Load next page |
| `Ctrl+]` / `Ctrl+[` | Widen / narrow list pane |
| `F1` or `?` | Toggle shortcut help overlay |

---

## Documentation & Maintenance

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Module map and concurrency model.
- [docs/UPSTREAM_MAINTENANCE.md](docs/UPSTREAM_MAINTENANCE.md) — Guide for fixing YouTube / `yt-dlp` / `mpv` upstream changes.

<details>
<summary>Quick map: where to fix upstream breakage</summary>

| Upstream Component | Symptom | Where to Fix |
| :--- | :--- | :--- |
| **InnerTube client version / context** | Search, Browse, or Next returns HTTP 400/403 | `INNERTUBE_CLIENT_VER` / `add_innertube_context()` in [src/innertube.c](src/innertube.c) |
| **YouTube JSON schema** | Missing metadata or 0 results | `parse_video_renderer`, `parse_lockup_view_model`, `innertube_parse_*_json` in [src/innertube.c](src/innertube.c) |
| **Search filter protobuf params** | Sort/Type/Live filters stop working | `innertube_encode_search_params()` in [src/innertube.c](src/innertube.c) |
| **Atom RSS feeds** | Subscription Feed stops parsing entries/dates | `innertube_parse_rss_xml()` in [src/innertube.c](src/innertube.c) (or set `"feed_use_rss": false`) |
| **Thumbnail URLs** | Thumbnails fail to load | `image_resolve_thumb_url()` in [src/image.c](src/image.c) |
| **`yt-dlp` format / search fallback** | Format selection or fallback search breaks | `player_map_video_format()` in [src/player.c](src/player.c), `search_ytdlp_fallback()` in [src/innertube.c](src/innertube.c) |
| **`mpv` / `vlc` CLI or C plugin ABI** | Playback or overlay issues | `player_launch()` in [src/player.c](src/player.c), [src/mpv_plugin.c](src/mpv_plugin.c), [src/mpv_cplugin.h](src/mpv_cplugin.h) |
| **Wayland protocols** | Scaling, decorations, IME, or clipboard issues | [src/wayland.c](src/wayland.c) and protocol paths in [Makefile](Makefile) |

</details>

---

## Credits

- **[ikatube](https://chino-chan.gitlab.io/programs.html#s2)** by **chino-chan** — UX, layout, theme, and config inspiration.
- **[mpv](https://mpv.io/)** & **[yt-dlp](https://github.com/yt-dlp/yt-dlp)** — Playback, stream extraction, and downloads.
- **C Libraries:** Wayland, libxkbcommon, Cairo, Pango, Fontconfig, libcurl, yyjson, libjpeg-turbo, and libpng.
