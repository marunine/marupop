// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "yomitanimporter.h"

#include "core/logging.h"
#include "dict/importers/structuredcontent.h"
#include "dict/keynorm.h"
#include "dict/store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <KZip>

#include <algorithm>
#include <functional>
#include <memory>

namespace maru::dict
{

namespace
{

// A dictionary's banks are numbered term_bank_1.json to term_bank_N.json, and N passes 9, so a
// lexicographic sort would read bank 10 before bank 2. The order only matters for the record ids,
// which the numeric sort keeps stable across re-imports.
QStringList sortedBankNames(QStringList names)
{
    static const QRegularExpression trailingNumber(QStringLiteral("_(\\d+)\\.json$"));
    // A name carrying no trailing number sorts as -1, ahead of bank 1. Ordering by the pair
    // (number, name) is what makes the comparator a strict weak ordering: mixing a numeric
    // comparison for two matching names with a lexicographic one for the rest admits the cycle
    // term_bank_9.json < term_bank_10.json < term_bank_1_old.json < term_bank_9.json, and
    // std::ranges::sort reads past the start of the range on a comparator that reports one.
    const auto bankNumber = [](const QString &name) {
        const QRegularExpressionMatch match = trailingNumber.match(name);
        return match.hasMatch() ? match.captured(1).toInt() : -1;
    };
    std::ranges::sort(names, [&bankNumber](const QString &left, const QString &right) {
        const int leftNumber = bankNumber(left);
        const int rightNumber = bankNumber(right);
        if (leftNumber != rightNumber)
            return leftNumber < rightNumber;
        return left < right;
    });
    return names;
}

// label names the bank in the warning a parse failure logs. An empty json produces no warning
// here: BankSource::read() has already reported it, naming the directory or the archive the bytes
// were to come from.
QJsonArray parseBank(const QByteArray &json, const QString &label)
{
    if (json.isEmpty())
        return {};
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(logDictImport) << "Cannot parse" << label << error.errorString();
        return {};
    }
    return document.array();
}

QList<QString> splitTags(const QString &text)
{
    QList<QString> tags;
    const QList<QStringView> parts = QStringView(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QStringView part : parts) {
        const QString tag = part.trimmed().toString();
        if (!tag.isEmpty() && !tags.contains(tag))
            tags.append(tag);
    }
    return tags;
}

// The port of JL's TextUtils.ExtractFirstInt (JL.Core/Utilities/TextUtils.cs): the first run of
// digits and group separators in the text, or -1. It is what turns JPDB's "12345㋕" display values
// into a rank.
int extractFirstInt(const QString &text)
{
    qsizetype start = -1;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i).isDigit()) {
            start = i;
            break;
        }
    }
    if (start < 0)
        return -1;

    QString digits;
    for (qsizetype i = start; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (character.isDigit())
            digits.append(character);
        else if (character == QLatin1Char(','))
            continue;
        else
            break;
    }
    bool parsed = false;
    const int value = digits.toInt(&parsed);
    return parsed ? value : -1;
}

// The four shapes a "freq" row's data element takes, from
// FrequencyYomichanLoader.cs. reading is set when the object carries one.
struct FrequencyValue
{
    int frequency = -1;
    QString reading;
};

int frequencyFromValue(const QJsonValue &value)
{
    if (value.isDouble())
        return static_cast<int>(value.toDouble());
    if (value.isString())
        return extractFirstInt(value.toString());
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        int frequency = -1;
        if (object.contains(QLatin1String("value")))
            frequency = static_cast<int>(object.value(QLatin1String("value")).toDouble());
        if (frequency <= 0 && object.contains(QLatin1String("displayValue")))
            frequency = extractFirstInt(object.value(QLatin1String("displayValue")).toString());
        return frequency;
    }
    return -1;
}

FrequencyValue parseFrequency(const QJsonValue &data)
{
    FrequencyValue result;
    if (data.isObject()) {
        const QJsonObject object = data.toObject();
        if (object.contains(QLatin1String("reading"))) {
            result.reading = object.value(QLatin1String("reading")).toString();
            result.frequency = frequencyFromValue(object.value(QLatin1String("frequency")));
            return result;
        }
    }
    result.frequency = frequencyFromValue(data);
    return result;
}

// The pitch positions a "pitch" row declares. A numeric position is the mora index; a string
// position is a non-standard HLLL contour, whose accent is the index of the first L after the
// first H (PitchAccentRecord.cs).
QList<quint8> parsePitchPositions(const QJsonArray &pitches)
{
    QList<quint8> positions;
    for (const auto &pitch : pitches) {
        const QJsonValue position = pitch.toObject().value(QLatin1String("position"));
        if (position.isDouble()) {
            const int value = static_cast<int>(position.toDouble());
            if (value >= 0 && value <= 255)
                positions.append(static_cast<quint8>(value));
            continue;
        }
        if (!position.isString())
            continue;
        const QString contour = position.toString();
        int accent = 0;
        bool foundHigh = false;
        for (qsizetype i = 0; i < contour.size() && i < 256; ++i) {
            if (foundHigh) {
                if (contour.at(i) == QLatin1Char('L')) {
                    accent = static_cast<int>(i);
                    break;
                }
            } else if (contour.at(i) == QLatin1Char('H')) {
                foundHigh = true;
            }
        }
        positions.append(static_cast<quint8>(accent));
    }
    return positions;
}

YomitanIndex parseIndex(const QByteArray &json)
{
    YomitanIndex index;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return index;

    const QJsonObject object = document.object();
    index.valid = true;
    index.title = object.value(QLatin1String("title")).toString();
    index.revision = object.value(QLatin1String("revision")).toString();
    index.author = object.value(QLatin1String("author")).toString();
    index.description = object.value(QLatin1String("description")).toString();
    index.attribution = object.value(QLatin1String("attribution")).toString();
    index.url = object.value(QLatin1String("url")).toString();
    index.indexUrl = object.value(QLatin1String("indexUrl")).toString();
    index.isUpdatable = object.value(QLatin1String("isUpdatable")).toBool();
    index.frequencyMode = object.value(QLatin1String("frequencyMode")).toString();
    if (object.contains(QLatin1String("format")))
        index.format = static_cast<int>(object.value(QLatin1String("format")).toDouble());
    else if (object.contains(QLatin1String("version")))
        index.format = static_cast<int>(object.value(QLatin1String("version")).toDouble());
    return index;
}

// A primary spelling holding U+FFFD or a newline is a broken row rather than a headword
// (DictUtils.s_invalidCharactersForPrimarySpellings, DictUtils.cs).
bool isInvalidSpelling(const QString &spelling)
{
    return spelling.contains(QChar(0xFFFD)) || spelling.contains(QLatin1Char('\n'));
}

// Extracts archive into directory. Returns false when the archive cannot be opened.
bool extractZip(const QString &archivePath, const QString &directory)
{
    KZip zip(archivePath);
    if (!zip.open(QIODevice::ReadOnly)) {
        qCWarning(logDictImport) << "Cannot open the archive" << archivePath << zip.errorString();
        return false;
    }
    const KArchiveDirectory *root = zip.directory();
    if (root == nullptr)
        return false;

    QDir().mkpath(directory);
    // A Yomitan archive holds its files either at the root or inside a single directory. copyTo()
    // reproduces whichever shape the archive has; the bank lookup below descends one level when
    // the root holds no index.json.
    if (!root->copyTo(directory, true)) {
        qCWarning(logDictImport) << "Cannot extract" << archivePath << "into" << directory;
        return false;
    }
    return true;
}

// The directory holding index.json: directory itself when it holds one, otherwise the one
// subdirectory of it holding one. Falls back to directory when no subdirectory holds index.json
// and when two or more do.
//
// The count that decides is the number of subdirectories holding index.json, not the number of
// subdirectories: a .zip written by the macOS Finder carries a __MACOSX directory beside the
// wrapper, and __MACOSX holds no index.json. A directory holding two dictionaries falls back to
// itself, so detectTypes() reports no type for it and the Add Dictionary dialog asks for a
// narrower path rather than importing whichever of the two sorts first.
//
// Hidden subdirectories are counted, because KArchiveDirectory::entries() lists a dot-directory
// and QDir::Hidden is what makes QDir::entryList() match one. archiveBankDirectory() applies this
// same rule inside an unextracted archive, so detectTypes() over a .zip and the import that
// extracts it select the same banks.
QString bankDirectory(const QString &directory)
{
    if (QFileInfo(directory + QLatin1String("/index.json")).isFile())
        return directory;

    const QStringList children = QDir(directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    QString wrapper;
    for (const QString &child : children) {
        const QString candidate = directory + QLatin1Char('/') + child;
        if (!QFileInfo(candidate + QLatin1String("/index.json")).isFile())
            continue;
        if (!wrapper.isEmpty())
            return directory;
        wrapper = candidate;
    }
    return wrapper.isEmpty() ? directory : wrapper;
}

// The archive entry holding index.json, by the rule bankDirectory() applies on disk: root when it
// holds one, otherwise the one subdirectory of root holding one. Falls back to root.
const KArchiveDirectory *archiveBankDirectory(const KArchiveDirectory *root)
{
    if (root == nullptr)
        return nullptr;
    const KArchiveEntry *index = root->entry(QStringLiteral("index.json"));
    if (index != nullptr && index->isFile())
        return root;

    // KArchiveDirectory::entries() returns the keys of a QHash, whose order is unspecified.
    // Selecting the one subdirectory that holds index.json makes the result independent of it.
    const QStringList children = root->entries();
    const KArchiveDirectory *wrapper = nullptr;
    for (const QString &child : children) {
        const auto *childDirectory = dynamic_cast<const KArchiveDirectory *>(root->entry(child));
        if (childDirectory == nullptr)
            continue;
        const KArchiveEntry *nested = childDirectory->entry(QStringLiteral("index.json"));
        if (nested == nullptr || !nested->isFile())
            continue;
        if (wrapper != nullptr)
            return root;
        wrapper = childDirectory;
    }
    return wrapper != nullptr ? wrapper : root;
}

// The bank files of one Yomitan source, over an extracted directory or over a .zip that has not
// been extracted:
//
//  - names(pattern) returns the file names matching a glob such as "term_bank_*.json".
//  - size(name) returns the uncompressed length of one of those files in bytes, or -1.
//  - read(name) returns its bytes, and logs a warning naming the directory or the archive it came
//    from when it produces none.
//  - readPrefix(name, maxBytes) returns its first maxBytes bytes, decompressing an archive entry
//    as it goes rather than inflating the whole entry, so a bank of any length costs maxBytes of
//    memory.
//
// read() owns the warning about an empty bank, rather than the parse step that follows it,
// because only read() knows which of the two sources the name was resolved against. readPrefix()
// carries no such warning: a short prefix of a long bank is the expected result.
struct BankSource
{
    std::function<QStringList(const QString &pattern)> names;
    std::function<qint64(const QString &name)> size;
    std::function<QByteArray(const QString &name)> read;
    std::function<QByteArray(const QString &name, qint64 maxBytes)> readPrefix;
};

BankSource directoryBanks(const QString &directory)
{
    const QDir dir(directory);
    return BankSource{
        .names =
            [dir](const QString &pattern) {
                return dir.entryList({pattern}, QDir::Files);
            },
        .size =
            [dir](const QString &name) {
                return QFileInfo(dir.filePath(name)).size();
            },
        .read =
            [dir](const QString &name) {
                const QString path = dir.filePath(name);
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    qCWarning(logDictImport) << "Cannot read" << path << file.errorString();
                    return QByteArray();
                }
                const QByteArray data = file.readAll();
                if (data.isEmpty())
                    qCWarning(logDictImport) << "Bank" << path << "holds no data";
                return data;
            },
        .readPrefix =
            [dir](const QString &name, qint64 maxBytes) {
                QFile file(dir.filePath(name));
                if (!file.open(QIODevice::ReadOnly))
                    return QByteArray();
                return file.read(maxBytes);
            },
    };
}

// root must outlive the returned BankSource, because every callable reads through it. archivePath
// names the .zip in the warnings read() logs.
BankSource archiveBanks(const KArchiveDirectory *root, const QString &archivePath)
{
    const auto entryFile = [root](const QString &name) {
        return dynamic_cast<const KArchiveFile *>(root->entry(name));
    };
    return BankSource{
        .names =
            [root](const QString &pattern) {
                // Compile wildcard patterns once for the entry list rather than recompiling them for
                // every archive entry.
                const QRegularExpression glob = QRegularExpression::fromWildcard(pattern, Qt::CaseInsensitive);
                QStringList matched;
                const QStringList children = root->entries();
                for (const QString &child : children) {
                    if (!glob.match(child).hasMatch())
                        continue;
                    const KArchiveEntry *entry = root->entry(child);
                    if (entry != nullptr && entry->isFile())
                        matched.append(child);
                }
                return matched;
            },
        .size =
            [entryFile](const QString &name) {
                const KArchiveFile *file = entryFile(name);
                return file != nullptr ? file->size() : qint64{-1};
            },
        .read =
            [entryFile, archivePath](const QString &name) {
                const KArchiveFile *file = entryFile(name);
                if (file == nullptr) {
                    qCWarning(logDictImport) << "No entry" << name << "in" << archivePath;
                    return QByteArray();
                }
                // KZip inflates a Store and a Deflate entry. data() is empty for every other
                // compression method, which is why an empty result is reported here.
                const QByteArray data = file->data();
                if (data.isEmpty())
                    qCWarning(logDictImport) << "Bank" << name << "in" << archivePath << "holds no data";
                return data;
            },
        .readPrefix =
            [entryFile](const QString &name, qint64 maxBytes) {
                const KArchiveFile *file = entryFile(name);
                if (file == nullptr)
                    return QByteArray();
                // createDevice() inflates on demand and hands the caller the device, where data()
                // inflates the whole entry into one QByteArray. A 81 MB term_meta_bank therefore
                // costs maxBytes rather than 81 MB.
                const std::unique_ptr<QIODevice> device(file->createDevice());
                if (device == nullptr)
                    return QByteArray();
                return device->read(maxBytes);
            },
    };
}

// The term_meta_bank bytes detectedTypes() reads before it stops looking for a mode it has not
// seen. A source declaring one mode alone never sets both flags, so an unbounded scan reads every
// bank it has; over an archive that means inflating each one on the thread AddDictionaryDialog
// calls detectTypes() from. 8 MiB is 8 term_meta_bank files at the 1 MiB Yomitan writes.
constexpr qsizetype metaScanBudget = qsizetype{8} * 1024 * 1024;

// A prefix of a JSON array of arrays, closed after its last complete element so that
// QJsonDocument can parse it. Returns an empty array for a prefix holding no complete element.
//
// A term_meta_bank is one array of three-element rows, and the mode detection reads element 1 of
// each row, so the rows a prefix carries whole answer the same question the whole bank does. The
// scan tracks the nesting depth and the string state, because a bracket inside a string and an
// escaped quotation mark are both legal in a gloss.
QByteArray completeRowsOf(QByteArrayView prefix)
{
    qsizetype depth = 0;
    qsizetype lastRowEnd = -1;
    bool inString = false;
    bool escaped = false;
    for (qsizetype i = 0; i < prefix.size(); ++i) {
        const char character = prefix.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (character == '\\')
                escaped = true;
            else if (character == '"')
                inString = false;
            continue;
        }
        switch (character) {
        case '"':
            inString = true;
            break;
        case '[':
        case '{':
            ++depth;
            break;
        case ']':
        case '}':
            --depth;
            // Depth 1 is the outer array, so a close that returns to it ended one row.
            if (depth == 1)
                lastRowEnd = i;
            break;
        default:
            break;
        }
    }
    if (lastRowEnd < 0)
        return {};
    return QByteArray(prefix.constData(), lastRowEnd + 1) + ']';
}

// The types banks can supply. A term bank, a kanji bank and a kanji_meta_bank are recognised by
// file name; a term_meta_bank is read, because the mode in element 1 of a row is what separates
// pitch data from frequency data. The scan stops at the first bank that has shown both modes, and
// at metaScanBudget otherwise.
//
// A bank longer than the remaining budget is read up to that bound and truncated to its last
// complete row, rather than skipped: a frequency dictionary is commonly published as one
// term_meta_bank_1.json of tens of megabytes, and skipping it left the source reporting no type
// at all, which AddDictionaryDialog refuses with "No Yomitan bank files were found".
//
// Reaching the budget reports the modes seen so far. The result selects which row of the
// "Treat as" combo the Add Dictionary dialog preselects, and every type stays selectable, so a
// dictionary whose second mode starts past the budget is registered by choosing that row.
QList<DictType> detectedTypes(const BankSource &banks)
{
    QList<DictType> types;
    if (!banks.names(QStringLiteral("term_bank_*.json")).isEmpty())
        types.append(DictType::YomitanWord);
    if (!banks.names(QStringLiteral("kanji_bank_*.json")).isEmpty())
        types.append(DictType::YomitanKanji);
    if (!banks.names(QStringLiteral("kanji_meta_bank_*.json")).isEmpty())
        types.append(DictType::YomitanKanjiFrequency);

    const QStringList metaBanks = sortedBankNames(banks.names(QStringLiteral("term_meta_bank_*.json")));
    bool hasPitch = false;
    bool hasFrequency = false;
    qsizetype scanned = 0;
    for (const QString &bank : metaBanks) {
        // Consulted before the read, because read() is what materialises the bank:
        // KArchiveFile::data() inflates the whole entry into RAM. A bank that fits in what is
        // left of the budget is read whole; a longer one is read through readPrefix(), which
        // decompresses only as far as the bound.
        const qint64 remaining = metaScanBudget - scanned;
        const qint64 bankSize = banks.size(bank);
        QByteArray bytes;
        QByteArray json;
        if (bankSize >= 0 && bankSize <= remaining) {
            bytes = banks.read(bank);
            json = bytes;
        } else {
            // A bank whose size does not fit in what is left of the budget, and a bank whose size
            // is unknown, which banks.size() reports as -1.
            bytes = banks.readPrefix(bank, remaining);
            json = completeRowsOf(bytes);
            if (json.isEmpty() && !bytes.isEmpty() && remaining == metaScanBudget) {
                // No row closed inside the whole budget, which is one row longer than 8 MiB. A
                // shorter remaining budget producing no row is the budget running out, and the
                // loop ends on that below.
                qCWarning(logDictImport) << "Bank" << bank << "declares" << bankSize
                                         << "bytes and closes no row in the first" << remaining;
            }
        }
        // The bytes read, not the bytes that parsed: the budget bounds the reading and the
        // inflating, and a prefix truncated to its last complete row is shorter than what
        // readPrefix() decompressed to produce it. Charging the truncated length would let a
        // source of banks that each end in one long row inflate a multiple of the budget.
        //
        // A bank that produced no bytes is a bank that could not be read: no entry under that
        // name, or a compression method KZip does not inflate. It costs nothing and the next bank
        // may still carry a mode, so the scan goes on rather than ending here.
        scanned += bytes.size();
        const QJsonArray rows = parseBank(json, bank);
        for (const auto &row : rows) {
            const QJsonArray fields = row.toArray();
            // import() drops a row shorter than 3 elements, so a shorter row must not report a
            // type the import would then produce no record for.
            if (fields.size() < 3)
                continue;
            const QString mode = fields.at(1).toString();
            if (mode == QLatin1String("pitch"))
                hasPitch = true;
            else if (mode == QLatin1String("freq"))
                hasFrequency = true;
        }
        if ((hasPitch && hasFrequency) || scanned >= metaScanBudget)
            break;
    }
    if (hasPitch)
        types.append(DictType::YomitanPitchAccent);
    if (hasFrequency)
        types.append(DictType::YomitanFrequency);
    return types;
}

} // namespace

YomitanImporter::YomitanImporter(DictType targetType)
    : m_targetType(targetType)
{}

YomitanImporter::~YomitanImporter() = default;

void YomitanImporter::setExtractDirectory(const QString &directory)
{
    m_extractDirectory = directory;
}

QString YomitanImporter::name() const
{
    return QStringLiteral("Yomitan");
}

YomitanIndex YomitanImporter::readIndex(const QString &source)
{
    const QFileInfo info(source);
    if (info.isDir()) {
        QFile file(bankDirectory(source) + QLatin1String("/index.json"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return parseIndex(file.readAll());
    }

    KZip zip(source);
    if (!zip.open(QIODevice::ReadOnly))
        return {};
    const KArchiveDirectory *directory = archiveBankDirectory(zip.directory());
    if (directory == nullptr)
        return {};
    const auto *indexFile = dynamic_cast<const KArchiveFile *>(directory->entry(QStringLiteral("index.json")));
    return indexFile != nullptr ? parseIndex(indexFile->data()) : YomitanIndex{};
}

QList<DictType> YomitanImporter::detectTypes(const QString &source)
{
    const QFileInfo info(source);
    if (info.isDir())
        return detectedTypes(directoryBanks(bankDirectory(source)));
    // The Add Dictionary dialog calls this for every path typed into its KUrlRequester, so a path
    // that names nothing is a partly typed one rather than an unreadable archive.
    if (!info.exists())
        return {};

    // Classify from the archive entry list and term_meta_bank rows without unpacking every
    // bank. Extraction happens once in resolveSource() when importing.
    KZip zip(source);
    if (!zip.open(QIODevice::ReadOnly)) {
        qCWarning(logDictImport) << "Cannot open the archive" << source << zip.errorString();
        return {};
    }
    const KArchiveDirectory *directory = archiveBankDirectory(zip.directory());
    if (directory == nullptr)
        return {};
    return detectedTypes(archiveBanks(directory, source));
}

QString YomitanImporter::resolveSource(const QString &source, QString *errorString)
{
    const QFileInfo info(source);
    if (info.isDir())
        return bankDirectory(source);

    if (!info.exists()) {
        *errorString = QStringLiteral("The dictionary source does not exist");
        return {};
    }

    QString target = m_extractDirectory;
    if (target.isEmpty()) {
        m_temporaryDirectory = std::make_unique<QTemporaryDir>();
        if (!m_temporaryDirectory->isValid()) {
            *errorString = QStringLiteral("Cannot create a directory to extract the archive into");
            return {};
        }
        target = m_temporaryDirectory->path();
    }

    if (!extractZip(source, target)) {
        *errorString = QStringLiteral("Cannot extract the dictionary archive");
        return {};
    }
    return bankDirectory(target);
}

ImportResult YomitanImporter::import(const QString &source,
                                     StoreWriter &writer,
                                     const ProgressFn &progress,
                                     std::atomic_bool &cancel)
{
    ImportResult result;

    m_resolvedPath = resolveSource(source, &result.errorString);
    if (m_resolvedPath.isEmpty()) {
        writer.abort();
        return result;
    }

    // Enumerated by the same glob and the same wrapper rule detectTypes() classified the source
    // through, so the banks the Add Dictionary dialog recognised are the banks read here. The
    // BankSource itself differs for a .zip: detectTypes() reads the archive, and this one reads
    // the tree resolveSource() extracted it into.
    const QDir directory(m_resolvedPath);
    const BankSource banks = directoryBanks(m_resolvedPath);
    QFile indexFile(directory.filePath(QStringLiteral("index.json")));
    if (indexFile.open(QIODevice::ReadOnly))
        m_index = parseIndex(indexFile.readAll());
    if (!m_index.valid)
        qCWarning(logDictImport) << "Yomitan dictionary" << m_resolvedPath << "has no readable index.json";

    // The tag bank turns a definition tag such as 名 into a category, an order and a note the
    // popup can render. Rows are [name, category, order, notes, score].
    QJsonObject tagObject;
    const QStringList tagBanks = sortedBankNames(banks.names(QStringLiteral("tag_bank_*.json")));
    for (const QString &bank : tagBanks) {
        const QJsonArray rows = parseBank(banks.read(bank), directory.filePath(bank));
        for (const auto &row : rows) {
            const QJsonArray fields = row.toArray();
            if (fields.size() < 5)
                continue;
            tagObject.insert(fields.at(0).toString(), fields);
        }
    }
    if (!tagObject.isEmpty())
        writer.setMeta(metakeys::tags, QString::fromUtf8(QJsonDocument(tagObject).toJson(QJsonDocument::Compact)));

    QString bankPattern;
    switch (m_targetType) {
    case DictType::YomitanKanji:
        bankPattern = QStringLiteral("kanji_bank_*.json");
        break;
    case DictType::YomitanKanjiFrequency:
        bankPattern = QStringLiteral("kanji_meta_bank_*.json");
        break;
    case DictType::YomitanPitchAccent:
    case DictType::YomitanFrequency:
        bankPattern = QStringLiteral("term_meta_bank_*.json");
        break;
    default:
        bankPattern = QStringLiteral("term_bank_*.json");
        break;
    }

    const QStringList bankNames = sortedBankNames(banks.names(bankPattern));
    if (bankNames.isEmpty()) {
        result.errorString = QStringLiteral("No %1 file was found in %2").arg(bankPattern, m_resolvedPath);
        writer.abort();
        return result;
    }

    const bool normalizedKeys = m_targetType != DictType::YomitanKanjiWordSchema &&
                                m_targetType != DictType::YomitanKanji &&
                                m_targetType != DictType::YomitanKanjiFrequency;
    const bool readingIsKey = m_targetType != DictType::YomitanName && normalizedKeys;

    // The frequency rows are collected in RAM and written at the end: a key that two rows reach
    // keeps the lower value, which JL's FreqUtils.AddOrUpdate does in the same way, and a store
    // cannot update a row it already wrote.
    QHash<QString, QHash<QString, int>> frequencies;
    int maxFrequency = 0;

    qint64 rowCount = 0;
    for (qsizetype bankIndex = 0; bankIndex < bankNames.size(); ++bankIndex) {
        if (cancel.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            writer.abort();
            return result;
        }

        const QString &bankName = bankNames.at(bankIndex);
        const QJsonArray rows = parseBank(banks.read(bankName), directory.filePath(bankName));
        for (const auto &rowValue : rows) {
            const QJsonArray row = rowValue.toArray();
            if (row.isEmpty())
                continue;
            ++rowCount;

            switch (m_targetType) {
            case DictType::YomitanKanji: {
                const QString character = row.at(0).toString();
                if (character.trimmed().isEmpty())
                    continue;
                YomitanKanjiRecord record;
                record.onReadings = splitTags(row.at(1).toString());
                record.kunReadings = splitTags(row.at(2).toString());
                record.tags = splitTags(row.at(3).toString());
                const QJsonArray meanings = row.at(4).toArray();
                for (const auto &meaning : meanings) {
                    const QString text = meaning.toString();
                    if (!text.trimmed().isEmpty())
                        record.definitions.append(text);
                }
                const QJsonObject stats = row.at(5).toObject();
                for (auto stat = stats.constBegin(); stat != stats.constEnd(); ++stat) {
                    record.stats.append(
                        stat.key() + QLatin1String(": ") +
                        (stat.value().isString() ? stat.value().toString() : QString::number(stat.value().toDouble())));
                }

                Record storeRecord;
                storeRecord.type = m_targetType;
                storeRecord.data = record;
                const std::vector<QString> keys{character};
                (void)writer.addRecord(storeRecord, keys);
                break;
            }

            case DictType::YomitanPitchAccent: {
                if (row.size() < 3 || row.at(1).toString() != QLatin1String("pitch"))
                    continue;
                const QJsonObject data = row.at(2).toObject();
                PitchAccentRecord record;
                record.spelling = row.at(0).toString();
                record.reading = data.value(QLatin1String("reading")).toString();
                record.positions = parsePitchPositions(data.value(QLatin1String("pitches")).toArray());
                if (record.positions.isEmpty())
                    continue;
                if (record.reading == record.spelling)
                    record.reading.clear();
                if (record.spelling.trimmed().isEmpty()) {
                    if (record.reading.trimmed().isEmpty())
                        continue;
                    record.spelling = record.reading;
                    record.reading.clear();
                }

                std::vector<QString> keys{normalizeKey(record.spelling)};
                if (!record.reading.isEmpty()) {
                    const QString readingKey = normalizeKey(record.reading);
                    if (readingKey != keys.front())
                        keys.push_back(readingKey);
                }

                Record storeRecord;
                storeRecord.type = m_targetType;
                storeRecord.data = record;
                (void)writer.addRecord(storeRecord, keys);
                break;
            }

            case DictType::YomitanFrequency:
            case DictType::YomitanKanjiFrequency: {
                if (row.size() < 3)
                    continue;
                if (m_targetType == DictType::YomitanFrequency && row.at(1).toString() != QLatin1String("freq"))
                    continue;

                const QString spelling = row.at(0).toString();
                if (spelling.isEmpty())
                    continue;
                FrequencyValue value = parseFrequency(row.at(2));
                if (value.frequency <= 0)
                    continue;
                maxFrequency = std::max(maxFrequency, value.frequency);
                if (value.reading == spelling)
                    value.reading.clear();

                const QString spellingKey = normalizedKeys ? normalizeKey(spelling) : spelling;
                if (value.reading.isEmpty()) {
                    int &stored = frequencies[spellingKey][spelling];
                    stored = stored == 0 ? value.frequency : std::min(stored, value.frequency);
                } else {
                    const QString readingKey = normalizeKey(value.reading);
                    int &byReading = frequencies[readingKey][spelling];
                    byReading = byReading == 0 ? value.frequency : std::min(byReading, value.frequency);
                    int &bySpelling = frequencies[spellingKey][value.reading];
                    bySpelling = bySpelling == 0 ? value.frequency : std::min(bySpelling, value.frequency);
                }
                break;
            }

            default: {
                // A term-bank row. Format 1 puts the glossary in the tail of the row; format 3
                // puts it in element 5 as an array.
                QString spelling = row.at(0).toString();
                QString reading = row.size() > 1 ? row.at(1).toString() : QString();
                if (reading.trimmed().isEmpty() || reading == spelling)
                    reading.clear();
                if (spelling.trimmed().isEmpty()) {
                    if (reading.isEmpty())
                        continue;
                    spelling = reading;
                    reading.clear();
                }
                if (isInvalidSpelling(spelling))
                    continue;

                const QList<QString> definitionTags =
                    row.size() > 2 ? splitTags(row.at(2).toString()) : QList<QString>{};
                // A row whose only tag is 子 or 句 is a child or phrase sub-entry of the row
                // before it, which duplicates content the parent already carries
                // (EpwingYomichanLoader.cs).
                if (definitionTags.size() == 1 && (definitionTags.first() == QString::fromUtf8("\xe5\xad\x90") ||
                                                   definitionTags.first() == QString::fromUtf8("\xe5\x8f\xa5"))) {
                    continue;
                }

                QJsonArray glossary;
                if (m_index.format <= 1 && row.size() > 5) {
                    for (qsizetype i = 5; i < row.size(); ++i)
                        glossary.append(row.at(i));
                } else if (row.size() > 5) {
                    glossary = row.at(5).toArray();
                } else if (row.size() == 3) {
                    // A three-element row is the oldest term-bank shape,
                    // [expression, reading, glossary].
                    glossary = row.at(2).toArray();
                }

                YomitanTermRecord record;
                record.primarySpelling = spelling;
                record.reading = reading;
                record.wordClasses = row.size() > 3 ? splitTags(row.at(3).toString()) : QList<QString>{};
                record.definitionTags = definitionTags;
                record.popularityScore = row.size() > 4 ? row.at(4).toDouble() : 0.0;
                record.sequence = row.size() > 6 ? static_cast<qint32>(row.at(6).toDouble()) : 0;
                record.termTags = row.size() > 7 ? splitTags(row.at(7).toString()) : QList<QString>{};

                for (const auto &element : glossary) {
                    StructuredContentResult rendered = renderStructuredContent(element, m_resolvedPath);
                    record.images += rendered.images;
                    // An element whose markup encloses no text, such as the empty <ul> surasura
                    // publishes for あへあへ, renders to rich text but to no plain text, and would
                    // otherwise reach the popup as a blank definition.
                    if (rendered.plainText.isEmpty())
                        continue;
                    if (record.definitionsPlain.contains(rendered.plainText))
                        continue;
                    record.definitions.append(rendered.richText);
                    record.definitionsPlain.append(rendered.plainText);
                }

                if (record.definitions.isEmpty() && record.images.isEmpty())
                    continue;

                std::vector<QString> keys{normalizedKeys ? normalizeKey(spelling) : spelling};
                if (readingIsKey && !reading.isEmpty()) {
                    const QString readingKey = normalizeKey(reading);
                    if (readingKey != keys.front())
                        keys.push_back(readingKey);
                }

                Record storeRecord;
                storeRecord.type = m_targetType;
                storeRecord.data = record;
                if (writer.addRecord(storeRecord, keys) < 0) {
                    result.errorString = QStringLiteral("Cannot write a record to the dictionary database");
                    writer.abort();
                    return result;
                }
                break;
            }
            }
        }

        if (progress) {
            const int percent = static_cast<int>((bankIndex + 1) * 100 / bankNames.size());
            progress(percent, QStringLiteral("Reading %1").arg(bankName));
        }
    }

    if (!frequencies.isEmpty()) {
        for (auto keyed = frequencies.constBegin(); keyed != frequencies.constEnd(); ++keyed) {
            for (auto entry = keyed.value().constBegin(); entry != keyed.value().constEnd(); ++entry) {
                FrequencyRecord record;
                record.spelling = entry.key();
                record.frequency = entry.value();

                Record storeRecord;
                storeRecord.type = m_targetType;
                storeRecord.data = record;
                const std::vector<QString> keys{keyed.key()};
                (void)writer.addRecord(storeRecord, keys);
            }
        }
        writer.setMeta(metakeys::maxFrequency, QString::number(maxFrequency));
    }

    writer.setMeta(metakeys::sourceFormat, QStringLiteral("Yomitan %1").arg(m_index.format));
    if (m_index.valid) {
        writer.setMeta(metakeys::title, m_index.title);
        writer.setMeta(metakeys::revision, m_index.revision);
        if (!m_index.indexUrl.isEmpty())
            writer.setMeta(metakeys::sourceUrl, m_index.indexUrl);
    }
    if (!writer.finish()) {
        result.errorString = QStringLiteral("Cannot finish writing the dictionary database");
        return result;
    }

    result.ok = true;
    result.recordCount = writer.recordCount();
    result.keyCount = writer.keyCount();
    result.maxKeyLength = writer.maxKeyLength();
    qCInfo(logDictImport,
           "Yomitan: %lld rows, %lld records, %lld keys from %s",
           static_cast<long long>(rowCount),
           static_cast<long long>(result.recordCount),
           static_cast<long long>(result.keyCount),
           qUtf8Printable(m_resolvedPath));
    return result;
}

} // namespace maru::dict
