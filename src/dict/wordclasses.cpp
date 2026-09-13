// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "wordclasses.h"

#include "core/logging.h"
#include "dict/keynorm.h"

#include <QCborStreamReader>
#include <QCborStreamWriter>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <array>

namespace maru::dict
{

namespace
{

// The file starts with this magic and a version, so a table written by an older build is rejected
// rather than mis-read.
constexpr quint64 tableFormatVersion = 1;

constexpr std::array<QLatin1StringView, 24> deconjugationWordClasses{
    QLatin1StringView("adj-i"), QLatin1StringView("adj-ix"), QLatin1StringView("cop"),   QLatin1StringView("v1"),
    QLatin1StringView("v1-s"),  QLatin1StringView("v4r"),    QLatin1StringView("v5aru"), QLatin1StringView("v5b"),
    QLatin1StringView("v5g"),   QLatin1StringView("v5k"),    QLatin1StringView("v5k-s"), QLatin1StringView("v5m"),
    QLatin1StringView("v5n"),   QLatin1StringView("v5r"),    QLatin1StringView("v5r-i"), QLatin1StringView("v5s"),
    QLatin1StringView("v5t"),   QLatin1StringView("v5u"),    QLatin1StringView("v5u-s"), QLatin1StringView("vk"),
    QLatin1StringView("vs-c"),  QLatin1StringView("vs-i"),   QLatin1StringView("vs-s"),  QLatin1StringView("vz")};

} // namespace

bool isDeconjugationWordClass(QStringView wordClass)
{
    return std::ranges::any_of(deconjugationWordClasses, [wordClass](QLatin1StringView candidate) {
        return wordClass == candidate;
    });
}

void WordClassTable::add(const QString &key, const WordClassEntry &entry)
{
    QList<WordClassEntry> &entries = m_entries[key];
    if (!entries.contains(entry))
        entries.append(entry);
}

void WordClassTable::indexReadings()
{
    const QList<QList<WordClassEntry>> values = m_entries.values();
    for (const QList<WordClassEntry> &entries : values) {
        for (const WordClassEntry &entry : entries) {
            for (const QString &reading : entry.readings) {
                const QString readingKey = normalizeKey(reading);
                QList<WordClassEntry> &target = m_entries[readingKey];
                if (!target.contains(entry))
                    target.append(entry);
            }
        }
    }
}

QList<WordClassEntry> WordClassTable::entriesFor(QStringView key) const
{
    return m_entries.value(key.toString());
}

bool WordClassTable::containsTag(QStringView primarySpelling, QStringView reading, QStringView tag) const
{
    const QList<WordClassEntry> entries = entriesFor(normalizeKey(primarySpelling));
    const bool hasReading = !reading.isEmpty();
    return std::ranges::any_of(entries, [&](const WordClassEntry &entry) {
        if (entry.spelling != primarySpelling)
            return false;
        const bool readingMatches = hasReading ? entry.readings.contains(reading.toString()) : entry.readings.isEmpty();
        return readingMatches && entry.wordClasses.contains(tag.toString());
    });
}

QList<QString> WordClassTable::wordClassesFor(QStringView primarySpelling, QStringView reading) const
{
    const QList<WordClassEntry> entries = entriesFor(normalizeKey(primarySpelling));
    const bool hasReading = !reading.isEmpty();
    const WordClassEntry *found = nullptr;
    for (const WordClassEntry &entry : entries) {
        if (entry.spelling != primarySpelling)
            continue;
        const bool readingMatches = hasReading ? entry.readings.contains(reading.toString()) : entry.readings.isEmpty();
        if (!readingMatches)
            continue;
        if (found != nullptr)
            return {};
        found = &entry;
    }
    return found != nullptr ? found->wordClasses : QList<QString>{};
}

void WordClassTable::clear()
{
    m_entries.clear();
}

bool WordClassTable::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(logDictImport) << "Cannot write the word-class table" << path << file.errorString();
        return false;
    }

    QCborStreamWriter writer(&file);
    writer.startArray();
    writer.append(tableFormatVersion);
    writer.startArray(static_cast<quint64>(m_entries.size()));
    for (auto keyed = m_entries.constBegin(); keyed != m_entries.constEnd(); ++keyed) {
        writer.startArray(2);
        writer.append(keyed.key());
        writer.startArray(static_cast<quint64>(keyed.value().size()));
        for (const WordClassEntry &entry : keyed.value()) {
            writer.startArray(3);
            writer.append(entry.spelling);
            writer.startArray(static_cast<quint64>(entry.wordClasses.size()));
            for (const QString &wordClass : entry.wordClasses)
                writer.append(wordClass);
            writer.endArray();
            writer.startArray(static_cast<quint64>(entry.readings.size()));
            for (const QString &reading : entry.readings)
                writer.append(reading);
            writer.endArray();
            writer.endArray();
        }
        writer.endArray();
        writer.endArray();
    }
    writer.endArray();
    writer.endArray();

    if (!file.commit()) {
        qCWarning(logDictImport) << "Cannot commit the word-class table" << path << file.errorString();
        return false;
    }
    return true;
}

namespace
{

QList<QString> readStringArray(QCborStreamReader &reader, bool &ok)
{
    QList<QString> values;
    if (!ok || !reader.isArray()) {
        ok = false;
        return values;
    }
    ok = reader.enterContainer();
    while (ok && reader.hasNext()) {
        if (!reader.isString()) {
            ok = false;
            break;
        }
        values.append(reader.readAllString());
        ok = reader.lastError() == QCborError::NoError;
    }
    if (ok)
        ok = reader.leaveContainer();
    return values;
}

} // namespace

bool WordClassTable::load(const QString &path)
{
    clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logDict) << "Cannot read the word-class table" << path << file.errorString();
        return false;
    }

    QCborStreamReader reader(&file);
    bool ok = reader.isArray() && reader.enterContainer();
    if (ok && reader.isUnsignedInteger()) {
        const quint64 version = reader.toUnsignedInteger();
        ok = reader.next() && version == tableFormatVersion;
        if (version != tableFormatVersion)
            qCWarning(logDict,
                      "Word-class table %s carries version %llu, this build reads %llu",
                      qUtf8Printable(path),
                      static_cast<unsigned long long>(version),
                      static_cast<unsigned long long>(tableFormatVersion));
    } else {
        ok = false;
    }

    if (ok)
        ok = reader.isArray() && reader.enterContainer();

    while (ok && reader.hasNext()) {
        ok = reader.isArray() && reader.enterContainer();
        if (!ok)
            break;
        if (!reader.isString()) {
            ok = false;
            break;
        }
        const QString key = reader.readAllString();
        ok = reader.lastError() == QCborError::NoError && reader.isArray() && reader.enterContainer();

        QList<WordClassEntry> entries;
        while (ok && reader.hasNext()) {
            ok = reader.isArray() && reader.enterContainer();
            if (!ok)
                break;
            WordClassEntry entry;
            if (!reader.isString()) {
                ok = false;
                break;
            }
            entry.spelling = reader.readAllString();
            ok = reader.lastError() == QCborError::NoError;
            entry.wordClasses = readStringArray(reader, ok);
            entry.readings = readStringArray(reader, ok);
            if (ok)
                ok = reader.leaveContainer();
            entries.append(entry);
        }
        if (ok)
            ok = reader.leaveContainer();
        if (ok)
            ok = reader.leaveContainer();
        if (ok)
            m_entries.insert(key, entries);
    }

    if (!ok) {
        qCWarning(logDict) << "Word-class table" << path << "is malformed";
        clear();
        return false;
    }
    return true;
}

} // namespace maru::dict
