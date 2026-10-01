#!/bin/sh
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
#
# Usage: tools/check-clang-tools-version.sh TOOL...
#
# Exits 1 when the LLVM major version of a TOOL differs from the version in .clang-tools-version,
# and prints the differing versions to stderr. Each TOOL is a clang-format or clang-tidy command
# name or path. Run from the repository root.
set -eu

pinned=$(sed -n '/^[0-9]/{p;q;}' .clang-tools-version)
status=0
for tool in "$@"; do
    # clang-format prints "clang-format version 23.1.1", clang-tidy prints "LLVM version 23.1.1".
    found=$("$tool" --version 2>/dev/null | sed -n 's/.*version \([0-9][0-9]*\)\..*/\1/p' | head -n 1)
    if [ "$found" != "$pinned" ]; then
        echo "$tool: LLVM ${found:-version unknown} differs from LLVM $pinned in .clang-tools-version." >&2
        status=1
    fi
done
exit $status
