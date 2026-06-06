# AGENTS.md

Project docs live in `docs/`.
Third party code in `third_party`.
Main working folders `assets`, `cmake`, `src` and `scripts`.

Use `README.md` for the human entry path, `docs/build-and-release.md` for
build, packaging, and SDK details, `docs/runtime-contract.md` for runtime
behavior, and `docs/tv-usb-operation.md` for rooted-TV and USB behavior.

## Tools

- Use project-local tooling only. Configure shared tools through `mise`; do
  not install global dependencies.
- Run all commands through Ubuntu/WSL or Linux. From a Windows host, run
  commands inside the default WSL Linux distribution.
- **Run `mise run check` after every completed fix or feature.** It is the
  repo's read-only format, lint, syntax, and test gate. Run it once the change
  is done, not after every intermediate edit.
- If `mise run check` reports a fixable formatting or lint issue covered by
  repo tooling, run `mise run fix` first. Do not hand-patch fixer-managed
  output unless the fixer does not resolve it or the remaining change needs
  human judgment.

### mise rules

- Use `mise use` to add tools. Do not edit the configuration directly.
- Move tasks longer than one or two lines into scripts.
- Always enable lockfile mode.
- Prefer `latest` with a lockfile. Pin versions only when required.
- Keep tasks DRY with `mise` templates, variables, and environment passing.
- Do not use the `asdf` backend. Shared tools must use explicit non-`asdf` backends such as `core`, `aqua`, or `npm`.
- After changing shared tools, run `mise lock` and verify `mise.lock` contains no `asdf:` backend entries.

### Task and helper design

- Prefer the simplest UX that matches the tooling already in use. If an
  upstream tool already owns a workflow or persistent state, use that
  interface instead of recreating repo-local state and commands.
- Keep task helpers composable and boring. Prefer helpers that do one
  operation for one target, and let `mise.toml` compose repo-specific
  workflows.
- Do not make helpers smart. Avoid hidden discovery, fallback selection,
  state persistence, or workflow branching in leaf scripts when the task
  definition can express the workflow directly.
- Keep project-specific policy and orchestration in `mise.toml` when they
  compose better there. Helpers should take explicit inputs and avoid hidden
  workflow decisions.
- Prefer native `mise` task composition over calling `mise` from inside a
  `mise` task. Use dependencies or sequential task steps when they express
  the workflow directly.
- Prefer directly executable scripts in tasks over `bash script.sh` wrappers
  when the script can declare its own interpreter and executable bit.
- Avoid defensive preflight validation when the downstream command will fail
  clearly. Add custom checks only when they produce a materially better
  error.
- For shell scripts, avoid extra subprocesses for simple parsing that Bash
  parameter expansion or `read` can handle clearly.

### Build control plane

- Treat `mise` task names as the public interface. Keep primary workflows
  behind `check`, `fmt`, `test`, `toolchain-check`, `host-setup`,
  `sdk-install`, `build-desktop`, `build-webos`, `package-webos`,
  `device-provision`, `device-install`, `device-check`, and `clean`. See
  `docs/build-and-release.md` for task behavior.
- Normal configure/build/package workflows must run through
  `CMakePresets.json`, not bespoke shell wrappers.
- Use `CMakeUserPresets.json` only for local machine overrides. Do not commit it.
- `device-provision` opens `ares-setup-device` for alias management and default-target selection.
- `device-install` and `device-check` use the current ares default target and fail if it is unset or `emulator`.
- Generated bench content cache lives under `assets/.local/bench/`; do not commit generated
  `.bench`, `.h264`, or `.hevc` files.
- Do not reintroduce ambient shell sourcing such as `webos-env.sh`.

### Dependency and SDK policy

- Normal CMake configure/build must not download from the network.
- Explicit setup steps may install Debian host packages or the pinned local webOS SDK.
- Debian host setup is for host integration only. Do not use it to supply
  target/source dependencies for webOS builds.
- Do not install target/source dependencies from Debian when the webOS build also needs them. Use pinned upstream source submodules or SDK pkg-config packages.
- Keep `ss4s`, `commons`, `inih`, and `opus` as passive pinned upstream submodules. Do not fork or patch them unless the user explicitly chooses that maintenance cost.
- The only shared SDK override is `DECODER_BENCH_WEBOS_NDK`.
- The webOS pkg-config wrapper must search only SDK sysroot pkg-config directories. It must not see host `/usr/lib/pkgconfig`.

## Commit style

[Conventional Commits](https://www.conventionalcommits.org/). Scope encouraged — use the area touched (`webos`, `decoder`, etc.).

## Writing style

- **Bold is for emphasis, not decoration.** Use `**bold**` only when a word or phrase must punch through — a hard constraint, a surprising gotcha, a rule that was repeatedly violated. If everything is bold, nothing is. Default to plain text.
- **Sentence case for all headings.** Write "What we tried", not "What We Tried".
- Keep prose direct. Short sentences, no filler, no preamble.
- Match the tone already in `docs/` — technical and flat, not promotional.
- Document general approaches, not specific scripts. Unless a workflow is non-obvious, highly specific, or genuinely one-off, describe the principle — not a concrete command sequence. Specific snippets are fine when the approach can't be generalized.

## Coding style

### Boring C

Generate userspace C that is easy to review, test, debug, and change.

Prefer call shapes where the relevant safety contract is visible: ownership, lifetime, bounds, string termination, integer range, and failure behavior. If the contract is unclear, rewrite the code.

Hard requirements:

- Build warning-free under strict warnings.
- Treat sanitizer, static-analysis, and test failures as bugs.
- Check every fallible operation.
- Each allocation has one clear owner and one clear cleanup path.
- Do not allow use-after-free, double-free, leaked ownership, or escaped borrowed pointers.
- For writes, destination capacity must be visible.
- For raw data, length must be visible.
- For C strings, NUL-termination must be guaranteed or checked.
- For constructed C strings, make capacity derivable. Prefer exact allocation
  or helper APIs when escaping or serialization can expand input. Fixed buffers
  are fine when their size follows from a named domain/API limit or the full
  construction contract: inputs, literals, separators, escaping/serialization
  expansion, and the NUL byte. If a formula needs slack, name the slack by the
  format term it covers or move the construction behind a helper.
- Do not use unchecked copy, concat, format, indexing, pointer arithmetic, or allocation-size arithmetic.
- Use integer types by domain; `size_t` is for object sizes, capacities, and byte counts, not a generic non-negative integer.
- Check narrowing, signed/unsigned conversion, and overflow risks.

Avoid APIs whose safety depends on hidden context or surprising semantics:
`gets`, `strcpy`, `strcat`, `strncpy`, `strncat`, `sprintf`, `vsprintf`, `strtok`.

Before finalizing C code, audit:

- Ownership/lifetime.
- Bounds.
- NUL-termination.
- String construction: can the capacity be derived from inputs,
  escaping/serialization rules, literals, separators, and the NUL byte?
- Integer overflow/conversion.
- NULL/dangling pointers.
- ...and every error path.

### Boring C build policy

Use CMake + Ninja by default.

Goal: a clean checkout builds, tests, and debugs predictably without hidden local state.

Generate target-based CMake. Ninja is only the generated executor.

Hard requirements:

- Out-of-source builds only.
- Shared workflows live in `CMakePresets.json`.
- Local machine overrides live in `CMakeUserPresets.json`.
- Model code as targets: libraries, executables, tests.
- Attach include dirs, compile definitions, compile options, and link deps to targets.
- Scope target usage with `PRIVATE`, `PUBLIC`, and `INTERFACE`.
- Keep generated files in the build tree.
- Make tests runnable through CTest.
- Do not hide real build behavior in shell scripts, IDE settings, or hand-written Ninja.
- Use the committed webOS toolchain entry point at `cmake/toolchains/webos-arm.cmake`.
- Use `pkg_check_modules(... REQUIRED IMPORTED_TARGET ...)` for SDK packages and link `PkgConfig::*` targets.
- Keep strict warning-as-error policy on owned code and tests. Do not force upstream third-party source slices to satisfy this repo's warning policy.

Avoid:

- Global include/link/compiler settings.
- In-source builds.
- Per-developer absolute paths.
- Mixing build types in one build directory.
- Network dependency fetching during normal builds unless explicitly intended.
- Adding upstream `third_party/*/cmake` directories to `CMAKE_MODULE_PATH`.
- Normal-build use of `ExternalProject`, `FetchContent`, `URL`, or `GIT_REPOSITORY`.
- Host pkg-config results in webOS cross builds.

Before finalizing, audit:

- Can a clean checkout build with one preset?
- Are all deps target-linked?
- Are usage requirements scoped correctly?
- Can tests run with CTest?
- Are generated/local files outside the source tree?

## Boring Python

Write clean, boring, modern Python for small greenfield projects.

The code should feel like practical Python: linear where possible, explicit where useful, and structured only where the structure earns its place. It should not feel artificially dumbed down nor should it read like enterprise Python.

Plain functions and ordinary dicts/lists/tuples are fine when they make the code obvious. Classes, dataclasses, and small helper objects are also fine when they clarify named data, config, state, or behavior. Do not add abstractions just to look "proper".

Validate external inputs early: CLI args, paths, files, JSON/TOML, env vars, HTTP responses, subprocess results, and data-shape assumptions. Fail fast. Catch expected operational errors only when you can give a better message or exit code. Let unexpected errors bubble.

Use modern Python pragmatically: Python 3.11+, `pathlib`, context managers, f-strings, `dataclasses`, `tomllib`, `argparse`, `logging`, and `subprocess.run(..., check=True)`. Prefer `uv` and `ruff` for low-ceremony tooling.

Start with the standard library, but use small modern libraries when they reduce complexity: `httpx` for HTTP, `rich` for useful CLI output, `pytest` for tests, `pydantic` for real external data/config validation, and `typer` when the CLI shape earns it.

Use type hints when they clarify boundaries, records, config, return values, or library interactions. Avoid type-driven design, dense generics, protocols, overloads, large `TypedDict` structures, and type gymnastics unless they genuinely improve the code.

For scripts, keep `main()` small: parse args, call the real work, handle expected user-facing errors, and return an exit code. Keep business logic separate enough from IO to test important behavior, but do not build layers for purity.

Judge the result by taste: nice clean Python, no extra ceremony, justified classes if any, plain dict usage where appropriate, visible errors, easy flow, modern conveniences where useful, no miniature framework.
