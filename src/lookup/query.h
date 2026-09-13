// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// One dictionary's answers for one set of candidates: the exact-key probe, the deconjugated
// probe behind the part-of-speech gate, and the single-character kanji probe.
//
// Ported from JL (Apache-2.0), JL.Core/Lookup/LookupUtils.cs at commit 85ae02eeb84f378387f48c12e7b468390a9f2007:
// GetWordResultsHelper for the probes and process merging, GetValidDeconjugatedResults
// for the gate, GetNameResults and GetKanjiResults for name and kanji queries.
#pragma once

#include "dict/dictionary.h"
#include "dict/wordclasses.h"
#include "lookup/lookuptypes.h"
#include "lookup/textinfo.h"

#include <QList>
#include <QString>

namespace maru::lookup
{

// The JMdict or JMnedict sequence number of the record behind result, 0 for a record kind that
// carries none.
[[nodiscard]] qint32 entryIdOf(const Result &result);

// The Yomitan popularity score of the record behind result. A record kind that carries none
// scores the lowest possible value, so it never wins criterion 9 against one that does.
[[nodiscard]] double popularityScoreOf(const Result &result);

// The deconjugation paths as one string, in JL's rendering: the first path prefixed with U+FF5E
// and the rest joined with "; " ("～させる→られる→なかった"). Empty when the result was matched
// without deconjugation (LookupResultUtils.DeconjugationProcessesToText).
[[nodiscard]] QString deconjugationProcessText(const Result &result);

// Whether record, found under the lemma a deconjugation reached, actually carries the word class
// the last rule of that deconjugation produced. Without the test, 見る deconjugated as a godan
// verb would match 見る the ichidan verb.
//
// The test differs per dictionary type: JMdict and custom words carry exact JMdict entity names,
// a Yomitan term bank carries the coarse class ("v5" for every godan verb) so the comparison is
// inverted, and a Yomitan bank with no classes at all falls back to the table JMdict's import
// built.
[[nodiscard]] bool acceptsDeconjugationTag(dict::DictType type,
                                           const dict::Record &record,
                                           QStringView lastTag,
                                           const dict::WordClassTable &wordClasses);

// The word dictionary's answers: every candidate's key probed directly, plus every lemma the
// deconjugator reached from it that passes the gate. Results are in first-seen key order, which
// is longest candidate first.
[[nodiscard]] QList<Result> queryWordDictionary(const dict::DictionaryHandle &dictionary,
                                                const TextInfo &info,
                                                const dict::WordClassTable &wordClasses);

// The name dictionary's answers: the exact key alone. A name is never deconjugated.
[[nodiscard]] QList<Result> queryNameDictionary(const dict::DictionaryHandle &dictionary, const TextInfo &info);

// The kanji dictionary's answer for one character, which is the first character of the lookup
// text when that character is a kanji. kanjiExamples and kanjiComponents are left to the caller.
[[nodiscard]] QList<Result> queryKanjiDictionary(const dict::DictionaryHandle &dictionary, const QString &kanji);

} // namespace maru::lookup
