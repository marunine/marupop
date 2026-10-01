#!/bin/sh
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
#
# Usage: git diff -U0 ... | tools/clang-tidy-diff.sh BUILD_DIR [VFS_OVERLAY]
#
# Runs clang-tidy over the added lines of a unified diff and reports every finding as an error.
# The CI build job and .githooks/pre-commit run it. BUILD_DIR holds the compile_commands.json that
# clang-tidy reads. VFS_OVERLAY names a clang virtual file system overlay, which the hook uses to
# replace working-tree files with their staged content. MARUPOP_CLANG_TIDY names the clang-tidy
# binary and defaults to clang-tidy on PATH.
#
# Exit status:
# - 0: clang-tidy reports no finding.
# - 1: clang-tidy reports a finding, or its LLVM major version differs from .clang-tools-version.
# - 77: clang-tidy, its clang-tidy-diff.py or python3 is absent.
#
# Run from the repository root.
set -eu

build_dir=$1
overlay=${2:-}

clang_tidy=$(command -v "${MARUPOP_CLANG_TIDY:-clang-tidy}" || true)
# clang-tidy-diff.py is in share/clang of the LLVM prefix that holds bin/clang-tidy. readlink -f
# resolves a versioned link, such as /usr/bin/clang-tidy-23 into /usr/lib/llvm-23/bin.
tidy_diff=""
if [ -n "$clang_tidy" ]; then
    for bin in "${clang_tidy%/*}" "$(dirname "$(readlink -f "$clang_tidy")")"; do
        if [ -f "$bin/../share/clang/clang-tidy-diff.py" ]; then
            tidy_diff=$bin/../share/clang/clang-tidy-diff.py
            break
        fi
    done
fi
if [ -z "$tidy_diff" ] || ! command -v python3 >/dev/null 2>&1; then
    echo "clang-tidy-diff.sh: clang-tidy, its share/clang/clang-tidy-diff.py or python3 is absent." >&2
    exit 77
fi
tools/check-clang-tools-version.sh "$clang_tidy"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 tools/compdb-filter-diff.py "$build_dir/compile_commands.json" >"$work/checked.diff"

binary=$clang_tidy
if [ -n "$overlay" ]; then
    # clang-tidy-diff.py passes the arguments after -- to the compiler, so a wrapper adds the
    # clang-tidy option. The wrapper expands the two variables when it runs.
    # shellcheck disable=SC2016
    printf '#!/bin/sh\nexec "$MARUPOP_TIDY_BINARY" --vfsoverlay="$MARUPOP_TIDY_OVERLAY" "$@"\n' >"$work/clang-tidy"
    chmod +x "$work/clang-tidy"
    MARUPOP_TIDY_BINARY=$clang_tidy
    MARUPOP_TIDY_OVERLAY=$overlay
    export MARUPOP_TIDY_BINARY MARUPOP_TIDY_OVERLAY
    binary=$work/clang-tidy
fi

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
python3 "$tidy_diff" -p1 -path "$build_dir" -quiet -j "$jobs" -clang-tidy-binary "$binary" \
    -warnings-as-errors='*' <"$work/checked.diff"
