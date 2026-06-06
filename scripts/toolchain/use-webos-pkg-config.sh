#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ -n "${DECODER_BENCH_WEBOS_NDK:-}" ]; then
  sdk_path="${DECODER_BENCH_WEBOS_NDK}"
else
  sdk_path="$("${script_dir}/install-webos-sdk.sh" --print-default-path)"
fi

if [ ! -d "$sdk_path" ]; then
  echo "SDK not found at ${sdk_path}." >&2
  echo "Run mise run sdk-install or set DECODER_BENCH_WEBOS_NDK." >&2
  exit 2
fi

sysroot="${sdk_path}/arm-webos-linux-gnueabi/sysroot"
if [ ! -d "$sysroot" ]; then
  echo "SDK sysroot not found at ${sysroot}." >&2
  exit 2
fi

if ! command -v pkg-config >/dev/null 2>&1; then
  echo "Host pkg-config is required." >&2
  exit 2
fi

mapfile -t pc_dirs < <(find "${sysroot}/usr" -type d -name pkgconfig | sort)

if [ "${#pc_dirs[@]}" -eq 0 ]; then
  echo "No pkg-config directories found under ${sdk_path}." >&2
  exit 2
fi

unset PKG_CONFIG_PATH
unset PKG_CONFIG_DIR
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
IFS=:
export PKG_CONFIG_LIBDIR="${pc_dirs[*]}"

exec pkg-config "$@"
