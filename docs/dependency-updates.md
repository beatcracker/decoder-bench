# Dependency updates

Maintainer workflow for updating repo-managed dependencies.

## Scope

There are two update lanes:

- Source dependencies are pinned submodules under `third_party/`.
- Shared developer tools are declared in `mise.toml` and resolved in
  `mise.lock`.

Do not update dependencies from Debian packages for target/source webOS builds.
Use the pinned upstream source submodules or SDK packages described in
[build-and-release.md](build-and-release.md).

## Source submodules

Preview source dependency updates:

```shell
mise run deps-update -- --dry-run
```

Apply source dependency updates:

```shell
mise run deps-update
```

The task updates:

- `third_party/ss4s` to upstream `main`
- `third_party/commons` to upstream `main`
- `third_party/inih` to the latest upstream tag
- `third_party/opus` to the latest upstream tag

Review submodule changes before committing:

```shell
git submodule status --recursive
git diff --submodule
```

## Shared tools

Preview local tool updates:

```shell
mise outdated
```

Apply only project-local tool updates:

```shell
mise upgrade --local
mise lock
```

`@webos-tools/cli` follows the latest released version and is resolved in
`mise.lock`. After updating it, verify that `mise run package-webos` succeeds.

Do not update user-level tools from this repo workflow. `mise outdated` may
also list tools from `~/.config/mise/config.toml`; ignore those unless you are
deliberately updating your own machine.

After updating shared tools, verify that `mise.lock` has no `asdf:` backend
entries.

## Validation

After any dependency update, run fixers before the read-only gate:

```shell
mise run fix
mise run check
```

Treat build, test, lint, and generated-content validation failures as update
bugs. Fix them before committing the dependency bump.
