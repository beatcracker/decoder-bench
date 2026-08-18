# Release publishing

Maintainer checklist for publishing decoder bench release assets to GitHub.

## Scope

This is a manual GitHub Release flow.

CI runs checks, packages the webOS app, and uploads a temporary test-only IPK
artifact on pushes and pull requests. CI does not create GitHub Releases,
publish stable release assets, upload a Homebrew manifest, or change the
webOS Brew catalog.

## Release inputs

- The app version comes from `project(decoder-bench VERSION ...)` in
  `CMakeLists.txt`.
- The webOS IPK and Homebrew Channel manifest use that version through
  `assets/webos/appinfo.json.in`.
- Release tags should match the app version, such as `v1.0.0`.
- The external USB suite must be generated before release staging can pass.

## Preflight

Run from a clean checkout on the release commit:

```shell
mise install
mise run sdk-install
mise run toolchain-check
mise run check
```

If the external suite cache is missing or stale, refresh it:

```shell
mise run external-gen
```

## Stage assets

Stage the manual GitHub Release attachments:

```shell
mise run release-prep
```

`release-prep` packages the webOS build, checks that the external suite is
ready, and writes release attachments to `build/release/`.

Expected files:

- `io.github.beatcracker.decoderbench_<version>_arm.ipk`
- `io.github.beatcracker.decoderbench.manifest.json`
- `decoder-bench-usb-bench.zip`

The USB ZIP contains a top-level `bench/` directory with `suites/` and
`samples/`. Users unpack that directory to a USB stick. Launcher mode discovers
it before bundled suites when HBC root is available.

## Release description

Use `.github/release-description.md` as the release body.

GitHub does not auto-load this file in the web release form. For the UI flow,
paste it into the release description field.

Use GitHub generated release notes only when you want a PR changelog instead of
the static asset description.

## Publish

Create and push the release tag:

```shell
git tag v1.0.0
git push origin v1.0.0
```

Create the GitHub Release with the staged assets:

```shell
gh release create v1.0.0 \
  build/release/*.ipk \
  build/release/*.manifest.json \
  build/release/decoder-bench-usb-bench.zip \
  --notes-file .github/release-description.md \
  --title "decoder-bench v1.0.0"
```

Use the GitHub UI instead when preferred: draft a release for the same tag,
paste the release description, and upload the three files from `build/release/`.

## Post-publish check

- Confirm the release contains the IPK, manifest, and USB ZIP.
- Download the USB ZIP once and verify it expands to `bench/suites` and
  `bench/samples`.
- Keep generated `.bench`, `.h264`, and `.hevc` content out of git.
