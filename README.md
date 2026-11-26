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

## Download & Prebuilt Releases

Prebuilt release archives (`tuitube-windows-x64.zip` with MSYS2 UCRT64 runtime DLLs, `tuitube-linux-x64.tar.gz`, and `SHA256SUMS.txt`) are available on the [GitHub Releases](https://github.com/NvnStar/TuiTube/releases) page (served directly over CDN without requiring a GitHub login). Both archives ship with a ready-to-edit `tuitube.json` and [docs/CONFIGURATION.md](docs/CONFIGURATION.md) beside the binaries.

> [!WARNING]
> **Runtime Dependencies Required (`mpv` and `yt-dlp` are NOT bundled)**  
> `tuitube-windows-x64.zip` and `tuitube-linux-x64.tar.gz` **do NOT include `mpv` or `yt-dlp`**. Both must be discoverable at runtime (`ffmpeg` is also recommended for merging high-resolution streams; `vlc` can be used as an alternative standalone player):
>
> - **Package Managers (`PATH`):**
>   - **Windows (`winget`):** `winget install shinchiro.mpv yt-dlp.yt-dlp Gyan.FFmpeg`
>   - **Windows (`scoop`):** `scoop install mpv yt-dlp ffmpeg`
>   - **Arch Linux:** `sudo pacman -S mpv yt-dlp ffmpeg`
>   - **Debian / Ubuntu:** `sudo apt install mpv yt-dlp ffmpeg`
> - **Local Drop-In Placement (no `PATH` edit required):**
>   - **Standalone (`tuitube.exe` / `./tuitube`):** Place `yt-dlp.exe` (and `mpv.exe`) beside `tuitube.exe` on Windows, or place `yt-dlp` (`chmod +x yt-dlp`) beside `./tuitube` on Linux — or inside TuiTube's config directory (`%APPDATA%\tuitube\yt-dlp.exe` / `~/.config/tuitube/yt-dlp`).
>   - **`mpv` Plugin (`tuitube.dll` / `tuitube.so`):** Place `yt-dlp.exe` beside `mpv.exe` (Windows), or place `yt-dlp` / `yt-dlp.exe` in `~/.config/tuitube/` / `%APPDATA%\tuitube/` (or beside the plugin when using portable mode).
> - **Custom Binary Override (`"ytdlp_path"` in `tuitube.json`):** Point `"ytdlp_path"` to any custom or nightly `yt-dlp` binary (or folder containing it) if a distro package is outdated or upstream YouTube breaks extraction.

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
- **Plugin Config Location:** `tuitube.json` is optional. When customizing the `mpv` plugin, you can drop `tuitube.json` directly into `mpv`'s `script-opts/` directory (`~/.config/mpv/script-opts/tuitube.json` on Linux, `%APPDATA%\mpv\script-opts\tuitube.json` on Windows, or `portable_config\script-opts\tuitube.json` for portable `mpv`) — or share `~/.config/tuitube/tuitube.json` / `%APPDATA%\tuitube\tuitube.json` with standalone TuiTube. You can also override the full directory via `script-opts-append=tuitube-config_dir=...` in `mpv.conf` or portable `tuitube_portable_mode.txt` beside `tuitube.so`/`tuitube.dll`.

> **Windows Security Note (Unsigned Binaries):** Windows release binaries (`tuitube.exe`, `tuitube.dll`, and bundled MSYS2 UCRT64 runtime DLLs) are **unsigned** open-source builds.
> - **Microsoft Defender SmartScreen:** Click *More info -> Run anyway*, or run `Unblock-File .\tuitube-windows-x64.zip` in PowerShell before extracting.
> - **Windows 11 Smart App Control (SAC):** When SAC is **On**, kernel Code Integrity blocks all unsigned `.exe`/`.dll` files without a *Run anyway* prompt; set Smart App Control to **Off** under *Windows Security -> App & browser control -> Smart App Control settings* to run unsigned builds.

### Shared State & Portable Mode

Both the standalone app and the `mpv` plugin share the same config, favorites, history, and thumbnail cache:

- **Linux:** `~/.config/tuitube/{tuitube.json,favlist.json,history.json}` and `~/.cache/tuitube/thumbs`.
- **Windows:** `%APPDATA%\tuitube\{tuitube.json,favlist.json,history.json}` and `%LOCALAPPDATA%\tuitube\thumbs`.
- **Portable Mode:** Place an empty `tuitube_portable_mode.txt` beside `./tuitube` / `./tuitube.so` (or `tuitube.exe` / `tuitube.dll`).

```text
Standard Mode (Linux)
├── ~/.config/tuitube/
│   ├── tuitube.json                # Shared config (standalone & mpv plugin fallback)
│   ├── favlist.json                # Saved channels & playlists
│   ├── history.json                # Watch history
│   └── yt-dlp                      # Optional local yt-dlp binary (chmod +x)
├── ~/.cache/tuitube/thumbs/        # Disk thumbnail cache
└── ~/.config/mpv/
    ├── script-opts/
    │   └── tuitube.json            # Optional mpv-plugin config (auto-discovered by tuitube.so)
    ├── input.conf                  # Optional: custom toggle key (default F12)
    └── scripts/
        └── tuitube.so              # Native mpv C plugin (optional)

Standard Mode (Windows)
├── %APPDATA%\tuitube\
│   ├── tuitube.json                # Shared config (standalone & mpv plugin fallback)
│   ├── favlist.json                # Saved channels & playlists
│   ├── history.json                # Watch history
│   └── yt-dlp.exe                  # Optional local yt-dlp.exe
├── %LOCALAPPDATA%\tuitube\thumbs\  # Disk thumbnail cache
└── %APPDATA%\mpv\
    ├── script-opts\
    │   └── tuitube.json            # Optional mpv-plugin config (auto-discovered by tuitube.dll)
    ├── input.conf                  # Optional: custom toggle key (default F12)
    └── scripts\
        └── tuitube.dll             # Native mpv C plugin (+ runtime DLLs if not in PATH)

Portable Mode (any directory containing tuitube_portable_mode.txt)
├── tuitube / tuitube.exe           # Standalone app (or tuitube.so / tuitube.dll plugin)
├── tuitube_portable_mode.txt       # Empty marker file enabling portable mode
├── tuitube.json                    # Config, favorites, and history live beside binary
├── favlist.json
├── history.json
├── yt-dlp / yt-dlp.exe             # Optional local yt-dlp (and mpv.exe on Windows)
├── cache/thumbs/                   # Local thumbnail cache
└── downloads/                      # Default download folder (when download_dir is "")
```

---

## Configuration & Keybindings

Release archives include a starter `tuitube.json` beside the executable. See **[docs/CONFIGURATION.md](docs/CONFIGURATION.md)** for the full 27-key reference, `yt-dlp` placement & lookup order, locale/region behavior, portable mode, custom themes, and `key_bindings` documentation.

Starter files: [examples/tuitube.json](examples/tuitube.json) · [examples/sample_theme.json](examples/sample_theme.json)

| Key | Purpose | Default |
| :--- | :--- | :--- |
| `language` | UI/YouTube metadata language (`"auto"` detects OS locale, or `"vi"`, `"en"`, `"ja"`, `"zh-TW"`, etc.) | `"auto"` |
| `region` | Optional 2-letter country override (`"US"`, `"VN"`, `"JP"`, etc.) | `""` |
| `player_path` | Standalone external player (`"mpv"`, `"vlc"`, or full path) | `"mpv"` |
| `ytdlp_path` | Custom `yt-dlp` binary or directory (`""` auto-detects config dir, app dir, and `PATH`) | `""` |
| `video_format` | Quality/codec format (empty = `"1080p60"` standalone / `mpv.conf` in plugin) | `""` |
| `sub_language` | Subtitle language (`--slang`; empty = `mpv.conf` pass-through in plugin) | `""` |
| `theme` | `"dark"`, `"light"`, `"yuuka"`, or `"custom"` (with `theme_path`) | `"dark"` |
| `download_dir` | Download folder for `Ctrl+S` (empty = system Downloads) | `""` |

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

## Project Structure

```text
TuiTube/
├── src/
│   ├── main.c                      # Standalone CLI & window entrypoint (./tuitube / tuitube.exe)
│   ├── mpv_plugin.c                # Native mpv C plugin (tuitube.so / tuitube.dll)
│   ├── mpv_cplugin.h               # Self-contained libmpv C plugin ABI declarations
│   ├── app.{h,c}                   # Shared app controller, async dispatch, NavStack & history
│   ├── ui.{h,c}                    # Cairo/PangoCairo UI, virtualized list, themes & fonts
│   ├── wayland.{h,c}               # Linux Wayland backend (wl_shm, xdg-shell, IME, clipboard)
│   ├── win32.c                     # Windows Win32 backend (HWND, DIBSection, DPI, IMM32)
│   ├── innertube.{h,c}             # InnerTube v1 API, RSS feed, URL parser & yt-dlp fallback
│   ├── player.{h,c}                # Unified yt-dlp resolver, mpv/vlc launcher & downloader
│   ├── config.{h,c}                # tuitube.json parser, locale detection, themes & keybindings
│   ├── image.{h,c}                 # JPEG/PNG decoding, disk & LRU thumbnail cache, mascot
│   ├── http.{h,c}                  # libcurl wrappers & 4-thread priority HttpPool
│   ├── model.{h,c}                 # MediaItem, FavoriteList & HistoryList persistence
│   ├── util.{h,c}                  # Path/UTF-8/Win32 helpers, StrBuf & process spawning
│   └── tt_keysyms.h                # Cross-platform XKB keysym constants
├── tests/
│   └── test_runner.c               # Offline unit test suite (ASan + UBSan)
├── docs/
│   ├── CONFIGURATION.md            # Binary placement matrix, 27 config keys, themes & keys
│   ├── ARCHITECTURE.md             # Module map and concurrency model
│   └── UPSTREAM_MAINTENANCE.md     # Guide for fixing YouTube / yt-dlp / mpv changes
├── examples/
│   ├── tuitube.json                # Starter configuration file
│   └── sample_theme.json           # Sample custom theme JSON
├── .github/workflows/
│   ├── ci.yml                      # Linux & Windows CI build, lint, and test workflow
│   └── release.yml                 # Prebuilt release archive & checksum workflow
├── Makefile                        # Auto-detects Linux vs Windows (MSYS2 UCRT64)
├── CONTRIBUTING.md
├── LICENSE
└── README.md
```

---

## Documentation & Maintenance

- [docs/CONFIGURATION.md](docs/CONFIGURATION.md) — Config lookup order, portable mode, all `tuitube.json` keys, custom themes, and keybindings.
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
