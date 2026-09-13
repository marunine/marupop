// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The custom name list importer, over JL's tab-separated custom_names.txt format.
//
// Ported from JL (Apache-2.0), JL.Core/Dicts/CustomNameDict/CustomNameLoader.cs.
//
//   <spelling>\t<reading>\t<nameType>[\t<extraInfo>[\t<imagePath>]]
//
// A reading equal to the spelling is dropped, a literal \n in extraInfo becomes a newline, and the
// spelling is the only search key.
#pragma once

#include "dict/importers/importer.h"
#include "dict/records.h"

#include <optional>

namespace maru::dict
{

class CustomNameImporter : public Importer
{
public:
    CustomNameImporter();
    ~CustomNameImporter() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;

    // The record one line produces, or nothing when the line holds fewer than three fields.
    [[nodiscard]] static std::optional<CustomNameRecord> parseLine(const QString &line);

    [[nodiscard]] static QString formatEntry(const QString &spelling,
                                             const QString &reading,
                                             const QString &nameType,
                                             const QString &extraInfo,
                                             const QString &imagePath);
    [[nodiscard]] static bool appendEntry(const QString &path,
                                          const QString &spelling,
                                          const QString &reading,
                                          const QString &nameType,
                                          const QString &extraInfo,
                                          const QString &imagePath);
};

} // namespace maru::dict
