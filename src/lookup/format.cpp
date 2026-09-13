// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "format.h"

#include <QStringList>

using namespace Qt::Literals::StringLiterals;

namespace maru::lookup
{

namespace
{

[[nodiscard]] QString bracketed(const QList<QString> &values)
{
    if (values.isEmpty())
        return {};
    return u"["_s % QStringList(values).join(u", "_s) % u"] "_s;
}

[[nodiscard]] QList<QString> tagsOfSense(const dict::SenseTags &tags, qsizetype senseIndex)
{
    // The per-sense array alone: the shared array is already in the prelude, and JL does not
    // repeat it per sense.
    return senseIndex < tags.perSense.size() ? tags.perSense.at(senseIndex) : QList<QString>{};
}

[[nodiscard]] QString loanwordEtymology(const QList<dict::LoanwordSource> &sources)
{
    if (sources.isEmpty())
        return {};

    QString text = u"["_s;
    for (qsizetype i = 0; i < sources.size(); ++i) {
        const dict::LoanwordSource &source = sources.at(i);
        if (source.isWasei)
            text += u"wasei "_s;
        else if (i == 0)
            text += u"from "_s;
        text += source.language;
        if (!source.originalWord.isEmpty())
            text += u": "_s % source.originalWord;
        if (i + 1 < sources.size())
            text += source.isPart ? u" + "_s : u", "_s;
    }
    return text % u"] "_s;
}

[[nodiscard]] QString restrictions(const dict::JmdictRecord &record, const dict::DictOptions &options, qsizetype index)
{
    if (!options.spellingRestrictionInfo)
        return {};
    const QList<QString> spellings =
        index < record.spellingRestrictions.size() ? record.spellingRestrictions.at(index) : QList<QString>{};
    const QList<QString> readings =
        index < record.readingRestrictions.size() ? record.readingRestrictions.at(index) : QList<QString>{};
    if (spellings.isEmpty() && readings.isEmpty())
        return {};

    QStringList values;
    values.append(QStringList(spellings));
    values.append(QStringList(readings));
    return u"(only applies to "_s % values.join(u"; "_s) % u") "_s;
}

[[nodiscard]] QString crossReferences(const QList<QString> &references)
{
    if (references.isEmpty())
        return {};
    return u"("_s % QStringList(references).join(u", "_s) % u") "_s;
}

// Every chunk this file builds carries a trailing space, which the separator replaces. JL does
// the replacement in place on its StringBuilder; here the space is dropped when the chunk is
// joined.
[[nodiscard]] QString withoutTrailingSpace(QString text)
{
    if (text.endsWith(u' '))
        text.chop(1);
    return text;
}

} // namespace

QString formatDefinitions(const dict::JmdictRecord &record, const dict::DictOptions &options)
{
    const QChar separator = options.newlineBetweenDefinitions ? QChar(u'\n') : QChar(u'；');
    const bool multipleDefinitions = record.definitions.size() > 1;

    QString prelude;
    if (options.wordClassInfo)
        prelude += bracketed(record.wordClasses.sharedByAllSenses);
    if (options.miscInfo)
        prelude += bracketed(record.misc.sharedByAllSenses);
    if (options.dialectInfo)
        prelude += bracketed(record.dialects.sharedByAllSenses);
    if (options.fieldInfo)
        prelude += bracketed(record.fields.sharedByAllSenses);
    if (options.loanwordEtymology)
        prelude += loanwordEtymology(record.loanwordEtymology);
    if (!record.info.isEmpty())
        prelude += QStringList(record.info).join(u", "_s) % u" "_s;
    if (!prelude.isEmpty() && multipleDefinitions && options.newlineBetweenDefinitions)
        prelude = withoutTrailingSpace(std::move(prelude)) % QChar(u'\n');

    QStringList chunks;
    chunks.reserve(record.definitions.size());
    for (qsizetype i = 0; i < record.definitions.size(); ++i) {
        QString chunk;
        if (multipleDefinitions) {
            chunk += QString::number(i + 1) % u". "_s;
            if (options.wordClassInfo)
                chunk += bracketed(tagsOfSense(record.wordClasses, i));
            if (options.miscInfo)
                chunk += bracketed(tagsOfSense(record.misc, i));
            if (options.dialectInfo)
                chunk += bracketed(tagsOfSense(record.dialects, i));
            if (options.fieldInfo)
                chunk += bracketed(tagsOfSense(record.fields, i));
        }

        chunk += QStringList(record.definitions.at(i)).join(u"; "_s) % u" "_s;

        if (options.extraDefinitionInfo && i < record.definitionInfo.size() && !record.definitionInfo.at(i).isEmpty())
            chunk += u"("_s % record.definitionInfo.at(i) % u") "_s;
        chunk += restrictions(record, options, i);
        if (options.crossReferences && i < record.crossReferences.size())
            chunk += crossReferences(record.crossReferences.at(i));

        chunks.append(withoutTrailingSpace(std::move(chunk)));
    }

    return prelude + chunks.join(separator);
}

QString formatDefinitions(const dict::Record &record, const dict::DictOptions &options)
{
    const QChar separator = options.newlineBetweenDefinitions ? QChar(u'\n') : QChar(u'；');

    if (const dict::JmdictRecord *jmdict = dict::asJmdict(record))
        return formatDefinitions(*jmdict, options);

    QStringList chunks;
    if (const dict::JmnedictRecord *jmnedict = dict::asJmnedict(record)) {
        for (qsizetype i = 0; i < jmnedict->definitions.size(); ++i) {
            const QString types = options.wordClassInfo && i < jmnedict->nameTypes.size()
                                      ? bracketed(jmnedict->nameTypes.at(i))
                                      : QString();
            chunks.append(types % QStringList(jmnedict->definitions.at(i)).join(u"; "_s));
        }
    } else if (const dict::YomitanTermRecord *term = dict::asYomitanTerm(record)) {
        chunks = QStringList(term->definitionsPlain);
    } else if (const dict::YomitanKanjiRecord *kanji = dict::asYomitanKanji(record)) {
        chunks = QStringList(kanji->definitions);
    } else if (const dict::KanjidicRecord *kanjidic = dict::asKanjidic(record)) {
        chunks = QStringList(kanjidic->definitions);
    } else if (const dict::CustomWordRecord *custom = dict::asCustomWord(record)) {
        const QString classes = options.wordClassInfo ? bracketed(custom->wordClasses) : QString();
        chunks.append(classes % QStringList(custom->definitions).join(u"; "_s));
    } else if (const dict::CustomNameRecord *name = dict::asCustomName(record)) {
        if (!name->nameType.isEmpty())
            chunks.append(u"("_s % name->nameType % u") "_s % name->extraInfo);
        else
            chunks.append(name->extraInfo);
    }

    chunks.removeAll(QString());
    return chunks.join(separator);
}

QString formatDefinitions(const Result &result)
{
    if (!result.record)
        return {};
    return formatDefinitions(*result.record, result.dictionary.options);
}

QString frequenciesToText(std::span<const FrequencyHit> frequencies)
{
    if (frequencies.empty())
        return {};
    if (frequencies.size() == 1)
        return u"#"_s % QString::number(frequencies.front().rank);

    QStringList parts;
    parts.reserve(static_cast<qsizetype>(frequencies.size()));
    for (const FrequencyHit &hit : frequencies)
        parts.append(hit.dictionaryName % u": "_s % QString::number(hit.rank));
    return parts.join(u", "_s);
}

} // namespace maru::lookup
