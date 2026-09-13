// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "query.h"

#include "core/logging.h"
#include "dict/lookupsupport.h"
#include "dict/records.h"

#include <QHash>

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace maru::lookup
{

namespace
{

// One key's worth of records before they are turned into Results, which is JL's
// IntermediaryResult. The two probes share it: an exact hit is filed under the normalized
// surface key and a deconjugated hit under the lemma, so 食べた can produce one entry for each.
struct Intermediary
{
    QString matchedText;
    QString deconjugatedMatchedText;
    std::vector<std::shared_ptr<const dict::Record>> records;
    // Index-parallel to records, and empty for an exact hit: the ways the deconjugator reached
    // that record's headword.
    std::vector<std::vector<deconj::ProcessPtr>> processes;
};

// Insertion-ordered map from search key to intermediary. .NET's Dictionary enumerates in
// insertion order for a dictionary that never removes, and JL's result order depends on it; a
// QHash does not, so the order is kept explicitly.
class IntermediaryMap
{
public:
    // JL's TryAdd: the first candidate to claim a key keeps it, and candidates run longest
    // first, so the longest matching prefix wins.
    bool tryAdd(const QString &key, Intermediary &&value)
    {
        if (m_index.contains(key))
            return false;
        m_index.insert(key, m_entries.size());
        m_entries.push_back(std::move(value));
        return true;
    }

    [[nodiscard]] Intermediary *find(const QString &key)
    {
        const auto position = m_index.constFind(key);
        return position == m_index.constEnd() ? nullptr : &m_entries[*position];
    }

    [[nodiscard]] const std::vector<Intermediary> &entries() const
    {
        return m_entries;
    }

private:
    QHash<QString, qsizetype> m_index;
    std::vector<Intermediary> m_entries;
};

// JL's ProcessNode.Equals (JL.Core/Deconjugation/ProcessNode.cs): the same node, or the
// same step under the same parent. Comparing the parents by pointer is what the shared ancestry
// of two sibling paths makes exact.
[[nodiscard]] bool sameProcess(const deconj::ProcessPtr &left, const deconj::ProcessPtr &right)
{
    if (left == right)
        return true;
    if (!left || !right)
        return false;
    return left->properStepCount == right->properStepCount && left->detail == right->detail &&
           left->parent == right->parent;
}

[[nodiscard]] bool listContains(const QList<QString> &values, QStringView value)
{
    return std::ranges::any_of(values, [value](const QString &entry) {
        return entry == value;
    });
}

// The readings a kanji card shows, in JL's order: on, then kun, then nanori.
[[nodiscard]] QList<QString> kanjiReadings(const dict::KanjidicRecord &record)
{
    QList<QString> readings = record.onReadings;
    readings.append(record.kunReadings);
    readings.append(record.nanoriReadings);
    return readings;
}

[[nodiscard]] bool sameJmdictResult(const Result &left, const Result &right)
{
    if (left.dictionary.id != right.dictionary.id || left.primarySpelling != right.primarySpelling)
        return false;
    if (left.readings != right.readings)
        return false;
    const dict::JmdictRecord *leftRecord = left.record ? dict::asJmdict(*left.record) : nullptr;
    const dict::JmdictRecord *rightRecord = right.record ? dict::asJmdict(*right.record) : nullptr;
    if (leftRecord == nullptr || rightRecord == nullptr)
        return leftRecord == rightRecord;
    return leftRecord->definitions == rightRecord->definitions;
}

// One record of one intermediary, as a Result. The frequency, the pitch and the kanji card are
// attached afterwards by decorate.h.
[[nodiscard]] Result
buildResult(const dict::DictionaryHandle &dictionary, const Intermediary &intermediary, qsizetype index)
{
    const std::shared_ptr<const dict::Record> &record = intermediary.records[index];

    Result result;
    result.dictionary = dictionary;
    result.record = record;
    result.matchedText = intermediary.matchedText;
    result.deconjugatedMatchedText = intermediary.deconjugatedMatchedText;
    result.primarySpelling = dict::primarySpelling(*record);
    result.readings = dict::readings(*record);
    result.alternativeSpellings = dict::alternativeSpellings(*record);
    if (const dict::JmdictRecord *jmdict = dict::asJmdict(*record))
        result.priorityRank = jmdict->priorityRank;

    if (index < std::ssize(intermediary.processes)) {
        int minimum = std::numeric_limits<int>::max();
        for (const deconj::ProcessPtr &process : intermediary.processes[index]) {
            const QString rendered = deconj::formattedProcess(process);
            if (rendered.isEmpty())
                continue;
            result.deconjugationPaths.append(rendered);
            minimum = std::min(minimum, deconj::properStepCount(process));
        }
        // A path that renders to nothing is dropped by JL too, and a record left with no
        // rendered path at all counts as un-deconjugated for the comparator.
        if (result.deconjugationPaths.isEmpty())
            result.deconjugatedMatchedText.clear();
        else
            result.minProperStepCount = minimum;
    }

    return result;
}

// The exact-key probe and the deconjugated probe for one candidate, against one word dictionary.
// key is the candidate's own key or one of its chōonpu variants.
void collectWordResults(const dict::DictionaryHandle &dictionary,
                        const dict::WordClassTable &wordClasses,
                        const QString &matchedText,
                        const QString &key,
                        const std::vector<deconj::Form> &forms,
                        int maxKeyLength,
                        IntermediaryMap &results)
{
    // The store holds no key longer than maxKeyLength, so a longer probe cannot match. The test
    // is per key rather than per candidate: a ten code unit span deconjugates to a three code
    // unit lemma, and a dictionary whose longest key is four still holds that lemma.
    const auto tooLong = [maxKeyLength](const QString &probe) {
        return maxKeyLength > 0 && probe.size() > maxKeyLength;
    };

    std::vector<std::shared_ptr<const dict::Record>> exact =
        tooLong(key) ? std::vector<std::shared_ptr<const dict::Record>>{} : dict::find(dictionary, key);
    if (!exact.empty()) {
        Intermediary intermediary;
        intermediary.matchedText = matchedText;
        intermediary.records = std::move(exact);
        (void)results.tryAdd(key, std::move(intermediary));
    }

    for (const deconj::Form &form : forms) {
        if (tooLong(form.text))
            continue;
        const std::vector<std::shared_ptr<const dict::Record>> records = dict::find(dictionary, form.text);
        if (records.empty())
            continue;

        std::vector<std::shared_ptr<const dict::Record>> valid;
        valid.reserve(records.size());
        for (const std::shared_ptr<const dict::Record> &record : records) {
            if (acceptsDeconjugationTag(dictionary.type, *record, form.lastTag, wordClasses))
                valid.push_back(record);
        }
        if (valid.empty())
            continue;

        Intermediary *existing = results.find(form.text);
        if (existing == nullptr) {
            Intermediary intermediary;
            intermediary.matchedText = matchedText;
            intermediary.deconjugatedMatchedText = form.text;
            intermediary.processes.assign(valid.size(), {form.process});
            intermediary.records = std::move(valid);
            (void)results.tryAdd(form.text, std::move(intermediary));
            continue;
        }

        // A lemma several forms of the same surface span reach collects every path that reached
        // it. A lemma already claimed by a longer span keeps that span and gains nothing, which
        // is what JL's MatchedText == OriginalText test enforces.
        if (existing->matchedText != form.originalText)
            continue;
        // The key is already held by the exact probe, whose records carry no path at all. JL
        // dereferences its null process list here; keeping the exact entry is the answer that
        // does not need one, and the records under the key are the same either way.
        if (existing->processes.size() != existing->records.size())
            continue;
        for (const std::shared_ptr<const dict::Record> &record : valid) {
            // By record id rather than by pointer: the store's decode cache can hand out two
            // shared_ptr for one row when the first has been evicted between two probes.
            const auto position =
                std::ranges::find_if(existing->records, [&record](const std::shared_ptr<const dict::Record> &stored) {
                    return stored->id == record->id;
                });
            if (position == existing->records.end()) {
                existing->records.push_back(record);
                existing->processes.push_back({form.process});
                continue;
            }
            const qsizetype index = position - existing->records.begin();
            if (index >= std::ssize(existing->processes))
                continue;
            std::vector<deconj::ProcessPtr> &paths = existing->processes[index];
            const bool known = std::ranges::any_of(paths, [&form](const deconj::ProcessPtr &path) {
                return sameProcess(path, form.process);
            });
            if (!known)
                paths.push_back(form.process);
        }
    }
}

} // namespace

qint32 entryIdOf(const Result &result)
{
    if (!result.record)
        return 0;
    if (const dict::JmdictRecord *jmdict = dict::asJmdict(*result.record))
        return jmdict->entryId;
    if (const dict::JmnedictRecord *jmnedict = dict::asJmnedict(*result.record))
        return jmnedict->entryId;
    return 0;
}

double popularityScoreOf(const Result &result)
{
    if (!result.record)
        return std::numeric_limits<double>::lowest();
    if (const dict::YomitanTermRecord *term = dict::asYomitanTerm(*result.record))
        return term->popularityScore;
    return std::numeric_limits<double>::lowest();
}

QString deconjugationProcessText(const Result &result)
{
    if (result.deconjugationPaths.isEmpty())
        return {};
    QString text = QChar(u'～') + result.deconjugationPaths.first();
    for (qsizetype i = 1; i < result.deconjugationPaths.size(); ++i)
        text += QStringLiteral("; ") + result.deconjugationPaths.at(i);
    return text;
}

bool acceptsDeconjugationTag(dict::DictType type,
                             const dict::Record &record,
                             QStringView lastTag,
                             const dict::WordClassTable &wordClasses)
{
    if (lastTag.isEmpty())
        return false;

    switch (type) {
    case dict::DictType::JMdict: {
        const dict::JmdictRecord *jmdict = dict::asJmdict(record);
        if (jmdict == nullptr)
            return false;
        if (listContains(jmdict->wordClasses.sharedByAllSenses, lastTag))
            return true;
        return std::ranges::any_of(jmdict->wordClasses.perSense, [lastTag](const QList<QString> &sense) {
            return listContains(sense, lastTag);
        });
    }

    case dict::DictType::CustomWord: {
        const dict::CustomWordRecord *custom = dict::asCustomWord(record);
        return custom != nullptr && listContains(custom->wordClasses, lastTag);
    }

    case dict::DictType::YomitanWord:
    case dict::DictType::YomitanOther: {
        const dict::YomitanTermRecord *term = dict::asYomitanTerm(record);
        if (term == nullptr)
            return false;
        if (!term->wordClasses.isEmpty()) {
            // A Yomitan rules field holds the coarse class, "v5" rather than "v5r", so the
            // comparison runs the other way round than the JMdict one.
            return std::ranges::any_of(term->wordClasses, [lastTag](const QString &wordClass) {
                return lastTag.startsWith(wordClass);
            });
        }
        return wordClasses.containsTag(term->primarySpelling, term->reading, lastTag);
    }

    default:
        // A name, kanji, frequency or pitch dictionary contributes no deconjugated result.
        return false;
    }
}

QList<Result> queryWordDictionary(const dict::DictionaryHandle &dictionary,
                                  const TextInfo &info,
                                  const dict::WordClassTable &wordClasses)
{
    if (!dictionary.store)
        return {};

    IntermediaryMap intermediaries;
    const int maxKeyLength = dictionary.maxKeyLength;
    for (const Candidate &candidate : info.candidates) {
        collectWordResults(
            dictionary, wordClasses, candidate.text, candidate.key, candidate.forms, maxKeyLength, intermediaries);

        for (qsizetype i = 0; i < candidate.longVowelVariants.size(); ++i) {
            const QString &variant = candidate.longVowelVariants.at(i);
            static const std::vector<deconj::Form> noForms;
            const std::vector<deconj::Form> &forms =
                i < std::ssize(candidate.longVowelVariantForms) ? candidate.longVowelVariantForms[i] : noForms;
            collectWordResults(dictionary, wordClasses, candidate.text, variant, forms, maxKeyLength, intermediaries);
        }
    }

    const bool dedup = dictionary.type == dict::DictType::JMdict;
    QList<Result> results;
    for (const Intermediary &intermediary : intermediaries.entries()) {
        for (qsizetype i = 0; i < std::ssize(intermediary.records); ++i) {
            Result result = buildResult(dictionary, intermediary, i);
            // JMdict explodes one entry into one record per headword, and two headwords of one
            // entry can decode to the same result. JL's comment names ヤンキー座り.
            if (dedup && std::ranges::any_of(results, [&result](const Result &existing) {
                    return sameJmdictResult(existing, result);
                })) {
                continue;
            }
            results.append(std::move(result));
        }
    }
    return results;
}

QList<Result> queryNameDictionary(const dict::DictionaryHandle &dictionary, const TextInfo &info)
{
    if (!dictionary.store)
        return {};

    IntermediaryMap intermediaries;
    const int maxKeyLength = dictionary.maxKeyLength;
    for (const Candidate &candidate : info.candidates) {
        if (maxKeyLength > 0 && candidate.key.size() > maxKeyLength)
            continue;
        std::vector<std::shared_ptr<const dict::Record>> records = dict::find(dictionary, candidate.key);
        if (records.empty())
            continue;
        Intermediary intermediary;
        intermediary.matchedText = candidate.text;
        intermediary.records = std::move(records);
        (void)intermediaries.tryAdd(candidate.key, std::move(intermediary));
    }

    QList<Result> results;
    for (const Intermediary &intermediary : intermediaries.entries()) {
        for (qsizetype i = 0; i < std::ssize(intermediary.records); ++i)
            results.append(buildResult(dictionary, intermediary, i));
    }
    return results;
}

QList<Result> queryKanjiDictionary(const dict::DictionaryHandle &dictionary, const QString &kanji)
{
    if (!dictionary.store || kanji.isEmpty())
        return {};

    QList<Result> results;
    for (const std::shared_ptr<const dict::Record> &record : dict::find(dictionary, kanji)) {
        Result result;
        result.dictionary = dictionary;
        result.record = record;
        result.matchedText = kanji;
        result.primarySpelling = kanji;
        if (const dict::KanjidicRecord *kanjidic = dict::asKanjidic(*record)) {
            result.kanjiRecord = record;
            result.readings = kanjiReadings(*kanjidic);
        } else {
            result.readings = dict::readings(*record);
            if (const dict::YomitanKanjiRecord *yomitan = dict::asYomitanKanji(*record)) {
                result.readings = yomitan->onReadings;
                result.readings.append(yomitan->kunReadings);
            }
        }
        results.append(std::move(result));
    }
    return results;
}

} // namespace maru::lookup
