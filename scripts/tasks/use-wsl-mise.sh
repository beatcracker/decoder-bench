#!/usr/bin/env bash
set -euo pipefail

if (($# < 1)); then
  echo "Missing task command." >&2
  exit 2
fi

script=$1
shift
for arg in "$@"; do
  printf -v quoted "%q" "$arg"
  script+=" $quoted"
done

exec mise exec -- bash -l -o errexit -o pipefail -c "$script"
