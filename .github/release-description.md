# decoder-bench release

Native decoder benchmark for rooted LG webOS TVs.

## Assets

- `*.ipk`: webOS package.
- `io.github.beatcracker.decoderbench.manifest.json`: Homebrew Channel manifest.
- `decoder-bench-usb-bench.zip`: external USB benchmark suite. Unpack it to a
  USB stick so the drive contains `bench/suites` and `bench/samples`.

Launcher mode runs USB suites first when HBC root support can expose the USB
stick to the app. Results are written beside the USB `bench/` directory under
`bench-results/<timestamp>/`.
