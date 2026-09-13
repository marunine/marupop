// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "jmnedictimporter.h"

#include "core/logging.h"
#include "dict/keynorm.h"
#include "dict/store.h"

#include <QSet>
#include <QXmlStreamReader>

namespace maru::dict
{

namespace
{

struct Translation
{
    QList<QString> details;   // trans_det
    QList<QString> nameTypes; // name_type entity names
};

struct Entry
{
    qint32 id = 0;
    QList<QString> kanjiForms;
    QList<QString> readingForms;
    QList<Translation> translations;
};

template <typename T>
QList<T> withoutIndex(const QList<T> &values, qsizetype index)
{
    QList<T> result;
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (i != index)
            result.append(values.at(i));
    }
    return result;
}

// Every inner list empty means JL's TrimNullableArray() would have produced null.
QList<QList<QString>> trimIfAllEmpty(QList<QList<QString>> values)
{
    for (const QList<QString> &inner : values) {
        if (!inner.isEmpty())
            return values;
    }
    return {};
}

QString readSingleChild(QXmlStreamReader &xml, QLatin1StringView containerName, QLatin1StringView childName)
{
    QString value;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == containerName)
            break;
        if (token != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == childName)
            value = xml.readElementText();
        else
            xml.skipCurrentElement();
    }
    return value;
}

Translation readTranslation(QXmlStreamReader &xml, const DtdEntityMap &entities)
{
    Translation translation;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("trans"))
            break;
        if (token != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == QLatin1String("name_type"))
            translation.nameTypes.append(entities.shortName(xml.readElementText()));
        else if (xml.name() == QLatin1String("trans_det"))
            translation.details.append(xml.readElementText());
        else
            xml.skipCurrentElement();
    }
    return translation;
}

Entry readEntry(QXmlStreamReader &xml, const DtdEntityMap &entities)
{
    Entry entry;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("entry"))
            break;
        if (token != QXmlStreamReader::StartElement)
            continue;

        const QStringView name = xml.name();
        if (name == QLatin1String("ent_seq"))
            entry.id = xml.readElementText().toInt();
        else if (name == QLatin1String("k_ele"))
            entry.kanjiForms.append(readSingleChild(xml, QLatin1StringView("k_ele"), QLatin1StringView("keb")));
        else if (name == QLatin1String("r_ele"))
            entry.readingForms.append(readSingleChild(xml, QLatin1StringView("r_ele"), QLatin1StringView("reb")));
        else if (name == QLatin1String("trans"))
            entry.translations.append(readTranslation(xml, entities));
        else
            xml.skipCurrentElement();
    }
    return entry;
}

} // namespace

JmnedictImporter::JmnedictImporter() = default;

JmnedictImporter::~JmnedictImporter() = default;

QString JmnedictImporter::name() const
{
    return QStringLiteral("JMnedict");
}

ImportResult JmnedictImporter::import(const QString &source,
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

    m_entities = {};
    DtdEntityMap entities;
    QXmlStreamReader xml(reader.device());
    qint64 entryCount = 0;
    int lastPercent = -1;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::DTD) {
            entities.readDeclarations(xml);
            continue;
        }
        if (token != QXmlStreamReader::StartElement || xml.name() != QLatin1String("entry"))
            continue;

        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            writer.abort();
            return result;
        }

        const Entry entry = readEntry(xml, entities);
        ++entryCount;
        if (entry.translations.isEmpty())
            continue;

        QList<QList<QString>> definitions;
        QList<QList<QString>> nameTypes;
        for (const Translation &translation : entry.translations) {
            definitions.append(translation.details);
            nameTypes.append(translation.nameTypes);
        }
        nameTypes = trimIfAllEmpty(nameTypes);

        // The readings are keys only when the entry has no kanji form, which is what keeps a name
        // dictionary from answering every kana span in the text (JmnedictLoader.cs).
        const QList<QString> &headwords = entry.kanjiForms.isEmpty() ? entry.readingForms : entry.kanjiForms;
        const bool fromKanji = !entry.kanjiForms.isEmpty();

        QSet<QString> seenKeys;
        for (qsizetype i = 0; i < headwords.size(); ++i) {
            const QString key = normalizeKey(headwords.at(i));
            if (key.isEmpty() || seenKeys.contains(key))
                continue;
            seenKeys.insert(key);

            JmnedictRecord record;
            record.entryId = entry.id;
            record.primarySpelling = headwords.at(i);
            record.alternativeSpellings = withoutIndex(headwords, i);
            record.readings = fromKanji ? entry.readingForms : QList<QString>{};
            record.definitions = definitions;
            record.nameTypes = nameTypes;

            Record storeRecord;
            storeRecord.type = DictType::JMnedict;
            storeRecord.data = record;
            const std::vector<QString> keys{key};
            if (writer.addRecord(storeRecord, keys) < 0) {
                result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
                writer.abort();
                return result;
            }
        }

        if ((entryCount & 0x0FFF) == 0 && progress) {
            const int percent = reader.percent();
            if (percent != lastPercent) {
                lastPercent = percent;
                progress(percent, QStringLiteral("Reading JMnedict"));
            }
        }
    }

    if (xml.hasError()) {
        result.errorString = xml.errorString();
        writer.abort();
        return result;
    }

    m_entities = entities.entities();
    writer.setMeta(metakeys::entities, entities.toJson());
    writer.setMeta(metakeys::sourceFormat, QStringLiteral("JMnedict"));
    if (!writer.finish()) {
        result.errorString = QStringLiteral("Cannot finish writing the dictionary database");
        return result;
    }

    result.ok = true;
    result.recordCount = writer.recordCount();
    result.keyCount = writer.keyCount();
    result.maxKeyLength = writer.maxKeyLength();
    qCInfo(logDictImport,
           "JMnedict: %lld entries, %lld records, %lld keys",
           static_cast<long long>(entryCount),
           static_cast<long long>(result.recordCount),
           static_cast<long long>(result.keyCount));
    return result;
}

} // namespace maru::dict
