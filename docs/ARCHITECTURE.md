# TuiTube Architecture

TuiTube is organized as a small set of decoupled C17 modules:

## Module Map

| Module | Responsibility |
| :--- | :--- |
| `src/util.{h,c}` | Dynamic string buffer (`StrBuf`), UTF-8 codepoint boundary helpers, Base64/URL encoding, XML entity unescaping, ISO-8601 UTC & age parsers, XDG/portable path resolution, atomic file writes. |
| `src/config.{h,c}` | `TuiTubeConfig`, built-in palettes (`yuuka`, `dark`, `light`), JSON config & custom theme loading (with comments & trailing commas), and XKB keybinding parser/matcher. |
| `src/model.{h,c}` | `MediaItem`, `MediaList`, `FavoriteList` (`favlist.json`), and `HistoryList` (`history.json`, capped at 500 deduplicated entries). |
| `src/http.{h,c}` | `libcurl` GET/POST wrappers and a 4-thread priority `HttpPool` (interactive API jobs take priority over `JOB_THUMBNAIL`) with generation-based cancellation and a non-blocking notification `pipe`. |
| `src/innertube.{h,c}` | YouTube InnerTube v1 (`search`, `browse`, `next`, `navigation/resolve_url`), protobuf+Base64 search filter encoding, dual legacy/`lockupViewModel` JSON parsers, `yt-dlp --flat-playlist` fallback, URL/handle detection, and Atom RSS feed parsing. |
| `src/image.{h,c}` | Safe `libjpeg` (`setjmp`) and `libpng` decoding to premultiplied `CAIRO_FORMAT_ARGB32`, disk thumbnail cache, 256-entry LRU `cairo_surface_t` cache, and mascot loader. |
| `src/player.{h,c}` | `video_format` to `yt-dlp` selector mapping, non-blocking `fork`+`setsid`+`execvp` launcher for `mpv`/`vlc` with `CLOEXEC` error pipe and `SIGCHLD` reaping, plus `wl-copy` helper. |
| `src/wayland.{h,c}` | Wayland globals, grow/shrink single-idle `wl_shm` Cairo surfaces, `wp_fractional_scale_v1`/`wp_viewporter` + `wl_surface.preferred_buffer_scale` scaling, `wl_surface.frame` VSync throttling, `WL_SHM_FORMAT_XRGB8888`/opaque-region fast path, `xdg_toplevel` + `zxdg_decoration_manager_v1` SSD/CSD negotiation, `wl_seat` pointer/cursors + `xkbcommon`/`timerfd` keyboard repeat, `wl_data_device` clipboard, and `poll()` loop. |
| `src/ui.{h,c}` | Cairo + PangoCairo renderer, Fontconfig custom TTF loader, search box, collapsible filter bar, resizable split pane, virtualized thumbnail list + scrollbar, video detail/description/mascot pane, and status bar. |
| `src/app.{h,c}` | Shared pure-C17 application controller (async search/browse/next/detail/resolve/feed/thumbnail dispatch, `NavStack` management, favorites/history persistence, download polling, and pluggable video playback hook). |
| `src/mpv_cplugin.h` | Self-contained ISC-compatible `libmpv` client ABI header (uses `<mpv/client.h>` when present, with exact C ABI fallback declarations when `libmpv-dev` is absent). |
| `src/mpv_plugin.c` | Native `mpv` C plugin (`mpv_open_cplugin`), `WaylandApp` host shim, `memfd` `CAIRO_FORMAT_ARGB32` `overlay-add` renderer, exclusive `define-section` input & on-demand `input-ime` bridge, and `loadfile` host playback. |
| `src/main.c` | Standalone Wayland entrypoint: CLI argument parsing (`--config-dir`, `--help`), signal setup, Wayland window creation, and event loop delegating to `src/app.{h,c}`. |

## Concurrency Model

- **Main Thread:** Owns Wayland state, `UiState`, `ThumbCache`, `FavoriteList`, and `HistoryList`. Never blocks on network I/O.
- **Worker Threads (4):** Own thread-local `CURL *` handles, decode images into raw `ImageBuffer` structs (never touching Cairo surfaces off-thread), and push completed results to the mutex-guarded queue before signaling the event-loop pipe.
- **Generation Counter:** Rapid searches or clicks increment `list_gen` / `detail_gen`, allowing stale responses to be dropped cleanly.
