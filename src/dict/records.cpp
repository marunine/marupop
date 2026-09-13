// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "records.h"

namespace maru::dict
{

QList<QString> SenseTags::forSense(qsizetype senseIndex) const
{
    QList<QString> tags = sharedByAllSenses;
    if (senseIndex >= 0 && senseIndex < perSense.size())
        tags += perSense.at(senseIndex);
    return tags;
}

namespace
{

template <typename T>
const T *alternative(const Record &record)
{
    return std::get_if<T>(&record.data);
}

} // namespace

const JmdictRecord *asJmdict(const Record &record)
{
    return alternative<JmdictRecord>(record);
}

const JmnedictRecord *asJmnedict(const Record &record)
{
    return alternative<JmnedictRecord>(record);
}

const KanjidicRecord *asKanjidic(const Record &record)
{
    return alternative<KanjidicRecord>(record);
}

const YomitanTermRecord *asYomitanTerm(const Record &record)
{
    return alternative<YomitanTermRecord>(record);
}

const YomitanKanjiRecord *asYomitanKanji(const Record &record)
{
    return alternative<YomitanKanjiRecord>(record);
}

const PitchAccentRecord *asPitchAccent(const Record &record)
{
    return alternative<PitchAccentRecord>(record);
}

const CustomWordRecord *asCustomWord(const Record &record)
{
    return alternative<CustomWordRecord>(record);
}

const CustomNameRecord *asCustomName(const Record &record)
{
    return alternative<CustomNameRecord>(record);
}

const FrequencyRecord *asFrequency(const Record &record)
{
    return alternative<FrequencyRecord>(record);
}

const KanjiExamplesRecord *asKanjiExamples(const Record &record)
{
    return alternative<KanjiExamplesRecord>(record);
}

const KanjiComponentsRecord *asKanjiComponents(const Record &record)
{
    return alternative<KanjiComponentsRecord>(record);
}

QString primarySpelling(const Record &record)
{
    return std::visit(
        [](const auto &value) -> QString {
            using T = std::decay_t<decltype(value)>;
            if constexpr (requires { value.primarySpelling; })
                return value.primarySpelling;
            else if constexpr (std::is_same_v<T, PitchAccentRecord>)
                return value.spelling;
            else
                return {};
        },
        record.data);
}

QList<QString> readings(const Record &record)
{
    return std::visit(
        [](const auto &value) -> QList<QString> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, JmdictRecord> || std::is_same_v<T, JmnedictRecord> ||
                          std::is_same_v<T, CustomWordRecord>) {
                return value.readings;
            } else if constexpr (std::is_same_v<T, YomitanTermRecord> || std::is_same_v<T, CustomNameRecord> ||
                                 std::is_same_v<T, PitchAccentRecord>) {
                return value.reading.isEmpty() ? QList<QString>{} : QList<QString>{value.reading};
            } else {
                return {};
            }
        },
        record.data);
}

QList<QString> alternativeSpellings(const Record &record)
{
    return std::visit(
        [](const auto &value) -> QList<QString> {
            if constexpr (requires { value.alternativeSpellings; })
                return value.alternativeSpellings;
            else
                return {};
        },
        record.data);
}

QList<QString> wordClasses(const Record &record)
{
    return std::visit(
        [](const auto &value) -> QList<QString> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, JmdictRecord>) {
                QList<QString> classes = value.wordClasses.sharedByAllSenses;
                for (const QList<QString> &senseClasses : value.wordClasses.perSense) {
                    for (const QString &wordClass : senseClasses) {
                        if (!classes.contains(wordClass))
                            classes.append(wordClass);
                    }
                }
                return classes;
            } else if constexpr (std::is_same_v<T, YomitanTermRecord> || std::is_same_v<T, CustomWordRecord>) {
                return value.wordClasses;
            } else {
                return {};
            }
        },
        record.data);
}

QString kanjiExamplesKey(QStringView kanji)
{
    return QString(extrasKeyPrefix) + QLatin1Char('x') + kanji.toString();
}

QString kanjiComponentsKey(QStringView kanji)
{
    return QString(extrasKeyPrefix) + QLatin1Char('c') + kanji.toString();
}

} // namespace maru::dict
