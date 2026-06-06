#!/usr/bin/env bash
set -euo pipefail

script_name=${BASH_SOURCE[0]##*/}

usage() {
  cat <<EOF
Usage: ${script_name} <glob> <tool> [args...]

Reads NUL-delimited paths on stdin, usually from scripts/tasks/get-repo-owned-files.sh.
EOF
}

if [ "$#" -lt 2 ]; then
  usage >&2
  exit 2
fi

glob="$1"
shift

files=()
while IFS= read -r -d '' file; do
  # shellcheck disable=SC2053
  if [[ "$file" == $glob ]]; then
    files+=("$file")
  fi
done

if [ "${#files[@]}" -eq 0 ]; then
  exit 0
fi

"$@" "${files[@]}"
