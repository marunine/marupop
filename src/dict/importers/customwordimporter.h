// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The custom word list importer, over JL's tab-separated custom_words.txt format.
//
// Ported from JL (Apache-2.0), JL.Core/Dicts/CustomWordDict/CustomWordLoader.cs.
//
//   <spellings>\t<readings>\t<definitions>\t<partOfSpeech>[\t<wordClasses>]
//
// The list fields are semicolon separated, a literal \n in the definitions becomes a newline, and
// partOfSpeech is one of Verb, Adjective or Noun, each of which expands to a canned JMdict word
// class array. The optional fifth column supplies exact JMdict word-class tags instead, which the
// deconjugation gate then honours as written.
#pragma once

#include "dict/importers/importer.h"
#include "dict/records.h"

namespace maru::dict
{

class CustomWordImporter : public Importer
{
public:
    CustomWordImporter();
    ~CustomWordImporter() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;

    // The word classes partOfSpeech expands to: the 21 godan and ichidan verb classes for "Verb",
    // adj-i and adj-ix for "Adjective", n for "Noun", and "other" for anything else.
    [[nodiscard]] static QList<QString> wordClassesForPartOfSpeech(QStringView partOfSpeech);

    // The records one line produces, one per spelling, and the keys each is stored under. The
    // reading keys attach to the first spelling alone, as JL does.
    struct ParsedLine
    {
        bool ok = false;
        QList<CustomWordRecord> records;
        QList<QList<QString>> keys; // parallel to records
    };

    [[nodiscard]] static ParsedLine parseLine(const QString &line);

    // Appends one entry to the file at path, creating it when absent, and returns the line
    // written. The Add Word dialog calls this and inserts the same records into the open store's
    // dictionary, so no re-import is needed.
    [[nodiscard]] static QString formatEntry(const QList<QString> &spellings,
                                             const QList<QString> &readings,
                                             const QList<QString> &definitions,
                                             const QString &partOfSpeech,
                                             const QList<QString> &wordClasses);
    [[nodiscard]] static bool appendEntry(const QString &path,
                                          const QList<QString> &spellings,
                                          const QList<QString> &readings,
                                          const QList<QString> &definitions,
                                          const QString &partOfSpeech,
                                          const QList<QString> &wordClasses);
};

} // namespace maru::dict
