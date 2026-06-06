# Known issues

This page tracks the validation gaps useful to interpreting TV results.

## SS4S synchronous consume is not validated on target hardware

### Impact

The source layer currently assumes `SS4S_PlayerVideoFeed` has consumed an
access unit by return. If that assumption is wrong, source buffer lifetime rules
will need to change.

### TODO

Run a targeted on-TV overwrite-after-feed test.

## Storage contention can still create false `storage-underflow` results

### Impact

A slow USB path with concurrent prefetch can still make the runtime report
storage starvation even when the decoder path is otherwise fine.

### TODO

Validate with a high-bitrate streaming run on slow USB and decide whether
prefetch deferral or loader-priority changes are needed.

## Storage warmup deadlines are best effort

### Impact

Linux regular-file `read()` can block inside I/O, so a wedged USB path may
exceed the nominal 5 s warmup deadline.

### TODO

Re-test on deliberately misbehaving storage and decide whether the
operator-facing messaging needs to change.

## Desktop dummy is only for code correctness validation

### Impact

Desktop runs can only validate parser, suite, and CLI behavior.
