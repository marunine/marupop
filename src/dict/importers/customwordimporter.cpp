// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "customwordimporter.h"

#include "core/logging.h"
#include "dict/keynorm.h"
#include "dict/store.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace maru::dict
{

namespace
{

QList<QString> splitList(QStringView field)
{
    QList<QString> values;
    const QList<QStringView> parts = field.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QStringView part : parts) {
        const QString value = part.trimmed().toString();
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}

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

} // namespace

CustomWordImporter::CustomWordImporter() = default;

CustomWordImporter::~CustomWordImporter() = default;

QString CustomWordImporter::name() const
{
    return QStringLiteral("Custom word list");
}

QList<QString> CustomWordImporter::wordClassesForPartOfSpeech(QStringView partOfSpeech)
{
    if (partOfSpeech == QLatin1String("Verb")) {
        return {QStringLiteral("v1"),  QStringLiteral("v1-s"), QStringLiteral("v4r"),  QStringLiteral("v5aru"),
                QStringLiteral("v5b"), QStringLiteral("v5g"),  QStringLiteral("v5k"),  QStringLiteral("v5k-s"),
                QStringLiteral("v5m"), QStringLiteral("v5n"),  QStringLiteral("v5r"),  QStringLiteral("v5r-i"),
                QStringLiteral("v5s"), QStringLiteral("v5t"),  QStringLiteral("v5u"),  QStringLiteral("v5u-s"),
                QStringLiteral("vk"),  QStringLiteral("vs-c"), QStringLiteral("vs-i"), QStringLiteral("vs-s"),
                QStringLiteral("vz")};
    }
    if (partOfSpeech == QLatin1String("Adjective"))
        return {QStringLiteral("adj-i"), QStringLiteral("adj-ix")};
    if (partOfSpeech == QLatin1String("Noun"))
        return {QStringLiteral("n")};
    return {QStringLiteral("other")};
}

CustomWordImporter::ParsedLine CustomWordImporter::parseLine(const QString &line)
{
    ParsedLine parsed;
    const QStringList fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 4)
        return parsed;

    const QList<QString> spellings = splitList(QStringView(fields.at(0)).trimmed());
    QList<QString> readings = splitList(QStringView(fields.at(1)).trimmed());
    const QString definitionsField = QString(fields.at(2)).trimmed().replace(QLatin1String("\\n"), QLatin1String("\n"));
    const QList<QString> definitions = splitList(definitionsField);
    const QString partOfSpeech = fields.at(3).trimmed();
    const QList<QString> userWordClasses =
        fields.size() >= 5 ? splitList(QStringView(fields.at(4)).trimmed()) : QList<QString>{};

    if (spellings.isEmpty() || definitions.isEmpty())
        return parsed;

    // A single spelling that equals its single reading carries no reading.
    if (readings.isEmpty() || (spellings.size() == 1 && readings.size() == 1 && spellings.first() == readings.first()))
        readings.clear();

    const bool hasUserDefined = !userWordClasses.isEmpty();
    const QList<QString> wordClasses = hasUserDefined ? userWordClasses : wordClassesForPartOfSpeech(partOfSpeech);

    for (qsizetype i = 0; i < spellings.size(); ++i) {
        CustomWordRecord record;
        record.primarySpelling = spellings.at(i);
        record.alternativeSpellings = withoutIndex(spellings, i);
        record.readings = readings;
        record.definitions = definitions;
        record.wordClasses = wordClasses;
        record.hasUserDefinedWordClass = hasUserDefined;

        QList<QString> keys{normalizeKey(record.primarySpelling)};
        if (i == 0) {
            for (const QString &reading : readings) {
                const QString readingKey = normalizeKey(reading);
                if (!keys.contains(readingKey))
                    keys.append(readingKey);
            }
        }

        parsed.records.append(record);
        parsed.keys.append(keys);
    }

    parsed.ok = true;
    return parsed;
}

QString CustomWordImporter::formatEntry(const QList<QString> &spellings,
                                        const QList<QString> &readings,
                                        const QList<QString> &definitions,
                                        const QString &partOfSpeech,
                                        const QList<QString> &wordClasses)
{
    QList<QString> escapedDefinitions;
    for (const QString &definition : definitions)
        escapedDefinitions.append(QString(definition).replace(QLatin1String("\n"), QLatin1String("\\n")));

    QString line = QStringList(spellings).join(QLatin1Char(';')) + QLatin1Char('\t') +
                   QStringList(readings).join(QLatin1Char(';')) + QLatin1Char('\t') +
                   QStringList(escapedDefinitions).join(QLatin1Char(';')) + QLatin1Char('\t') + partOfSpeech;
    if (!wordClasses.isEmpty())
        line += QLatin1Char('\t') + QStringList(wordClasses).join(QLatin1Char(';'));
    return line;
}

bool CustomWordImporter::appendEntry(const QString &path,
                                     const QList<QString> &spellings,
                                     const QList<QString> &readings,
                                     const QList<QString> &definitions,
                                     const QString &partOfSpeech,
                                     const QList<QString> &wordClasses)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCWarning(logDictImport) << "Cannot append to" << path << file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream << formatEntry(spellings, readings, definitions, partOfSpeech, wordClasses) << Qt::endl;
    return true;
}

ImportResult CustomWordImporter::import(const QString &source,
                                        StoreWriter &writer,
                                        const ProgressFn &progress,
                                        std::atomic_bool &cancel)
{
    ImportResult result;

    QFile file(source);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorString = file.errorString();
        writer.abort();
        return result;
    }

    QTextStream stream(&file);
    const qint64 size = std::max<qint64>(1, file.size());
    qint64 lineCount = 0;

    while (!stream.atEnd()) {
        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            writer.abort();
            return result;
        }

        const ParsedLine parsed = parseLine(stream.readLine());
        ++lineCount;
        if (!parsed.ok)
            continue;

        for (qsizetype i = 0; i < parsed.records.size(); ++i) {
            Record storeRecord;
            storeRecord.type = DictType::CustomWord;
            storeRecord.data = parsed.records.at(i);
            const QList<QString> &keys = parsed.keys.at(i);
            const std::vector<QString> keyVector(keys.cbegin(), keys.cend());
            if (writer.addRecord(storeRecord, keyVector) < 0) {
                result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
                writer.abort();
                return result;
            }
        }

        if ((lineCount & 0xFF) == 0 && progress)
            progress(static_cast<int>(file.pos() * 100 / size), QStringLiteral("Reading the custom word list"));
    }

    writer.setMeta(metakeys::sourceFormat, QStringLiteral("Custom words"));
    if (!writer.finish()) {
        result.errorString = QStringLiteral("Cannot finish writing the dictionary database");
        return result;
    }

    result.ok = true;
    result.recordCount = writer.recordCount();
    result.keyCount = writer.keyCount();
    result.maxKeyLength = writer.maxKeyLength();
    return result;
}

} // namespace maru::dict
