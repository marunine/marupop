// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "idsimporter.h"

#include "core/logging.h"
#include "dict/store.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace maru::dict
{

namespace
{

// U+2FF0 to U+2FFF, the Ideographic Description Characters that join the components of a sequence.
bool isDescriptionCharacter(char32_t codePoint)
{
    return codePoint >= 0x2FF0 && codePoint <= 0x2FFF;
}

} // namespace

IdsImporter::IdsImporter() = default;

IdsImporter::~IdsImporter() = default;

QString IdsImporter::name() const
{
    return QStringLiteral("Kanji components");
}

QList<QString> IdsImporter::parseComponents(const QString &line, QString *kanji)
{
    if (kanji != nullptr)
        kanji->clear();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
        return {};

    const QStringList fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3)
        return {};

    const QString &character = fields.at(1);
    if (character.isEmpty())
        return {};
    if (kanji != nullptr)
        *kanji = character;

    // The region tag names the standards a sequence describes; the sequence itself ends before it.
    QString sequence = fields.at(2);
    const qsizetype tagStart = sequence.indexOf(QLatin1Char('['));
    if (tagStart >= 0)
        sequence = sequence.left(tagStart);

    QList<QString> components;
    const QList<uint> codePoints = sequence.toUcs4();
    for (const uint unit : codePoints) {
        const auto codePoint = static_cast<char32_t>(unit);
        if (isDescriptionCharacter(codePoint))
            continue;
        const QString component = QString::fromUcs4(&codePoint, 1);
        if (component == character || components.contains(component))
            continue;
        components.append(component);
    }
    return components;
}

ImportResult
IdsImporter::import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel)
{
    ImportResult result;

    SourceReader reader;
    if (!reader.open(source)) {
        result.errorString = reader.errorString();
        writer.abort();
        return result;
    }

    QTextStream stream(reader.device());
    qint64 lineCount = 0;
    int lastPercent = -1;

    while (!stream.atEnd()) {
        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            writer.abort();
            return result;
        }

        QString kanji;
        const QList<QString> components = parseComponents(stream.readLine(), &kanji);
        ++lineCount;
        if (kanji.isEmpty() || components.isEmpty())
            continue;

        KanjiComponentsRecord record;
        record.kanji = kanji;
        record.components = components;

        Record storeRecord;
        storeRecord.type = DictType::KanjiComponents;
        storeRecord.data = record;
        const std::vector<QString> keys{kanjiComponentsKey(kanji)};
        if (writer.addRecord(storeRecord, keys) < 0) {
            result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
            writer.abort();
            return result;
        }

        if ((lineCount & 0x03FF) == 0 && progress) {
            const int percent = reader.percent();
            if (percent != lastPercent) {
                lastPercent = percent;
                progress(percent, QStringLiteral("Reading the kanji component list"));
            }
        }
    }

    writer.setMeta(metakeys::sourceFormat, QStringLiteral("cjkvi-ids"));
    if (!writer.finish()) {
        result.errorString = QStringLiteral("Cannot finish writing the dictionary database");
        return result;
    }

    result.ok = true;
    result.recordCount = writer.recordCount();
    result.keyCount = writer.keyCount();
    result.maxKeyLength = writer.maxKeyLength();
    qCInfo(logDictImport,
           "Kanji components: %lld lines, %lld records",
           static_cast<long long>(lineCount),
           static_cast<long long>(result.recordCount));
    return result;
}

} // namespace maru::dict
