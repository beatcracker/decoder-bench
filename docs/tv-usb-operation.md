# TV and USB operation

This document records rooted-TV launcher and USB behavior observed on one LG C2
running webOS 7.4. Treat it as the current target's operating record, not as a
universal webOS guarantee.

## Why root and HBC are involved

Launcher-started native apps are jailed. On the observed target, that jail was
the real blocker for external USB suites: the app could not see the host's real
USB content even though the host system could.

Decoder bench uses Homebrew Channel because HBC provides the root-side bridge
needed to prepare USB visibility for the app. That keeps launcher-mode testing
practical without copying multi-gigabyte fixture sets into internal storage.

If HBC root is unavailable, decoder bench still runs, but launcher mode falls
back to the bundled benchmark content.

## Launch flow

1. The app acquires `/tmp/<appid>/.bench-usb.lock` with an exclusive `flock`.
2. The app calls HBC `checkRoot`.
3. On success it runs the packaged helper script through HBC `/exec`.
4. The helper removes stale decoder-bench-managed jail USB mounts.
5. The helper bind-mounts each host-visible `/tmp/usb/...` entry into the
   app-visible `/tmp/<appid>/usb/...` tree.
6. If an external suite is found there, launcher-mode CSVs and logs are written
   beside the USB `bench/` directory under `bench-results/<timestamp>/`.
7. A background watcher blocks on the same lock file and cleans up the managed
   mounts when the app exits.

## Observed platform behavior

- The launcher app was a jailed prisoner process, not host root, even with
  `rootRequired: true` in `appinfo.json`.
- The blocker was jail-root visibility, not a separate mount namespace.
- The app-visible `/tmp/usb/...` path was only a skeleton on this target.
- This was not a Unix permission problem. Once the real USB mount was visible
  inside the jail path, the app could read `bench/suites`.
- Direct app writes to the jail-visible USB bind mount worked on this target,
  so launcher-mode results could be written back to USB without an HBC copy-out
  step.
- `com.webos.service.storageaccess` was absent on this target.
- `com.webos.service.pdm` existed and could report USB mount information.

## Working path model

The working strategy is a host-to-jail bind mount:

```text
host: /tmp/usb/<device>/<partition>
bind -> /var/palm/jail/<appid>/tmp/<appid>/usb/<device>/<partition>
```

Decoder bench scans the app-visible path:

```text
/tmp/<appid>/usb/<device>/<partition>
```

Cleanup is unconditional for the decoder-bench-owned mount prefix. Correctness
does not depend on the exit watcher succeeding because each launch cleans stale
managed mounts before attempting fresh USB setup.

## Debugging rules

- Treat SSH root as the host view, not the app view.
- To test app-visible paths, use app logging or `chroot /var/palm/jail/<appid>`.
- Do not use `nsenter` as the first tool for this issue; it does not address
  jail-root visibility on the observed target.
- Do not copy USB fixture sets into `/media/developer` unless you deliberately
  want local staging.
- If a UI-launched USB run finds external suites, inspect
  `<usb>/bench-results/<timestamp>/bench.err` and
  `<usb>/bench-results/<timestamp>/bench.log` first.
- Very early launcher logs can still remain under `/tmp/<appid>/` before USB
  output is attached.
- The helper's exit watcher writes post-exit cleanup diagnostics to
  `/tmp/<appid>/usb-mount.log`.

## HBC helper notes

HBC `/exec` runs as host root outside the app jail. Root-side paths are not
the same thing as app-side paths, so root-side `/tmp` is not a reliable shared
diagnostic location.

Use the packaged helper script for the mount flow. Avoid large inline shell
payloads in Luna JSON beyond trivial diagnostics. Use
[runtime-contract.md](runtime-contract.md) for the externally visible launcher
selection and results behavior.
