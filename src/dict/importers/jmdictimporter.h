// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The JMdict importer: a streaming QXmlStreamReader pass over JMdict_e.xml or JMdict_e.gz that
// explodes every <entry> into one record per headword.
//
// Ported from JL (Apache-2.0): JL.Core/Dicts/JMdict/JmdictLoader.cs for the element handling and
// the ISO 639-2/B language table, and JL.Core/Dicts/JMdict/JmdictRecordBuilder.cs for the
// explosion, the re_restr and stagk/stagr filtering, the sK/sk search-only forms and the
// shared/exclusive sense-tag factoring.
//
// Two additions JL does not have: ke_pri and re_pri are parsed into JmdictRecord::priorityRank
// (JL reads and discards them, JmdictLoader.cs), and the kanji example words the popup's
// kanji card shows are collected while the records are built.
#pragma once

#include "dict/importers/importer.h"
#include "dict/wordclasses.h"

#include <QMap>
#include <QString>

namespace maru::dict
{

// The rank JMdict's priority tags give a headword, in words, where a lower value is more common
// and 0 means the headword carries no priority tag.
//
// The tags name bands of a frequency-ordered corpus, and each band is mapped to the rank of its
// last member, so a rank compares directly against a frequency list's rank:
//
//   nfNN  -> NN * 500   the newspaper frequency band, 500 words wide (nf01 -> 500, nf48 -> 24000)
//   ichi1, news1, spec1, gai1 -> 12000   the first band of their list
//   ichi2, news2, spec2, gai2 -> 24000   the second band
//
// A headword carrying several tags takes the smallest rank among them, so a headword tagged
// news1 and nf03 ranks 1500 rather than 12000: nf03 is the narrower statement about the same
// corpus. The lookup engine uses the result only when no frequency dictionary is active.
[[nodiscard]] qint32 jmdictPriorityRank(const QList<QString> &priorityTags);

class JmdictImporter : public Importer
{
public:
    // properNameEntries follows JL's ProperNameEntries option: entries with an ent_seq in
    // 5000000 to 5999999 are the proper names that overlap JMnedict, and are skipped when it is
    // false.
    explicit JmdictImporter(bool properNameEntries = true);
    ~JmdictImporter() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;

    // The DTD entity map, short name to description, filled during import() and written into the
    // store's meta table under metaKeys::entities.
    [[nodiscard]] const QMap<QString, QString> &entities() const
    {
        return m_entities;
    }

    // The deconjugation gate's table, filled during import(). DictionaryImportJob writes it to
    // <dictionary id>.pos beside the store.
    [[nodiscard]] const WordClassTable &wordClassTable() const
    {
        return m_wordClasses;
    }

    [[nodiscard]] WordClassTable &wordClassTable()
    {
        return m_wordClasses;
    }

private:
    bool m_properNameEntries = true;
    QMap<QString, QString> m_entities;
    WordClassTable m_wordClasses;
};

} // namespace maru::dict
