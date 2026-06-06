# Build and release

## Operating model

Use Ubuntu/WSL or Linux for normal development and packaging. On Windows hosts,
`mise run` hands task bodies to the default WSL distribution so the shared
workflow still runs with Linux tools and POSIX paths.

Shared workflows are exposed through `mise` task names. Normal
configure/build/package flows run through `CMakePresets.json`.

## Common workflows

Initial setup

```shellell
mise install
mise run host-setup
mise run check
mise run build-desktop
```

Package a TV build:

```shell
mise run sdk-install
mise run toolchain-check
mise run package-webos
```

Install on a target TV:

```shell
mise run device-provision
mise run device-check
mise run device-install
```

Stage release artifacts:

```shell
mise run release-prep
```

`device-provision` opens `ares-setup-device` and lets you manage aliases or set
the current ares default TV target. `device-check` and `device-install` use that
current default directly and fail if it is unset or still points at
`emulator`.

## Other tasks

- `mise run check`: read-only formatting, lint, benchset validation, and tests.
- `mise run fmt`: rewrite owned source, docs, and config formatting.
- `mise run test`: run benchplan unit tests and desktop CTest.
- `mise run build-desktop`: build the desktop dummy benchmark.
- `mise run build-webos`: build the webOS target without packaging.
- `mise run package-webos`: refresh bundled media as needed, then write the IPK
  and matching Homebrew Channel manifest.
- `mise run device-provision`: manage ares aliases and set the active target.
- `mise run device-check`: verify the active target before install or run work.
- `mise run device-install`: install the current package on the active target.
- `mise run release-prep`: stage the current release attachments without
  forcing an external media refresh.
- `mise run bench-plan -- <selector>`: preview or validate a selector.
- `mise run bench-status -- <selector>`: report whether a cache is current,
  missing outputs, dirty, or orphaned.
- `mise run bench-prune -- <selector>`: remove stale generated bench artifacts
  under the selected cache.
- `mise run clean`: remove build output and local state while preserving the
  expensive generated fixture media cache.

For `bench-plan`, `bench-status`, `bench-gen`, and `bench-prune`, selector
`bundled` uses the managed bundled cache by default when `--out` is not set.
Use `--out` only for custom self-contained cache roots.

## Toolchain and dependencies

Managed by `mise`:

- Python 3.14
- Node.js 22 for the webOS CLI
- CMake, Ninja, formatters, linters, and repo utility tools
- webOS CLI

Installed by `mise run host-setup`:

- C/C++ build basics and `pkg-config`
- SDL2 development files for desktop dummy builds
- FFmpeg and FFprobe for generated benchmark fixtures

Installed by `mise run sdk-install`:

- the pinned webOSBrew native SDK release
- default path: `.local/webos-sdk/webos-b17b4cc/`
- override: `DECODER_BENCH_WEBOS_NDK`

Built from pinned upstream source submodules:

- `ss4s`
- `commons`
- `inih`
- `opus`

Normal CMake configure/build does not download dependencies. The webOS preset
uses `cmake/toolchains/webos-arm.cmake` and the target pkg-config wrapper under
`scripts/toolchain/`.

## Managed bench content

- Bundled fixture cache: `assets/.local/bench/bundled/`
- External fixture cache: `assets/.local/bench/external/`
- Managed generator state: `assets/.local/bench-state/{bundled,external}/`

`mise run package-webos` refreshes the bundled cache before packaging the app.
`mise run clean` removes build output, managed generator state, and local
script/test caches, but intentionally keeps `assets/.local/bench/` so expensive
generated fixtures survive cleanup.

Generated `.bench`, `.h264`, and `.hevc` files are intentionally ignored by
git. Prune tasks only remove generated bench artifacts inside the selected
fixture cache.

## Outputs

- Desktop build: `build/desktop/`
- webOS build and package staging: `build/webos/`
- Homebrew Channel manifest: `build/webos/io.github.beatcracker.decoderbench.manifest.json`
- Manual release attachments: `build/release/`

`mise run release-prep` stages three manual release attachments under
`build/release/`:

- the raw IPK
- the raw Homebrew Channel manifest
- a USB bench ZIP with a top-level `bench/` directory
