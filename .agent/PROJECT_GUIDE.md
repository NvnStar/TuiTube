# Project Development Guide

This repository contains **TuiTube**, a pure C17 native Wayland YouTube client.

## Repository Layout

- `src/` — Core C17 source modules (`util`, `config`, `model`, `http`, `innertube`, `image`, `player`, `wayland`, `ui`, `main`).
- `tests/` — Offline unit test suite (`test_runner.c`) compiled with ASan/UBSan.
- `docs/` — Architecture and upstream maintenance guides.
- `examples/` — Sample `tuitube.json` and custom theme JSON files.
- `Makefile` — Wayland protocol codegen (`wayland-scanner`) and build/lint/test targets.

## Coding Conventions

- Target C17 (`-std=c17 -D_POSIX_C_SOURCE=200809L`).
- Keep `make lint` (`-Wall -Wextra -Wpedantic -Werror`) clean with zero warnings.
- Never shell-interpolate user input or video titles; pass arguments via `execvp` argument arrays.
- Keep `src/wayland.c` and `src/ui.c` decoupled from `tests/test_runner.c` so core parsing/config/model/player logic remains testable headlessly without a Wayland compositor.
- Keep Cairo surface operations on the main thread; worker threads only decode into raw `ImageBuffer` pixels.

## Standard Verification Commands

```bash
make clean && make release && make debug && make lint && make test
```
