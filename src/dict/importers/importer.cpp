// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "importer.h"

#include "core/logging.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>

#include <KCompressionDevice>

#include <algorithm>

namespace maru::dict
{

Importer::~Importer() = default;

SourceReader::SourceReader() = default;

SourceReader::~SourceReader() = default;

bool SourceReader::open(const QString &path)
{
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::ReadOnly)) {
        m_errorString = m_file->errorString();
        m_file = nullptr;
        return false;
    }
    m_size = m_file->size();

    // The gzip magic rather than the file name: the EDRDG downloads arrive as JMdict_e.gz and are
    // stored gunzipped, and a user who renames a plain XML file to .gz or an archive to .xml
    // still gets the right reader.
    const QByteArray magic = m_file->peek(2);
    if (magic.size() == 2 && static_cast<quint8>(magic[0]) == 0x1F && static_cast<quint8>(magic[1]) == 0x8B) {
        auto decompressor = std::make_unique<KCompressionDevice>(m_file.get(), false, KCompressionDevice::GZip);
        if (!decompressor->open(QIODevice::ReadOnly)) {
            m_errorString = decompressor->errorString();
            m_file = nullptr;
            return false;
        }
        m_decompressor = std::move(decompressor);
        m_device = m_decompressor.get();
    } else {
        m_device = m_file.get();
    }
    return true;
}

int SourceReader::percent() const
{
    if (m_file == nullptr || m_size <= 0)
        return 0;
    const qint64 position = m_file->pos();
    return static_cast<int>(std::min<qint64>(100, position * 100 / m_size));
}

void DtdEntityMap::readDeclarations(const QXmlStreamReader &reader)
{
    const QList<QXmlStreamEntityDeclaration> declarations = reader.entityDeclarations();
    for (const QXmlStreamEntityDeclaration &declaration : declarations) {
        const QString name = declaration.name().toString();
        const QString value = declaration.value().toString();
        if (name.isEmpty() || value.isEmpty())
            continue;
        // An XML processor takes the first declaration of a name, so a repeated declaration is
        // ignored here as well.
        if (!m_entities.contains(name))
            m_entities.insert(name, value);
        if (!m_shortNames.contains(value)) {
            // The reverse direction is the point: the parser recovers a short name from the text the
            // expanded entity produced.
            // NOLINTNEXTLINE(readability-suspicious-call-argument)
            m_shortNames.insert(value, name);
        }
    }
}

QString DtdEntityMap::shortName(const QString &text) const
{
    const auto found = m_shortNames.constFind(text);
    return found != m_shortNames.constEnd() ? found.value() : text;
}

QString DtdEntityMap::toJson() const
{
    QJsonObject object;
    for (auto entry = m_entities.constBegin(); entry != m_entities.constEnd(); ++entry)
        object.insert(entry.key(), entry.value());
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace maru::dict
