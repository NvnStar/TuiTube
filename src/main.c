#include "app.h"
#include "http.h"
#include "player.h"
#include "ui.h"
#include "wayland.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

static void attach_parent_console(void) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
    }
}
#else
#include <signal.h>

static volatile sig_atomic_t g_sig_stop = 0;

static void handle_stop_sig(int sig) {
    (void)sig;
    g_sig_stop = 1;
}
#endif

static void print_help(const char *prog) {
#ifdef _WIN32
    const char *tagline = "Lightweight native Windows YouTube client.";
#else
    const char *tagline = "Lightweight native Wayland YouTube client.";
#endif
    printf("Usage: %s [OPTIONS]\n\n"
           "%s\n\n"
           "Options:\n"
           "  --config-dir <dir>   Override configuration directory\n"
           "  -h, --help           Show this help message and exit\n",
           prog, tagline);
}

static bool standalone_play_video(void *play_ctx, const TuiTubeConfig *cfg,
                                  const MediaItem *video,
                                  char *err_buf, size_t err_cap) {
    (void)play_ctx;
    return player_launch(cfg, video, err_buf, err_cap);
}

static void wl_tramp_render(void *ctx, cairo_t *cr, int w, int h) {
    AppState *app = ctx;
    if (app->ui) ui_on_render(app->ui, cr, w, h);
}
static void wl_tramp_resize(void *ctx, int w, int h) {
    AppState *app = ctx;
    if (app->ui) ui_on_resize(app->ui, w, h);
}
static void wl_tramp_motion(void *ctx, double x, double y) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_motion(app->ui, x, y);
}
static void wl_tramp_button(void *ctx, TtMouseButton btn, bool pressed, bool dbl, double x, double y) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_button(app->ui, btn, pressed, dbl, x, y);
}
static void wl_tramp_axis(void *ctx, double dx, double dy) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_axis(app->ui, dx, dy);
}
static void wl_tramp_leave(void *ctx) {
    AppState *app = ctx;
    if (app->ui) ui_on_pointer_leave(app->ui);
}
static void wl_tramp_key(void *ctx, uint32_t mods, xkb_keysym_t sym, const char *utf8, bool pressed) {
    AppState *app = ctx;
    if (app->ui) ui_on_key(app->ui, mods, sym, utf8, pressed);
}
static void wl_tramp_ime(void *ctx, const WaylandImeEvent *ev) {
    AppState *app = ctx;
    if (app->ui) ui_on_ime(app->ui, ev);
}

int main(int argc, char **argv) {
    const char *cli_config_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
#ifdef _WIN32
            attach_parent_console();
#endif
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--config-dir") == 0) {
            if (i + 1 >= argc) {
#ifdef _WIN32
                attach_parent_console();
#endif
                fprintf(stderr, "Error: --config-dir requires an argument\n");
                return 1;
            }
            cli_config_dir = argv[++i];
        } else {
#ifdef _WIN32
            attach_parent_console();
#endif
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 1;
        }
    }

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_stop_sig);
    signal(SIGTERM, handle_stop_sig);
#endif
    player_install_sigchld_handler();

    if (!http_global_init()) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        return 1;
    }

    AppState app = {0};

    WaylandCallbacks wl_cbs = {
        .on_render = wl_tramp_render,
        .on_resize = wl_tramp_resize,
        .on_pointer_motion = wl_tramp_motion,
        .on_pointer_button = wl_tramp_button,
        .on_pointer_axis = wl_tramp_axis,
        .on_pointer_leave = wl_tramp_leave,
        .on_key = wl_tramp_key,
        .on_ime = wl_tramp_ime,
    };

    char err[256] = {0};
    WaylandApp *wl = wayland_create(1024, 640, "TuiTube", &wl_cbs, &app, err, sizeof(err));
    if (!wl) {
        fprintf(stderr, "Error: %s\n", err[0] ? err : "Failed to initialize Wayland");
        http_global_cleanup();
        return 1;
    }

    if (!app_init(&app, cli_config_dir, wl, standalone_play_video, NULL, NULL)) {
        fprintf(stderr, "Error: failed to initialize application\n");
        wayland_destroy(wl);
        http_global_cleanup();
        return 1;
    }

#ifdef _WIN32
    void *notify_handle = app_notify_handle(&app);
    while (!wayland_should_quit(wl)) {
        app_poll_downloads(&app);
        wayland_render_if_needed(wl);
        bool extra_ready = false;
        int pr = win32_poll_once(wl, notify_handle, 250, &extra_ready);
        if (pr < 0) break;
        if (extra_ready) {
            app_process_async_results(&app);
        }
        app_poll_downloads(&app);
    }
#else
    int notify_fd = app_notify_fd(&app);
    while (!g_sig_stop && !wayland_should_quit(wl)) {
        app_poll_downloads(&app);
        wayland_render_if_needed(wl);
        bool extra_ready = false;
        int pr = wayland_poll_once(wl, notify_fd, 250, &extra_ready);
        if (pr < 0) break;
        if (extra_ready) {
            app_process_async_results(&app);
        }
        app_poll_downloads(&app);
    }
#endif

    app_destroy(&app);
    wayland_destroy(wl);
    http_global_cleanup();
    return 0;
}
