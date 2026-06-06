# Project story

Decoder bench exists because end-to-end streaming failures on rooted LG webOS
TVs are hard to localize. When playback stutters, stalls, or never appears,
there are too many moving parts to blame with confidence: encoder settings,
network transport, session setup, UI state, storage behavior, or the local TV
decoder path itself.

This project strips that problem down to one narrow question:

> Can this TV decode and present this local workload cleanly through the SS4S
> NDL video path?

## Why a TV-side benchmark matters

The benchmark runs in the same broad environment that matters for rooted-TV
testing: native webOS packaging, the real TV decoder, the SS4S NDL module, and
the same launcher constraints the operator sees on device.

That makes it useful for answering questions such as:

- Does a specific codec, resolution, FPS, or bitrate shape decode cleanly?
- Are late submits or decoder-latency spikes coming from the local decoder path?
- Is a failure reproducible with local content before Moonlight, Sunshine, or
  network transport enter the picture?

The desktop build is still useful, but only for parser, suite, and CLI
validation. It is not evidence about real TV decode behavior.

## Why rooted TVs and Homebrew Channel are part of the model

The launcher app is jailed. On the observed rooted target, that means a normal
app process cannot directly see the real USB mount content it needs for large
external suite sets.

Homebrew Channel matters here because it provides the root-side bridge used by
decoder bench to prepare USB visibility for the app. That lets launcher mode
run externally supplied suites without copying large media sets into internal TV
storage. It keeps the operator workflow simple: install the app, put suites on
USB, and run them on the TV.

Without that rooted-TV path, the packaged app can still run its bundled smoke
content, but it cannot rely on the same external USB workflow.

## Bundled and external suites

The project supports two practical content paths:

- Bundled suites: small packaged smoke content that always ships with the app.
- External suites: larger USB-hosted content for real device testing.

Launcher autorun prefers external suites first and falls back to bundled suites
when no usable external suite set is visible. That gives operators a safe
baseline while keeping larger, more expensive content out of the IPK.

SSH and CLI remain the primary measurement path when you want explicit control.
Launcher mode is better treated as operator convenience and visual smoke.

## Non-goals

Decoder bench does not try to be:

- An end-to-end Moonlight benchmark.
- A Sunshine encoder benchmark.
- A network transport or RTP benchmark.
- A general webOS performance score.
- A replacement for real session debugging once the local decoder path has been
  ruled in or out.

## Read next

- [build-and-release.md](build-and-release.md) for setup, packaging, and release flow.
- [runtime-contract.md](runtime-contract.md) for the normative runtime and output contract.
- [tv-usb-operation.md](tv-usb-operation.md) for rooted-TV launcher and USB behavior.
