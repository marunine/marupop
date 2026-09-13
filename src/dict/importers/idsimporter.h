// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The kanji-components importer, over cjkvi-ids ids.txt.
//
// JL ships the same data as a prebuilt SQLite file (Resources/Kanji Compositions.sqlite, built by
// JL.Core/Dicts/KanjiComposition/KanjiCompositionDBManager.cs from ids.txt) rather than importing
// the text. marupop imports the text, so the resource is a normal downloadable dictionary rather
// than a binary in the source tree.
//
// A line is tab separated:
//
//   U+6E05<TAB>清<TAB>⿰氵青
//   U+4E38<TAB>丸<TAB>⿻九丶[GJ]<TAB>⿵九丶[TKV]
//
// The third field is an Ideographic Description Sequence: one or two Ideographic Description
// Characters (U+2FF0 to U+2FFF) followed by the components in order. Only the first sequence is
// read, the description characters are dropped, and a trailing [GTJKV] region tag is dropped.
#pragma once

#include "dict/importers/importer.h"

#include <QList>

namespace maru::dict
{

class IdsImporter : public Importer
{
public:
    IdsImporter();
    ~IdsImporter() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;

    // The first-level components one ids.txt line declares, in sequence order, without the
    // character itself and without duplicates. An empty result means the line declares the
    // character as its own only component, which cjkvi-ids does for every atomic character.
    [[nodiscard]] static QList<QString> parseComponents(const QString &line, QString *kanji);
};

} // namespace maru::dict
