#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "${script_dir}/../.." && pwd)

pass=0
fail=0

ok() {
  printf '  OK   %-28s %s\n' "$1" "$2"
  pass=$((pass + 1))
}

bad() {
  printf '  FAIL %-28s %s\n' "$1" "$2"
  fail=$((fail + 1))
}

check() {
  local label="$1"
  shift
  local output
  if output=$("$@" 2>&1); then
    ok "$label" "$(printf '%s' "$output" | head -n1)"
  else
    bad "$label" "MISSING"
  fi
}

check_file() {
  local label="$1"
  local path="$2"
  local missing="${3:-not found at $path}"
  if [ -f "$path" ]; then
    ok "$label" "$path"
  else
    bad "$label" "$missing"
  fi
}

check_dir() {
  local label="$1"
  local path="$2"
  local missing="${3:-not found at $path}"
  if [ -d "$path" ]; then
    ok "$label" "$path"
  else
    bad "$label" "$missing"
  fi
}

if [ -n "${DECODER_BENCH_WEBOS_NDK:-}" ]; then
  sdk_path="${DECODER_BENCH_WEBOS_NDK}"
else
  sdk_path="$("${script_dir}/install-webos-sdk.sh" --print-default-path)"
fi

echo "Checking mise-managed tools..."
check "python" python --version
check "node" node --version
check "cmake" cmake --version
check "ninja" ninja --version
check "shellcheck" shellcheck --version
check "shfmt" shfmt --version
check "ripgrep" rg --version
check "ares" ares --version
check "ares-package" ares-package --version
check "ares-install" ares-install --version

echo ""
echo "Checking host packages..."
check "pkg-config" pkg-config --version
check "host pkg sdl2" pkg-config --modversion sdl2
check "ffmpeg" ffmpeg -version
check "ffprobe" ffprobe -version
check_file "inih source" "${repo_root}/third_party/inih/ini.c" "missing; run git submodule update --init --recursive"
check_file "Opus source" "${repo_root}/third_party/opus/CMakeLists.txt" "missing; run git submodule update --init --recursive"

echo ""
echo "Checking SDK..."
check_dir "SDK root" "$sdk_path" "not found at $sdk_path; run mise run sdk-install"
toolchain_file="${sdk_path}/share/buildroot/toolchainfile.cmake"
check_file "toolchain file" "$toolchain_file"
check "webOS gcc" "${sdk_path}/bin/arm-webos-linux-gnueabi-gcc" --version

if "${script_dir}/use-webos-pkg-config.sh" --version >/dev/null 2>&1; then
  ok "webOS pkg-config" "$("${script_dir}/use-webos-pkg-config.sh" --version)"
else
  bad "webOS pkg-config" "failed"
fi

for package in sdl2 pbnjson_c helpers NDL_directmedia; do
  check "webOS pkg ${package}" "${script_dir}/use-webos-pkg-config.sh" --modversion "$package"
done

echo ""
echo "Checking mise lock backends..."
if grep -q 'backend = "asdf:' "${repo_root}/mise.lock" 2>/dev/null; then
  bad "mise backend policy" "mise.lock contains asdf backend entries"
else
  ok "mise backend policy" "no asdf backends"
fi

echo ""
echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
