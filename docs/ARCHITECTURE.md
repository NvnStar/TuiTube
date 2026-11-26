# TuiTube Architecture

TuiTube is organized as a small set of decoupled C17 modules:

## Module Map

| Module | Responsibility |
| :--- | :--- |
| `src/tt_keysyms.h` | Cross-platform `xkb_keysym_t` and `XKB_KEY_*` constants (delegating to `<xkbcommon/...>` on POSIX and providing exact self-contained hex values on `_WIN32`). |
| `src/util.{h,c}` | Dynamic string buffer (`StrBuf`), UTF-8 codepoint boundary helpers, UTF-8/UTF-16 helpers (`_WIN32`), Base64/URL encoding, XML entity unescaping, ISO-8601 UTC & age parsers, XDG / `%APPDATA%` / portable path resolution, atomic file writes, Win32 argv quoting (`tt_win32_quote_arg`), OpenType/TrueType `name` family parser (`tt_ttf_read_family_name`), and cross-platform `tt_spawn_capture_stdout`. |
| `src/config.{h,c}` | `TuiTubeConfig`, built-in palettes (`yuuka`, `dark`, `light`), JSON config & custom theme loading (with comments & trailing commas), and XKB keybinding parser/matcher. |
| `src/model.{h,c}` | `MediaItem`, `MediaList`, `FavoriteList` (`favlist.json`), and `HistoryList` (`history.json`, capped at 500 deduplicated entries). |
| `src/http.{h,c}` | `libcurl` GET/POST wrappers and a 4-thread priority `HttpPool` (interactive API jobs take priority over `JOB_THUMBNAIL`) with generation-based cancellation and a non-blocking notification `pipe` (POSIX) or manual-reset `HANDLE` event (`_WIN32`). |
| `src/innertube.{h,c}` | YouTube InnerTube v1 (`search`, `browse`, `next`, `navigation/resolve_url`), protobuf+Base64 search filter encoding, dual legacy/`lockupViewModel` JSON parsers, `yt-dlp --flat-playlist` fallback via `tt_spawn_capture_stdout`, URL/handle detection, and Atom RSS feed parsing. |
| `src/image.{h,c}` | Safe `libjpeg` (`setjmp`) and `libpng` decoding to premultiplied `CAIRO_FORMAT_ARGB32`, disk thumbnail cache, 256-entry LRU `cairo_surface_t` cache, and mascot loader. |
| `src/player.{h,c}` | `video_format` to `yt-dlp` selector mapping, non-blocking `mpv`/`vlc` launcher and `yt-dlp` downloader (`fork`+`execvp`+`SIGCHLD` on POSIX; `CreateProcessW`+`PeekNamedPipe`+`WaitForSingleObject` on `_WIN32`), plus `wl-copy` / `CF_UNICODETEXT` clipboard fallback. |
| `src/wayland.{h,c}` | Wayland backend (`src/wayland.c`): globals, grow/shrink single-idle `wl_shm` Cairo surfaces, `wp_fractional_scale_v1`/`wp_viewporter`, `wl_surface.frame` VSync throttling, `xdg_toplevel` + SSD/CSD negotiation, `wl_seat` + `xkbcommon`, `text-input-v3` IME, `wl_data_device` clipboard, and `poll()` loop. |
| `src/win32.c` | Native Win32 `WaylandApp` backend: Per-Monitor-DPI-v2 `HWND`, dark titlebar, top-down BGRA `DIBSection` Cairo surface, `MsgWaitForMultipleObjects` loop, mouse/cursors/`WM_KEYDOWN`+`WM_CHAR` keyboard, IMM32 IME, and `CF_UNICODETEXT` clipboard. |
| `src/ui.{h,c}` | Cairo + PangoCairo renderer, Fontconfig (POSIX) / `AddFontResourceExW`+`tt_ttf_read_family_name` (`_WIN32`) custom TTF loader, search box, collapsible filter bar, resizable split pane, virtualized thumbnail list + scrollbar, video detail/description/mascot pane, and status bar. |
| `src/app.{h,c}` | Shared pure-C17 application controller (async search/browse/next/detail/resolve/feed/thumbnail dispatch, `NavStack` management, favorites/history persistence, download polling, and pluggable video playback hook). |
| `src/mpv_cplugin.h` | Self-contained ISC-compatible `libmpv` client ABI header (uses `<mpv/client.h>` when present, with exact C ABI fallback declarations and `MPV_CPLUGIN_DYNAMIC_SYM` `pfn_mpv_*` exports on `_WIN32`). |
| `src/mpv_plugin.c` | Native `mpv` C plugin (`mpv_open_cplugin`), `WaylandApp` host shim, `memfd` `@<fd>` (POSIX) / `VirtualAlloc` `&<uintptr_t>` (`_WIN32`) `CAIRO_FORMAT_ARGB32` `overlay-add` renderer, exclusive `define-section` input & on-demand `input-ime` bridge, and `loadfile` host playback. |
| `src/main.c` | Standalone entrypoint (`./tuitube` / `tuitube.exe`): CLI argument parsing (`--config-dir`, `--help`), parent console attach on `_WIN32` CLI output, signal setup (POSIX), window creation, and event loop delegating to `src/app.{h,c}`. |

## Concurrency Model

- **Main Thread:** Owns UI/window state (`WaylandApp` / `mpv` host loop), `UiState`, `ThumbCache`, `FavoriteList`, and `HistoryList`. Never blocks on network I/O.
- **Worker Threads (4):** Own thread-local `CURL *` handles, decode images into raw `ImageBuffer` structs (never touching Cairo surfaces off-thread), and push completed results to the mutex-guarded queue before signaling the event-loop `pipe` (POSIX) or `CreateEventW` handle (`_WIN32`).
- **Generation Counter:** Rapid searches or clicks increment `list_gen` / `detail_gen`, allowing stale responses to be dropped cleanly.
