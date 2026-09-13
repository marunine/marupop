// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "kanjidicimporter.h"

#include "core/logging.h"
#include "dict/store.h"

#include <QXmlStreamReader>

namespace maru::dict
{

KanjidicImporter::KanjidicImporter() = default;

KanjidicImporter::~KanjidicImporter() = default;

QString KanjidicImporter::name() const
{
    return QStringLiteral("KANJIDIC2");
}

ImportResult KanjidicImporter::import(const QString &source,
                                      StoreWriter &writer,
                                      const ProgressFn &progress,
                                      std::atomic_bool &cancel)
{
    ImportResult result;

    SourceReader reader;
    if (!reader.open(source)) {
        result.errorString = reader.errorString();
        writer.abort();
        return result;
    }

    QXmlStreamReader xml(reader.device());
    qint64 characterCount = 0;
    int lastPercent = -1;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token != QXmlStreamReader::StartElement || xml.name() != QLatin1String("character"))
            continue;

        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            writer.abort();
            return result;
        }

        QString literal;
        KanjidicRecord record;

        while (!xml.atEnd()) {
            const QXmlStreamReader::TokenType inner = xml.readNext();
            if (inner == QXmlStreamReader::EndElement && xml.name() == QLatin1String("character"))
                break;
            if (inner != QXmlStreamReader::StartElement)
                continue;

            const QStringView name = xml.name();
            if (name == QLatin1String("literal")) {
                literal = xml.readElementText();
            } else if (name == QLatin1String("grade")) {
                record.grade = static_cast<quint8>(xml.readElementText().toInt());
            } else if (name == QLatin1String("stroke_count")) {
                // A character carries one <stroke_count> per accepted count, the first of which is
                // the standard one, so a later element does not overwrite it.
                const auto strokes = static_cast<quint8>(xml.readElementText().toInt());
                if (record.strokeCount == 0)
                    record.strokeCount = strokes;
            } else if (name == QLatin1String("freq")) {
                record.frequency = xml.readElementText().toInt();
            } else if (name == QLatin1String("meaning")) {
                // A <meaning> with an m_lang attribute is a translation into another language.
                if (xml.attributes().isEmpty())
                    record.definitions.append(xml.readElementText());
                else
                    xml.skipCurrentElement();
            } else if (name == QLatin1String("nanori")) {
                record.nanoriReadings.append(xml.readElementText());
            } else if (name == QLatin1String("rad_name")) {
                record.radicalNames.append(xml.readElementText());
            } else if (name == QLatin1String("reading")) {
                const QString type = xml.attributes().value(QLatin1String("r_type")).toString();
                if (type == QLatin1String("ja_on"))
                    record.onReadings.append(xml.readElementText());
                else if (type == QLatin1String("ja_kun"))
                    record.kunReadings.append(xml.readElementText());
                else
                    xml.skipCurrentElement();
            }
        }

        if (literal.isEmpty())
            continue;

        Record storeRecord;
        storeRecord.type = DictType::Kanjidic;
        storeRecord.data = record;
        const std::vector<QString> keys{literal};
        if (writer.addRecord(storeRecord, keys) < 0) {
            result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
            writer.abort();
            return result;
        }

        ++characterCount;
        if ((characterCount & 0x03FF) == 0 && progress) {
            const int percent = reader.percent();
            if (percent != lastPercent) {
                lastPercent = percent;
                progress(percent, QStringLiteral("Reading KANJIDIC2"));
            }
        }
    }

    if (xml.hasError()) {
        result.errorString = xml.errorString();
        writer.abort();
        return result;
    }

    writer.setMeta(metakeys::sourceFormat, QStringLiteral("KANJIDIC2"));
    if (!writer.finish()) {
        result.errorString = QStringLiteral("Cannot finish writing the dictionary database");
        return result;
    }

    result.ok = true;
    result.recordCount = writer.recordCount();
    result.keyCount = writer.keyCount();
    result.maxKeyLength = writer.maxKeyLength();
    qCInfo(logDictImport,
           "KANJIDIC2: %lld characters, %lld records",
           static_cast<long long>(characterCount),
           static_cast<long long>(result.recordCount));
    return result;
}

} // namespace maru::dict
