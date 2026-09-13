// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popupadapter.h"

#include "dict/dictionary.h"
#include "dict/records.h"
#include "dict/store.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QStringList>
#include <QUuid>

using namespace Qt::Literals::StringLiterals;

namespace maru::lookup
{

namespace
{

[[nodiscard]] QStringList toStringList(const QList<QString> &values)
{
    return {values};
}

// Two ProcessNode chains can render to one string: 食べさせられなかった reaches
// "passive/potential/honorific→negative→past" through the passive rule and through the potential
// rule, because both rules carry the same display name. The formatter preserves
// every path for compatibility with JL, while the popup shows each distinct path once. The first occurrence is kept, so
// the order stays the deconjugator's.
[[nodiscard]] QStringList distinctPaths(const QList<QString> &paths)
{
    QStringList distinct;
    distinct.reserve(paths.size());
    for (const QString &path : paths) {
        if (!distinct.contains(path))
            distinct.append(path);
    }
    return distinct;
}

[[nodiscard]] QList<QString> elementAt(const QList<QList<QString>> &values, qsizetype index)
{
    return index >= 0 && index < values.size() ? values.at(index) : QList<QString>{};
}

// One entry per reading, as the pitch painter reads it: the lookup fills the list only when a
// pitch dictionary answered, and a record with no readings at all gets the one position the
// dictionary holds for its headword.
[[nodiscard]] QList<std::optional<quint8>> pitchForReadings(const Result &result)
{
    QList<std::optional<quint8>> positions = result.pitchPositions;
    const qsizetype wanted = result.readings.size();
    while (positions.size() > wanted && wanted > 0)
        positions.removeLast();
    while (positions.size() < wanted)
        positions.append(std::nullopt);
    return positions;
}

[[nodiscard]] QString remainingFrequencyText(const QList<FrequencyHit> &frequencies)
{
    QStringList parts;
    for (qsizetype i = 1; i < frequencies.size(); ++i) {
        const FrequencyHit &hit = frequencies.at(i);
        parts.append(hit.dictionaryName % u": "_s % QString::number(hit.rank));
    }
    return parts.join(u", "_s);
}

void fillJmdictSenses(popup::Entry &entry, const dict::JmdictRecord &record)
{
    entry.orthographyInfo = toStringList(record.primarySpellingOrthographyInfo);
    entry.senses.reserve(record.definitions.size());
    for (qsizetype i = 0; i < record.definitions.size(); ++i) {
        popup::Sense sense;
        sense.glosses = toStringList(record.definitions.at(i));
        sense.pos = toStringList(record.wordClasses.forSense(i));
        sense.misc = toStringList(record.misc.forSense(i));
        sense.fields = toStringList(record.fields.forSense(i));
        sense.dialects = toStringList(record.dialects.forSense(i));
        if (i < record.definitionInfo.size())
            sense.info = record.definitionInfo.at(i);
        sense.crossReferences = toStringList(elementAt(record.crossReferences, i));
        sense.spellingRestrictions = toStringList(elementAt(record.spellingRestrictions, i));
        sense.spellingRestrictions.append(toStringList(elementAt(record.readingRestrictions, i)));
        entry.senses.append(std::move(sense));
    }
}

void fillJmnedictSenses(popup::Entry &entry, const dict::JmnedictRecord &record)
{
    entry.senses.reserve(record.definitions.size());
    for (qsizetype i = 0; i < record.definitions.size(); ++i) {
        popup::Sense sense;
        sense.glosses = toStringList(record.definitions.at(i));
        // A name type is JMnedict's counterpart of <misc>, and the renderer shows it in the same
        // place.
        sense.misc = toStringList(elementAt(record.nameTypes, i));
        entry.senses.append(std::move(sense));
    }
}

QMutex &entityMutex()
{
    static QMutex mutex;
    return mutex;
}

// Keyed by the dictionary's id rather than by its store pointer: a re-import replaces the
// Store, and the address of the old one can come back for an unrelated dictionary. The map a
// re-import produces holds the same DTD descriptions, so the kept entry stays correct.
QHash<QUuid, QHash<QString, QString>> &entityCache()
{
    static QHash<QUuid, QHash<QString, QString>> cache;
    return cache;
}

} // namespace

popup::Entry toPopupEntry(const Result &result)
{
    popup::Entry entry;
    entry.headword = result.primarySpelling;
    entry.readings = toStringList(result.readings);
    entry.alternativeSpellings = toStringList(result.alternativeSpellings);
    entry.pitchPositions = pitchForReadings(result);
    entry.dictionaryName = result.dictionary.name;

    if (!result.deconjugationPaths.isEmpty()) {
        // The renderer joins the paths with "; " inside one pair of parentheses, so the U+FF5E
        // marker goes on the first path alone and the joined text is JL's "～させる→なかった".
        entry.deconjugationPaths = distinctPaths(result.deconjugationPaths);
        entry.deconjugationPaths.first().prepend(QChar(u'～'));
    }

    if (!result.frequencies.isEmpty()) {
        entry.frequencyRank = result.frequencies.first().rank;
        entry.frequencyText = remainingFrequencyText(result.frequencies);
    }

    if (result.record) {
        if (const dict::JmdictRecord *record = dict::asJmdict(*result.record)) {
            fillJmdictSenses(entry, *record);
        } else if (const dict::JmnedictRecord *record = dict::asJmnedict(*result.record)) {
            fillJmnedictSenses(entry, *record);
        } else if (const dict::YomitanTermRecord *record = dict::asYomitanTerm(*result.record)) {
            entry.richTextGlossary = toStringList(record->definitions).join(u"<br>"_s);
        } else if (const dict::YomitanKanjiRecord *record = dict::asYomitanKanji(*result.record)) {
            popup::Sense sense;
            sense.glosses = toStringList(record->definitions);
            entry.senses.append(std::move(sense));
        } else if (const dict::CustomWordRecord *record = dict::asCustomWord(*result.record)) {
            popup::Sense sense;
            sense.glosses = toStringList(record->definitions);
            sense.pos = toStringList(record->wordClasses);
            entry.senses.append(std::move(sense));
        } else if (const dict::CustomNameRecord *record = dict::asCustomName(*result.record)) {
            popup::Sense sense;
            if (!record->extraInfo.isEmpty())
                sense.glosses = QStringList{record->extraInfo};
            if (!record->nameType.isEmpty())
                sense.misc = QStringList{record->nameType};
            entry.senses.append(std::move(sense));
        }
    }

    return entry;
}

Response firstResults(const Response &response, int count)
{
    Response cut = response;
    if (count > 0 && cut.results.size() > count)
        cut.results.resize(count);
    return cut;
}

popup::PopupModel toPopupModel(const Response &response)
{
    popup::PopupModel model;
    if (response.results.isEmpty())
        return model;

    model.matchedText = response.results.first().matchedText;
    for (const Result &result : response.results) {
        const dict::KanjidicRecord *kanji = result.kanjiRecord ? dict::asKanjidic(*result.kanjiRecord) : nullptr;
        if (kanji != nullptr) {
            // One card per lookup: it is built from the first character of the span, so every
            // kanji dictionary answers about the same character.
            if (model.kanji.has_value())
                continue;
            popup::KanjiCard card;
            card.character = result.primarySpelling;
            card.onReadings = toStringList(kanji->onReadings);
            card.kunReadings = toStringList(kanji->kunReadings);
            card.nanoriReadings = toStringList(kanji->nanoriReadings);
            card.meanings = toStringList(kanji->definitions);
            card.radicalNames = toStringList(kanji->radicalNames);
            card.examples = toStringList(result.kanjiExamples);
            card.components = toStringList(result.kanjiComponents);
            card.strokeCount = kanji->strokeCount;
            if (kanji->grade > 0)
                card.grade = kanji->grade;
            if (kanji->frequency > 0)
                card.frequency = kanji->frequency;
            model.kanji = std::move(card);
            continue;
        }
        model.entries.append(toPopupEntry(result));
    }
    return model;
}

QString entityDescription(const dict::DictionaryHandle &dictionary, QStringView code)
{
    // The shared_ptr is copied rather than dereferenced through the handle, so a concurrent
    // republish that drops the last other reference cannot free the Store mid-read.
    const std::shared_ptr<dict::Store> store = dictionary.store;
    if (!store || code.isEmpty())
        return code.toString();

    QMutexLocker locker(&entityMutex());
    auto position = entityCache().find(dictionary.id);
    if (position == entityCache().end()) {
        QHash<QString, QString> entities;
        const QJsonObject object = QJsonDocument::fromJson(store->meta(dict::metakeys::entities).toUtf8()).object();
        for (auto entity = object.constBegin(); entity != object.constEnd(); ++entity)
            entities.insert(entity.key(), entity.value().toString());
        position = entityCache().insert(dictionary.id, std::move(entities));
    }

    const QString name = code.toString();
    return position->value(name, name);
}

} // namespace maru::lookup
