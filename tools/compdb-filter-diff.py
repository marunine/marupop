#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Keeps the file sections of a unified diff that a compilation database can check.

clang-tidy infers a compile command from a neighbouring file for a file absent from the
database. A Linux database omits the Windows sources, and an inferred Linux command fails on
<windows.h>.

Every header a Linux translation unit includes has a source file the database lists in the
header's folder.

A file whose first 10 lines hold the marker "GENERATED FILE" is the output of a tools/gen-*.py
generator and is dropped, because its lines change only through the generator.

Usage: git diff -U0 ... | compdb-filter-diff.py build/compile_commands.json | clang-tidy-diff.py ...
Run from the repository root; the diff paths are relative to the repository root.
"""
import json
import os
import sys

GENERATED_MARKER = "GENERATED FILE"


def is_generated(path):
    if not os.path.isfile(path):
        return False
    with open(path, encoding="utf-8", errors="replace") as source:
        return any(GENERATED_MARKER in source.readline() for _ in range(10))


def main():
    with open(sys.argv[1], encoding="utf-8") as database_file:
        database = json.load(database_file)
    root = os.getcwd()
    sources = {
        os.path.relpath(os.path.normpath(os.path.join(entry["directory"], entry["file"])), root)
        for entry in database
    }
    folders = {os.path.dirname(path) for path in sources}

    keep = False
    for line in sys.stdin:
        if line.startswith("diff --git"):
            path = line.rstrip("\n").split(" b/", 1)[1]
            keep = path in sources or (path.endswith(".h") and os.path.dirname(path) in folders)
            keep = keep and not is_generated(path)
        if keep:
            sys.stdout.write(line)


if __name__ == "__main__":
    main()
