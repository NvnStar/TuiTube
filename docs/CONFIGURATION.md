# TuiTube Configuration Reference

TuiTube is configured through a JSON file named `tuitube.json` (plus optional custom theme JSON files). Both the standalone app (`./tuitube` / `tuitube.exe`) and the `mpv` C plugin (`./tuitube.so` / `tuitube.dll`) read the same configuration format and share favorites/history state by default.

---

## 1. Configuration Lookup & Portable Mode

At startup, TuiTube resolves its active directory (`config_dir`) and loads `tuitube.json` using the following precedence:

1. **Explicit CLI / Plugin Override (`cli_config_dir`):**
   - **Standalone:** `tuitube --config-dir /path/to/dir` (or `C:\path\to\dir` on Windows).
   - **`mpv` Plugin:** `mpv --script-opts=tuitube-config_dir=/path/to/dir` (also accepts `tuitube_config_dir=...`, `~/...` home expansion, and mpv `%<len>%...` length-prefixed values).
2. **Portable Mode (`tuitube_portable_mode.txt`):**
   - If an empty file named `tuitube_portable_mode.txt` exists in the same directory as the binary or plugin (`<exe_dir>`), TuiTube stores `tuitube.json`, `favlist.json`, `history.json`, thumbnail cache (`<exe_dir>/cache/thumbs`), and default downloads (`<exe_dir>/downloads`) inside `<exe_dir>`.
   - Release archives intentionally do **not** include `tuitube_portable_mode.txt` so extracting updates does not overwrite user data or fail in read-only install directories (e.g. `C:\Program Files`, `/opt`).
3. **Standard User Config Directory:**
   - **Linux:** `$XDG_CONFIG_HOME/tuitube/tuitube.json` (if `XDG_CONFIG_HOME` is set) or `~/.config/tuitube/tuitube.json`.
   - **Windows:** `$XDG_CONFIG_HOME/tuitube/tuitube.json` (if set), `%APPDATA%\tuitube\tuitube.json`, or `%USERPROFILE%\AppData\Roaming\tuitube\tuitube.json`.
4. **Standalone `<exe_dir>/tuitube.json` Fallback:**
   - When no `--config-dir` override and no `tuitube_portable_mode.txt` are active, and `<config_dir>/tuitube.json` does not exist yet, standalone TuiTube falls back to `<exe_dir>/tuitube.json` (such as the starter `tuitube.json` shipped beside `tuitube` / `tuitube.exe`).
   - `favlist.json` and `history.json` still save to the standard `<config_dir>` so user data stays safe across upgrades.
   - Relative asset paths (`theme_path`, `font`, `mascot`) also fall back to `<exe_dir>/<relative_path>` when not found in `<config_dir>`.
5. **Built-in Defaults:** If no `tuitube.json` exists anywhere, built-in defaults are used cleanly.

### State, Cache, and Download Locations

| Artifact | Standard Mode (Linux) | Standard Mode (Windows) | Portable Mode |
| :--- | :--- | :--- | :--- |
| Config, Favorites, History | `~/.config/tuitube/{tuitube.json,favlist.json,history.json}` | `%APPDATA%\tuitube\{tuitube.json,favlist.json,history.json}` | `<exe_dir>/{tuitube.json,favlist.json,history.json}` |
| Thumbnail Cache | `$XDG_CACHE_HOME/tuitube/thumbs` or `~/.cache/tuitube/thumbs` | `%LOCALAPPDATA%\tuitube\thumbs` | `<exe_dir>/cache/thumbs` |
| Default Downloads (`download_dir: ""`) | `$XDG_DOWNLOAD_DIR` or `~/Downloads` | `%USERPROFILE%\Downloads` | `<exe_dir>/downloads` |

---

## 2. Runtime Binaries (`yt-dlp` & `mpv`) Placement & Lookup

TuiTube does not bundle `mpv` or `yt-dlp`. Both standalone and `mpv` plugin targets share a unified `yt-dlp` resolver across startup checks, video playback (`--script-opts-append=ytdl_hook-ytdl_path=...` for standalone `mpv` and file-local `script-opts-append` for the `mpv` plugin), `Ctrl+S` background downloads, and `yt-dlp` search fallback.

### Placement Matrix

| Target | System `PATH` / Package Manager | App / Host Directory | TuiTube Config Directory | Explicit `"ytdlp_path"` in `tuitube.json` |
| :--- | :--- | :--- | :--- | :--- |
| **Linux Standalone (`./tuitube`)** | `yt-dlp` & `mpv` in `PATH` (`pacman`, `apt`, `~/.local/bin`) | `./yt-dlp` beside `./tuitube` (`chmod +x yt-dlp`) | `~/.config/tuitube/yt-dlp` (`chmod +x`) | Custom file, dir, `~/...`, relative path, or binary name |
| **Windows Standalone (`tuitube.exe`)** | `yt-dlp.exe` & `mpv.exe` in `PATH`, WinGet, Scoop, or Chocolatey | `yt-dlp.exe` & `mpv.exe` beside `tuitube.exe` | `%APPDATA%\tuitube\yt-dlp.exe` | Custom `.exe`, dir, relative path, or binary name |
| **Linux `mpv` Plugin (`tuitube.so`)** | Host `mpv` uses `yt-dlp` in `PATH` (respects `mpv.conf`) | `<plugin_dir>/yt-dlp` when portable mode is active | `~/.config/tuitube/yt-dlp` (`chmod +x`) | Forwarded to host `mpv` via `ytdl_hook-ytdl_path` |
| **Windows `mpv` Plugin (`tuitube.dll`)** | Host `mpv.exe` uses `yt-dlp.exe` in `PATH`, WinGet, Scoop, or Chocolatey | `yt-dlp.exe` beside `mpv.exe` (or `<plugin_dir>\yt-dlp.exe` in portable mode) | `%APPDATA%\tuitube\yt-dlp.exe` | Forwarded to host `mpv.exe` via `ytdl_hook-ytdl_path` |

### Exact `yt-dlp` Lookup Order

1. **Explicit `"ytdlp_path"` in `tuitube.json` (when non-empty):**
   - Strictly resolves the configured target (`~` / `~/...`, absolute file or directory, relative path against `<config_dir>` then `<exe_dir>`, or bare executable name).
   - **Never silently falls back** to a different system `yt-dlp` if the configured target is missing or non-executable.
2. **TuiTube Config Directory (`<config_dir>`):**
   - Checks `~/.config/tuitube/yt-dlp` (Linux) or `%APPDATA%\tuitube\yt-dlp.exe` / `.com` (Windows), or `<exe_dir>` in portable mode.
3. **Standalone App Directory (`<exe_dir>`):**
   - Checks `yt-dlp` (`chmod +x`) beside `./tuitube` on Linux (prioritized before `PATH` when `<exe_dir>` is not already on `PATH`) or `yt-dlp.exe` beside `tuitube.exe` on Windows.
4. **System `PATH` & Windows Package Managers:**
   - Searches `PATH`, plus Windows Registry `PATH`, WinGet (`WinGet\Links` and `WinGet\Packages`), Scoop (`scoop\shims` and `scoop\apps\yt-dlp\current\yt-dlp.exe`), and Chocolatey (`chocolatey\bin`).
   - When `ytdlp_path` is `""` and `yt-dlp` is found directly on `PATH`, TuiTube does **not** inject `ytdl_hook-ytdl_path`, preserving your `mpv.conf` / `ytdl_hook.conf` settings. When `yt-dlp` is found only in a local/non-`PATH` location, TuiTube passes `ytdl_hook-ytdl_path=<resolved>` automatically.
5. **Sibling Directory of `player_path`:**
   - Resolves `player_path` (default `mpv`) and checks for `yt-dlp` / `yt-dlp.exe` in that player's directory.

### Quick Recovery When YouTube Breaks Distro `yt-dlp`

If a distribution package becomes outdated and YouTube stream extraction fails, drop the latest official standalone binary into TuiTube's config directory (no `PATH` changes or root privileges needed):

```bash
# Linux (standalone ./tuitube and mpv plugin tuitube.so)
mkdir -p ~/.config/tuitube
curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux -o ~/.config/tuitube/yt-dlp
chmod +x ~/.config/tuitube/yt-dlp
```

```powershell
# Windows (standalone tuitube.exe and mpv plugin tuitube.dll)
New-Item -ItemType Directory -Force "$env:APPDATA\tuitube" | Out-Null
Invoke-WebRequest https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe -OutFile "$env:APPDATA\tuitube\yt-dlp.exe"
```

Alternatively, save the binary anywhere and point `"ytdlp_path"` in `tuitube.json` to that file or directory.

---

## 3. JSON Syntax

`tuitube.json` and custom theme files are parsed with `yyjson` using relaxed JSON flags:

- Single-line comments (`// ...`) and block comments (`/* ... */`) are allowed.
- Trailing commas in objects and arrays are allowed.

---

## 4. Complete `tuitube.json` Option Reference

All 27 configuration keys are optional; omitted keys keep their built-in defaults.

| Key | Type | Default | Valid Values / Clamping | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `language` | `string` | `"auto"` | `"auto"` or BCP-47 / POSIX locale tag (e.g. `"vi"`, `"en"`, `"en-GB"`, `"ja"`, `"ko"`, `"zh-CN"`, `"zh-TW"`, `"zh-HK"`, `"pt-BR"`, `"pt-PT"`, `"es-419"`) | `"auto"` detects OS locale (`GetUserDefaultLocaleName` on Windows; `LC_ALL` / `LC_MESSAGES` / `LANG` on POSIX, fallback `"en"` / `"US"`). Wired into InnerTube `context.client.hl` and `Accept-Language`. |
| `region` | `string` | `""` | `""`, `"auto"`, or 2-letter ISO-3166 country code (e.g. `"US"`, `"VN"`, `"JP"`) | Overrides `context.client.gl`. When empty or `"auto"`, `gl` is derived from `language` or the OS locale. |
| `scale` | `number` | `0.0` | `0.0` (auto) or `0.5`–`3.0` (parsed up to `8.0`, clamped by UI scale to `[0.5, 3.0]`) | `0.0` uses compositor/DPI scaling (`1.0` fallback). |
| `font` | `string` | `""` | Font family name or `.ttf`/`.otf`/`.ttc` file path | Empty uses the platform sans-serif font. Relative paths check `<config_dir>` then `<exe_dir>`. |
| `font_size` | `integer` | `13` | Clamped `[8, 48]` | Base UI font size in logical pixels. |
| `thumb_size` | `integer` | `72` | Clamped `[36, 240]` | Thumbnail row height in logical pixels (16:9 width derived automatically). |
| `playlist_width` | `integer` | `380` | Clamped `[200, 1200]` | Width of the video list pane in logical pixels (`Ctrl+]` / `Ctrl+[` adjusts interactively). |
| `player_path` | `string` | `"mpv"` | Executable name or path (e.g. `"mpv"`, `"mpv.exe"`, `"vlc"`, `"C:\\mpv\\mpv.exe"`) | **Standalone only.** Ignored by the `mpv` plugin (plays directly in host `mpv`). |
| `ytdlp_path` | `string` | `""` | `""` (auto-detect), `~`/`~/...`, file path, directory path (auto-appends `yt-dlp` / `yt-dlp.exe`), relative path (`<config_dir>` then `<exe_dir>`), or binary name. On Windows JSON, use forward slashes `"C:/Tools/yt-dlp.exe"` or escaped backslashes `"C:\\Tools\\yt-dlp.exe"`. | Used by dependency checks, standalone `mpv` (`--script-opts-append=ytdl_hook-ytdl_path=...`), `mpv` plugin (`loadfile` file-local `script-opts-append`), `Ctrl+S` downloads, and `yt-dlp` search fallback. Non-empty values are strict (no silent fallback to system `yt-dlp` on error). |
| `video_format` | `string` | `""` *(starter)* / `"1080p60"` *(internal)* | Shorthand like `"1080p60"`, `"720p_vp9"`, `"2160p60_av1"`, or raw `yt-dlp` format string containing `[` or `/` | **Standalone:** empty falls back to `"1080p60"`. **`mpv` plugin:** empty omits `--ytdl-format` so `mpv.conf` applies; non-empty overrides host `ytdl-format`. |
| `allow_hdr` | `boolean` | `false` | `true` / `false` | When `false`, shorthand `video_format` appends `[dynamic_range!=HDR]`. |
| `all_codecs` | `boolean` | `false` | `true` / `false` | When `false`, shorthand `video_format` prefers `avc1` (or `_vp9` / `_av1` suffix) with fallback to any codec. |
| `sub_language` | `string` | `""` *(starter)* / `"en"` *(internal)* | Language list like `"en"`, `"vi,en"`, `"ja,en"`, or `""` | **Standalone:** non-empty passes `--slang=...`; empty disables `--slang` (omitting key defaults to `"en"`). **`mpv` plugin:** empty respects `mpv.conf`. |
| `theme` | `string` | `"dark"` | `"dark"`, `"light"`, `"yuuka"`, `"custom"` | `"custom"` loads colors from `theme_path` (falls back to `"dark"` on error). |
| `theme_path` | `string` | `""` | Path to custom theme JSON | Used when `"theme": "custom"`. Relative paths check `<config_dir>` then `<exe_dir>`. |
| `mascot` | `string` | `""` | Path to PNG/JPEG image | Optional mascot artwork displayed on the empty detail view. Relative paths check `<config_dir>` then `<exe_dir>`. |
| `single_click_select` | `boolean` | `false` | `true` / `false` | Activate (play video / open channel or playlist) on single click instead of double click. |
| `show_extra_metadata` | `boolean` | `false` | `true` / `false` | Show additional metadata (view counts, publish dates) in list rows. |
| `infinite_scroll` | `boolean` | `false` | `true` / `false` | Automatically fetch the next page when scrolling near the end of a list. |
| `save_history` | `boolean` | `true` | `true` / `false` | Persist watched videos to `history.json`. |
| `dim_watched_videos` | `boolean` | `true` | `true` / `false` | Dim list rows for videos present in watch history. |
| `feed_use_rss` | `boolean` | `true` | `true` / `false` | `true` uses fast YouTube Atom RSS feeds (max 15 items/channel); `false` uses InnerTube `browse` (enables `hide_members_only` in Feed). |
| `max_feed_videos_per_channel` | `integer` | `15` | Clamped `[1, 100]` | Maximum items fetched per favorite channel/playlist for Subscription Feed. |
| `max_feed_age` | `string` | `"1 month"` | `"<N> hour(s)\|day(s)\|week(s)\|month(s)\|year(s)"` | Filter out feed videos older than this age. |
| `hide_members_only` | `boolean` | `false` | `true` / `false` | Also accepts alias `"hide_member_videos"`. Hides channel-members-only videos in search/browse/recommended (and Feed when `feed_use_rss: false`). |
| `download_dir` | `string` | `""` | Directory path (`~`, `~/...`, absolute, or relative to `config_dir`) | Target folder for `yt-dlp` downloads (`Ctrl+S`). |
| `key_bindings` | `array` | `[]` *(appends over defaults)* | Array of `{"action", "mod", "key"}` objects | Custom bindings override earlier/default bindings on matching `mod` + `key`. |

### Note on Language Localization (`language` / `hl`)

YouTube's InnerTube API returns localized video titles and descriptions according to the request's `context.client.hl` and `Accept-Language` header. Because YouTube does not provide an unauthenticated *"original uploader language only"* parameter, videos that have creator-provided or auto-translated metadata will match your configured `"language"`:
- `"language": "auto"` (default) aligns YouTube results with your OS language (e.g., Vietnamese `"vi"` / `"VN"`, Japanese `"ja"` / `"JP"`, English `"en"` / `"US"`).
- To force a consistent language regardless of OS settings, set `"language": "en"` or `"language": "vi"`, etc.

---

## 5. Custom Themes (`theme_path` + `"theme": "custom"`)

Set `"theme": "custom"` and `"theme_path": "my_theme.json"` in `tuitube.json`. A custom theme JSON defines up to 13 `ThemePair` entries (see [examples/sample_theme.json](../examples/sample_theme.json)).

Each entry is an 8-element integer array `[fg_r, fg_g, fg_b, fg_a, bg_r, bg_g, bg_b, bg_a]` with channel values clamped to `0`–`255`:

- `base` — Window/panel background & secondary labels
- `text_normal`, `text_hover`, `text_pressed`, `text_selected` — Text & list item states
- `elem_normal`, `elem_hover`, `elem_pressed`, `elem_selected` — Interactive controls & buttons
- `scrollbar_normal`, `scrollbar_hover`, `scrollbar_pressed`, `scrollbar_selected` — Scrollbar track/thumb states

Any omitted keys fall back to the built-in `"dark"` theme.

---

## 6. Custom Keybindings (`key_bindings`)

Each entry in `"key_bindings"` has the form:

```json
{ "action": "show_feed", "mod": "CTRL-SHIFT", "key": "F" }
```

### Supported `action` Names (24)

- **App & Playback:** `quit`, `play_video`, `download_video`
- **Navigation & Views:** `view_user_videos`, `view_recommended_videos`, `next_page`, `show_favorites`, `show_feed`, `show_history`, `navigate_back`, `focus_search_input`, `focus_playlist`, `toggle_help`
- **Favorites & Clipboard:** `add_favorites`, `copy_video_url`, `copy_video_title`, `copy_video_description`
- **Themes & Cache:** `set_dark_theme`, `set_light_theme`, `set_yuuka_theme`, `set_custom_theme`, `clear_thumbnail_cache`
- **Layout:** `inc_list_size`, `dec_list_size`

### Modifiers (`mod`)

Combine zero or more of `CTRL`, `SHIFT`, `ALT`, `SUPER` (case-insensitive), separated by `-`, `+`, or spaces (for example `""`, `"CTRL"`, `"CTRL-ALT"`, `"Ctrl+Shift"`).

### Supported Key Strings (`key`)

- **Letters & Digits:** `"A"`–`"Z"` (or `"a"`–`"z"`), `"0"`–`"9"`
- **Function Keys:** `"F1"`–`"F12"`
- **Named Keys (case-insensitive):** `"SPACE"`, `"ESCAPE"`, `"ENTER"`, `"RETURN"`, `"TAB"`, `"BACKSPACE"`, `"INSERT"`, `"DELETE"`, `"LEFT"`, `"RIGHT"`, `"UP"`, `"DOWN"`, `"PAGEUP"`, `"PAGEDOWN"`, `"HOME"`, `"END"`
- **Punctuation:** `"'"`, `","`, `"-"`, `"."`, `"/"`, `"?"`, `";"`, `"="`, `"["`, `"]"`, `"\\"`, ``"`"``
