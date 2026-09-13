#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Emits src/ocr/meikicorrectiontable.h from a meikiocr correction file.

Reads the repository's operational rule specification. Rules run in array order;
context has at most two tokens per side. See tools/data/README.md.

Usage: gen-meiki-corrections.py [corrections.json] [output.h]
"""
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tools/data/meiki-corrections.json")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "src/ocr/meikicorrectiontable.h")

# The context width the C++ side stores per rule, per side.
MAX_CONTEXT = 2

CLASS_KINDS = {
    "hiragana": "Hiragana",
    "katakana": "Katakana",
    "kanji": "Kanji",
    "digit": "Digit",
    "latin": "Latin",
    "space": "Space",
    "other": "Other",
}
MODES = {"h": "Horizontal", "v": "Vertical", "any": "Any"}

raw = open(SRC, "rb").read()
spec = json.loads(raw.decode("utf-8"))
if spec.get("format") != "meikiocr-corrections" or int(spec.get("version", 0)) != 1:
    sys.exit(f"{SRC}: not a version 1 meikiocr correction file")



def u16(text):
    """A C++ u"" literal. Every character of the file is in the Basic Multilingual Plane."""
    out = []
    for c in text:
        if ord(c) > 0xFFFF:
            sys.exit(f"non-BMP character U+{ord(c):04X} in a rule")
        if c in '"\\' or not c.isprintable() or ord(c) == 0xFFFD:
            out.append(f"\\u{ord(c):04X}")
        else:
            out.append(c)
    return 'u"' + "".join(out) + '"'


def token(tok, edge):
    if tok in ("BOS", "EOS"):
        if tok != edge:
            sys.exit(f"{tok} on the wrong side of a rule")
        return "{.kind = CorrectionToken::Kind::LineEdge, .character = 0}"
    if len(tok) == 1:
        if ord(tok) > 0xFFFF:
            sys.exit("non-BMP context character")
        return f"{{.kind = CorrectionToken::Kind::Literal, .character = 0x{ord(tok):04X}}}"
    if tok not in CLASS_KINDS:
        sys.exit(f"unknown class {tok!r}")
    return f"{{.kind = CorrectionToken::Kind::{CLASS_KINDS[tok]}, .character = 0}}"


def tokens(toks, edge):
    if len(toks) > MAX_CONTEXT:
        sys.exit(f"a context of {len(toks)} tokens exceeds {MAX_CONTEXT}")
    cells = [token(t, edge) for t in toks] + ["{}"] * (MAX_CONTEXT - len(toks))
    return "{{" + ", ".join(cells) + "}}", len(toks)


lines = []
for rule in spec["rules"]:
    if not rule["find"]:
        sys.exit("a rule needs a non-empty find")
    before, before_count = tokens(rule.get("before") or [], "BOS")
    after, after_count = tokens(rule.get("after") or [], "EOS")
    gate = rule.get("max_conf")
    gate_text = "std::nullopt" if gate is None else repr(float(gate))
    lines.append(f"    CorrectionRule{{.find = {u16(rule['find'])},\n"
                 f"                   .replace = {u16(rule['replace'])},\n"
                 f"                   .before = {before},\n"
                 f"                   .beforeCount = {before_count},\n"
                 f"                   .after = {after},\n"
                 f"                   .afterCount = {after_count},\n"
                 f"                   .mode = CorrectionMode::{MODES[rule.get('mode', 'any')]},\n"
                 f"                   .maxConfidence = {gate_text}}},")

digest = hashlib.sha256(raw).hexdigest()
header = f"""// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// GENERATED FILE - do not edit by hand.
// Generator: tools/gen-meiki-corrections.py
// Source: operational correction specification, sha256 {digest}.
// {len(spec['rules'])} rules, in application order. See tools/data/README.md.
#pragma once

#include "ocr/meikicorrections.h"

#include <array>

namespace maru::ocr
{{

inline constexpr std::array<CorrectionRule, {len(spec['rules'])}> kMeikiCorrectionRules = {{{{
{chr(10).join(lines)}
}}}};

}} // namespace maru::ocr
"""
with open(OUT, "w", encoding="utf-8") as out:
    out.write(header)
subprocess.check_call(["clang-format", "--style=file:" + os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".clang-format"), "-i", OUT])
print(f"wrote {len(spec['rules'])} rules to {OUT}")
