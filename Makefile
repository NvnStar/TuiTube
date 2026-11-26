CC ?= gcc
WAYLAND_SCANNER ?= wayland-scanner

PKG_DEPS := wayland-client wayland-cursor xkbcommon cairo pangocairo fontconfig libcurl yyjson libjpeg libpng
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LIBS := $(shell pkg-config --libs $(PKG_DEPS)) -lpthread -lm

PLUGIN_PKG_DEPS := xkbcommon cairo pangocairo fontconfig libcurl yyjson libjpeg libpng
PLUGIN_CFLAGS := -std=c17 -D_POSIX_C_SOURCE=200809L -Isrc $(shell pkg-config --cflags $(PLUGIN_PKG_DEPS)) -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Wpedantic
PLUGIN_LIBS := $(shell pkg-config --libs $(PLUGIN_PKG_DEPS)) -lpthread -lm -ldl

CFLAGS_BASE := -std=c17 -D_POSIX_C_SOURCE=200809L -Isrc -Ibuild/gen $(PKG_CFLAGS)
CFLAGS_RELEASE := $(CFLAGS_BASE) -O2 -Wall -Wextra -Wpedantic
CFLAGS_DEBUG := $(CFLAGS_BASE) -g -O1 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS_DEBUG := -fsanitize=address,undefined

XDG_SHELL_XML := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
XDG_DECO_XML := /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
TEXT_INPUT_XML := /usr/share/wayland-protocols/unstable/text-input/text-input-unstable-v3.xml
VIEWPORTER_XML := /usr/share/wayland-protocols/stable/viewporter/viewporter.xml
FRAC_SCALE_XML := /usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml

GEN_DIR := build/gen
GEN_HDRS := $(GEN_DIR)/xdg-shell-client-protocol.h $(GEN_DIR)/xdg-decoration-client-protocol.h $(GEN_DIR)/text-input-unstable-v3-client-protocol.h $(GEN_DIR)/viewporter-client-protocol.h $(GEN_DIR)/fractional-scale-v1-client-protocol.h
GEN_SRCS := $(GEN_DIR)/xdg-shell-client-protocol.c $(GEN_DIR)/xdg-decoration-client-protocol.c $(GEN_DIR)/text-input-unstable-v3-client-protocol.c $(GEN_DIR)/viewporter-client-protocol.c $(GEN_DIR)/fractional-scale-v1-client-protocol.c

CORE_SRCS := src/util.c src/config.c src/model.c src/http.c src/innertube.c src/image.c src/player.c
GUI_SRCS := src/app.c src/wayland.c src/ui.c src/main.c
ALL_SRCS := $(CORE_SRCS) $(GUI_SRCS)
PLUGIN_SRCS := $(CORE_SRCS) src/app.c src/ui.c src/mpv_plugin.c
TEST_SRCS := $(CORE_SRCS) tests/test_runner.c

REL_OBJS := $(patsubst %.c,build/release/%.o,$(ALL_SRCS) $(GEN_SRCS))
DBG_OBJS := $(patsubst %.c,build/debug/%.o,$(ALL_SRCS) $(GEN_SRCS))
PLG_OBJS := $(patsubst %.c,build/plugin/%.o,$(PLUGIN_SRCS))
TST_OBJS := $(patsubst %.c,build/test/%.o,$(TEST_SRCS))

.PHONY: all release plugin debug test lint clean

all: release

release: tuitube tuitube.so

plugin: tuitube.so

debug: tuitube-debug

tuitube: $(REL_OBJS)
	$(CC) $(CFLAGS_RELEASE) -o $@ $^ $(PKG_LIBS)

tuitube.so: $(PLG_OBJS)
	$(CC) $(PLUGIN_CFLAGS) -shared -o $@ $^ $(PLUGIN_LIBS)

tuitube-debug: $(DBG_OBJS)
	$(CC) $(CFLAGS_DEBUG) $(LDFLAGS_DEBUG) -o $@ $^ $(PKG_LIBS)

build/test_runner: $(TST_OBJS)
	$(CC) $(CFLAGS_DEBUG) $(LDFLAGS_DEBUG) -o $@ $^ $(PKG_LIBS)

test: build/test_runner tuitube.so
	./build/test_runner
	nm -D --defined-only ./tuitube.so | grep -q ' mpv_open_cplugin$$'
	! readelf -d ./tuitube.so | grep -qE 'NEEDED.*(libmpv|libwayland)'

lint: $(GEN_HDRS)
	$(CC) $(CFLAGS_BASE) -fsyntax-only -Wall -Wextra -Wpedantic -Werror $(ALL_SRCS) tests/test_runner.c
	$(CC) $(PLUGIN_CFLAGS) -fsyntax-only -Wall -Wextra -Wpedantic -Werror src/mpv_plugin.c

$(GEN_DIR)/xdg-shell-client-protocol.h: $(XDG_SHELL_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) client-header $< $@

$(GEN_DIR)/xdg-shell-client-protocol.c: $(XDG_SHELL_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) private-code $< $@

$(GEN_DIR)/xdg-decoration-client-protocol.h: $(XDG_DECO_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) client-header $< $@

$(GEN_DIR)/xdg-decoration-client-protocol.c: $(XDG_DECO_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) private-code $< $@

$(GEN_DIR)/text-input-unstable-v3-client-protocol.h: $(TEXT_INPUT_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) client-header $< $@

$(GEN_DIR)/text-input-unstable-v3-client-protocol.c: $(TEXT_INPUT_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) private-code $< $@

$(GEN_DIR)/viewporter-client-protocol.h: $(VIEWPORTER_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) client-header $< $@

$(GEN_DIR)/viewporter-client-protocol.c: $(VIEWPORTER_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) private-code $< $@

$(GEN_DIR)/fractional-scale-v1-client-protocol.h: $(FRAC_SCALE_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) client-header $< $@

$(GEN_DIR)/fractional-scale-v1-client-protocol.c: $(FRAC_SCALE_XML)
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) private-code $< $@

build/release/%.o: %.c $(GEN_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_RELEASE) -c -o $@ $<

build/debug/%.o: %.c $(GEN_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DEBUG) -c -o $@ $<

build/plugin/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PLUGIN_CFLAGS) -c -o $@ $<

build/test/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DEBUG) -c -o $@ $<

clean:
	rm -rf build tuitube tuitube-debug tuitube.so
