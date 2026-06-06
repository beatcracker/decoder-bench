#!/usr/bin/env bash
set -euo pipefail

while IFS= read -r -d '' file; do
  if [ -e "$file" ]; then
    printf '%s\0' "$file"
  fi
done < <(
  git ls-files -z --cached --others --exclude-standard --deduplicate -- . \
    ':(exclude)third_party/**' \
    ':(exclude)build/**' \
    ':(exclude)assets/.local/**' \
    ':(exclude).local/**' \
    ':(exclude).wip/**' \
    ':(exclude).benchgen*/**' \
    ':(exclude)*.bench' \
    ':(exclude)*.h264' \
    ':(exclude)*.hevc' \
    ':(exclude)mise.lock' \
    ':(exclude)assets/build/**'
)
