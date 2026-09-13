// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The spelling to word-class table the deconjugation gate reads.
//
// A deconjugated form is only accepted when the dictionary entry it matched actually carries the
// word class the last deconjugation rule produced: without the table, 見る deconjugated as a godan
// verb matches 見る the ichidan verb and the popup shows a form that does not exist. JMdict entries
// carry the classes themselves, so the gate matters for the dictionaries that do not — Yomitan
// term banks, whose [3] rules field is optional and often empty.
//
// JL builds the same table into Resources/PoS.json
// (JL.Core/WordClass/JmdictWordClassUtils.cs, Apache-2.0) and reads it back through
// LookupUtils.WordClassDictionaryContainsTag (JL.Core/Lookup/LookupUtils.cs).
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringView>

namespace maru::dict
{

// One JMdict headword's deconjugation classes.
struct WordClassEntry
{
    QString spelling;           // the record's primary spelling, unnormalized
    QList<QString> wordClasses; // the JMdict entity names the deconjugator accepts
    QList<QString> readings;    // the record's readings, unnormalized

    [[nodiscard]] bool operator==(const WordClassEntry &other) const = default;
};

// The 24 JMdict part-of-speech entity names the deconjugation rules can produce, from JL's
// DeconjugatorUtils.ValidWordClasses (JL.Core/Deconjugation/DeconjugatorUtils.cs). A class
// outside this set is never written into the table, which keeps it at about 49 000 keys rather
// than one key per JMdict headword.
[[nodiscard]] bool isDeconjugationWordClass(QStringView wordClass);

class WordClassTable
{
public:
    // Records one headword under key, which is the normalized search key the JMdict record is
    // stored under. Called by JmdictImporter for each record it writes.
    void add(const QString &key, const WordClassEntry &entry);

    // Indexes every entry additionally under each of its normalized readings, which is the second
    // pass JmdictWordClassUtils.Load() performs after deserializing PoS.json. Called once, after
    // the last add().
    void indexReadings();

    // Whether a headword written primarySpelling and read reading carries tag. reading may be
    // empty, which matches an entry that declares no readings. primarySpelling is normalized
    // before the table is probed, so a katakana headword resolves against the hiragana key the
    // table is built with; the spelling and reading comparisons themselves are exact, as JL's
    // are.
    [[nodiscard]] bool containsTag(QStringView primarySpelling, QStringView reading, QStringView tag) const;

    // The classes of the single entry matching primarySpelling and reading. Empty when no entry
    // matches and when more than one does, because two entries with the same spelling and reading
    // and different classes cannot be told apart (JMdict 駆ける and 振りかえる are the examples JL
    // names in MiningUtils.GetWordClassesFromWordClassDictionary).
    [[nodiscard]] QList<QString> wordClassesFor(QStringView primarySpelling, QStringView reading) const;

    [[nodiscard]] QList<WordClassEntry> entriesFor(QStringView key) const;

    [[nodiscard]] qsizetype keyCount() const
    {
        return m_entries.size();
    }

    [[nodiscard]] bool isEmpty() const
    {
        return m_entries.isEmpty();
    }

    void clear();

    // Writes the table as one CBOR document. The file lives beside the JMdict store in
    // paths::dictionariesDir(), named <dictionary id>.pos.
    [[nodiscard]] bool save(const QString &path) const;
    [[nodiscard]] bool load(const QString &path);

private:
    QHash<QString, QList<WordClassEntry>> m_entries;
};

} // namespace maru::dict
