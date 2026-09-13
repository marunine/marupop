#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Generate synthetic, aligned inputs for marupop-lookupprobe --pairs.

Exercises isolated character confusions; it does not measure real OCR accuracy.
A separately supplied dictionary is required by the lookup probe.
"""
import argparse
import json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', required=True)
args = parser.parse_args()
# Original short examples, chosen to exercise ambiguous kana and kanji shapes.
pairs = [('カタカナ', '力タカナ'), ('東京', '束京'), ('学校', '学挍'), ('日本語', '曰本語'), ('こんにちは', 'こんにちば')]
with open(args.out, 'w', encoding='utf-8') as out:
    for truth, read in pairs:
        for vertical in (False, True):
            for confidence in (0.25, 0.65, 0.95):
                out.write(json.dumps({'read': read, 'truth': truth, 'conf': [confidence] * len(read),
                                      'vertical': vertical, 'ctx': 'synthetic'}, ensure_ascii=False) + '\n')
print('wrote 30 synthetic pairs')
