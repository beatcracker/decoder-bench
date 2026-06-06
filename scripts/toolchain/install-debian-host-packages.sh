#!/usr/bin/env bash
set -euo pipefail

script_name=${BASH_SOURCE[0]##*/}

usage() {
  printf 'Usage: %s [--quiet]\n' "$script_name"
}

quiet=false

while [ "$#" -gt 0 ]; do
  case "$1" in
    --quiet)
      quiet=true
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if ! command -v apt-get >/dev/null 2>&1; then
  echo "apt-get was not found." >&2
  echo "Host setup supports apt-based Debian/Ubuntu-style systems. Install equivalent packages manually." >&2
  exit 2
fi

packages=(
  build-essential
  pkg-config
  git
  curl
  unzip
  openssh-client
  ca-certificates
  ffmpeg
  file
  libsdl2-dev
)

if [ "$(id -u)" -eq 0 ]; then
  apt_get=(env DEBIAN_FRONTEND=noninteractive apt-get)
else
  if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo was not found. Re-run as root or install sudo first." >&2
    exit 2
  fi
  echo "sudo may prompt for your password." >&2
  sudo -v
  apt_get=(sudo env DEBIAN_FRONTEND=noninteractive apt-get)
fi

if [ "$quiet" = true ]; then
  apt_get+=(-qq -o APT::Color=0 -o Dpkg::Use-Pty=0 -o Dpkg::Progress-Fancy=0)
fi

"${apt_get[@]}" update
"${apt_get[@]}" install -y "${packages[@]}"
