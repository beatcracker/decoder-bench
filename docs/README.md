# Documentation

| Document                                       | Read this when...                                                                           |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------- |
| [project-story.md](project-story.md)           | Project context, the rooted-TV and Homebrew Channel details, and the scope boundaries.      |
| [build-and-release.md](build-and-release.md)   | To set up the toolchain, build the app, package it, install it, or stage release artifacts. |
| [dependency-updates.md](dependency-updates.md) | To update pinned source submodules or shared `mise` tools.                                  |
| [release-publishing.md](release-publishing.md) | To cut a maintainer release and upload GitHub Release assets.                               |
| [runtime-contract.md](runtime-contract.md)     | Runtime behavior, suite layout, CSV outputs, exit codes, or launcher rules.                 |
| [tv-usb-operation.md](tv-usb-operation.md)     | Rooted-TV launcher behavior, USB visibility, HBC helper flow, or app-jail debugging.        |
| [known-issues.md](known-issues.md)             | List and the validation gaps.                                                               |

## Repo map

- `src/`: C runtime and platform integration.
- `scripts/bench/`: benchmark planning, rendering, and generated-content orchestration.
- `scripts/tasks/`: task helpers used by `mise`.
- `scripts/toolchain/`: WSL, SDK, and device-facing helpers.
- `assets/webos/`: packaged app metadata, icons, and USB mount helper.
- `third_party/`: pinned upstream source submodules.
