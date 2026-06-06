#!/usr/bin/env bash
set -euo pipefail

script_dir=${BASH_SOURCE[0]%/*}
repo_root=$(cd "${script_dir}/../.." && pwd)
cd "$repo_root"
script_name=${BASH_SOURCE[0]##*/}

dry_run=${UPDATE_DEPS_DRY_RUN:-false}
check_only=${UPDATE_DEPS_CHECK:-false}
args=()

if [ "${usage_dry_run:-false}" = true ]; then
  dry_run=true
fi
if [ "${usage_check:-false}" = true ]; then
  dry_run=true
  check_only=true
fi

usage() {
  cat <<EOF
Usage: ${script_name} <submodule> <git-ref|latest-tag> [--dry-run] [--check]

Examples:
  ${script_name} third_party/commons refs/heads/main
  ${script_name} third_party/opus latest-tag
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --dry-run | -n)
      dry_run=true
      ;;
    --check)
      dry_run=true
      check_only=true
      ;;
    --help | -h)
      usage
      exit 0
      ;;
    *)
      args+=("$1")
      ;;
  esac
  shift
done

if [ "${#args[@]}" -ne 2 ]; then
  usage >&2
  exit 2
fi

ref_sha() {
  local refs sha remote_ref direct peeled

  refs=$(git ls-remote "$1" "${2}^{}" "$2")
  while read -r sha remote_ref; do
    if [ "${remote_ref%"^{}"}" != "$remote_ref" ]; then
      peeled=$sha
      break
    fi
    if [ -n "${remote_ref:-}" ] && [ -z "${direct:-}" ]; then
      direct=$sha
    fi
  done <<<"$refs"
  printf '%s\n' "${peeled:-${direct:-}}"
}

latest_tag_ref() {
  local refs remote_ref tag

  refs=$(git ls-remote --tags --refs --sort='v:refname' "$1")
  while read -r _ remote_ref; do
    if [ -n "${remote_ref:-}" ]; then
      tag=${remote_ref#refs/tags/}
    fi
  done <<<"$refs"
  if [ -z "${tag:-}" ]; then
    return 1
  fi
  printf 'refs/tags/%s\n' "$tag"
}

path=${args[0]}
ref=${args[1]}
url=$(git config --file .gitmodules --get "submodule.${path}.url" || true)
if [ -z "$url" ]; then
  echo "No submodule URL for ${path} in .gitmodules." >&2
  exit 2
fi

if [ "$ref" = "latest-tag" ]; then
  if ! ref=$(latest_tag_ref "$url"); then
    echo "No tags found for ${path} at ${url}." >&2
    exit 2
  fi
fi

target=$(ref_sha "$url" "$ref")
if [ -z "$target" ]; then
  echo "Cannot resolve ${ref} for ${path} at ${url}." >&2
  exit 2
fi

if git -C "$path" rev-parse --git-dir >/dev/null 2>&1; then
  current=$(git -C "$path" rev-parse HEAD)
else
  current=$(git rev-parse "HEAD:${path}")
  if [ "$dry_run" = false ]; then
    git submodule update --init --recursive -- "$path"
  fi
fi

if [ "$current" = "$target" ]; then
  printf 'OK     %-24s %.7s (%s)\n' "$path" "$current" "$ref"
  exit 0
fi

if [ "$dry_run" = true ]; then
  printf 'WOULD  %-24s %.7s -> %.7s (%s)\n' "$path" "$current" "$target" "$ref"
  if [ "$check_only" = true ]; then
    exit 1
  fi
  exit 0
fi

git -C "$path" fetch --force "$url" "$ref"
git -C "$path" checkout --detach "$target"
printf 'UPDATE %-24s %.7s -> %.7s (%s)\n' "$path" "$current" "$target" "$ref"
