// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The KANJIDIC2 importer: a streaming pass over kanjidic2.xml or kanjidic2.xml.gz, one record per
// <character>.
//
// Ported from JL (Apache-2.0), JL.Core/Dicts/KANJIDIC/KanjidicLoader.cs. The key is the raw
// <literal> character with no normalization, so the store's longest key is 1 UTF-16 code unit for
// a BMP kanji and 2 for a supplementary one.
#pragma once

#include "dict/importers/importer.h"

namespace maru::dict
{

class KanjidicImporter : public Importer
{
public:
    KanjidicImporter();
    ~KanjidicImporter() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;
};

} // namespace maru::dict
