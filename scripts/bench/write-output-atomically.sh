#!/usr/bin/env bash
set -euo pipefail

script_name=${BASH_SOURCE[0]##*/}

usage() {
  printf 'Usage: %s <output> <signature> -- <command> [args...]\n' "$script_name"
}

if [ "$#" -lt 4 ]; then
  usage >&2
  exit 2
fi

output="$1"
signature="$2"
separator="$3"
shift 3

if [ "$separator" != "--" ]; then
  usage >&2
  exit 2
fi

if [ "$#" -eq 0 ]; then
  usage >&2
  exit 2
fi

: "$signature"

if [[ "$output" == */* ]]; then
  output_dir=${output%/*}
  output_name=${output##*/}
else
  output_dir=.
  output_name=$output
fi
tmp_output="$output_dir/.$output_name.tmp"

cleanup() {
  rm -f -- "$tmp_output"
}

trap cleanup EXIT HUP INT TERM

mkdir -p -- "$output_dir"
rm -f -- "$tmp_output"
"$@" "$tmp_output"
mv -f -- "$tmp_output" "$output"
