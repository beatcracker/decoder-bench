# Documentation

| Document                                       | What's inside                                                                     |
| ---------------------------------------------- | --------------------------------------------------------------------------------- |
| [project-story.md](project-story.md)           | Project context and scope, Homebrew Channel/root details.                         |
| [build-and-release.md](build-and-release.md)   | Toolchain setup, building/packaging/insatlling app, release prep.                 |
| [dependency-updates.md](dependency-updates.md) | Pinned source submodules or shared `mise` tools update docs.                      |
| [release-publishing.md](release-publishing.md) | How to cut release and upload GitHub Release assets.                              |
| [runtime-contract.md](runtime-contract.md)     | Runtime behavior, suite layout, CSV outputs, exit codes, launcher rules.          |
| [tv-usb-operation.md](tv-usb-operation.md)     | Rooted-TV launcher behavior, USB visibility, HBC helper flow, app-jail debugging. |
| [known-issues.md](known-issues.md)             | List and the existing validation gaps.                                            |

## Repo map

- `src/`: C runtime and platform integration.
- `scripts/bench/`: benchmark planning, rendering, and generated-content orchestration.
- `scripts/tasks/`: task helpers used by `mise`.
- `scripts/toolchain/`: WSL, SDK, and device-facing helpers.
- `assets/webos/`: packaged app metadata, icons, and USB mount helper.
- `third_party/`: pinned upstream source submodules.
