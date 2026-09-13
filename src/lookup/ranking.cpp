// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ranking.h"

#include "dict/dictionary.h"
#include "dict/records.h"
#include "lookup/query.h"

#include <algorithm>
#include <limits>

using namespace Qt::Literals::StringLiterals;

namespace maru::lookup
{

namespace
{

constexpr int absentScore = std::numeric_limits<int>::max();

[[nodiscard]] bool containsAny(const QList<QString> &values, std::initializer_list<QLatin1StringView> wanted)
{
    return std::ranges::any_of(values, [&wanted](const QString &value) {
        return std::ranges::any_of(wanted, [&value](QLatin1StringView entry) {
            return value == entry;
        });
    });
}

[[nodiscard]] const dict::JmdictRecord *jmdictRecord(const Result &result)
{
    return result.record ? dict::asJmdict(*result.record) : nullptr;
}

[[nodiscard]] bool matchedPrimarySpelling(const Result &result)
{
    return result.primarySpelling == result.matchedText;
}

// JL's GetNormalizedFrequencyScore: a rank list is already ascending, and an occurrence count is
// mirrored so that both compare with "smaller is better".
[[nodiscard]] int normalizedFrequencyScore(const FrequencyHit &hit)
{
    if (!hit.higherIsBetter)
        return hit.rank;
    return hit.rank == absentScore ? absentScore : absentScore - hit.rank;
}

// Criterion 8's tie-break over the lower-priority frequency dictionaries. JL walks the parallel
// lists by index; marupop records only the dictionaries that answered, so the walk pairs them by
// name and stops at the first dictionary that answered for one result and not the other.
[[nodiscard]] int compareSecondaryFrequencies(const Result &left, const Result &right)
{
    for (qsizetype i = 1; i < left.frequencies.size(); ++i) {
        const FrequencyHit &hit = left.frequencies.at(i);
        const auto position =
            std::find_if(right.frequencies.cbegin() + 1, right.frequencies.cend(), [&hit](const FrequencyHit &other) {
                return other.dictionaryName == hit.dictionaryName;
            });
        if (position == right.frequencies.cend())
            continue;
        if (hit.rank != position->rank)
            return normalizedFrequencyScore(hit) < normalizedFrequencyScore(*position) ? -1 : 1;
    }
    return 0;
}

[[nodiscard]] int compareInt(int left, int right)
{
    if (left == right)
        return 0;
    return left < right ? -1 : 1;
}

} // namespace

qsizetype readingIndexOfMatchedText(const Result &result)
{
    return result.readings.indexOf(result.matchedText);
}

int primarySpellingOrthographyScore(const Result &result)
{
    const dict::JmdictRecord *record = jmdictRecord(result);
    if (record == nullptr || !matchedPrimarySpelling(result))
        return absentScore;
    return containsAny(record->primarySpellingOrthographyInfo, {"oK"_L1, "iK"_L1, "rK"_L1}) ? 1 : 0;
}

int readingOrthographyScore(const Result &result)
{
    const dict::JmdictRecord *record = jmdictRecord(result);
    const qsizetype index = readingIndexOfMatchedText(result);
    if (record == nullptr || index < 0)
        return absentScore;

    if (index < record->readingsOrthographyInfo.size() &&
        containsAny(record->readingsOrthographyInfo.at(index), {"ok"_L1, "ik"_L1, "rk"_L1})) {
        return 2;
    }
    if (containsAny(record->misc.sharedByAllSenses, {"uk"_L1}))
        return 0;
    for (const QList<QString> &misc : record->misc.perSense) {
        if (containsAny(misc, {"uk"_L1}))
            return 0;
    }
    return 1;
}

int frequencyScore(const Result &result)
{
    if (!result.frequencies.isEmpty())
        return normalizedFrequencyScore(result.frequencies.first());
    // JMdict's own ke_pri and re_pri bands, which decorate() leaves in place only while no
    // frequency dictionary is enabled. A headword with no priority tag ranks 0, which means
    // "absent" rather than "most common".
    return result.priorityRank > 0 ? result.priorityRank : absentScore;
}

QString definitionText(const Result &result)
{
    if (!result.record)
        return {};

    QStringList parts;
    if (const dict::JmdictRecord *record = dict::asJmdict(*result.record)) {
        for (const QList<QString> &sense : record->definitions)
            parts.append(QStringList(sense).join(QStringLiteral("; ")));
        return parts.join(QChar(u'；'));
    }
    if (const dict::JmnedictRecord *record = dict::asJmnedict(*result.record)) {
        for (const QList<QString> &translation : record->definitions)
            parts.append(QStringList(translation).join(QStringLiteral("; ")));
        return parts.join(QChar(u'；'));
    }
    if (const dict::YomitanTermRecord *record = dict::asYomitanTerm(*result.record))
        return QStringList(record->definitionsPlain).join(QChar(u'\n'));
    if (const dict::YomitanKanjiRecord *record = dict::asYomitanKanji(*result.record))
        return QStringList(record->definitions).join(QStringLiteral("; "));
    if (const dict::KanjidicRecord *record = dict::asKanjidic(*result.record))
        return QStringList(record->definitions).join(QStringLiteral("; "));
    if (const dict::CustomWordRecord *record = dict::asCustomWord(*result.record))
        return QStringList(record->definitions).join(QStringLiteral("; "));
    if (const dict::CustomNameRecord *record = dict::asCustomName(*result.record))
        return record->extraInfo;
    return {};
}

int compareUndecorated(const Result &left, const Result &right)
{
    // 1. The longest matched span first. This is the dominant key, and the reason
    // results[0].matchedText is what the popup highlights.
    if (left.matchedText.size() != right.matchedText.size())
        return left.matchedText.size() > right.matchedText.size() ? -1 : 1;

    // 2. An exact headword match beats an inflected one.
    const bool leftPrimary = matchedPrimarySpelling(left);
    const bool rightPrimary = matchedPrimarySpelling(right);
    if (leftPrimary != rightPrimary)
        return leftPrimary ? -1 : 1;

    // 3. A reading match beats neither.
    const qsizetype leftReadingIndex = readingIndexOfMatchedText(left);
    const qsizetype rightReadingIndex = readingIndexOfMatchedText(right);
    if ((leftReadingIndex >= 0) != (rightReadingIndex >= 0))
        return leftReadingIndex >= 0 ? -1 : 1;

    // 4. Fewer conjugation steps first, so an uninflected result wins.
    if (left.minProperStepCount != right.minProperStepCount)
        return compareInt(left.minProperStepCount, right.minProperStepCount);

    // 5. The user's dictionary order. DictionaryManager renumbers an enabled dictionary to 1 or
    // more, so a priority of 0 is a Result no dictionary produced and sorts last.
    const int leftPriority = left.dictionary.priority > 0 ? left.dictionary.priority : absentScore;
    const int rightPriority = right.dictionary.priority > 0 ? right.dictionary.priority : absentScore;
    if (leftPriority != rightPriority)
        return compareInt(leftPriority, rightPriority);

    // 6. and 7. Orthography of the matched headword and of the matched reading.
    const int leftPrimaryScore = primarySpellingOrthographyScore(left);
    const int rightPrimaryScore = primarySpellingOrthographyScore(right);
    if (leftPrimaryScore != rightPrimaryScore)
        return compareInt(leftPrimaryScore, rightPrimaryScore);

    const int leftReadingScore = readingOrthographyScore(left);
    const int rightReadingScore = readingOrthographyScore(right);
    if (leftReadingScore != rightReadingScore)
        return compareInt(leftReadingScore, rightReadingScore);

    return 0;
}

int compareResults(const Result &left, const Result &right)
{
    if (const int undecorated = compareUndecorated(left, right); undecorated != 0)
        return undecorated;

    // Criterion 3 read it too, and criteria 8 and 10 read it again. The list is the readings of
    // one entry, so the search is over a handful of strings.
    const qsizetype leftReadingIndex = readingIndexOfMatchedText(left);
    const qsizetype rightReadingIndex = readingIndexOfMatchedText(right);

    // 8. Frequency. A result a frequency dictionary covers beats one it does not, because an
    // uncovered result scores INT_MAX.
    const int leftFrequency = frequencyScore(left);
    const int rightFrequency = frequencyScore(right);
    if (leftFrequency != rightFrequency)
        return compareInt(leftFrequency, rightFrequency);

    if (left.frequencies.size() > 1 && right.frequencies.size() > 1 &&
        (leftFrequency == absentScore ||
         (leftReadingIndex < 0 && rightReadingIndex < 0 && left.readings != right.readings))) {
        const int secondary = compareSecondaryFrequencies(left, right);
        if (secondary != 0)
            return secondary;
    }

    // 9. The Yomitan popularity score, descending.
    const double leftPopularity = popularityScoreOf(left);
    const double rightPopularity = popularityScoreOf(right);
    if (leftPopularity != rightPopularity)
        return leftPopularity > rightPopularity ? -1 : 1;

    // 10. The position of the matched reading among the readings.
    const qsizetype leftReadingRank = leftReadingIndex >= 0 ? leftReadingIndex : absentScore;
    const qsizetype rightReadingRank = rightReadingIndex >= 0 ? rightReadingIndex : absentScore;
    if (leftReadingRank != rightReadingRank)
        return leftReadingRank < rightReadingRank ? -1 : 1;

    // 11. The dictionary's own entry number, which orders JMdict by its editorial sequence.
    const qint32 leftId = entryIdOf(left);
    const qint32 rightId = entryIdOf(right);
    const int leftIdScore = leftId > 0 ? leftId : absentScore;
    const int rightIdScore = rightId > 0 ? rightId : absentScore;
    if (leftIdScore != rightIdScore)
        return compareInt(leftIdScore, rightIdScore);

    // 12. to 14. Code-unit order of the headword, then the longest definition, then its code
    // unit order, so that the whole comparison is a total order over distinct results.
    const int spelling = QString::compare(left.primarySpelling, right.primarySpelling, Qt::CaseSensitive);
    if (spelling != 0)
        return spelling < 0 ? -1 : 1;

    const QString leftDefinitions = definitionText(left);
    const QString rightDefinitions = definitionText(right);
    if (leftDefinitions.size() != rightDefinitions.size())
        return leftDefinitions.size() > rightDefinitions.size() ? -1 : 1;
    const int definitions = QString::compare(leftDefinitions, rightDefinitions, Qt::CaseSensitive);
    if (definitions == 0)
        return 0;
    return definitions < 0 ? -1 : 1;
}

bool lessThan(const Result &left, const Result &right)
{
    return compareResults(left, right) < 0;
}

bool lessThanUndecorated(const Result &left, const Result &right)
{
    return compareUndecorated(left, right) < 0;
}

} // namespace maru::lookup
