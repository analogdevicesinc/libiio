#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <base-ref>" >&2
    exit 2
fi

cd "$(git rev-parse --show-toplevel)"
source ./format.sh

command -v clang-format >/dev/null
command -v cmake-format >/dev/null
clang-format --version
cmake-format --version

# Write the file list before the loop so an invalid base or missing history
# fails the check rather than appearing to be an empty diff.
changed_files=$(mktemp)
trap 'rm -f "$changed_files"' EXIT
git diff --name-only --diff-filter=ACMR -z "$1...HEAD" > "$changed_files"

status=0
while IFS= read -r -d '' file; do
    [[ -f "$file" ]] || continue
    if is_source_file "$file" && is_not_ignored_in "$file" .clangformatignore; then
        if ! clang-format --dry-run --Werror "$file"; then
            printf '\nFormatting issues in %s (C):\n' "$file"
            clang-format "$file" | diff -u "$file" - || true
            status=1
        fi
    fi
    if is_cmake_file "$file" && is_not_ignored_in "$file" .cmakeformatignore; then
        if ! cmake-format --check "$file"; then
            printf '\nFormatting issues in %s (CMake):\n' "$file"
            cmake-format "$file" | diff -u "$file" - || true
            status=1
        fi
    fi
done < "$changed_files"

exit "$status"
