// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "customnameimporter.h"

#include "core/logging.h"
#include "dict/keynorm.h"
#include "dict/store.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace maru::dict
{

CustomNameImporter::CustomNameImporter() = default;

CustomNameImporter::~CustomNameImporter() = default;

QString CustomNameImporter::name() const
{
    return QStringLiteral("Custom name list");
}

std::optional<CustomNameRecord> CustomNameImporter::parseLine(const QString &line)
{
    const QStringList fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3)
        return std::nullopt;

    CustomNameRecord record;
    record.primarySpelling = fields.at(0).trimmed();
    record.reading = fields.at(1).trimmed();
    record.nameType = fields.at(2).trimmed();
    if (record.primarySpelling.isEmpty())
        return std::nullopt;
    if (record.reading == record.primarySpelling)
        record.reading.clear();

    if (fields.size() >= 4)
        record.extraInfo = fields.at(3).trimmed().replace(QLatin1String("\\n"), QLatin1String("\n"));
    if (fields.size() >= 5) {
        const QString imagePath = fields.at(4).trimmed();
        if (!imagePath.isEmpty()) {
            ImageInfo image;
            image.path = imagePath;
            record.image = image;
        }
    }
    return record;
}

QString CustomNameImporter::formatEntry(const QString &spelling,
                                        const QString &reading,
                                        const QString &nameType,
                                        const QString &extraInfo,
                                        const QString &imagePath)
{
    QString line = spelling + QLatin1Char('\t') + reading + QLatin1Char('\t') + nameType;
    if (!extraInfo.isEmpty() || !imagePath.isEmpty())
        line += QLatin1Char('\t') + QString(extraInfo).replace(QLatin1String("\n"), QLatin1String("\\n"));
    if (!imagePath.isEmpty())
        line += QLatin1Char('\t') + imagePath;
    return line;
}

bool CustomNameImporter::appendEntry(const QString &path,
                                     const QString &spelling,
                                     const QString &reading,
                                     const QString &nameType,
                                     const QString &extraInfo,
                                     const QString &imagePath)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCWarning(logDictImport) << "Cannot append to" << path << file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream << formatEntry(spelling, reading, nameType, extraInfo, imagePath) << Qt::endl;
    return true;
}

ImportResult CustomNameImporter::import(const QString &source,
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

        const std::optional<CustomNameRecord> record = parseLine(stream.readLine());
        ++lineCount;
        if (!record.has_value())
            continue;

        Record storeRecord;
        storeRecord.type = DictType::CustomName;
        storeRecord.data = *record;
        const std::vector<QString> keys{normalizeKey(record->primarySpelling)};
        if (writer.addRecord(storeRecord, keys) < 0) {
            result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
            writer.abort();
            return result;
        }

        if ((lineCount & 0xFF) == 0 && progress)
            progress(static_cast<int>(file.pos() * 100 / size), QStringLiteral("Reading the custom name list"));
    }

    writer.setMeta(metakeys::sourceFormat, QStringLiteral("Custom names"));
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
