// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "codec.h"

#include "core/logging.h"

#include <QCborStreamReader>
#include <QCborStreamWriter>

namespace maru::dict
{

namespace
{

// The top-level payload array is written with an indefinite length, so a field can be appended to
// a record without recounting a literal, and every nested list with its definite length, because
// a nested list's size is already known and a definite length costs one byte less per list. A
// JMdict record carries about twenty nested lists, which is where the byte matters.
class Encoder
{
public:
    explicit Encoder(QByteArray *target)
        : m_writer(target)
    {}

    void startRecord(RecordKind kind)
    {
        m_writer.startArray();
        m_writer.append(static_cast<quint64>(codecVersion));
        m_writer.append(static_cast<quint64>(kind));
    }

    void endRecord()
    {
        m_writer.endArray();
    }

    void number(qint64 value)
    {
        m_writer.append(value);
    }

    void real(double value)
    {
        m_writer.append(value);
    }

    void boolean(bool value)
    {
        m_writer.append(value);
    }

    void string(const QString &value)
    {
        m_writer.append(value);
    }

    void stringList(const QList<QString> &values)
    {
        m_writer.startArray(static_cast<quint64>(values.size()));
        for (const QString &value : values)
            m_writer.append(value);
        m_writer.endArray();
    }

    void byteList(const QList<quint8> &values)
    {
        m_writer.startArray(static_cast<quint64>(values.size()));
        for (const quint8 value : values)
            m_writer.append(static_cast<quint64>(value));
        m_writer.endArray();
    }

    void stringListList(const QList<QList<QString>> &values)
    {
        m_writer.startArray(static_cast<quint64>(values.size()));
        for (const QList<QString> &inner : values)
            stringList(inner);
        m_writer.endArray();
    }

    void senseTags(const SenseTags &tags)
    {
        m_writer.startArray(2);
        stringList(tags.sharedByAllSenses);
        stringListList(tags.perSense);
        m_writer.endArray();
    }

    void image(const ImageInfo &info)
    {
        m_writer.startArray(5);
        m_writer.append(info.path);
        m_writer.append(static_cast<qint64>(info.pixelWidth));
        m_writer.append(static_cast<qint64>(info.pixelHeight));
        m_writer.append(info.width);
        m_writer.append(info.height);
        m_writer.endArray();
    }

    void optionalImage(const std::optional<ImageInfo> &info)
    {
        if (info.has_value())
            image(*info);
        else
            m_writer.appendNull();
    }

    void imageList(const QList<ImageInfo> &infos)
    {
        m_writer.startArray(static_cast<quint64>(infos.size()));
        for (const ImageInfo &info : infos)
            image(info);
        m_writer.endArray();
    }

    void loanwordSources(const QList<LoanwordSource> &sources)
    {
        m_writer.startArray(static_cast<quint64>(sources.size()));
        for (const LoanwordSource &source : sources) {
            m_writer.startArray(4);
            m_writer.append(source.language);
            m_writer.append(source.originalWord);
            m_writer.append(source.isPart);
            m_writer.append(source.isWasei);
            m_writer.endArray();
        }
        m_writer.endArray();
    }

    void kanjiExamples(const QList<KanjiExample> &examples)
    {
        m_writer.startArray(static_cast<quint64>(examples.size()));
        for (const KanjiExample &example : examples) {
            m_writer.startArray(3);
            m_writer.append(example.spelling);
            m_writer.append(example.reading);
            m_writer.append(example.gloss);
            m_writer.endArray();
        }
        m_writer.endArray();
    }

private:
    QCborStreamWriter m_writer;
};

// Every read checks m_ok first, so one malformed field short-circuits the rest of the record and
// the caller only has to test ok() once, after the last field.
class Decoder
{
public:
    Decoder(const char *data, qsizetype size)
        : m_reader(data, size)
    {}

    [[nodiscard]] bool ok() const
    {
        return m_ok && m_reader.lastError() == QCborError::NoError;
    }

    bool enterArray()
    {
        if (!m_ok)
            return false;
        if (!m_reader.isArray()) {
            m_ok = false;
            return false;
        }
        m_ok = m_reader.enterContainer();
        return m_ok;
    }

    // Reads and discards whatever remains of the current container, then leaves it. A record
    // written by the same codec version has nothing left, so this is a no-op on the normal path.
    void leaveArray()
    {
        if (!m_ok)
            return;
        while (m_reader.hasNext())
            skip();
        m_ok = m_reader.leaveContainer();
    }

    qint64 number()
    {
        if (!m_ok)
            return 0;
        if (!m_reader.isInteger()) {
            m_ok = false;
            return 0;
        }
        const qint64 value = m_reader.toInteger();
        m_ok = m_reader.next();
        return value;
    }

    double real()
    {
        if (!m_ok)
            return 0.0;
        if (m_reader.isDouble()) {
            const double value = m_reader.toDouble();
            m_ok = m_reader.next();
            return value;
        }
        if (m_reader.isInteger()) {
            const auto value = static_cast<double>(m_reader.toInteger());
            m_ok = m_reader.next();
            return value;
        }
        m_ok = false;
        return 0.0;
    }

    bool boolean()
    {
        if (!m_ok)
            return false;
        if (!m_reader.isBool()) {
            m_ok = false;
            return false;
        }
        const bool value = m_reader.toBool();
        m_ok = m_reader.next();
        return value;
    }

    QString string()
    {
        if (!m_ok)
            return {};
        if (!m_reader.isString()) {
            m_ok = false;
            return {};
        }
        QString value = m_reader.readAllString();
        if (m_reader.lastError() != QCborError::NoError)
            m_ok = false;
        return value;
    }

    QList<QString> stringList()
    {
        QList<QString> values;
        if (!enterArray())
            return values;
        while (m_ok && m_reader.hasNext())
            values.append(string());
        leaveArray();
        return values;
    }

    QList<quint8> byteList()
    {
        QList<quint8> values;
        if (!enterArray())
            return values;
        while (m_ok && m_reader.hasNext())
            values.append(static_cast<quint8>(number()));
        leaveArray();
        return values;
    }

    QList<QList<QString>> stringListList()
    {
        QList<QList<QString>> values;
        if (!enterArray())
            return values;
        while (m_ok && m_reader.hasNext())
            values.append(stringList());
        leaveArray();
        return values;
    }

    SenseTags senseTags()
    {
        SenseTags tags;
        if (!enterArray())
            return tags;
        tags.sharedByAllSenses = stringList();
        tags.perSense = stringListList();
        leaveArray();
        return tags;
    }

    ImageInfo image()
    {
        ImageInfo info;
        if (!enterArray())
            return info;
        info.path = string();
        info.pixelWidth = static_cast<int>(number());
        info.pixelHeight = static_cast<int>(number());
        info.width = real();
        info.height = real();
        leaveArray();
        return info;
    }

    std::optional<ImageInfo> optionalImage()
    {
        if (!m_ok)
            return std::nullopt;
        if (m_reader.isNull()) {
            m_ok = m_reader.next();
            return std::nullopt;
        }
        return image();
    }

    QList<ImageInfo> imageList()
    {
        QList<ImageInfo> infos;
        if (!enterArray())
            return infos;
        while (m_ok && m_reader.hasNext())
            infos.append(image());
        leaveArray();
        return infos;
    }

    QList<LoanwordSource> loanwordSources()
    {
        QList<LoanwordSource> sources;
        if (!enterArray())
            return sources;
        while (m_ok && m_reader.hasNext()) {
            LoanwordSource source;
            if (!enterArray())
                break;
            source.language = string();
            source.originalWord = string();
            source.isPart = boolean();
            source.isWasei = boolean();
            leaveArray();
            sources.append(source);
        }
        leaveArray();
        return sources;
    }

    QList<KanjiExample> kanjiExamples()
    {
        QList<KanjiExample> examples;
        if (!enterArray())
            return examples;
        while (m_ok && m_reader.hasNext()) {
            KanjiExample example;
            if (!enterArray())
                break;
            example.spelling = string();
            example.reading = string();
            example.gloss = string();
            leaveArray();
            examples.append(example);
        }
        leaveArray();
        return examples;
    }

private:
    // A CBOR container holds containers, so skipping one is a recursive descent. The depth is
    // bounded by the record shapes above, which nest three levels at most.
    // NOLINTNEXTLINE(misc-no-recursion)
    void skip()
    {
        if (m_reader.isContainer()) {
            if (!m_reader.enterContainer()) {
                m_ok = false;
                return;
            }
            while (m_reader.hasNext())
                skip();
            m_ok = m_reader.leaveContainer();
            return;
        }
        if (m_reader.isString()) {
            (void)m_reader.readAllString();
            return;
        }
        if (m_reader.isByteArray()) {
            (void)m_reader.readAllByteArray();
            return;
        }
        m_ok = m_reader.next();
    }

    QCborStreamReader m_reader;
    bool m_ok = true;
};

void encodeJmdict(Encoder &encoder, const JmdictRecord &record)
{
    encoder.number(record.entryId);
    encoder.string(record.primarySpelling);
    encoder.stringList(record.primarySpellingOrthographyInfo);
    encoder.stringList(record.alternativeSpellings);
    encoder.stringListList(record.alternativeSpellingsOrthographyInfo);
    encoder.stringList(record.readings);
    encoder.stringListList(record.readingsOrthographyInfo);
    encoder.stringListList(record.definitions);
    encoder.senseTags(record.wordClasses);
    encoder.senseTags(record.fields);
    encoder.senseTags(record.misc);
    encoder.senseTags(record.dialects);
    encoder.stringListList(record.spellingRestrictions);
    encoder.stringListList(record.readingRestrictions);
    encoder.stringList(record.definitionInfo);
    encoder.stringListList(record.crossReferences);
    encoder.loanwordSources(record.loanwordEtymology);
    encoder.stringList(record.info);
    encoder.number(record.priorityRank);
}

JmdictRecord decodeJmdict(Decoder &decoder)
{
    JmdictRecord record;
    record.entryId = static_cast<qint32>(decoder.number());
    record.primarySpelling = decoder.string();
    record.primarySpellingOrthographyInfo = decoder.stringList();
    record.alternativeSpellings = decoder.stringList();
    record.alternativeSpellingsOrthographyInfo = decoder.stringListList();
    record.readings = decoder.stringList();
    record.readingsOrthographyInfo = decoder.stringListList();
    record.definitions = decoder.stringListList();
    record.wordClasses = decoder.senseTags();
    record.fields = decoder.senseTags();
    record.misc = decoder.senseTags();
    record.dialects = decoder.senseTags();
    record.spellingRestrictions = decoder.stringListList();
    record.readingRestrictions = decoder.stringListList();
    record.definitionInfo = decoder.stringList();
    record.crossReferences = decoder.stringListList();
    record.loanwordEtymology = decoder.loanwordSources();
    record.info = decoder.stringList();
    record.priorityRank = static_cast<qint32>(decoder.number());
    return record;
}

void encodeJmnedict(Encoder &encoder, const JmnedictRecord &record)
{
    encoder.number(record.entryId);
    encoder.string(record.primarySpelling);
    encoder.stringList(record.alternativeSpellings);
    encoder.stringList(record.readings);
    encoder.stringListList(record.definitions);
    encoder.stringListList(record.nameTypes);
}

JmnedictRecord decodeJmnedict(Decoder &decoder)
{
    JmnedictRecord record;
    record.entryId = static_cast<qint32>(decoder.number());
    record.primarySpelling = decoder.string();
    record.alternativeSpellings = decoder.stringList();
    record.readings = decoder.stringList();
    record.definitions = decoder.stringListList();
    record.nameTypes = decoder.stringListList();
    return record;
}

void encodeKanjidic(Encoder &encoder, const KanjidicRecord &record)
{
    encoder.stringList(record.definitions);
    encoder.stringList(record.onReadings);
    encoder.stringList(record.kunReadings);
    encoder.stringList(record.nanoriReadings);
    encoder.stringList(record.radicalNames);
    encoder.number(record.strokeCount);
    encoder.number(record.grade);
    encoder.number(record.frequency);
}

KanjidicRecord decodeKanjidic(Decoder &decoder)
{
    KanjidicRecord record;
    record.definitions = decoder.stringList();
    record.onReadings = decoder.stringList();
    record.kunReadings = decoder.stringList();
    record.nanoriReadings = decoder.stringList();
    record.radicalNames = decoder.stringList();
    record.strokeCount = static_cast<quint8>(decoder.number());
    record.grade = static_cast<quint8>(decoder.number());
    record.frequency = static_cast<qint32>(decoder.number());
    return record;
}

void encodeYomitanTerm(Encoder &encoder, const YomitanTermRecord &record)
{
    encoder.string(record.primarySpelling);
    encoder.string(record.reading);
    encoder.real(record.popularityScore);
    encoder.stringList(record.definitions);
    encoder.stringList(record.definitionsPlain);
    encoder.stringList(record.wordClasses);
    encoder.stringList(record.definitionTags);
    encoder.stringList(record.termTags);
    encoder.imageList(record.images);
    encoder.number(record.sequence);
}

YomitanTermRecord decodeYomitanTerm(Decoder &decoder)
{
    YomitanTermRecord record;
    record.primarySpelling = decoder.string();
    record.reading = decoder.string();
    record.popularityScore = decoder.real();
    record.definitions = decoder.stringList();
    record.definitionsPlain = decoder.stringList();
    record.wordClasses = decoder.stringList();
    record.definitionTags = decoder.stringList();
    record.termTags = decoder.stringList();
    record.images = decoder.imageList();
    record.sequence = static_cast<qint32>(decoder.number());
    return record;
}

void encodeYomitanKanji(Encoder &encoder, const YomitanKanjiRecord &record)
{
    encoder.stringList(record.onReadings);
    encoder.stringList(record.kunReadings);
    encoder.stringList(record.tags);
    encoder.stringList(record.definitions);
    encoder.stringList(record.stats);
}

YomitanKanjiRecord decodeYomitanKanji(Decoder &decoder)
{
    YomitanKanjiRecord record;
    record.onReadings = decoder.stringList();
    record.kunReadings = decoder.stringList();
    record.tags = decoder.stringList();
    record.definitions = decoder.stringList();
    record.stats = decoder.stringList();
    return record;
}

void encodePitchAccent(Encoder &encoder, const PitchAccentRecord &record)
{
    encoder.string(record.spelling);
    encoder.string(record.reading);
    encoder.byteList(record.positions);
}

PitchAccentRecord decodePitchAccent(Decoder &decoder)
{
    PitchAccentRecord record;
    record.spelling = decoder.string();
    record.reading = decoder.string();
    record.positions = decoder.byteList();
    return record;
}

void encodeCustomWord(Encoder &encoder, const CustomWordRecord &record)
{
    encoder.string(record.primarySpelling);
    encoder.stringList(record.alternativeSpellings);
    encoder.stringList(record.readings);
    encoder.stringList(record.definitions);
    encoder.stringList(record.wordClasses);
    encoder.boolean(record.hasUserDefinedWordClass);
}

CustomWordRecord decodeCustomWord(Decoder &decoder)
{
    CustomWordRecord record;
    record.primarySpelling = decoder.string();
    record.alternativeSpellings = decoder.stringList();
    record.readings = decoder.stringList();
    record.definitions = decoder.stringList();
    record.wordClasses = decoder.stringList();
    record.hasUserDefinedWordClass = decoder.boolean();
    return record;
}

void encodeCustomName(Encoder &encoder, const CustomNameRecord &record)
{
    encoder.string(record.primarySpelling);
    encoder.string(record.reading);
    encoder.string(record.nameType);
    encoder.string(record.extraInfo);
    encoder.optionalImage(record.image);
}

CustomNameRecord decodeCustomName(Decoder &decoder)
{
    CustomNameRecord record;
    record.primarySpelling = decoder.string();
    record.reading = decoder.string();
    record.nameType = decoder.string();
    record.extraInfo = decoder.string();
    record.image = decoder.optionalImage();
    return record;
}

void encodeFrequency(Encoder &encoder, const FrequencyRecord &record)
{
    encoder.string(record.spelling);
    encoder.number(record.frequency);
}

FrequencyRecord decodeFrequency(Decoder &decoder)
{
    FrequencyRecord record;
    record.spelling = decoder.string();
    record.frequency = static_cast<qint32>(decoder.number());
    return record;
}

void encodeKanjiExamples(Encoder &encoder, const KanjiExamplesRecord &record)
{
    encoder.string(record.kanji);
    encoder.kanjiExamples(record.examples);
}

KanjiExamplesRecord decodeKanjiExamples(Decoder &decoder)
{
    KanjiExamplesRecord record;
    record.kanji = decoder.string();
    record.examples = decoder.kanjiExamples();
    return record;
}

void encodeKanjiComponents(Encoder &encoder, const KanjiComponentsRecord &record)
{
    encoder.string(record.kanji);
    encoder.stringList(record.components);
}

KanjiComponentsRecord decodeKanjiComponents(Decoder &decoder)
{
    KanjiComponentsRecord record;
    record.kanji = decoder.string();
    record.components = decoder.stringList();
    return record;
}

// The first two payload elements, read without touching the rest of the record.
struct PayloadHeader
{
    bool ok = false;
    int version = 0;
    RecordKind kind = RecordKind::Jmdict;
};

PayloadHeader readHeader(Decoder &decoder)
{
    PayloadHeader header;
    if (!decoder.enterArray())
        return header;
    const qint64 version = decoder.number();
    const qint64 kind = decoder.number();
    if (!decoder.ok())
        return header;
    if (kind < 0 || kind > static_cast<qint64>(RecordKind::KanjiComponents))
        return header;
    header.ok = true;
    header.version = static_cast<int>(version);
    header.kind = static_cast<RecordKind>(kind);
    return header;
}

} // namespace

QByteArray encodeRecord(const Record &record)
{
    QByteArray payload;
    Encoder encoder(&payload);
    encoder.startRecord(record.kind());
    std::visit(
        [&encoder](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, JmdictRecord>)
                encodeJmdict(encoder, value);
            else if constexpr (std::is_same_v<T, JmnedictRecord>)
                encodeJmnedict(encoder, value);
            else if constexpr (std::is_same_v<T, KanjidicRecord>)
                encodeKanjidic(encoder, value);
            else if constexpr (std::is_same_v<T, YomitanTermRecord>)
                encodeYomitanTerm(encoder, value);
            else if constexpr (std::is_same_v<T, YomitanKanjiRecord>)
                encodeYomitanKanji(encoder, value);
            else if constexpr (std::is_same_v<T, PitchAccentRecord>)
                encodePitchAccent(encoder, value);
            else if constexpr (std::is_same_v<T, CustomWordRecord>)
                encodeCustomWord(encoder, value);
            else if constexpr (std::is_same_v<T, CustomNameRecord>)
                encodeCustomName(encoder, value);
            else if constexpr (std::is_same_v<T, FrequencyRecord>)
                encodeFrequency(encoder, value);
            else if constexpr (std::is_same_v<T, KanjiExamplesRecord>)
                encodeKanjiExamples(encoder, value);
            else
                encodeKanjiComponents(encoder, value);
        },
        record.data);
    encoder.endRecord();
    return payload;
}

std::optional<Record> decodeRecord(DictType type, QByteArrayView payload)
{
    Decoder decoder(payload.data(), payload.size());
    const PayloadHeader header = readHeader(decoder);
    if (!header.ok) {
        qCWarning(logDict, "Record payload is malformed, %lld bytes", static_cast<long long>(payload.size()));
        return std::nullopt;
    }
    if (header.version != codecVersion) {
        qCWarning(
            logDict, "Record payload carries codec version %d, this build reads %d", header.version, codecVersion);
        return std::nullopt;
    }

    Record record;
    record.type = type;
    switch (header.kind) {
    case RecordKind::Jmdict:
        record.data = decodeJmdict(decoder);
        break;
    case RecordKind::Jmnedict:
        record.data = decodeJmnedict(decoder);
        break;
    case RecordKind::Kanjidic:
        record.data = decodeKanjidic(decoder);
        break;
    case RecordKind::YomitanTerm:
        record.data = decodeYomitanTerm(decoder);
        break;
    case RecordKind::YomitanKanji:
        record.data = decodeYomitanKanji(decoder);
        break;
    case RecordKind::PitchAccent:
        record.data = decodePitchAccent(decoder);
        break;
    case RecordKind::CustomWord:
        record.data = decodeCustomWord(decoder);
        break;
    case RecordKind::CustomName:
        record.data = decodeCustomName(decoder);
        break;
    case RecordKind::Frequency:
        record.data = decodeFrequency(decoder);
        break;
    case RecordKind::KanjiExamples:
        record.data = decodeKanjiExamples(decoder);
        break;
    case RecordKind::KanjiComponents:
        record.data = decodeKanjiComponents(decoder);
        break;
    }

    if (!decoder.ok()) {
        qCWarning(logDict, "Record payload of kind %d is truncated", static_cast<int>(header.kind));
        return std::nullopt;
    }
    return record;
}

std::optional<RecordKind> payloadKind(QByteArrayView payload)
{
    Decoder decoder(payload.data(), payload.size());
    const PayloadHeader header = readHeader(decoder);
    if (!header.ok || header.version != codecVersion)
        return std::nullopt;
    return header.kind;
}

} // namespace maru::dict
