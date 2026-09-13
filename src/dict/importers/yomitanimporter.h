// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Yomitan (Yomichan) importer, over an extracted dictionary directory or a .zip.
//
// Ported from JL (Apache-2.0): JL.Core/Dicts/EPWING/Yomichan/EpwingYomichanLoader.cs for the term
// banks, JL.Core/Dicts/KanjiDict/YomichanKanjiLoader.cs for the kanji banks,
// JL.Core/Dicts/PitchAccent/YomichanPitchAccentLoader.cs and
// JL.Core/Dicts/PitchAccent/PitchAccentRecord.cs for the pitch rows, and
// JL.Core/Freqs/FrequencyYomichan/FrequencyYomichanLoader.cs for the frequency rows.
//
// Four differences from JL:
//  - index.json's format/version is read, so a v1 term bank whose glossary occupies the tail of
//    the row is imported. JL assumes v3 unconditionally and would fail on a v1 bank.
//  - term_meta_bank rows are dispatched on their mode element rather than on the type the user
//    registered the dictionary as, so one directory can feed both a pitch dictionary and a
//    frequency list.
//  - tag_bank_*.json is read into the store's meta table. Neither JL nor meikipop reads it, which
//    is what leaves their definition tags opaque.
//  - structured content is rendered to the Qt rich-text subset rather than flattened to plain
//    text (see importers/structuredcontent.h).
#pragma once

#include "dict/dicttypes.h"
#include "dict/importers/importer.h"

#include <QList>
#include <QString>

#include <memory>

class QTemporaryDir;

namespace maru::dict
{

// The index.json fields marupop reads.
struct YomitanIndex
{
    bool valid = false;
    QString title;
    QString revision;
    QString author;
    QString description;
    QString attribution;
    QString url;      // the dictionary's home page
    QString indexUrl; // the update endpoint, present when isUpdatable is true
    // 1 or 3. Yomitan writes it as "format" and older dictionaries as "version".
    int format = 3;
    bool isUpdatable = false;
    // "rank-based" or "occurrence-based". An occurrence-based list means a higher value is more
    // frequent, which inverts the comparison in the popup and in the result ordering.
    QString frequencyMode;
};

class YomitanImporter : public Importer
{
public:
    // targetType selects which bank files are read:
    //   YomitanWord, YomitanName, YomitanOther, YomitanKanjiWordSchema -> term_bank_*.json
    //   YomitanKanji                                                   -> kanji_bank_*.json
    //   YomitanPitchAccent      -> term_meta_bank_*.json, rows whose mode is "pitch"
    //   YomitanFrequency        -> term_meta_bank_*.json, rows whose mode is "freq"
    //   YomitanKanjiFrequency   -> kanji_meta_bank_*.json
    explicit YomitanImporter(DictType targetType);
    ~YomitanImporter() override;

    // Where a .zip source is unpacked. The manager passes the dictionary's own directory under
    // paths::dictionariesDir(), so the images the records reference survive the import. An empty
    // value unpacks into a temporary directory that is removed with the importer, which leaves
    // the images unreachable.
    void setExtractDirectory(const QString &directory);

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;

    // The directory the banks were read from, which differs from source when source was a .zip.
    [[nodiscard]] QString resolvedPath() const
    {
        return m_resolvedPath;
    }

    [[nodiscard]] const YomitanIndex &index() const
    {
        return m_index;
    }

    // index.json of a directory or a .zip, without importing anything. The Add Dictionary dialog
    // prefills the name, the revision and the update settings from it.
    [[nodiscard]] static YomitanIndex readIndex(const QString &source);

    // The dictionary types source can supply, from the bank files it holds. source is a directory
    // or a .zip; a .zip is classified from its entry list and its term_meta_bank rows, without
    // being extracted. A source holding term_meta_bank files with both modes reports
    // YomitanPitchAccent and YomitanFrequency, so the Add dialog can offer to create both.
    [[nodiscard]] static QList<DictType> detectTypes(const QString &source);

private:
    [[nodiscard]] QString resolveSource(const QString &source, QString *errorString);

    DictType m_targetType;
    QString m_extractDirectory;
    QString m_resolvedPath;
    YomitanIndex m_index;
    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
};

} // namespace maru::dict
