#!/usr/bin/env bash
set -euo pipefail

script_name=${BASH_SOURCE[0]##*/}

usage() {
  printf 'Usage: %s <release-dir> <package-dir> <external-bench-dir>\n' "$script_name"
}

if [ "$#" -ne 3 ]; then
  usage >&2
  exit 2
fi

release_dir=$1
package_dir=$2
external_bench_dir=$3
usb_zip=decoder-bench-usb-bench.zip

find_one() {
  local dir=$1
  local pattern=$2
  local label=$3
  local matches=()

  while IFS= read -r -d '' path; do
    matches+=("$path")
  done < <(find "$dir" -maxdepth 1 -type f -name "$pattern" -print0 2>/dev/null | sort -z)

  if [ "${#matches[@]}" -ne 1 ]; then
    printf 'Expected exactly one %s in %s, found %d.\n' "$label" "$dir" "${#matches[@]}" >&2
    exit 1
  fi

  printf '%s\n' "${matches[0]}"
}

ipk=$(find_one "$package_dir" '*.ipk' 'IPK')
manifest=$(find_one "$package_dir" '*.manifest.json' 'Homebrew Channel manifest')

mkdir -p -- "$release_dir"
find "$release_dir" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
mkdir -p -- "$release_dir/bench"

cp -- "$ipk" "$manifest" "$release_dir/"
cp -R -- "$external_bench_dir/suites" "$release_dir/bench/"
cp -R -- "$external_bench_dir/samples" "$release_dir/bench/"

cmake -E chdir "$release_dir" tar cf "$usb_zip" bench
rm -rf -- "$release_dir/bench"

printf 'Staged release artifacts in: %s\n' "$release_dir"
find "$release_dir" -maxdepth 1 -type f -printf '%f\n' | sort
