// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "jmdictimporter.h"

#include "core/logging.h"
#include "dict/keynorm.h"
#include "dict/store.h"
#include "jp/japanese.h"

#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>
#include <array>
#include <memory>

namespace maru::dict
{

namespace
{

// The ISO 639-2/B codes JMdict uses in lsource/@xml:lang, mapped to the English language names JL
// resolves them to (JL.Core/Dicts/JMdict/JmdictLoader.cs). A code outside the table is kept
// as written.
using LanguageName = std::pair<QLatin1StringView, QLatin1StringView>;

constexpr auto languageNames = std::to_array<LanguageName>({
    {QLatin1StringView("afr"), QLatin1StringView("Afrikaans")},
    {QLatin1StringView("ain"), QLatin1StringView("Ainu")},
    {QLatin1StringView("alb"), QLatin1StringView("Albanian")},
    {QLatin1StringView("alg"), QLatin1StringView("Algonquian languages")},
    {QLatin1StringView("amh"), QLatin1StringView("Amharic")},
    {QLatin1StringView("ara"), QLatin1StringView("Arabic")},
    {QLatin1StringView("arm"), QLatin1StringView("Armenian")},
    {QLatin1StringView("arn"), QLatin1StringView("Mapuche")},
    {QLatin1StringView("aze"), QLatin1StringView("Azerbaijani")},
    {QLatin1StringView("baq"), QLatin1StringView("Basque")},
    {QLatin1StringView("ben"), QLatin1StringView("Bengali")},
    {QLatin1StringView("bnt"), QLatin1StringView("Bantu languages")},
    {QLatin1StringView("bre"), QLatin1StringView("Breton")},
    {QLatin1StringView("bul"), QLatin1StringView("Bulgarian")},
    {QLatin1StringView("bur"), QLatin1StringView("Burmese")},
    {QLatin1StringView("chi"), QLatin1StringView("Chinese")},
    {QLatin1StringView("chn"), QLatin1StringView("Chinook Jargon")},
    {QLatin1StringView("cze"), QLatin1StringView("Czech")},
    {QLatin1StringView("dan"), QLatin1StringView("Danish")},
    {QLatin1StringView("div"), QLatin1StringView("Dhivehi")},
    {QLatin1StringView("dut"), QLatin1StringView("Dutch")},
    {QLatin1StringView("eng"), QLatin1StringView("English")},
    {QLatin1StringView("epo"), QLatin1StringView("Esperanto")},
    {QLatin1StringView("est"), QLatin1StringView("Estonian")},
    {QLatin1StringView("fil"), QLatin1StringView("Filipino")},
    {QLatin1StringView("fin"), QLatin1StringView("Finnish")},
    {QLatin1StringView("fre"), QLatin1StringView("French")},
    {QLatin1StringView("geo"), QLatin1StringView("Georgian")},
    {QLatin1StringView("ger"), QLatin1StringView("German")},
    {QLatin1StringView("glg"), QLatin1StringView("Galician")},
    {QLatin1StringView("grc"), QLatin1StringView("Ancient Greek")},
    {QLatin1StringView("gre"), QLatin1StringView("Greek")},
    {QLatin1StringView("haw"), QLatin1StringView("Hawaiian")},
    {QLatin1StringView("heb"), QLatin1StringView("Hebrew")},
    {QLatin1StringView("hin"), QLatin1StringView("Hindi")},
    {QLatin1StringView("hun"), QLatin1StringView("Hungarian")},
    {QLatin1StringView("ice"), QLatin1StringView("Icelandic")},
    {QLatin1StringView("ind"), QLatin1StringView("Indonesian")},
    {QLatin1StringView("ita"), QLatin1StringView("Italian")},
    {QLatin1StringView("kaz"), QLatin1StringView("Kazakh")},
    {QLatin1StringView("khm"), QLatin1StringView("Central Khmer")},
    {QLatin1StringView("kir"), QLatin1StringView("Kyrgyz")},
    {QLatin1StringView("kor"), QLatin1StringView("Korean")},
    {QLatin1StringView("kur"), QLatin1StringView("Kurdish")},
    {QLatin1StringView("lao"), QLatin1StringView("Lao")},
    {QLatin1StringView("lat"), QLatin1StringView("Latin")},
    {QLatin1StringView("lit"), QLatin1StringView("Lithuanian")},
    {QLatin1StringView("mac"), QLatin1StringView("Macedonian")},
    {QLatin1StringView("mal"), QLatin1StringView("Malayalam")},
    {QLatin1StringView("mao"), QLatin1StringView("Maori")},
    {QLatin1StringView("may"), QLatin1StringView("Malay")},
    {QLatin1StringView("mlg"), QLatin1StringView("Malagasy")},
    {QLatin1StringView("mnc"), QLatin1StringView("Manchu")},
    {QLatin1StringView("mol"), QLatin1StringView("Moldavian")},
    {QLatin1StringView("mon"), QLatin1StringView("Mongolian")},
    {QLatin1StringView("nep"), QLatin1StringView("Nepali")},
    {QLatin1StringView("nor"), QLatin1StringView("Norwegian")},
    {QLatin1StringView("per"), QLatin1StringView("Persian")},
    {QLatin1StringView("pol"), QLatin1StringView("Polish")},
    {QLatin1StringView("por"), QLatin1StringView("Portuguese")},
    {QLatin1StringView("rum"), QLatin1StringView("Romanian")},
    {QLatin1StringView("rus"), QLatin1StringView("Russian")},
    {QLatin1StringView("san"), QLatin1StringView("Sanskrit")},
    {QLatin1StringView("scr"), QLatin1StringView("Serbo-Croatian")},
    {QLatin1StringView("slo"), QLatin1StringView("Slovak")},
    {QLatin1StringView("slv"), QLatin1StringView("Slovenian")},
    {QLatin1StringView("smo"), QLatin1StringView("Samoan")},
    {QLatin1StringView("som"), QLatin1StringView("Somali")},
    {QLatin1StringView("sot"), QLatin1StringView("Southern Sotho")},
    {QLatin1StringView("spa"), QLatin1StringView("Spanish")},
    {QLatin1StringView("swa"), QLatin1StringView("Swahili")},
    {QLatin1StringView("swe"), QLatin1StringView("Swedish")},
    {QLatin1StringView("tah"), QLatin1StringView("Tahitian")},
    {QLatin1StringView("tam"), QLatin1StringView("Tamil")},
    {QLatin1StringView("tgk"), QLatin1StringView("Tajik")},
    {QLatin1StringView("tgl"), QLatin1StringView("Tagalog")},
    {QLatin1StringView("tha"), QLatin1StringView("Thai")},
    {QLatin1StringView("tib"), QLatin1StringView("Tibetan")},
    {QLatin1StringView("tuk"), QLatin1StringView("Turkmen")},
    {QLatin1StringView("tur"), QLatin1StringView("Turkish")},
    {QLatin1StringView("ukr"), QLatin1StringView("Ukrainian")},
    {QLatin1StringView("urd"), QLatin1StringView("Urdu")},
    {QLatin1StringView("uzb"), QLatin1StringView("Uzbek")},
    {QLatin1StringView("vie"), QLatin1StringView("Vietnamese")},
    {QLatin1StringView("wel"), QLatin1StringView("Welsh")},
    {QLatin1StringView("yid"), QLatin1StringView("Yiddish")},
});

QString languageName(const QString &code)
{
    for (const auto &[languageCode, englishName] : languageNames) {
        if (code == languageCode)
            return englishName;
    }
    return code;
}

struct KanjiElement
{
    QString keb;
    QList<QString> orthographyInfo; // ke_inf entity names
    QList<QString> priority;        // ke_pri
    bool searchOnly = false;        // ke_inf holds sK
};

struct ReadingElement
{
    QString reb;
    QList<QString> restrictions;    // re_restr
    QList<QString> orthographyInfo; // re_inf entity names
    QList<QString> priority;        // re_pri
    bool searchOnly = false;        // re_inf holds sk
};

struct Sense
{
    QList<QString> glosses;
    QList<QString> partsOfSpeech;
    QString info;                        // s_inf
    QList<QString> spellingRestrictions; // stagk
    QList<QString> readingRestrictions;  // stagr
    QList<QString> fields;
    QList<QString> misc;
    QList<QString> dialects;
    QList<QString> crossReferences;
};

struct Entry
{
    qint32 id = 0;
    QList<KanjiElement> kanjiElements;
    QList<ReadingElement> readingElements;
    QList<Sense> senses;
    QList<LoanwordSource> loanwordEtymology;
    QList<QString> info;
};

using RecordPtr = std::shared_ptr<JmdictRecord>;

// The key to record mapping one entry produces, with insertion order preserved so the store is
// written the same way on every run. JL uses a Dictionary<string, JmdictRecord> and relies on
// Remove() for the sK re-pointing; a removed slot is left as a hole here.
class RecordMap
{
public:
    [[nodiscard]] bool contains(const QString &key) const
    {
        return m_index.contains(key);
    }

    void add(const QString &key, const RecordPtr &record)
    {
        if (m_index.contains(key))
            return;
        m_index.insert(key, static_cast<qsizetype>(m_slots.size()));
        m_slots.emplace_back(key, record);
    }

    [[nodiscard]] RecordPtr value(const QString &key) const
    {
        const auto found = m_index.constFind(key);
        return found != m_index.constEnd() ? m_slots[static_cast<size_t>(found.value())].second : RecordPtr{};
    }

    RecordPtr take(const QString &key)
    {
        const auto found = m_index.constFind(key);
        if (found == m_index.constEnd())
            return {};
        const qsizetype slot = found.value();
        RecordPtr record = m_slots[static_cast<size_t>(slot)].second;
        m_slots[static_cast<size_t>(slot)].second.reset();
        m_index.erase(found);
        return record;
    }

    [[nodiscard]] const std::vector<std::pair<QString, RecordPtr>> &slots() const
    {
        return m_slots;
    }

private:
    std::vector<std::pair<QString, RecordPtr>> m_slots;
    QHash<QString, qsizetype> m_index;
};

// Every inner list empty means JL's TrimListOfNullableElementsToArray() would have produced null.
QList<QList<QString>> trimIfAllEmpty(QList<QList<QString>> values)
{
    for (const QList<QString> &inner : values) {
        if (!inner.isEmpty())
            return values;
    }
    return {};
}

QList<QString> trimIfAllEmpty(QList<QString> values)
{
    for (const QString &inner : values) {
        if (!inner.isEmpty())
            return values;
    }
    return {};
}

// The port of JmdictRecordBuilder.GetExclusiveAndSharedValuesForNullableSenseField and
// GetExclusiveAndSharedValuesForSenseField (JL.Core/Dicts/JMdict/JmdictRecordBuilder.cs).
// An empty inner list stands for JL's null array.
SenseTags factorSenseTags(const QList<QList<QString>> &values)
{
    SenseTags tags;
    if (values.isEmpty())
        return tags;
    if (values.size() == 1) {
        tags.sharedByAllSenses = values.first();
        return tags;
    }
    for (const QList<QString> &value : values) {
        if (value.isEmpty()) {
            tags.perSense = trimIfAllEmpty(values);
            return tags;
        }
    }

    QList<QString> shared = values.first();
    for (qsizetype i = 1; i < values.size(); ++i) {
        for (qsizetype j = shared.size() - 1; j >= 0; --j) {
            if (values.at(i).contains(shared.at(j)))
                continue;
            if (shared.size() == 1) {
                tags.perSense = values;
                return tags;
            }
            shared.removeAt(j);
        }
    }

    QList<QList<QString>> exclusive;
    exclusive.reserve(values.size());
    bool anyExclusive = false;
    for (const QList<QString> &value : values) {
        QList<QString> extras;
        for (const QString &tag : value) {
            if (!shared.contains(tag))
                extras.append(tag);
        }
        if (!extras.isEmpty())
            anyExclusive = true;
        exclusive.append(extras);
    }

    tags.sharedByAllSenses = shared;
    if (anyExclusive)
        tags.perSense = exclusive;
    return tags;
}

bool containsAny(const QList<QString> &haystack, const QList<QString> &needles)
{
    return std::ranges::any_of(needles, [&haystack](const QString &needle) {
        return haystack.contains(needle);
    });
}

// The senses of entry that survive the stagk/stagr filter, and the record fields they produce.
struct SenseSelection
{
    QList<QList<QString>> definitions;
    QList<QList<QString>> wordClasses;
    QList<QList<QString>> fields;
    QList<QList<QString>> misc;
    QList<QList<QString>> dialects;
    QList<QList<QString>> spellingRestrictions;
    QList<QList<QString>> readingRestrictions;
    QList<QString> definitionInfo;
    QList<QList<QString>> crossReferences;
};

void appendSense(SenseSelection &selection, const Sense &sense)
{
    selection.definitions.append(sense.glosses);
    selection.wordClasses.append(sense.partsOfSpeech);
    selection.fields.append(sense.fields);
    selection.misc.append(sense.misc);
    selection.dialects.append(sense.dialects);
    selection.spellingRestrictions.append(sense.spellingRestrictions);
    selection.readingRestrictions.append(sense.readingRestrictions);
    selection.definitionInfo.append(sense.info);
    selection.crossReferences.append(sense.crossReferences);
}

void applySenseSelection(JmdictRecord &record, const SenseSelection &selection)
{
    record.definitions = selection.definitions;
    record.wordClasses = factorSenseTags(selection.wordClasses);
    record.fields = factorSenseTags(selection.fields);
    record.misc = factorSenseTags(selection.misc);
    record.dialects = factorSenseTags(selection.dialects);
    record.spellingRestrictions = trimIfAllEmpty(selection.spellingRestrictions);
    record.readingRestrictions = trimIfAllEmpty(selection.readingRestrictions);
    record.definitionInfo = trimIfAllEmpty(selection.definitionInfo);
    record.crossReferences = trimIfAllEmpty(selection.crossReferences);
}

template <typename T>
QList<T> withoutIndex(const QList<T> &values, qsizetype index)
{
    QList<T> result;
    result.reserve(std::max<qsizetype>(0, values.size() - 1));
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (i != index)
            result.append(values.at(i));
    }
    return result;
}

} // namespace

qint32 jmdictPriorityRank(const QList<QString> &priorityTags)
{
    qint32 best = 0;
    for (const QString &tag : priorityTags) {
        qint32 rank = 0;
        if (tag.size() == 4 && tag.startsWith(QLatin1String("nf"))) {
            bool parsed = false;
            const int band = QStringView(tag).mid(2).toInt(&parsed);
            if (parsed && band > 0)
                rank = band * 500;
        } else if (tag == QLatin1String("ichi1") || tag == QLatin1String("news1") || tag == QLatin1String("spec1") ||
                   tag == QLatin1String("gai1")) {
            rank = 12000;
        } else if (tag == QLatin1String("ichi2") || tag == QLatin1String("news2") || tag == QLatin1String("spec2") ||
                   tag == QLatin1String("gai2")) {
            rank = 24000;
        }
        if (rank == 0)
            continue;
        best = best == 0 ? rank : std::min(best, rank);
    }
    return best;
}

namespace
{

// Collects the example words the popup's kanji card shows: for each kanji, the up-to-three
// headwords with the smallest priorityRank that are written with it and are longer than one
// character. A one-character headword is skipped because it is the kanji itself.
class KanjiExampleCollector
{
public:
    void offer(const JmdictRecord &record)
    {
        if (record.priorityRank <= 0 || record.primarySpelling.size() < 2 || record.definitions.isEmpty() ||
            record.definitions.first().isEmpty()) {
            return;
        }

        KanjiExample example;
        example.spelling = record.primarySpelling;
        example.reading = record.readings.value(0);
        example.gloss = record.definitions.first().first();

        QList<QString> seen;
        const QList<uint> codePoints = record.primarySpelling.toUcs4();
        for (const uint unit : codePoints) {
            const auto codePoint = static_cast<char32_t>(unit);
            if (!jp::isKanji(codePoint))
                continue;
            const QString kanji = QString::fromUcs4(&codePoint, 1);
            if (seen.contains(kanji))
                continue;
            seen.append(kanji);
            insert(kanji, record.priorityRank, example);
        }
    }

    [[nodiscard]] const QHash<QString, QList<std::pair<qint32, KanjiExample>>> &examples() const
    {
        return m_examples;
    }

private:
    void insert(const QString &kanji, qint32 rank, const KanjiExample &example)
    {
        QList<std::pair<qint32, KanjiExample>> &candidates = m_examples[kanji];
        const bool present = std::ranges::any_of(candidates, [&example](const auto &candidate) {
            return candidate.second.spelling == example.spelling;
        });
        if (present)
            return;
        candidates.append({rank, example});
        std::ranges::stable_sort(candidates, [](const auto &left, const auto &right) {
            return left.first < right.first;
        });
        while (candidates.size() > 3)
            candidates.removeLast();
    }

    QHash<QString, QList<std::pair<qint32, KanjiExample>>> m_examples;
};

class EntryReader
{
public:
    EntryReader(QXmlStreamReader &reader, DtdEntityMap &entities)
        : m_reader(reader)
        , m_entities(entities)
    {}

    // Reads the <entry> element the reader is positioned on.
    Entry readEntry()
    {
        Entry entry;
        while (!m_reader.atEnd()) {
            const QXmlStreamReader::TokenType token = m_reader.readNext();
            if (token == QXmlStreamReader::EndElement && m_reader.name() == QLatin1String("entry"))
                break;
            if (token != QXmlStreamReader::StartElement)
                continue;

            const QStringView name = m_reader.name();
            if (name == QLatin1String("ent_seq"))
                entry.id = m_reader.readElementText().toInt();
            else if (name == QLatin1String("k_ele"))
                entry.kanjiElements.append(readKanjiElement());
            else if (name == QLatin1String("r_ele"))
                entry.readingElements.append(readReadingElement());
            else if (name == QLatin1String("sense"))
                entry.senses.append(readSense());
            else if (name == QLatin1String("info"))
                entry.info.append(m_reader.readElementText());
            else if (name == QLatin1String("lsource"))
                entry.loanwordEtymology.append(readLoanwordSource());
            else
                m_reader.skipCurrentElement();
        }
        return entry;
    }

private:
    QString readEntity()
    {
        return m_entities.shortName(m_reader.readElementText());
    }

    KanjiElement readKanjiElement()
    {
        KanjiElement element;
        while (!m_reader.atEnd()) {
            const QXmlStreamReader::TokenType token = m_reader.readNext();
            if (token == QXmlStreamReader::EndElement && m_reader.name() == QLatin1String("k_ele"))
                break;
            if (token != QXmlStreamReader::StartElement)
                continue;
            const QStringView name = m_reader.name();
            if (name == QLatin1String("keb"))
                element.keb = m_reader.readElementText();
            else if (name == QLatin1String("ke_inf"))
                element.orthographyInfo.append(readEntity());
            else if (name == QLatin1String("ke_pri"))
                element.priority.append(m_reader.readElementText());
            else
                m_reader.skipCurrentElement();
        }
        element.searchOnly = element.orthographyInfo.contains(QLatin1String("sK"));
        return element;
    }

    ReadingElement readReadingElement()
    {
        ReadingElement element;
        while (!m_reader.atEnd()) {
            const QXmlStreamReader::TokenType token = m_reader.readNext();
            if (token == QXmlStreamReader::EndElement && m_reader.name() == QLatin1String("r_ele"))
                break;
            if (token != QXmlStreamReader::StartElement)
                continue;
            const QStringView name = m_reader.name();
            if (name == QLatin1String("reb"))
                element.reb = m_reader.readElementText();
            else if (name == QLatin1String("re_restr"))
                element.restrictions.append(m_reader.readElementText());
            else if (name == QLatin1String("re_inf"))
                element.orthographyInfo.append(readEntity());
            else if (name == QLatin1String("re_pri"))
                element.priority.append(m_reader.readElementText());
            else
                m_reader.skipCurrentElement();
        }
        element.searchOnly = element.orthographyInfo.contains(QLatin1String("sk"));
        return element;
    }

    Sense readSense()
    {
        Sense sense;
        while (!m_reader.atEnd()) {
            const QXmlStreamReader::TokenType token = m_reader.readNext();
            if (token == QXmlStreamReader::EndElement && m_reader.name() == QLatin1String("sense"))
                break;
            if (token != QXmlStreamReader::StartElement)
                continue;

            const QStringView name = m_reader.name();
            if (name == QLatin1String("stagk")) {
                sense.spellingRestrictions.append(m_reader.readElementText());
            } else if (name == QLatin1String("stagr")) {
                sense.readingRestrictions.append(m_reader.readElementText());
            } else if (name == QLatin1String("pos")) {
                sense.partsOfSpeech.append(readEntity());
            } else if (name == QLatin1String("field")) {
                sense.fields.append(readEntity());
            } else if (name == QLatin1String("misc")) {
                sense.misc.append(readEntity());
            } else if (name == QLatin1String("dial")) {
                sense.dialects.append(readEntity());
            } else if (name == QLatin1String("s_inf")) {
                sense.info = m_reader.readElementText();
            } else if (name == QLatin1String("gloss")) {
                const QString glossType = m_reader.attributes().value(QLatin1String("g_type")).toString();
                QString gloss = glossType.isEmpty() ? QString() : QLatin1String("(") + glossType + QLatin1String(".) ");
                gloss += m_reader.readElementText();
                sense.glosses.append(gloss);
            } else if (name == QLatin1String("xref")) {
                const QString type = m_reader.attributes().value(QLatin1String("type")).toString();
                QString prefix;
                if (type == QLatin1String("see"))
                    prefix = QStringLiteral("see: ");
                else if (type == QLatin1String("ant"))
                    prefix = QStringLiteral("antonym: ");
                else if (type == QLatin1String("syn"))
                    prefix = QStringLiteral("synonym: ");
                else if (!type.isEmpty())
                    prefix = type + QLatin1String(": ");
                sense.crossReferences.append(prefix + m_reader.readElementText());
            } else {
                m_reader.skipCurrentElement();
            }
        }
        return sense;
    }

    LoanwordSource readLoanwordSource()
    {
        LoanwordSource source;
        const QXmlStreamAttributes attributes = m_reader.attributes();
        const QString language = attributes.value(QLatin1String("xml:lang")).toString();
        source.language = language.isEmpty() ? QStringLiteral("English") : languageName(language);
        source.isPart = attributes.value(QLatin1String("ls_type")) == QLatin1String("part");
        source.isWasei = attributes.hasAttribute(QLatin1String("ls_wasei"));
        source.originalWord = m_reader.readElementText();
        return source;
    }

    QXmlStreamReader &m_reader;
    DtdEntityMap &m_entities;
};

// The port of JmdictRecordBuilder.GetRecordsFromEntry.
class RecordBuilder
{
public:
    RecordBuilder(const Entry &entry, RecordMap &records)
        : m_entry(entry)
        , m_records(records)
    {}

    void build()
    {
        if (m_entry.senses.isEmpty() || m_entry.readingElements.isEmpty())
            return;

        for (const Sense &sense : m_entry.senses) {
            QList<QString> stagK;
            for (const QString &value : sense.spellingRestrictions)
                stagK.append(normalizeKey(value));
            QList<QString> stagR;
            for (const QString &value : sense.readingRestrictions)
                stagR.append(normalizeKey(value));
            m_stagKInHiragana.append(stagK);
            m_stagRInHiragana.append(stagR);
        }

        for (const KanjiElement &element : m_entry.kanjiElements) {
            if (element.searchOnly)
                continue;
            m_spellings.append(element.keb);
            m_spellingOrthographyInfo.append(element.orthographyInfo);
        }

        if (!m_spellings.isEmpty())
            processKanjiElements();
        processReadingElements();
    }

private:
    void processKanjiElements()
    {
        const QString firstPrimarySpellingInHiragana = normalizeKey(m_spellings.first());
        RecordPtr recordForFirstPrimarySpelling;
        qsizetype index = 0;

        for (const KanjiElement &element : m_entry.kanjiElements) {
            const QString key = normalizeKey(element.keb);
            if (m_records.contains(key)) {
                if (!element.searchOnly)
                    ++index;
                continue;
            }

            if (element.searchOnly) {
                repointSearchOnly(key, firstPrimarySpellingInHiragana, recordForFirstPrimarySpelling);
                continue;
            }

            QList<QString> readings;
            QList<QString> readingsInHiragana;
            QList<QList<QString>> readingsOrthographyInfo;
            for (const ReadingElement &reading : m_entry.readingElements) {
                if (reading.searchOnly)
                    continue;
                if (!reading.restrictions.isEmpty() && !reading.restrictions.contains(element.keb))
                    continue;
                readings.append(reading.reb);
                readingsInHiragana.append(normalizeKey(reading.reb));
                readingsOrthographyInfo.append(reading.orthographyInfo);
            }

            SenseSelection selection;
            for (qsizetype i = 0; i < m_entry.senses.size(); ++i) {
                const QList<QString> &stagK = m_stagKInHiragana.at(i);
                const QList<QString> &stagR = m_stagRInHiragana.at(i);
                const bool unrestricted = stagK.isEmpty() && stagR.isEmpty();
                if (unrestricted || (!stagK.isEmpty() && stagK.contains(key)) ||
                    (!stagR.isEmpty() && containsAny(stagR, readingsInHiragana))) {
                    appendSense(selection, m_entry.senses.at(i));
                }
            }

            auto record = std::make_shared<JmdictRecord>();
            record->entryId = m_entry.id;
            record->primarySpelling = element.keb;
            record->primarySpellingOrthographyInfo = m_spellingOrthographyInfo.value(index);
            record->alternativeSpellings = withoutIndex(m_spellings, index);
            record->alternativeSpellingsOrthographyInfo =
                trimIfAllEmpty(withoutIndex(m_spellingOrthographyInfo, index));
            record->readings = readings;
            record->readingsOrthographyInfo = trimIfAllEmpty(readingsOrthographyInfo);
            record->loanwordEtymology = m_entry.loanwordEtymology;
            record->info = m_entry.info;
            // The rank of this keb's own ke_pri. The reading's re_pri is the union over every
            // spelling of the entry, so folding it in would give 廃虚 the rank of 廃墟.
            record->priorityRank = jmdictPriorityRank(element.priority);
            applySenseSelection(*record, selection);

            m_records.add(key, record);
            ++index;
        }
    }

    void processReadingElements()
    {
        QList<QString> readings;
        QList<QList<QString>> readingsOrthographyInfo;
        QList<QList<QString>> readingPriority;
        for (const ReadingElement &element : m_entry.readingElements) {
            if (element.searchOnly)
                continue;
            readings.append(element.reb);
            readingsOrthographyInfo.append(element.orthographyInfo);
            readingPriority.append(element.priority);
        }
        if (readings.isEmpty())
            return;

        const QString firstReadingInHiragana = normalizeKey(readings.first());
        RecordPtr recordForFirstReading;
        const bool spellingsExist = !m_spellings.isEmpty();
        qsizetype index = 0;

        for (qsizetype elementIndex = 0; elementIndex < m_entry.readingElements.size(); ++elementIndex) {
            const ReadingElement &element = m_entry.readingElements.at(elementIndex);
            const QString key = normalizeKey(element.reb);
            if (m_records.contains(key)) {
                if (!element.searchOnly)
                    ++index;
                continue;
            }

            if (element.searchOnly) {
                repointSearchOnly(key, firstReadingInHiragana, recordForFirstReading);
                continue;
            }

            QString primarySpelling;
            QList<QString> primarySpellingOrthographyInfo;
            QList<QString> recordReadings;
            QList<QList<QString>> recordReadingsOrthographyInfo;
            QList<QString> alternativeSpellings;
            QList<QList<QString>> alternativeSpellingsOrthographyInfo;
            qint32 spellingRank = 0;

            if (!element.restrictions.isEmpty() || spellingsExist) {
                if (!element.restrictions.isEmpty()) {
                    primarySpelling = element.restrictions.first();
                    alternativeSpellings = withoutIndex(element.restrictions, 0);
                } else {
                    primarySpelling = m_spellings.first();
                    alternativeSpellings = withoutIndex(m_spellings, 0);
                }

                // A reading restricted to a spelling borrows the readings and the orthography
                // info of the record already built for that spelling, so the two records render
                // the same headword group (JmdictRecordBuilder.cs).
                if (const RecordPtr mainRecord = m_records.value(normalizeKey(primarySpelling))) {
                    recordReadings = mainRecord->readings;
                    primarySpellingOrthographyInfo = mainRecord->primarySpellingOrthographyInfo;
                    alternativeSpellingsOrthographyInfo = mainRecord->alternativeSpellingsOrthographyInfo;
                    recordReadingsOrthographyInfo = mainRecord->readingsOrthographyInfo;
                }

                for (const KanjiElement &kanjiElement : m_entry.kanjiElements) {
                    if (kanjiElement.keb == primarySpelling)
                        spellingRank = jmdictPriorityRank(kanjiElement.priority);
                }
            } else {
                primarySpelling = element.reb;
                primarySpellingOrthographyInfo = readingsOrthographyInfo.value(index);
                alternativeSpellings = withoutIndex(readings, index);
                alternativeSpellingsOrthographyInfo = trimIfAllEmpty(withoutIndex(readingsOrthographyInfo, index));
            }

            QList<QString> alternativeSpellingsInHiragana;
            for (const QString &spelling : alternativeSpellings)
                alternativeSpellingsInHiragana.append(normalizeKey(spelling));
            const QString primarySpellingInHiragana = normalizeKey(primarySpelling);

            SenseSelection selection;
            for (qsizetype i = 0; i < m_entry.senses.size(); ++i) {
                const QList<QString> &stagK = m_stagKInHiragana.at(i);
                const QList<QString> &stagR = m_stagRInHiragana.at(i);
                const bool unrestricted = stagK.isEmpty() && stagR.isEmpty();
                const bool readingAllows = !stagR.isEmpty() && stagR.contains(key);
                const bool spellingAllows = !stagK.isEmpty() && (stagK.contains(primarySpellingInHiragana) ||
                                                                 containsAny(stagK, alternativeSpellingsInHiragana));
                if (unrestricted || readingAllows || spellingAllows)
                    appendSense(selection, m_entry.senses.at(i));
            }

            auto record = std::make_shared<JmdictRecord>();
            record->entryId = m_entry.id;
            record->primarySpelling = primarySpelling;
            record->primarySpellingOrthographyInfo = primarySpellingOrthographyInfo;
            record->alternativeSpellings = alternativeSpellings;
            record->alternativeSpellingsOrthographyInfo = alternativeSpellingsOrthographyInfo;
            record->readings = recordReadings;
            record->readingsOrthographyInfo = recordReadingsOrthographyInfo;
            record->loanwordEtymology = m_entry.loanwordEtymology;
            record->info = m_entry.info;
            // The rank of the element the primary spelling came from: the borrowed kanji element
            // when the record answers for a spelling, and this reading element otherwise.
            record->priorityRank = spellingRank != 0 ? spellingRank : jmdictPriorityRank(readingPriority.value(index));
            applySenseSelection(*record, selection);

            m_records.add(key, record);
            ++index;

            // The record built for the first reading answers for every kanji spelling that has
            // no record of its own, which is what makes a kana-only entry reachable by its kanji
            // form (JmdictRecordBuilder.cs).
            if (elementIndex == 0 && spellingsExist) {
                for (const KanjiElement &kanjiElement : m_entry.kanjiElements)
                    m_records.add(normalizeKey(kanjiElement.keb), record);
            }
        }
    }

    // The sK and sk forms: a search-only spelling or reading becomes a key pointing at the record
    // of the first primary form, unless the long-vowel machinery already reaches one from the
    // other (JmdictRecordBuilder.cs).
    void repointSearchOnly(const QString &key, const QString &firstFormInHiragana, RecordPtr &cachedRecord)
    {
        if (jp::normalizeLongVowelMark(key).contains(firstFormInHiragana))
            return;

        if (jp::normalizeLongVowelMark(firstFormInHiragana).contains(key)) {
            if (cachedRecord) {
                (void)m_records.take(firstFormInHiragana);
                m_records.add(key, cachedRecord);
                return;
            }
            cachedRecord = m_records.take(firstFormInHiragana);
            if (cachedRecord)
                m_records.add(key, cachedRecord);
            return;
        }

        if (!cachedRecord)
            cachedRecord = m_records.value(firstFormInHiragana);
        if (cachedRecord)
            m_records.add(key, cachedRecord);
    }

    const Entry &m_entry;
    RecordMap &m_records;
    QList<QList<QString>> m_stagKInHiragana;
    QList<QList<QString>> m_stagRInHiragana;
    QList<QString> m_spellings;
    QList<QList<QString>> m_spellingOrthographyInfo;
};

} // namespace

JmdictImporter::JmdictImporter(bool properNameEntries)
    : m_properNameEntries(properNameEntries)
{}

JmdictImporter::~JmdictImporter() = default;

QString JmdictImporter::name() const
{
    return QStringLiteral("JMdict");
}

ImportResult
JmdictImporter::import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel)
{
    ImportResult result;

    SourceReader reader;
    if (!reader.open(source)) {
        result.errorString = reader.errorString();
        writer.abort();
        return result;
    }

    m_entities = {};
    m_wordClasses.clear();

    DtdEntityMap entities;
    KanjiExampleCollector examples;
    QXmlStreamReader xml(reader.device());
    qint64 entryCount = 0;
    int lastPercent = -1;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::DTD) {
            entities.readDeclarations(xml);
            continue;
        }
        if (token != QXmlStreamReader::StartElement || xml.name() != QLatin1String("entry"))
            continue;

        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            writer.abort();
            return result;
        }

        EntryReader entryReader(xml, entities);
        const Entry entry = entryReader.readEntry();
        ++entryCount;

        // https://github.com/JMdictProject/JMdictIssues/issues/94: the proper names that overlap
        // JMnedict occupy this ent_seq range.
        if (!m_properNameEntries && entry.id >= 5000000 && entry.id <= 5999999)
            continue;

        RecordMap records;
        RecordBuilder(entry, records).build();

        // One store row per distinct record, carrying every key that reaches it.
        std::vector<std::pair<RecordPtr, QList<QString>>> grouped;
        QHash<const JmdictRecord *, qsizetype> groupIndex;
        for (const auto &slot : records.slots()) {
            if (!slot.second)
                continue;
            const auto found = groupIndex.constFind(slot.second.get());
            if (found != groupIndex.constEnd()) {
                grouped[static_cast<size_t>(found.value())].second.append(slot.first);
            } else {
                groupIndex.insert(slot.second.get(), static_cast<qsizetype>(grouped.size()));
                grouped.emplace_back(slot.second, QList<QString>{slot.first});
            }

            // The word-class table is keyed the same way the store is, so the deconjugation gate
            // sees every key a record answers for.
            const JmdictRecord &record = *slot.second;
            QList<QString> validClasses;
            for (const QList<QString> &senseClasses : record.wordClasses.perSense) {
                for (const QString &wordClass : senseClasses) {
                    if (isDeconjugationWordClass(wordClass) && !validClasses.contains(wordClass))
                        validClasses.append(wordClass);
                }
            }
            for (const QString &wordClass : record.wordClasses.sharedByAllSenses) {
                if (isDeconjugationWordClass(wordClass) && !validClasses.contains(wordClass))
                    validClasses.append(wordClass);
            }
            if (validClasses.isEmpty())
                continue;

            // A key that came from a reading only contributes when it is also the normalized
            // primary spelling, which is what keeps one entry per headword rather than one per
            // reading (JmdictWordClassUtils.PopulateFromJmdictContents).
            bool keyFromReading = false;
            for (const QString &reading : record.readings) {
                if (normalizeKey(reading) == slot.first) {
                    keyFromReading = true;
                    break;
                }
            }
            if (keyFromReading && normalizeKey(record.primarySpelling) != slot.first)
                continue;

            m_wordClasses.add(slot.first,
                              WordClassEntry{.spelling = record.primarySpelling,
                                             .wordClasses = validClasses,
                                             .readings = record.readings});
        }

        for (const auto &group : grouped) {
            Record storeRecord;
            storeRecord.type = DictType::JMdict;
            storeRecord.data = *group.first;
            const std::vector<QString> keys(group.second.cbegin(), group.second.cend());
            if (writer.addRecord(storeRecord, keys) < 0) {
                result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
                writer.abort();
                return result;
            }
            examples.offer(*group.first);
        }

        if ((entryCount & 0x0FFF) == 0 && progress) {
            const int percent = reader.percent();
            if (percent != lastPercent) {
                lastPercent = percent;
                progress(percent, QStringLiteral("Reading JMdict"));
            }
        }
    }

    if (xml.hasError()) {
        result.errorString = xml.errorString();
        writer.abort();
        return result;
    }

    // The example words are keyed under the extras prefix, which no normalized search key can
    // start with, so the kanji card is one point query rather than a scan of the kanji's own
    // JMdict records.
    const auto &collected = examples.examples();
    for (auto entry = collected.constBegin(); entry != collected.constEnd(); ++entry) {
        KanjiExamplesRecord record;
        record.kanji = entry.key();
        for (const auto &candidate : entry.value())
            record.examples.append(candidate.second);

        Record storeRecord;
        storeRecord.type = DictType::JMdict;
        storeRecord.data = record;
        const std::vector<QString> keys{kanjiExamplesKey(record.kanji)};
        (void)writer.addRecord(storeRecord, keys);
    }

    m_entities = entities.entities();
    m_wordClasses.indexReadings();

    writer.setMeta(metakeys::entities, entities.toJson());
    writer.setMeta(metakeys::sourceFormat, QStringLiteral("JMdict"));
    if (!writer.finish()) {
        result.errorString = QStringLiteral("Cannot finish writing the dictionary database");
        return result;
    }

    result.ok = true;
    result.recordCount = writer.recordCount();
    result.keyCount = writer.keyCount();
    result.maxKeyLength = writer.maxKeyLength();
    qCInfo(logDictImport,
           "JMdict: %lld entries, %lld records, %lld keys, longest key %d",
           static_cast<long long>(entryCount),
           static_cast<long long>(result.recordCount),
           static_cast<long long>(result.keyCount),
           result.maxKeyLength);
    return result;
}

} // namespace maru::dict
