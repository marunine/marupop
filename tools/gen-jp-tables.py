#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
"""Emits src/jp/japanesetables.h from JL.Core/Japanese/JapaneseUtils.cs.

The C# file holds the normalization maps as `new('x', 'y')` / `new("xy", 'z')` initializer
lists inside named FrozenDictionary fields. This scrapes those lists and writes them as sorted
constexpr arrays so the C++ side can binary-search them.
"""
import argparse
import os
import re
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", help="JL checkout at the revision recorded in the generated header")
parser.add_argument("output", nargs="?", default=os.path.join(os.path.dirname(__file__), "..", "src/jp/japanesetables.h"))
args = parser.parse_args()
JL = os.path.abspath(args.source)
SRC = os.path.join(JL, "JL.Core/Japanese/JapaneseUtils.cs")
OUT = args.output

text = open(SRC, encoding="utf-8").read()


def field_body(name):
    start = text.index("s_%s = new KeyValuePair" % name)
    open_brace = text.index("{", text.index("[]", start))
    depth = 0
    i = open_brace
    while True:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    return text[open_brace:i]


CHAR_PAIR = re.compile(r"new\('(.)',\s*'(.)'\)")
STR_PAIR = re.compile(r'new\("(.+?)",\s*\'(.)\'\)')


def char_map(name):
    body = field_body(name)
    # Sections are separated by `// comment` lines; the pairs themselves are uniform.
    return [(a, b) for a, b in CHAR_PAIR.findall(body)]


def supplementary_map():
    body = field_body("supplementaryNormalizationDict")
    return [(a, b) for a, b in STR_PAIR.findall(body)]


def cp(ch):
    return ord(ch)


def emit_char_table(w, name, pairs, comment):
    pairs = sorted(set(pairs), key=lambda p: cp(p[0]))
    seen = {}
    for a, b in pairs:
        if a in seen and seen[a] != b:
            raise SystemExit("conflicting mapping for %r" % a)
        seen[a] = b
    w("// %s\n" % comment)
    w("inline constexpr std::array<CharPair, %d> %s{{\n" % (len(pairs), name))
    for a, b in pairs:
        w("    {.from = 0x%04X, .to = 0x%04X}, // %s -> %s\n" % (cp(a), cp(b), a, b))
    w("}};\n\n")


def emit_supplementary(w, pairs):
    pairs = sorted(set(pairs), key=lambda p: cp(p[0]))
    w("// Supplementary-plane single-code-point mappings: kyuujitai and the hentaigana block\n")
    w("// U+1B002-U+1B11E. Keyed by code point, so the caller decodes the surrogate pair first.\n")
    w("inline constexpr std::array<SupplementaryPair, %d> kSupplementaryNormalization{{\n" % len(pairs))
    for a, b in pairs:
        w("    {.from = 0x%05X, .to = 0x%04X}, // %s -> %s\n" % (cp(a), cp(b), a, b))
    w("}};\n\n")


def emit_char_set(w, name, chars, comment):
    chars = sorted(set(chars), key=cp)
    w("// %s\n" % comment)
    w("inline constexpr std::array<char16_t, %d> %s{{\n" % (len(chars), name))
    line = "    "
    for c in chars:
        piece = "0x%04X, " % cp(c)
        if len(line) + len(piece) > 116:
            w(line.rstrip() + "\n")
            line = "    "
        line += piece
    if line.strip():
        w(line.rstrip().rstrip(",") + "\n")
    w("}};\n\n")


def literal_set(field):
    """Reads a SearchValues.Create([...]) / SearchValues.Create('a', 'b') field body."""
    m = re.search(r"s_%s = SearchValues\.Create\(" % field, text)
    i = m.end() - 1
    depth = 0
    j = i
    while True:
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    body = text[i:j]
    return re.findall(r"'(?:\\u([0-9A-Fa-f]{4})|(.))'", body)


def chars_of(field):
    out = []
    for esc, lit in literal_set(field):
        out.append(chr(int(esc, 16)) if esc else lit)
    return out


normalization = char_map("normalizationDict")
dakuten = char_map("hiraganaToDakutenDict")
final_vowel = char_map("kanaFinalVowelDict")
supplementary = supplementary_map()

charsToStrip = chars_of("charsToStrip")
fuseji = chars_of("fuseji") + ["○"]
longVowelMarks = chars_of("longVowelMarkChars")
sentenceTerminators = re.findall(r"'(?:\\u([0-9A-Fa-f]{4})|(.))'",
                                 text[text.index("s_sentenceTerminatingCharacters ="):
                                      text.index("s_leftToRightBracketDict")])
sentenceTerminators = [chr(int(e, 16)) if e else l for e, l in sentenceTerminators if e or l != "\\"]
# The escape \n survives the regex as the two characters '\' and 'n'.
sentenceTerminators = [c for c in sentenceTerminators if c != "n"] + ["\n"]

brackets = re.findall(r"new\('(.)',\s*'(.)'\)",
                      text[text.index("s_leftToRightBracketDict = new"):
                           text.index("s_rightToLeftBracketDict =")])
smallCombining = chars_of("SmallCombiningKanaSet".replace("SmallCombiningKanaSet", "x")) if False else None
m = re.search(r"SmallCombiningKanaSet = SearchValues\.Create\(([^)]*)\)", text)
smallCombining = [x for x in re.findall(r"'(.)'", m.group(1))]

smallVowel = re.findall(r"new\('(.)',\s*'(.)'\)",
                        text[text.index("SmallVowelHiraganaToFinalVowelDict = new"):
                             text.index("SmallCombiningKanaSet")])

commit = subprocess.check_output(["git", "-C", JL, "rev-parse", "HEAD"]).decode().strip()

# The set that drives NormalizeText's fast bail, exactly as JL builds s_charactersToNormalize.
iteration_marks = ["々", "〻", "ゝ", "ゞ"]
charactersToNormalize = set(iteration_marks) | set(fuseji) | set(charsToStrip) | {"っ", "\uDB40"}
charactersToNormalize |= {a for a, _ in normalization}
charactersToNormalize |= {chr(0xD800 + ((cp(a) - 0x10000) >> 10)) for a, _ in supplementary}
charactersToNormalize |= {chr(c) for c in range(0xFE00, 0xFE10)}

with open(OUT, "w", encoding="utf-8") as f:
    w = f.write
    w("// SPDX-FileCopyrightText: 2026 marunine\n")
    w("// SPDX-License-Identifier: LGPL-3.0-only\n")
    w("// GENERATED FILE - do not edit by hand.\n")
    w("// Generator: tools/gen-jp-tables.py\n")
    w("// Ported from JL (Apache-2.0), JL.Core/Japanese/JapaneseUtils.cs at commit %s.\n" % commit)
    w("// Every table below is one of that file's FrozenDictionary or SearchValues fields,\n")
    w("// sorted by key so jp/japanese.cpp can binary-search it.\n")
    w("#pragma once\n\n")
    w("#include <array>\n")
    w("#include <cstdint>\n\n")
    w("namespace maru::jp::tables\n{\n\n")
    w("struct CharPair\n{\n    char16_t from;\n    char16_t to;\n};\n\n")
    w("struct SupplementaryPair\n{\n    char32_t from;\n    char16_t to;\n};\n\n")
    emit_char_table(w, "kNormalization", normalization,
                    "s_normalizationDict: katakana to hiragana, the hiragana wi/we fold, the CJK "
                    "radical\n// supplement to kanji block, and the kyuujitai to shinjitai block.")
    emit_char_table(w, "kHiraganaToDakuten", dakuten,
                    "s_hiraganaToDakutenDict: the voiced counterpart the iteration mark U+309E adds.")
    emit_char_table(w, "kKanaFinalVowel", final_vowel,
                    "s_kanaFinalVowelDict: the vowel a kana ends on, for the elongation trigger test.")
    emit_char_table(w, "kSmallVowelHiraganaToFinalVowel", smallVowel,
                    "SmallVowelHiraganaToFinalVowelDict: the vowel a small hiragana vowel stands for.")
    emit_char_table(w, "kLeftToRightBracket", brackets,
                    "s_leftToRightBracketDict: opening bracket to its closing counterpart.")
    emit_supplementary(w, supplementary)
    emit_char_set(w, "kCharsToStrip", charsToStrip,
                  "s_charsToStrip: dropped when neither the first nor the last code unit.")
    emit_char_set(w, "kFuseji", fuseji, "s_fuseji: censoring marks, all folded to U+25CB.")
    emit_char_set(w, "kLongVowelMarks", longVowelMarks, "s_longVowelMarkChars.")
    emit_char_set(w, "kSentenceTerminators", sentenceTerminators, "s_sentenceTerminatingCharacters.")
    emit_char_set(w, "kSmallCombiningKana", smallCombining,
                  "SmallCombiningKanaSet: glued onto the preceding character by combinedForm().")
    emit_char_set(w, "kCharactersToNormalize", sorted(charactersToNormalize, key=cp),
                  "s_charactersToNormalize: the fast-bail set of normalizeText(). A text holding\n"
                  "// none of these is returned unchanged after the NFKC and uppercase passes.")
    w("} // namespace maru::jp::tables\n")

# The repository holds only clang-formatted sources, so the output is passed through the
# tracked .clang-format before it is compared or committed.
subprocess.check_call(["clang-format", "--style=file:" + os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".clang-format"), "-i", OUT])

print("normalization=%d dakuten=%d finalVowel=%d supplementary=%d strip=%d fuseji=%d "
      "terminators=%d brackets=%d smallCombining=%d toNormalize=%d"
      % (len(set(normalization)), len(set(dakuten)), len(set(final_vowel)), len(set(supplementary)),
         len(set(charsToStrip)), len(set(fuseji)), len(set(sentenceTerminators)), len(set(brackets)),
         len(set(smallCombining)), len(charactersToNormalize)))
