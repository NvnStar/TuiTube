# Contributing to TuiTube

Thank you for helping improve TuiTube!

## Development Standards

- **Language:** Pure C17 (`-std=c17 -D_POSIX_C_SOURCE=200809L`).
- **Compiler Cleanliness:** All code in `src/` and `tests/` must compile with **zero warnings** under `make lint` (`-Wall -Wextra -Wpedantic -Werror`).
- **Memory Safety:** Every change must pass `make test` under AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`).
- **No Unnecessary Dependencies:** Keep TuiTube free of GTK, Qt, Electron, or heavy runtime frameworks.

## Before Opening a Pull Request

Run the full verification suite locally:

```bash
make clean && make release && make debug && make lint && make test
```

## Updating YouTube Parsers

When YouTube updates InnerTube JSON structures (such as adding new `lockupViewModel` variants or changing renderer fields):

1. Capture a minimal, sanitized JSON snippet demonstrating the new payload.
2. Update the relevant parser in `src/innertube.c`.
3. Add an offline regression test in `tests/test_runner.c` covering both the previous and new payload shapes so existing support never regresses.
4. Update `docs/UPSTREAM_MAINTENANCE.md` if new endpoints, headers, or `clientVersion` values are required.
