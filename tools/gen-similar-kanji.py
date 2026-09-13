#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Emits src/lookup/similarkanjitable.h from similar-kanji's kanji.tgz_similars.ut8.

similar-kanji (MIT, https://github.com/siikamiika/similar-kanji) lists one kanji per line
followed by the kanji that look like it, slash separated. The list is symmetric: every
neighbour of a kanji lists that kanji back. The header holds the neighbour sets as one sorted
index plus one flat array, so the C++ side binary-searches the index.

Usage: gen-similar-kanji.py [kanji.tgz_similars.ut8] [output.h]
"""
import argparse
import os
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", help="kanji.tgz_similars.ut8 from the public similar-kanji checkout")
parser.add_argument("output", nargs="?", default=os.path.join(os.path.dirname(__file__), "..", "src/lookup/similarkanjitable.h"))
args = parser.parse_args()
SRC, OUT = os.path.abspath(args.source), args.output

neighbours = {}
for line in open(SRC, encoding="utf-8"):
    fields = [f for f in line.strip().split("/") if f]
    if not fields:
        continue
    for c in fields:
        if len(c) != 1 or ord(c) > 0xFFFF:
            sys.exit(f"{c!r} is not one BMP character")
    head, rest = fields[0], fields[1:]
    neighbours.setdefault(head, [])
    for c in rest:
        if c != head and c not in neighbours[head]:
            neighbours[head].append(c)
# Closed under symmetry, so the generated table does not depend on the source staying symmetric.
for head, rest in list(neighbours.items()):
    for c in rest:
        back = neighbours.setdefault(c, [])
        if head not in back:
            back.append(head)

try:
    commit = subprocess.run(["git", "-C", os.path.dirname(SRC), "log", "-1", "--format=%H"],
                            capture_output=True, text=True, check=True).stdout.strip()
except (OSError, subprocess.CalledProcessError):
    commit = "unknown"

index = []
flat = []
for head in sorted(neighbours, key=ord):
    rest = neighbours[head]
    if not rest:
        continue
    index.append((ord(head), len(flat), len(rest)))
    flat.extend(ord(c) for c in rest)

if len(flat) > 0xFFFF:
    sys.exit("the flat array outgrows a 16-bit offset")


def rows(values, per_line, fmt):
    out = []
    for i in range(0, len(values), per_line):
        out.append("    " + " ".join(fmt(v) for v in values[i:i + per_line]))
    return "\n".join(out)


header = f"""// SPDX-FileCopyrightText: 2017 siikamiika
// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: MIT
// GENERATED FILE - do not edit by hand.
// Generator: tools/gen-similar-kanji.py
// Source: similar-kanji (MIT, https://github.com/siikamiika/similar-kanji),
// kanji.tgz_similars.ut8 at commit {commit}; third_party/similar-kanji/LICENSE.
// {len(index)} kanji and {len(flat)} directed neighbour links, closed under symmetry.
#pragma once

#include <array>
#include <cstdint>

namespace maru::lookup::detail
{{

struct SimilarKanjiEntry
{{
    char16_t kanji;
    std::uint16_t offset;
    std::uint8_t count;
}};

// NOLINTBEGIN(modernize-use-designated-initializers): 2,902 positional rows of generated data.
// Sorted by kanji.
inline constexpr std::array<SimilarKanjiEntry, {len(index)}> kSimilarKanjiIndex = {{{{
{rows(index, 4, lambda e: f"{{0x{e[0]:04X}, {e[1]}, {e[2]}}},")}
}}}};

inline constexpr std::array<char16_t, {len(flat)}> kSimilarKanjiNeighbours = {{{{
{rows(flat, 12, lambda v: f"0x{v:04X},")}
}}}};
// NOLINTEND(modernize-use-designated-initializers)

}} // namespace maru::lookup::detail
"""
with open(OUT, "w", encoding="utf-8") as out:
    out.write(header)
subprocess.check_call(["clang-format", "--style=file:" + os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".clang-format"), "-i", OUT])
print(f"wrote {len(index)} kanji, {len(flat)} links to {OUT}")
