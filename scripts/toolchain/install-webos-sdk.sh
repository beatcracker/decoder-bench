#!/usr/bin/env bash
set -euo pipefail

SDK_REPO="openlgtv/buildroot-nc4"
SDK_TAG="webos-b17b4cc"
SDK_FILE="arm-webos-linux-gnueabi_sdk-buildroot.tar.gz"
SDK_URL="https://github.com/${SDK_REPO}/releases/download/${SDK_TAG}/${SDK_FILE}"
SDK_SHA256="1f69af43caac4b6898d8f0323c975be60af04abdd1e1c144773072b28c961c20"
SDK_SIZE="312272500"
SDK_DIR_NAME="arm-webos-linux-gnueabi_sdk-buildroot"

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "${script_dir}/../.." && pwd)

sdk_root="${repo_root}/.local/webos-sdk"
sdk_parent="${sdk_root}/${SDK_TAG}"
sdk_path="${sdk_parent}/${SDK_DIR_NAME}"
cache_dir="${sdk_root}/.cache"
archive="${cache_dir}/${SDK_FILE}"

if [ "${1:-}" = "--print-default-path" ]; then
  printf '%s\n' "$sdk_path"
  exit 0
fi

need_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required tool '$1'. Run mise run host-setup." >&2
    exit 2
  fi
}

archive_size() {
  wc -c <"$archive" | tr -d '[:space:]'
}

verify_archive() {
  [ -f "$archive" ] || return 1
  [ "$(archive_size)" = "$SDK_SIZE" ] || return 1
  printf '%s  %s\n' "$SDK_SHA256" "$archive" | sha256sum -c - >/dev/null
}

for tool in curl sha256sum tar mktemp; do
  need_tool "$tool"
done

if [ -f "${sdk_path}/share/buildroot/toolchainfile.cmake" ]; then
  echo "SDK already installed: ${sdk_path}"
  exit 0
fi

if [ -e "$sdk_parent" ]; then
  echo "Found incomplete SDK directory: ${sdk_parent}" >&2
  echo "Remove it and rerun mise run sdk-install." >&2
  exit 2
fi

mkdir -p "$cache_dir"
if ! verify_archive; then
  echo "Downloading pinned webOS SDK..."
  echo "  Source: ${SDK_URL}"
  echo "  Cache:  ${archive}"
  rm -f "$archive"
  tmp_archive="${archive}.tmp"
  rm -f "$tmp_archive"
  curl --fail --location --retry 3 --connect-timeout 30 --output "$tmp_archive" "$SDK_URL"
  mv "$tmp_archive" "$archive"
fi

printf '%s  %s\n' "$SDK_SHA256" "$archive" | sha256sum -c -
actual_size=$(archive_size)
if [ "$actual_size" != "$SDK_SIZE" ]; then
  echo "Archive size mismatch: got ${actual_size}, expected ${SDK_SIZE}." >&2
  exit 2
fi

mkdir -p "$sdk_root"
tmp_parent=$(mktemp -d "${sdk_root}/.install.${SDK_TAG}.XXXXXX")
trap 'rm -rf "$tmp_parent"' EXIT

echo "Extracting SDK..."
tar -xzf "$archive" -C "$tmp_parent"

tmp_sdk="${tmp_parent}/${SDK_DIR_NAME}"
if [ ! -x "${tmp_sdk}/relocate-sdk.sh" ]; then
  echo "SDK archive did not contain ${SDK_DIR_NAME}/relocate-sdk.sh." >&2
  exit 2
fi

echo "Relocating SDK..."
"${tmp_sdk}/relocate-sdk.sh"

mv "$tmp_parent" "$sdk_parent"
trap - EXIT

echo "SDK installed: ${sdk_path}"
