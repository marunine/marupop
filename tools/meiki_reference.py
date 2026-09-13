# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Independent scalar interpreter for the operational correction specification.

Uses Python code points and half-open boxes. Inputs are BMP characters, with
float32 confidences supplied by the caller. It does not invoke MaruPop binaries.
"""
import json
from types import SimpleNamespace


def character_class(c):
    n = ord(c)
    if 0x3041 <= n <= 0x309f:
        return 'hiragana'
    if 0x30a1 <= n <= 0x30ff or 0x31f0 <= n <= 0x31ff or 0xff66 <= n <= 0xff9f:
        return 'katakana'
    if 0x4e00 <= n <= 0x9fff or 0x3400 <= n <= 0x4dbf or 0xf900 <= n <= 0xfaff or c in '々〆〇':
        return 'kanji'
    if c.isdigit():
        return 'digit'
    if c.isascii() and c.isalpha() or 0xff21 <= n <= 0xff3a or 0xff41 <= n <= 0xff5a:
        return 'latin'
    return 'space' if c.isspace() else 'other'


class Corrections:
    @classmethod
    def load(cls, path):
        spec = json.loads(open(path, encoding='utf-8').read())
        if spec.get('format') != 'meikiocr-corrections' or spec.get('version') != 1:
            raise ValueError('expected version 1 meikiocr-corrections')
        result = cls()
        result.rules = [SimpleNamespace(**r) for r in spec['rules']]
        return result

    def apply_chars(self, chars, vertical):
        chars = [dict(c) for c in chars]
        for rule in self.rules:
            if rule.mode == ('h' if vertical else 'v'):
                continue
            text = ''.join(c['char'] for c in chars)
            result, pos = [], 0
            def match(token, index):
                if not 0 <= index < len(text):
                    return token in ('BOS', 'EOS')
                return token == text[index] if len(token) == 1 else token == character_class(text[index])
            while pos < len(chars):
                end = pos + len(rule.find)
                matches = (text.startswith(rule.find, pos)
                           and all(match(t, pos - len(rule.before) + i) for i, t in enumerate(rule.before))
                           and all(match(t, end + i) for i, t in enumerate(rule.after))
                           and (rule.max_conf is None or all(c['conf'] <= rule.max_conf for c in chars[pos:end])))
                if not matches:
                    result.append(chars[pos])
                    pos += 1
                    continue
                span = chars[pos:end]
                confidence = min(c['conf'] for c in span)
                count = len(rule.replace)
                bounds = [min(c['bbox'][i] for c in span) if i < 2 else max(c['bbox'][i] for c in span) for i in range(4)]
                for i, char in enumerate(rule.replace):
                    if count == len(span):
                        box = list(span[i]['bbox'])
                    else:
                        box = list(bounds)
                        axis = int(vertical)
                        size = bounds[axis + 2] - bounds[axis]
                        box[axis] = bounds[axis] + size * i // count
                        box[axis + 2] = bounds[axis] + size * (i + 1) // count
                    result.append({'char': char, 'conf': confidence, 'bbox': box})
                pos = end
            chars = result
        return chars
