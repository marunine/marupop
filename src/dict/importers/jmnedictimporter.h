// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The JMnedict importer: a streaming pass over JMnedict.xml or JMnedict.xml.gz.
//
// Ported from JL (Apache-2.0), JL.Core/Dicts/JMnedict/JmnedictLoader.cs. The element set is much
// smaller than JMdict's: one entry holds kanji forms, reading forms and translation blocks, and a
// record is built per kanji form, or per reading form when the entry has no kanji form.
#pragma once

#include "dict/importers/importer.h"

#include <QMap>

namespace maru::dict
{

class JmnedictImporter : public Importer
{
public:
    JmnedictImporter();
    ~JmnedictImporter() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) override;

    // The DTD entity map, short name_type name to description.
    [[nodiscard]] const QMap<QString, QString> &entities() const
    {
        return m_entities;
    }

private:
    QMap<QString, QString> m_entities;
};

} // namespace maru::dict
