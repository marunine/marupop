<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Operational OCR corrections

`meiki-corrections.json` is the editable source for the 74 ordered rules in
`src/ocr/meikicorrectiontable.h`. Its operational fields were transcribed from
MaruPop's existing LGPL-3.0-only table. No mining statistics, evaluation records,
rendered text corpus or external reference implementation is included. This
records the repository's existing attribution; it does not establish independent
rights clearance for earlier development inputs.

Rules are heuristics, not a claim of measured recognition accuracy. Their
confidence gates assume the default punctuation factor 0.2 and recognition
threshold 0.1. Changing those settings changes which rules can fire.

The version 1 format contains an ordered `rules` array. Each rule has a nonempty
BMP `find` string, a possibly empty BMP `replace` string, `before` and `after`
arrays of up to two tokens, `mode` (`any`, `h`, `v`), and nullable `max_conf`.
Context tokens are literal BMP characters, `BOS` before a match, `EOS` after a
match, or `hiragana`, `katakana`, `kanji`, `digit`, `latin`, `space`, `other`.
Context arrays follow reading order, with the last `before` token adjacent to the
match. The class ranges are specified in `tools/meiki_reference.py` and exercised
by hand-written C++ tests. A confidence gate accepts a match only when every
matched character's confidence is at most the gate.

For each rule, matching uses the current line before that rule's replacements.
Accepted occurrences do not overlap. Rules run once in array order, so later
rules see earlier replacements. Replacement characters inherit the minimum
confidence of the matched span. Equal-length replacements preserve the original
boxes. Otherwise the half-open union box is split into equal integer partitions
along the reading axis, rounding each boundary down. Empty replacements delete
the matched span.

Run from any working directory with Python 3 and clang-format installed:

```sh
python3 tools/gen-meiki-corrections.py
python3 tools/gen-meiki-correction-cases.py
```

The header generator embeds the specification's SHA-256 digest. The fixture
generator constructs synthetic contexts, boxes and float32 confidences using a
fixed pseudorandom seed, then evaluates them with the independently written
scalar interpreter in `tools/meiki_reference.py`. It never invokes the C++
implementation. Hand-written tests separately cover context order, mode and
gate checks, overlapping matches, box splitting and deletion. Review those tests
when changing the specification; generated agreement alone is insufficient to
establish the intended behavior.
