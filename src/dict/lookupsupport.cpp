// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "lookupsupport.h"

#include "dict/keynorm.h"
#include "dict/store.h"
#include "jp/japanese.h"

namespace maru::dict
{

namespace
{

bool startsWithKatakana(QStringView text)
{
    return !text.isEmpty() && jp::isKatakana(text.at(0).unicode());
}

QList<QList<quint8>>
collectPitchPositions(const DictionaryHandle &pitchDict, QStringView primarySpelling, const QList<QString> &readings)
{
    QList<QList<quint8>> positions;
    if (!pitchDict.store)
        return positions;

    const QList<QString> probes = readings.isEmpty() ? QList<QString>{primarySpelling.toString()} : readings;
    for (const QString &reading : probes) {
        QList<quint8> found;
        const std::vector<std::shared_ptr<const Record>> records = find(pitchDict, normalizeKey(reading));
        for (const std::shared_ptr<const Record> &record : records) {
            const PitchAccentRecord *pitch = asPitchAccent(*record);
            if (pitch == nullptr)
                continue;
            // A row matches when its own reading is the reading being asked about, or when it
            // carries no reading and its spelling is the headword.
            const bool matchesReading = !pitch->reading.isEmpty() && pitch->reading == reading;
            const bool matchesSpelling = pitch->spelling == primarySpelling || pitch->spelling == reading;
            if (!matchesReading && !matchesSpelling)
                continue;
            found = pitch->positions;
            break;
        }
        positions.append(found);
    }
    return positions;
}

} // namespace

std::vector<std::shared_ptr<const Record>> find(const DictionaryHandle &dictionary, QStringView normalizedKey)
{
    return dictionary.find(normalizedKey);
}

std::optional<int>
frequencyFor(const DictionaryHandle &freqDict, QStringView primarySpelling, const QList<QString> &readings)
{
    if (!freqDict.store)
        return std::nullopt;

    const std::vector<std::shared_ptr<const Record>> bySpelling = find(freqDict, normalizeKey(primarySpelling));
    if (!bySpelling.empty()) {
        for (const std::shared_ptr<const Record> &record : bySpelling) {
            const FrequencyRecord *frequency = asFrequency(*record);
            if (frequency == nullptr)
                continue;
            if (frequency->spelling == primarySpelling || readings.contains(frequency->spelling))
                return frequency->frequency;
        }
        return std::nullopt;
    }

    for (const QString &reading : readings) {
        const std::vector<std::shared_ptr<const Record>> byReading = find(freqDict, normalizeKey(reading));
        for (const std::shared_ptr<const Record> &record : byReading) {
            const FrequencyRecord *frequency = asFrequency(*record);
            if (frequency == nullptr)
                continue;
            if (frequency->spelling == primarySpelling ||
                (frequency->spelling == reading && startsWithKatakana(reading))) {
                return frequency->frequency;
            }
        }
    }
    return std::nullopt;
}

QList<std::optional<quint8>>
pitchPositionsFor(const DictionaryHandle &pitchDict, QStringView primarySpelling, const QList<QString> &readings)
{
    QList<std::optional<quint8>> first;
    const QList<QList<quint8>> positions = collectPitchPositions(pitchDict, primarySpelling, readings);
    for (const QList<quint8> &reading : positions)
        first.append(reading.isEmpty() ? std::optional<quint8>{} : std::optional<quint8>{reading.first()});
    return first;
}

QList<QList<quint8>>
allPitchPositionsFor(const DictionaryHandle &pitchDict, QStringView primarySpelling, const QList<QString> &readings)
{
    return collectPitchPositions(pitchDict, primarySpelling, readings);
}

QList<KanjiExample> kanjiExamplesFor(const DictionaryHandle &jmdict, QStringView kanji)
{
    const std::vector<std::shared_ptr<const Record>> records = find(jmdict, kanjiExamplesKey(kanji));
    for (const std::shared_ptr<const Record> &record : records) {
        if (const KanjiExamplesRecord *examples = asKanjiExamples(*record))
            return examples->examples;
    }
    return {};
}

QList<QString> kanjiComponentsFor(const DictionaryHandle &components, QStringView kanji)
{
    const std::vector<std::shared_ptr<const Record>> records = find(components, kanjiComponentsKey(kanji));
    for (const std::shared_ptr<const Record> &record : records) {
        if (const KanjiComponentsRecord *composition = asKanjiComponents(*record))
            return composition->components;
    }
    return {};
}

} // namespace maru::dict
