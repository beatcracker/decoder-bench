#!/usr/bin/env bash
set -euo pipefail

device_list=$(ares-setup-device --list)
default_device=$(printf '%s\n' "$device_list" | awk '/\(default\)/ { print $1; exit }')

if [ -z "$default_device" ]; then
  echo "No ares default device is set." >&2
  echo "Run 'mise run device-provision' and choose 'set default' in ares-setup-device." >&2
  exit 2
fi

if [ "$default_device" = "emulator" ]; then
  echo "The current ares default device is 'emulator'." >&2
  echo "Run 'mise run device-provision' and set a non-emulator device as the ares default." >&2
  exit 2
fi
