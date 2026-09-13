#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Generate synthetic cases using the standalone scalar reference interpreter.

Inputs combine rule contexts with pseudorandom characters, confidences and boxes.
Every confidence is float32. No captured text or external corpus is used.
Usage: gen-meiki-correction-cases.py [corrections.json] [output.json]
"""
import json
import os
import random
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tools/data/meiki-corrections.json")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "tests/data/ocr/meikicorrections_cases.json")
from meiki_reference import Corrections
corrections = Corrections.load(SRC)

POOLS = {
    "hiragana": "あいうかがきくこしたっつてでとなにのはばぱへべまゃるをん",
    "katakana": "アァイコタビピプブボポケーンシフトニノュ",
    "kanji": "一二大太郎牛生某基本日会社々〇",
    "digit": "0123９①²",
    "latin": "abxIPTｘＡ",
    "space": " 　",
    "other": "、。「」…・.|:?�〔―】(×/",
}
ALL = "".join(POOLS.values())

rng = random.Random(20260912)


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def instantiate(tok):
    if tok in POOLS:
        return rng.choice(POOLS[tok])
    return tok


def confidences(n, gate):
    out = []
    for _ in range(n):
        if gate is None or rng.random() < 0.3:
            out.append(f32(rng.random()))
        else:
            out.append(f32(min(1.0, max(0.0, gate + rng.choice((-0.05, -0.001, 0.0, 0.001, 0.05))))))
    return out


def boxes(n, vertical):
    out, pos = [], rng.randint(0, 40)
    cross = rng.randint(0, 40)
    extent = rng.randint(12, 40)
    for _ in range(n):
        size = rng.randint(8, 40)
        if vertical:
            out.append([cross, pos, cross + extent, pos + size])
        else:
            out.append([pos, cross, pos + size, cross + extent])
        pos += size + rng.randint(0, 6)
    return out


def case(text, confs, vertical):
    chars = [{"char": c, "bbox": b, "conf": f} for c, b, f in zip(text, boxes(len(text), vertical), confs)]
    result = corrections.apply_chars(chars, vertical)
    return {
        "text": text,
        "confs": confs,
        "boxes": [c["bbox"] for c in chars],
        "vertical": vertical,
        "expected": {
            "text": "".join(c["char"] for c in result),
            "confs": [c["conf"] for c in result],
            "boxes": [c["bbox"] for c in result],
        },
    }


cases = []
for rule in corrections.rules:
    for _ in range(10):
        before = "".join(instantiate(t) for t in rule.before if t not in ("BOS", "EOS"))
        after = "".join(instantiate(t) for t in rule.after if t not in ("BOS", "EOS"))
        head = "" if "BOS" in rule.before else "".join(rng.choice(ALL) for _ in range(rng.randint(0, 3)))
        tail = "" if "EOS" in rule.after else "".join(rng.choice(ALL) for _ in range(rng.randint(0, 3)))
        text = head + before + rule.find + after + tail
        vertical = rule.mode == "v" if rng.random() < 0.8 and rule.mode != "any" else rng.random() < 0.5
        cases.append(case(text, confidences(len(text), rule.max_conf), vertical))
for _ in range(300):
    text = "".join(rng.choice(ALL) for _ in range(rng.randint(1, 14)))
    cases.append(case(text, confidences(len(text), rng.choice((0.1, 0.3, 0.5, 0.7, 0.9, None))), rng.random() < 0.5))

changed = sum(1 for c in cases if c["expected"]["text"] != c["text"] or c["expected"]["boxes"] != c["boxes"])
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as out:
    json.dump({"SPDX-FileCopyrightText": "2026 marunine", "SPDX-License-Identifier": "LGPL-3.0-only",
               "source": "Synthetic inputs evaluated by tools/meiki_reference.py",
               "generator": "tools/gen-meiki-correction-cases.py", "cases": cases},
              out, ensure_ascii=False, separators=(",", ":"))
    out.write("\n")
print(f"wrote {len(cases)} cases ({changed} rewritten) to {OUT}")
