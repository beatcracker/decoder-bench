#!/usr/bin/env bash
set -euo pipefail

script_name=${BASH_SOURCE[0]##*/}

usage() {
  printf 'Usage: %s <build-dir>\n' "$script_name"
}

if [ "$#" -lt 1 ]; then
  usage >&2
  exit 2
fi

build_dir=$1

ipk=$(find "$build_dir" -name '*.ipk' -print -quit 2>/dev/null)
if [ -z "$ipk" ]; then
  echo "No IPK found in $build_dir. Build the decoder-bench package first." >&2
  exit 1
fi
echo "Installing: $ipk"
ares-install "$ipk"
