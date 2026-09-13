#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Scrapes JL's NUnit deconjugation suite into one C++ header of constexpr case tables.

Every [Test] method in JL.Core.Tests/Deconjugation/DeconjugatorTestsFor*.cs has the identical
shape: a `termToDeconjugate` literal, an `expected` literal, and a
`form is { Text: "...", LastTag: "..." }` filter. The four values are the whole test.
Output: tests/deconj/deconjugation_cases.h
"""
import argparse
import os
import re
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", help="JL checkout at the revision recorded in the generated header")
parser.add_argument("output", nargs="?", default=os.path.join(os.path.dirname(__file__), "..", "tests/deconj/deconjugation_cases.h"))
args = parser.parse_args()
JL = os.path.abspath(args.source)
SRC = os.path.join(JL, "JL.Core.Tests/Deconjugation")
OUT = args.output

TEST_RE = re.compile(
    r'public void (?P<name>\w+)\(\)\s*\{\s*'
    r'(?://[^\n]*\n\s*)*'
    r'const string termToDeconjugate = "(?P<input>[^"]*)";\s*'
    r'const string expected = (?:"(?P<expected>[^"]*)"|(?P<null>null));\s*'
    r'string\?\s*actual = .*?form is \{ Text: "(?P<lemma>[^"]*)", LastTag: "(?P<tag>[^"]*)" \}',
    re.S)

# Collapse identical source cases by their values rather than by method name.

def cpp_escape(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

def main():
    commit = subprocess.check_output(["git", "-C", JL, "rev-parse", "HEAD"]).decode().strip()
    files = sorted(f for f in os.listdir(SRC) if f.endswith(".cs"))

    tables = []
    stats = {"scraped": 0, "duplicates": 0, "strays": 0}
    v5k_rows = set()

    parsed = {}
    for f in files:
        text = open(os.path.join(SRC, f), encoding="utf-8").read()
        rows = []
        for m in TEST_RE.finditer(text):
            expected = m.group("expected")
            if m.group("null") is not None:
                expected = None
            rows.append((m.group("input"), m.group("lemma"), m.group("tag"), expected, m.group("name")))
        parsed[f] = rows
        stats["scraped"] += len(rows)
        if f.endswith("ForV5K.cs"):
            v5k_rows = {(r[0], r[1], r[2], r[3]) for r in rows}

    for f in files:
        rows = parsed[f]
        seen = set()
        kept = []
        for inp, lemma, tag, expected, name in rows:
            key = (inp, lemma, tag, expected)
            if key in seen:
                stats["duplicates"] += 1
                continue
            # The VK file carries four 泣く/v5k rows that belong to the V5K file. Drop them here
            # only when the V5K file holds the identical row.
            if f.endswith("ForVK.cs") and lemma == "泣く" and key in v5k_rows:
                stats["strays"] += 1
                continue
            seen.add(key)
            kept.append((inp, lemma, tag, expected, name))
        tables.append((f, kept))

    ident = lambda f: re.match(r"DeconjugatorTestsFor(\w+)\.cs", f).group(1)

    with open(OUT, "w", encoding="utf-8") as out:
        w = out.write
        w("// SPDX-FileCopyrightText: 2026 marunine\n")
        w("// SPDX-License-Identifier: LGPL-3.0-only\n")
        w("// GENERATED FILE - do not edit by hand.\n")
        w("// Generator: tools/gen-deconj-tests.py\n")
        w("// Source: JL (Apache-2.0), JL.Core.Tests/Deconjugation/DeconjugatorTestsFor*.cs at\n")
        w("// commit %s. Every row is one [Test] method: the termToDeconjugate literal, the\n" % commit)
        w("// Text/LastTag filter of the form predicate, and the expected rendering of\n")
        w("// LookupResultUtils.DeconjugationProcessesToText over the surviving processes.\n")
        w("//\n")
        w("// %d [Test] methods scraped, %d exact duplicates collapsed, %d 泣く rows dropped from\n"
          % (stats["scraped"], stats["duplicates"], stats["strays"]))
        w("// the VK file as duplicates of V5K rows, %d rows emitted.\n"
          % sum(len(k) for _, k in tables))
        w("#pragma once\n\n")
        w("#include <QStringView>\n\n")
        w("#include <array>\n\n")
        w("namespace maru::deconj::testcases\n{\n\n")
        w("// One transcribed JL test. expectedPath is the whole rendered path set; an empty\n")
        w("// expectedPath stands for JL's null (no surviving deconjugation).\n")
        w("struct Case\n{\n")
        w("    QStringView input;\n")
        w("    QStringView lemma;\n")
        w("    QStringView tag;\n")
        w("    QStringView expectedPath;\n")
        w("};\n\n")
        # Every row is positional. A designator per field turns 5,224 lines into 20,896, in a
        # table that no reader edits by hand, so the check is suppressed over the tables instead.
        w("// NOLINTBEGIN(modernize-use-designated-initializers)\n")
        for f, rows in tables:
            name = ident(f)
            w("// %s: %d cases.\n" % (f, len(rows)))
            w("inline constexpr std::array<Case, %d> k%sCases{{\n" % (len(rows), name))
            for inp, lemma, tag, expected, _ in rows:
                exp = "" if expected is None else expected
                w('    {u"%s", u"%s", u"%s", u"%s"},\n'
                  % (cpp_escape(inp), cpp_escape(lemma), cpp_escape(tag), cpp_escape(exp)))
            w("}};\n\n")
        w("// NOLINTEND(modernize-use-designated-initializers)\n\n")
        w("} // namespace maru::deconj::testcases\n")

    # The repository holds only clang-formatted sources, so the output is passed through the
    # tracked .clang-format before it is compared or committed.
    subprocess.check_call(["clang-format", "--style=file:" + os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".clang-format"), "-i", OUT])

    print("scraped=%d duplicates=%d strays=%d emitted=%d"
          % (stats["scraped"], stats["duplicates"], stats["strays"], sum(len(k) for _, k in tables)))
    for f, rows in tables:
        print("  %-36s %d" % (f, len(rows)))

main()
