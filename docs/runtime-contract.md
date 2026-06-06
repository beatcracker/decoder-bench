# Runtime contract

## Purpose

Decoder bench answers one narrow question:

```text
Can local webOS NDL decode and present this codec, resolution, FPS, and bitrate
shape when fed directly through SS4S?
```

It does not measure Sunshine, network transport, RTP, GameStream session
setup, Moonlight UI behavior, or end-to-end streaming.

## Runtime rules

- webOS uses the SS4S NDL video module and refuses non-NDL video modules.
- Desktop builds use the SS4S dummy module for parser, suite, and CLI
  validation only.
- The core runtime feeds one complete access unit per frame and records timing
  after the measured loop.
- Fixtures are read through `BenchSource`, which validates the first access
  unit, detects stream metadata, preserves access-unit boundaries, and reports
  the source mode written into logs and summary CSV.
- A one-time storage warmup runs at the start of the first suite per process so
  cold USB latency does not poison the first measured run.
- Decoder failures, invalid fixtures, and storage starvation are reported as
  distinct stop reasons.
- Source reads, prefetch reads, and warmup reads are serialized in 1 MiB chunks
  to avoid USB seek thrash.

## Inputs

- Suite files live under `<bench-root>/suites/*.bench`.
- Samples live under sibling `<bench-root>/samples/`.
- Suite rows require `file`, `fps`, and `run_seconds`.
- Title-card rows may use `skip_stats`; other row keys are invalid unless the
  runtime documents them.
- An optional `[suite]` section accepts `source_buffer_mib = N` in the range
  `32..512`.
- `--source-buffer-mib N` overrides any suite-level buffer setting.
- Fixture FPS must match the suite row FPS.
- Direct `--file --run-seconds N` is an explicit fixed-workload run.
- Direct `--file` without `--run-seconds` runs in auto mode until EOF or a
  30-second cap.

## Outputs and verdicts

Raw frame CSV and summary CSV are compatibility surfaces.

Summary rows include at least these operator-facing fields:

- `run_length_mode`
- `target_frames`
- `stop_reason`
- `source_mode`
- `source_buffer_mib`
- `source_error`
- `latency_probe_stall_max_frames`
- `verdict_reason`
- `verdict_detail`

`source_mode` is:

- `complete` when the fixture fits in the initial source fill
- `streaming` when playback uses the loader-backed double-buffer path

Exit codes are:

- `0`: pass or stopped by operator
- `1`: warn
- `2`: fail
- `3`: invalid input or configuration

Operator-facing verdicts are:

- `PASS`: the configured workload completed cleanly
- `WARN`: the workload completed, but late submits or non-severe decoder
  latency growth were observed
- `FAIL`: the workload could not be completed, or the run hit a hard source or
  decoder failure

## Launcher behavior

The packaged app id is `io.github.beatcracker.decoderbench`.

- No-argument launcher autorun tries external USB suites first.
- If no external suite is found, the launcher falls back to the bundled suite
  set discovered under the packaged bench root.
- Bundled launcher fallback runs the discovered suites in alphabetical order.
- Bundled no-argument fallback runs the discovered suite set once by default.
- Launcher autorun writes CSVs and logs to
  `<usb>/bench-results/<timestamp>/` when it discovers an external suite under
  the managed USB bind root.
- Other runs use `--results-dir` or the platform default results directory.

Launcher mode is visual smoke and automation glue. SSH or CLI remains the
primary measurement interface when you need explicit control.
