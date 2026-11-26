# TuiTube

A lightweight, pure-C17 YouTube client for **Linux (Wayland)** and **Windows (Win32)** that builds as both a **standalone desktop app (`./tuitube` / `tuitube.exe`)** and a **native `mpv` C plugin (`./tuitube.so` / `tuitube.dll`)**.

No Electron, no GTK, no Qt — just Cairo/PangoCairo, `libcurl`, `yyjson`, `libjpeg`/`libpng`, and playback via `mpv` + `yt-dlp`.

---

## Features

- **Dual Native Targets (Linux & Windows):**
  - **Standalone Desktop (`./tuitube` on Wayland, `tuitube.exe` on Win32):**
    - *Linux:* `wl_shm` rendering with `xdg-shell`, fractional HiDPI scaling (`wp_fractional_scale_v1` / `wp_viewporter`), SSD/CSD negotiation, `text-input-v3` IME, and Wayland clipboard.
    - *Windows:* Native Win32 `HWND` (`-mwindows`) with Per-Monitor-DPI-v2 scaling, dark titlebar, top-down BGRA `DIBSection` Cairo rendering, IMM32 IME, Win32 clipboard, and zero Fontconfig/xkbcommon bloat.
  - **`mpv` C Plugin (`./tuitube.so` on Linux, `tuitube.dll` on Windows):** Single-file OSD overlay (`overlay-add`) playing videos directly in the host `mpv` instance (respects your `mpv.conf` by default, zero `libmpv` build/runtime dependency, flushes decoded thumbnail/framebuffer RAM when hidden).
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

**MSYS2 UCRT64 (Windows 10/11):**
```bash
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-cairo mingw-w64-ucrt-x86_64-pango \
  mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-yyjson \
  mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-libpng
```

</details>

---

## Build

`Makefile` auto-detects Linux vs Windows (MSYS2 UCRT64):

```bash
make release   # Linux: ./tuitube & ./tuitube.so | Windows: tuitube.exe & tuitube.dll
make plugin    # Build only the mpv C plugin (tuitube.so / tuitube.dll)
make debug     # Build debug binary (tuitube-debug with ASan+UBSan on Linux; tuitube-debug.exe on Windows)
make test      # Run offline unit tests + plugin symbol/linkage checks
make lint      # Strict -Wall -Wextra -Wpedantic -Werror syntax check
```

Prebuilt binaries (`tuitube-windows-x64.zip` with MSYS2 UCRT64 runtime DLLs, `tuitube-linux-x64.tar.gz`, and `SHA256SUMS.txt`) are published on the [GitHub Releases](https://github.com/NvnStar/TuiTube/releases) page (served via direct CDN downloads without requiring a GitHub login).

---

## Usage

### Standalone (`./tuitube` / `tuitube.exe`)

```bash
# Linux
./tuitube
./tuitube --config-dir ~/.config/tuitube   # Override config directory

# Windows
tuitube.exe
tuitube.exe --config-dir C:\Users\You\tuitube
```

### Native `mpv` C Plugin (`./tuitube.so` / `tuitube.dll`)

```bash
# Linux: install for automatic loading in mpv
mkdir -p ~/.config/mpv/scripts
cp ./tuitube.so ~/.config/mpv/scripts/tuitube.so
mpv --idle

# Or load directly without installing
mpv --idle --script=./tuitube.so
```

```powershell
# Windows: install for automatic loading in mpv (ensure runtime DLLs are beside tuitube.dll or in PATH)
mkdir "$env:APPDATA\mpv\scripts" -Force
copy .\tuitube.dll "$env:APPDATA\mpv\scripts\tuitube.dll"
mpv.exe --idle

# Or load directly without installing
mpv.exe --idle --script=.\tuitube.dll
```

- **Toggle Overlay:** Press `F12` by default, or bind your own key in `input.conf`:
  ```conf
  Tab script-message tuitube-toggle
  ```
  (`script-binding tuitube/toggle`, `script-message tuitube-open`, and `script-message tuitube-close` are also supported.)
- **Closing `mpv`:** Triggering an OS/window-manager close-window action (titlebar `[X]`, `Alt+F4`, or your compositor's window-close shortcut) quits `mpv` directly even while the TuiTube overlay is open (`Ctrl+Q` and `Esc` hide only the overlay).
- **Playback Behavior:** Videos play directly in the host `mpv` window via `loadfile`, respecting your `mpv.conf` unless `"video_format"` or `"sub_language"` are explicitly set in `tuitube.json`.
- **Override Config Dir:** Pass `--script-opts=tuitube-config_dir=/path/to/dir` (or `C:\path\to\dir` on Windows).

> **Windows Security Note (Unsigned Binaries):** Windows release binaries (`tuitube.exe`, `tuitube.dll`, and bundled MSYS2 UCRT64 runtime DLLs) are **unsigned** open-source builds.
> - **Microsoft Defender SmartScreen:** Click *More info -> Run anyway*, or run `Unblock-File .\tuitube-windows-x64.zip` in PowerShell before extracting.
> - **Windows 11 Smart App Control (SAC):** When SAC is **On**, kernel Code Integrity blocks all unsigned `.exe`/`.dll` files without a *Run anyway* prompt; set Smart App Control to **Off** under *Windows Security -> App & browser control -> Smart App Control settings* to run unsigned builds.

### Shared State & Portable Mode

- **Linux:** `~/.config/tuitube/{tuitube.json,favlist.json,history.json}` and `~/.cache/tuitube/thumbs`.
- **Windows:** `%APPDATA%\tuitube\{tuitube.json,favlist.json,history.json}` and `%LOCALAPPDATA%\tuitube\thumbs`.
- **Portable Mode:** Place an empty `tuitube_portable_mode.txt` beside `./tuitube` / `./tuitube.so` (or `tuitube.exe` / `tuitube.dll`).

---

## Configuration & Keybindings

Starter files: [examples/tuitube.json](examples/tuitube.json) · [examples/sample_theme.json](examples/sample_theme.json)

| Key / Input | Action |
| :--- | :--- |
| `F12` *(plugin)* | Toggle TuiTube overlay inside `mpv` |
| `Ctrl+Q` | Quit (`./tuitube` / `tuitube.exe`) / Hide overlay (`tuitube.so` / `tuitube.dll`; OS/WM window close quits `mpv`) |
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
