// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Request and result shapes shared by dictionaries, lookup and popup rendering.
// Structure and field semantics derive from JL's LookupResult
// (JL.Core/Lookup/LookupResult.cs, Apache-2.0); see NOTICE for the pinned source.
#pragma once

#include "core/enums.h"
#include "dict/dictionary.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace maru::dict
{
// Defined by dict/records.h: a Result holds the record it came from by shared_ptr, and the
// popup renderer reads the sense structure out of it.
struct Record;
} // namespace maru::dict

namespace maru::lookup
{

// One frequency dictionary's opinion of a headword. higherIsBetter is false for a rank list,
// where 1 is the most common word, and true for a raw corpus count.
struct FrequencyHit
{
    QString dictionaryName;
    int rank = 0;
    bool higherIsBetter = false;
};

// One dictionary entry matched at the cursor.
struct Result
{
    QString primarySpelling;
    QList<QString> readings;
    QList<QString> alternativeSpellings;

    // The raw source characters this result matched, taken from Request::sourceText without
    // normalization: the popup highlights this span, so it has to index back into the text the
    // caller passed rather than into the normalized key the store was queried with.
    QString matchedText;
    // The lemma the deconjugator reached, which is the form the dictionary holds.
    QString deconjugatedMatchedText;
    // One rendered path per way the deconjugator reached the lemma, as
    // deconj::formattedProcess() renders it ("る→た", "causative→past"). JL's single
    // U+FF5E-prefixed, "; "-joined string is what deconjugationProcessText() builds out of
    // them, and what the popup adapter hands the renderer.
    QList<QString> deconjugationPaths;
    // Steps of the shortest path, counting only the steps JL counts as proper. Zero for a
    // result matched without deconjugation, and one criterion of the comparator.
    int minProperStepCount = 0;
    // True for a result the recognition-variant pass found (lookup/ocrvariants.h): its key
    // differs from the normalized matchedText in one character.
    bool matchedByVariant = false;

    QList<FrequencyHit> frequencies;
    // The rank JMdict's own ke_pri and re_pri tags give this headword, 0 when it carries none.
    // Kept apart from frequencies because it is not a dictionary's opinion: the comparator falls
    // back to it only while no frequency dictionary is enabled, and the popup never shows it.
    qint32 priorityRank = 0;
    // One entry per element of readings, in the same order. An empty optional marks a reading
    // no pitch-accent dictionary covers.
    QList<std::optional<quint8>> pitchPositions;

    // The record this result was decoded from, which carries the senses, the tags and the
    // kanji fields the popup renders. dict/records.h defines the type.
    std::shared_ptr<const dict::Record> record;
    // The kanji card for a single-character lookup: the KANJIDIC2 record plus the example
    // words and the component characters built at import.
    std::shared_ptr<const dict::Record> kanjiRecord;
    QList<QString> kanjiExamples;
    QList<QString> kanjiComponents;

    // The dictionary the record came from, which supplies the name the popup can show and the
    // per-dictionary render options. Held by value: the handle carries a shared_ptr to the
    // Store, so a Result keeps the store it was decoded from alive for its own lifetime and a
    // cached Result cannot outlive the dictionary it names. A default-constructed handle, whose
    // priority is 0, marks a Result no dictionary produced.
    dict::DictionaryHandle dictionary;
};

// One lookup at one point in one paragraph of recognized text.
struct Request
{
    // The whole paragraph the pointer is over. A lookup reads forward from cursorIndex, so a
    // word running past a line break is matched only when the caller passes the paragraph
    // rather than the line.
    QString sourceText;
    // Index into sourceText, in UTF-16 code units, of the character under the pointer.
    qsizetype cursorIndex = 0;
    int maxSearchLength = 41;
    int maxResults = 10;
    LookupCategory category = LookupCategory::All;
    // The recognition confidence of each code unit of sourceText, index-parallel to it, or empty
    // where the text carries none. The recognition-variant pass substitutes only a character at
    // or under lookup::variantConfidenceGate, and every character where the list is empty.
    QList<float> confidences;
};

// The results of one Request, ordered by lessThan(). highlightStart equals
// Request::cursorIndex, and highlightLength the matchedText length of the first result, which
// is the span scan/ maps back to logical desktop coordinates for the popup anchor.
struct Response
{
    QList<Result> results;
    qsizetype highlightStart = 0;
    qsizetype highlightLength = 0;
};

} // namespace maru::lookup
