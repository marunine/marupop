// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Yomitan importer over the hand-written v3 and v1 fixtures in tests/data/dict: term banks
// with structured content, kanji banks, the four term_meta_bank frequency shapes, pitch rows, the
// tag bank, and the two .zip shapes tests/support/yomitanfixture.h packs.
#include "dict/importers/structuredcontent.h"
#include "dict/importers/yomitanimporter.h"
#include "dict/keynorm.h"
#include "dict/records.h"
#include "dict/store.h"
#include "yomitanfixture.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>

using namespace maru::dict;
using maru::test::packWrappedYomitanFixture;
using maru::test::packYomitanFixture;

namespace
{

QString fixture(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

ImportResult runImport(YomitanImporter &importer, const QString &source, const QString &path, DictType type)
{
    StoreWriter writer;
    if (!writer.begin(path, type))
        return {};
    std::atomic_bool cancel{false};
    return importer.import(source, writer, {}, cancel);
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(contents) == contents.size();
}

// Copies the files directly inside sourceDirectory into a new directory at target, which is
// enough to make target a Yomitan source: the wrapper rule tests read index.json and the bank
// file names, not the img/ subdirectory.
bool copyDirectory(const QString &sourceDirectory, const QString &target)
{
    if (!QDir().mkpath(target))
        return false;
    const QDir source(sourceDirectory);
    const QStringList files = source.entryList(QDir::Files);
    return std::ranges::all_of(files, [&source, &target](const QString &name) {
        return QFile::copy(source.filePath(name), target + QLatin1Char('/') + name);
    });
}

// A term_meta_bank of "freq" rows holding at least minimumBytes bytes, for the detection budget.
QByteArray freqRowsOfAtLeast(qsizetype minimumBytes)
{
    QByteArray bank("[");
    while (bank.size() < minimumBytes)
        bank += R"(["あ","freq",1],)";
    bank.chop(1);
    bank += "]";
    return bank;
}

} // namespace

TEST(DictYomitanImporter, ReadsTheIndex)
{
    const YomitanIndex index = YomitanImporter::readIndex(fixture(QStringLiteral("yomitan_v3")));
    ASSERT_TRUE(index.valid);
    EXPECT_EQ(index.title, QStringLiteral("Marupop Test Dictionary"));
    EXPECT_EQ(index.revision, QStringLiteral("test-1"));
    EXPECT_EQ(index.format, 3);
    EXPECT_TRUE(index.isUpdatable);
    EXPECT_EQ(index.indexUrl, QStringLiteral("https://example.invalid/index.json"));

    const YomitanIndex v1 = YomitanImporter::readIndex(fixture(QStringLiteral("yomitan_v1")));
    ASSERT_TRUE(v1.valid);
    EXPECT_EQ(v1.format, 1);
    EXPECT_FALSE(v1.isUpdatable);
}

TEST(DictYomitanImporter, DetectsTheTypesADirectoryCanSupply)
{
    const QList<DictType> types = YomitanImporter::detectTypes(fixture(QStringLiteral("yomitan_v3")));
    EXPECT_TRUE(types.contains(DictType::YomitanWord));
    EXPECT_TRUE(types.contains(DictType::YomitanKanji));
    EXPECT_TRUE(types.contains(DictType::YomitanPitchAccent));
    EXPECT_TRUE(types.contains(DictType::YomitanFrequency));
    EXPECT_TRUE(types.contains(DictType::YomitanKanjiFrequency));

    const QList<DictType> v1Types = YomitanImporter::detectTypes(fixture(QStringLiteral("yomitan_v1")));
    EXPECT_EQ(v1Types, QList<DictType>{DictType::YomitanWord});
}

TEST(DictYomitanImporter, DetectsTheTypesAZipCanSupply)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QList<DictType> expected = YomitanImporter::detectTypes(fixture(QStringLiteral("yomitan_v3")));
    ASSERT_TRUE(expected.contains(DictType::YomitanWord));

    // A .zip reports the same types as the directory it holds, so the Add Dictionary dialog
    // accepts the archive a dictionary is published as.
    const QString flat = directory.filePath(QStringLiteral("flat.zip"));
    ASSERT_TRUE(packYomitanFixture(fixture(QStringLiteral("yomitan_v3")), flat));
    EXPECT_EQ(YomitanImporter::detectTypes(flat), expected);

    const QString wrapped = directory.filePath(QStringLiteral("wrapped.zip"));
    ASSERT_TRUE(
        packWrappedYomitanFixture(fixture(QStringLiteral("yomitan_v3")), wrapped, QStringLiteral("dictionary")));
    EXPECT_EQ(YomitanImporter::detectTypes(wrapped), expected);
    EXPECT_EQ(YomitanImporter::readIndex(wrapped).title, QStringLiteral("Marupop Test Dictionary"));
}

// A second top-level directory is what the macOS Finder writes as __MACOSX. Detection over the
// archive and the import that extracts it have to agree about which directory holds the banks,
// or the Add Dictionary dialog enables OK for a source whose import then reports
// "No term_bank_*.json file was found".
TEST(DictYomitanImporter, ImportsAWrappedZipHoldingASecondTopLevelDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString archive = directory.filePath(QStringLiteral("finder.zip"));
    ASSERT_TRUE(packWrappedYomitanFixture(
        fixture(QStringLiteral("yomitan_v3")), archive, QStringLiteral("dictionary"), {QStringLiteral("__MACOSX")}));

    EXPECT_TRUE(YomitanImporter::detectTypes(archive).contains(DictType::YomitanWord));
    EXPECT_EQ(YomitanImporter::readIndex(archive).title, QStringLiteral("Marupop Test Dictionary"));

    const QString extractDirectory = directory.filePath(QStringLiteral("extracted"));
    YomitanImporter importer(DictType::YomitanWord);
    importer.setExtractDirectory(extractDirectory);
    const ImportResult result =
        runImport(importer, archive, directory.filePath(QStringLiteral("finder.db")), DictType::YomitanWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 3);
    EXPECT_EQ(importer.resolvedPath(), extractDirectory + QStringLiteral("/dictionary"));

    // addLocalDirectory() recurses, so the wrapped archive is the one shape that carries the
    // fixture's img/, and the image a record references resolves under the extracted path.
    Store store;
    ASSERT_EQ(store.open(directory.filePath(QStringLiteral("finder.db"))), OpenResult::Ok);
    const auto hashiru = store.find(normalizeKey(QStringLiteral("走る")));
    ASSERT_EQ(hashiru.size(), 1U);
    const YomitanTermRecord *record = asYomitanTerm(*hashiru.front());
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->images.size(), 1);
    EXPECT_TRUE(QFile::exists(importer.resolvedPath() + QLatin1Char('/') + record->images.first().path));
}

// The wrapper rule on disk, which readIndex() and detectTypes() apply to a directory the user
// picked and resolveSource() applies to the tree it extracted an archive into.
TEST(DictYomitanImporter, ResolvesTheWrappingDirectoryOfAFolder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString fixtureDirectory = fixture(QStringLiteral("yomitan_v3"));

    // One dictionary beside a directory holding no index.json, which is the __MACOSX the macOS
    // Finder writes: the dictionary is resolved.
    const QString wrapped = directory.filePath(QStringLiteral("wrapped"));
    ASSERT_TRUE(QDir().mkpath(wrapped + QStringLiteral("/__MACOSX")));
    ASSERT_TRUE(copyDirectory(fixtureDirectory, wrapped + QStringLiteral("/dictionary")));
    EXPECT_EQ(YomitanImporter::readIndex(wrapped).title, QStringLiteral("Marupop Test Dictionary"));
    EXPECT_TRUE(YomitanImporter::detectTypes(wrapped).contains(DictType::YomitanWord));

    // A hidden wrapper is resolved too. QDir::entryList() matches a dot-directory only with
    // QDir::Hidden set, while KArchiveDirectory::entries() lists one unconditionally, so leaving
    // it out is what would make detection over an archive and the import after it disagree.
    const QString hidden = directory.filePath(QStringLiteral("hidden"));
    ASSERT_TRUE(QDir().mkpath(hidden));
    ASSERT_TRUE(copyDirectory(fixtureDirectory, hidden + QStringLiteral("/.dictionary")));
    EXPECT_EQ(YomitanImporter::readIndex(hidden).title, QStringLiteral("Marupop Test Dictionary"));
    EXPECT_TRUE(YomitanImporter::detectTypes(hidden).contains(DictType::YomitanWord));

    // Two dictionaries under one parent are ambiguous. Reporting no type is what makes the Add
    // Dictionary dialog ask for a narrower path, rather than importing whichever sorts first and
    // recording the parent as the source of it.
    const QString both = directory.filePath(QStringLiteral("both"));
    ASSERT_TRUE(QDir().mkpath(both));
    ASSERT_TRUE(copyDirectory(fixtureDirectory, both + QStringLiteral("/first")));
    ASSERT_TRUE(copyDirectory(fixtureDirectory, both + QStringLiteral("/second")));
    EXPECT_FALSE(YomitanImporter::readIndex(both).valid);
    EXPECT_TRUE(YomitanImporter::detectTypes(both).isEmpty());
}

TEST(DictYomitanImporter, ImportsAZipWrappedInAHiddenDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString archive = directory.filePath(QStringLiteral("hidden.zip"));
    ASSERT_TRUE(packWrappedYomitanFixture(fixture(QStringLiteral("yomitan_v3")), archive, QStringLiteral(".jitendex")));

    ASSERT_TRUE(YomitanImporter::detectTypes(archive).contains(DictType::YomitanWord));

    const QString extractDirectory = directory.filePath(QStringLiteral("extracted"));
    YomitanImporter importer(DictType::YomitanWord);
    importer.setExtractDirectory(extractDirectory);
    const ImportResult result =
        runImport(importer, archive, directory.filePath(QStringLiteral("hidden.db")), DictType::YomitanWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(importer.resolvedPath(), extractDirectory + QStringLiteral("/.jitendex"));
}

// detectedTypes() stops after metaScanBudget = 8 MiB of term_meta_bank data, so a source whose
// second mode starts past that reports the first mode alone. Every type stays selectable in the
// "Treat as" combo, so the second one is still reachable by hand.
TEST(DictYomitanImporter, StopsTheModeScanAtTheDetectionBudget)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("wide"));
    ASSERT_TRUE(QDir().mkpath(source));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/index.json"),
                          QByteArrayLiteral(R"({"title":"Wide","revision":"1","format":3})")));

    // Two banks of 4 MiB each: neither is skipped for its own size, and the pair reaches the
    // budget, so the scan ends before the third bank and its pitch rows.
    const QByteArray freqBank = freqRowsOfAtLeast(qsizetype{4} * 1024 * 1024);
    ASSERT_LT(freqBank.size(), qsizetype{8} * 1024 * 1024);
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_1.json"), freqBank));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_2.json"), freqBank));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_3.json"),
                          QByteArrayLiteral(R"([["橋","pitch",{"reading":"はし","pitches":[]}]])")));

    const QList<DictType> types = YomitanImporter::detectTypes(source);
    EXPECT_TRUE(types.contains(DictType::YomitanFrequency));
    EXPECT_FALSE(types.contains(DictType::YomitanPitchAccent));

    // A source whose banks stay under the budget reports both modes, which is what pins the
    // budget as the cause above rather than the bank count.
    const QString narrow = directory.filePath(QStringLiteral("narrow"));
    ASSERT_TRUE(QDir().mkpath(narrow));
    ASSERT_TRUE(writeFile(narrow + QStringLiteral("/index.json"),
                          QByteArrayLiteral(R"({"title":"Narrow","revision":"1","format":3})")));
    ASSERT_TRUE(writeFile(narrow + QStringLiteral("/term_meta_bank_1.json"), freqRowsOfAtLeast(1024)));
    ASSERT_TRUE(writeFile(narrow + QStringLiteral("/term_meta_bank_2.json"),
                          QByteArrayLiteral(R"([["橋","pitch",{"reading":"はし","pitches":[]}]])")));
    const QList<DictType> both = YomitanImporter::detectTypes(narrow);
    EXPECT_TRUE(both.contains(DictType::YomitanFrequency));
    EXPECT_TRUE(both.contains(DictType::YomitanPitchAccent));
}

// One bank longer than the whole budget is left unread. KArchiveFile::data() inflates an entry
// whole, so consulting the declared length first is what keeps a single oversized bank from being
// materialised in RAM.
// A frequency dictionary is commonly published as one term_meta_bank_1.json of tens of megabytes:
// JPDB v2.2 ships 31,488,509 bytes, jiten_freq_global 55,062,662, Freq_CC100 11,671,168 and
// BCCWJ_SUW_LUW 81,306,805. Skipping such a bank reported no type at all
// and the Add Dictionary dialog refused the source.
//
// The pitch row past the budget is what pins the bound: the prefix carries the freq rows and stops
// before the last row, so exactly one of the two modes is reported.
TEST(DictYomitanImporter, ReadsAPrefixOfABankLongerThanTheBudget)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("oversized"));
    ASSERT_TRUE(QDir().mkpath(source));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/index.json"),
                          QByteArrayLiteral(R"({"title":"Oversized","revision":"1","format":3})")));

    QByteArray bank = freqRowsOfAtLeast(qsizetype{8} * 1024 * 1024 + 1);
    bank.chop(1);
    bank += R"(,["橋","pitch",{"reading":"はし","pitches":[]}]])";
    ASSERT_GT(bank.size(), qsizetype{8} * 1024 * 1024);
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_1.json"), bank));

    const QList<DictType> types = YomitanImporter::detectTypes(source);
    EXPECT_TRUE(types.contains(DictType::YomitanFrequency));
    EXPECT_FALSE(types.contains(DictType::YomitanPitchAccent));
}

// The same over an archive, which is the shape all four dictionaries above are published in.
// KArchiveFile::data() would inflate the whole entry, so the prefix is read through
// createDevice() instead.
TEST(DictYomitanImporter, ReadsAPrefixOfAnOversizedBankInsideAnArchive)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("oversized"));
    ASSERT_TRUE(QDir().mkpath(source));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/index.json"),
                          QByteArrayLiteral(R"({"title":"Oversized","revision":"1","format":3})")));

    QByteArray bank = freqRowsOfAtLeast(qsizetype{8} * 1024 * 1024 + 1);
    bank.chop(1);
    bank += R"(,["橋","pitch",{"reading":"はし","pitches":[]}]])";
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_1.json"), bank));

    const QString archive = directory.filePath(QStringLiteral("oversized.zip"));
    ASSERT_TRUE(packYomitanFixture(source, archive));

    const QList<DictType> types = YomitanImporter::detectTypes(archive);
    EXPECT_TRUE(types.contains(DictType::YomitanFrequency));
    EXPECT_FALSE(types.contains(DictType::YomitanPitchAccent));
}

// The budget is charged the bytes read, not the bytes that parsed. A bank holding one row longer
// than the whole budget closes no row, so its truncated prefix is empty while its read cost the
// budget; charging the truncated length would leave the budget unspent and let every later bank
// inflate another 8 MiB.
TEST(DictYomitanImporter, ChargesTheBudgetTheBytesReadRatherThanTheBytesParsed)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const auto build = [](const QString &source, qsizetype headwordBytes) {
        EXPECT_TRUE(QDir().mkpath(source));
        EXPECT_TRUE(writeFile(source + QStringLiteral("/index.json"),
                              QByteArrayLiteral(R"({"title":"Long","revision":"1","format":3})")));
        // One row, whose headword is headwordBytes of あ, so no row closes inside the prefix that
        // the budget allows.
        QByteArray bank(R"([[")");
        bank += QByteArray("\xe3\x81\x82").repeated(static_cast<int>(headwordBytes / 3));
        bank += R"(","freq",1]])";
        EXPECT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_1.json"), bank));
        EXPECT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_2.json"),
                              QByteArrayLiteral(R"([["橋","pitch",{"reading":"はし","pitches":[]}]])")));
    };

    // The first bank spends the whole budget, so the pitch row of the second is never read.
    const QString spent = directory.filePath(QStringLiteral("spent"));
    build(spent, qsizetype{9} * 1024 * 1024);
    EXPECT_FALSE(YomitanImporter::detectTypes(spent).contains(DictType::YomitanPitchAccent));

    // The same shape under the budget reaches the second bank, which pins the budget as the cause
    // above rather than the unclosed row.
    const QString cheap = directory.filePath(QStringLiteral("cheap"));
    build(cheap, 1024);
    EXPECT_TRUE(YomitanImporter::detectTypes(cheap).contains(DictType::YomitanPitchAccent));
}

// A prefix that ends inside a row is truncated to the last row that closed. Without the
// truncation the prefix is not JSON, QJsonDocument reports an error and the source reports no
// type, which is the failure this replaces.
TEST(DictYomitanImporter, TruncatesAPrefixThatEndsInsideARow)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("split"));
    ASSERT_TRUE(QDir().mkpath(source));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/index.json"),
                          QByteArrayLiteral(R"({"title":"Split","revision":"1","format":3})")));

    // A gloss holding a bracket and an escaped quotation mark, so the scan that finds the row
    // boundary has to track the string state rather than count brackets alone.
    QByteArray bank("[");
    while (bank.size() < qsizetype{8} * 1024 * 1024)
        bank += R"(["あ","freq",{"value":1,"displayValue":"1 [\"]"}],)";
    bank += R"(["い","freq",1]])";
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_meta_bank_1.json"), bank));

    EXPECT_TRUE(YomitanImporter::detectTypes(source).contains(DictType::YomitanFrequency));
}

// import() drops a term_meta row shorter than 3 elements, so detectTypes() must not offer a type
// such a row is the only evidence for.
TEST(DictYomitanImporter, IgnoresATruncatedTermMetaRowWhenDetecting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("truncated"));
    ASSERT_TRUE(QDir().mkpath(source));

    QFile index(source + QStringLiteral("/index.json"));
    ASSERT_TRUE(index.open(QIODevice::WriteOnly));
    index.write(R"({"title":"Truncated","revision":"1","format":3})");
    index.close();

    QFile metaBank(source + QStringLiteral("/term_meta_bank_1.json"));
    ASSERT_TRUE(metaBank.open(QIODevice::WriteOnly));
    metaBank.write(R"([["x","freq"],["y","pitch"]])");
    metaBank.close();

    EXPECT_TRUE(YomitanImporter::detectTypes(source).isEmpty());
}

TEST(DictYomitanImporter, DetectsNoTypeInAFileThatIsNotAnArchive)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("notes.txt"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not a zip");
    file.close();

    EXPECT_TRUE(YomitanImporter::detectTypes(path).isEmpty());
    EXPECT_FALSE(YomitanImporter::readIndex(path).valid);
}

TEST(DictYomitanImporter, ImportsTermBanksWithStructuredContent)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("terms.db"));

    YomitanImporter importer(DictType::YomitanWord);
    const ImportResult result = runImport(importer, fixture(QStringLiteral("yomitan_v3")), path, DictType::YomitanWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    // The 子 sub-entry and the U+FFFD spelling are dropped, so three of the five rows survive.
    EXPECT_EQ(result.recordCount, 3);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);

    const auto hashiru = store.find(normalizeKey(QStringLiteral("走る")));
    ASSERT_EQ(hashiru.size(), 1U);
    const YomitanTermRecord *record = asYomitanTerm(*hashiru.front());
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->primarySpelling, QStringLiteral("走る"));
    EXPECT_EQ(record->reading, QStringLiteral("はしる"));
    EXPECT_DOUBLE_EQ(record->popularityScore, 42.0);
    EXPECT_EQ(record->wordClasses, QList<QString>{QStringLiteral("v5r")});
    EXPECT_EQ(record->definitionTags, (QList<QString>{QStringLiteral("v5r"), QStringLiteral("vs")}));
    EXPECT_EQ(record->termTags, QList<QString>{QStringLiteral("common")});
    EXPECT_EQ(record->sequence, 1);
    ASSERT_EQ(record->definitions.size(), 2);
    EXPECT_EQ(record->definitionsPlain.first(), QStringLiteral("to run"));

    // The structured element renders to the Qt rich-text subset, keeping the list, the table and
    // the ruby reading.
    const QString rich = record->definitions.at(1);
    EXPECT_TRUE(rich.contains(QStringLiteral("<ul>")));
    EXPECT_TRUE(rich.contains(QStringLiteral("<li")));
    EXPECT_TRUE(rich.contains(QStringLiteral("<table>")));
    EXPECT_TRUE(rich.contains(QStringLiteral("font-weight:bold")));
    EXPECT_TRUE(rich.contains(QStringLiteral("（かんじ）")));
    EXPECT_TRUE(rich.contains(QStringLiteral("［例］")));

    // The 4-pixel spacer image is dropped and the real image is recorded.
    ASSERT_EQ(record->images.size(), 1);
    EXPECT_EQ(record->images.first().path, QStringLiteral("img/real.png"));
    EXPECT_EQ(record->images.first().pixelWidth, 120);

    // The reading is a key of its own.
    EXPECT_EQ(store.find(normalizeKey(QStringLiteral("はしる"))).size(), 1U);

    // A row whose expression is blank promotes its reading to the primary spelling.
    const auto promoted = store.find(normalizeKey(QStringLiteral("かなだけ")));
    ASSERT_EQ(promoted.size(), 1U);
    EXPECT_EQ(asYomitanTerm(*promoted.front())->primarySpelling, QStringLiteral("かなだけ"));

    // The tag bank reaches the meta table.
    const QJsonObject tags = QJsonDocument::fromJson(store.meta(metakeys::tags).toUtf8()).object();
    ASSERT_TRUE(tags.contains(QStringLiteral("v5r")));
    EXPECT_EQ(tags.value(QStringLiteral("v5r")).toArray().at(1).toString(), QStringLiteral("partOfSpeech"));

    EXPECT_EQ(store.meta(metakeys::title), QStringLiteral("Marupop Test Dictionary"));
    EXPECT_EQ(store.meta(metakeys::revision), QStringLiteral("test-1"));
}

TEST(DictYomitanImporter, ImportsAFormatOneTermBank)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("v1.db"));

    YomitanImporter importer(DictType::YomitanWord);
    const ImportResult result = runImport(importer, fixture(QStringLiteral("yomitan_v1")), path, DictType::YomitanWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 2);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto records = store.find(normalizeKey(QStringLiteral("歩く")));
    ASSERT_EQ(records.size(), 1U);
    const YomitanTermRecord *record = asYomitanTerm(*records.front());
    ASSERT_NE(record, nullptr);
    // The glossary occupies the tail of a format 1 row, so both strings become definitions.
    EXPECT_EQ(record->definitionsPlain, (QList<QString>{QStringLiteral("to walk"), QStringLiteral("to go on foot")}));
    EXPECT_EQ(record->wordClasses, QList<QString>{QStringLiteral("v5k")});
}

TEST(DictYomitanImporter, NameDictionariesDoNotKeyTheReading)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("names.db"));

    YomitanImporter importer(DictType::YomitanName);
    const ImportResult result = runImport(importer, fixture(QStringLiteral("yomitan_v3")), path, DictType::YomitanName);
    ASSERT_TRUE(result.ok);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(normalizeKey(QStringLiteral("走る"))).size(), 1U);
    EXPECT_TRUE(store.find(normalizeKey(QStringLiteral("はしる"))).empty());
}

TEST(DictYomitanImporter, ImportsKanjiBanks)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("kanji.db"));

    YomitanImporter importer(DictType::YomitanKanji);
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("yomitan_v3")), path, DictType::YomitanKanji);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 2);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto records = store.find(QStringLiteral("日"));
    ASSERT_EQ(records.size(), 1U);
    const YomitanKanjiRecord *record = asYomitanKanji(*records.front());
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->onReadings, (QList<QString>{QStringLiteral("ニチ"), QStringLiteral("ジツ")}));
    EXPECT_EQ(record->kunReadings.size(), 3);
    EXPECT_EQ(record->tags, QList<QString>{QStringLiteral("jouyou")});
    EXPECT_EQ(record->definitions, (QList<QString>{QStringLiteral("day"), QStringLiteral("sun")}));
    EXPECT_EQ(record->stats.size(), 2);
}

TEST(DictYomitanImporter, ImportsEveryFrequencyShape)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("freq.db"));

    YomitanImporter importer(DictType::YomitanFrequency);
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("yomitan_v3")), path, DictType::YomitanFrequency);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);

    // A bare number.
    const auto number = store.find(normalizeKey(QStringLiteral("走る")));
    ASSERT_EQ(number.size(), 1U);
    EXPECT_EQ(asFrequency(*number.front())->frequency, 1234);

    // A string carrying the JPDB ㋕ marker.
    const auto text = store.find(normalizeKey(QStringLiteral("テスト")));
    ASSERT_EQ(text.size(), 1U);
    EXPECT_EQ(asFrequency(*text.front())->frequency, 5678);

    // An object whose value is 0, which falls back to displayValue.
    const auto display = store.find(normalizeKey(QStringLiteral("語")));
    ASSERT_EQ(display.size(), 1U);
    EXPECT_EQ(asFrequency(*display.front())->frequency, 9012);

    // An object carrying a reading, which is keyed both ways: the reading key holds the spelling
    // and the spelling key holds the reading.
    const auto byReading = store.find(normalizeKey(QStringLiteral("ひ")));
    ASSERT_EQ(byReading.size(), 1U);
    EXPECT_EQ(asFrequency(*byReading.front())->spelling, QStringLiteral("日"));
    EXPECT_EQ(asFrequency(*byReading.front())->frequency, 7);

    const auto bySpelling = store.find(normalizeKey(QStringLiteral("日")));
    ASSERT_EQ(bySpelling.size(), 1U);
    EXPECT_EQ(asFrequency(*bySpelling.front())->spelling, QStringLiteral("ひ"));

    EXPECT_EQ(store.meta(metakeys::maxFrequency).toInt(), 9012);
}

TEST(DictYomitanImporter, ImportsKanjiFrequencyBanks)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("kanjifreq.db"));

    YomitanImporter importer(DictType::YomitanKanjiFrequency);
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("yomitan_v3")), path, DictType::YomitanKanjiFrequency);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto records = store.find(QStringLiteral("語"));
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(asFrequency(*records.front())->frequency, 301);
}

TEST(DictYomitanImporter, ImportsPitchRows)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("pitch.db"));

    YomitanImporter importer(DictType::YomitanPitchAccent);
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("yomitan_v3")), path, DictType::YomitanPitchAccent);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 3);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);

    const auto hashiru = store.find(normalizeKey(QStringLiteral("走る")));
    ASSERT_EQ(hashiru.size(), 1U);
    const PitchAccentRecord *record = asPitchAccent(*hashiru.front());
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->positions, QList<quint8>{2});

    // Every position of a multi-accent row is kept, unlike JL which keeps only the first.
    const auto hashi = store.find(normalizeKey(QStringLiteral("橋")));
    ASSERT_EQ(hashi.size(), 1U);
    EXPECT_EQ(asPitchAccent(*hashi.front())->positions, (QList<quint8>{2, 0}));

    // A non-standard string position is the index of the first L after the first H.
    const auto edge = store.find(normalizeKey(QStringLiteral("端")));
    ASSERT_EQ(edge.size(), 1U);
    EXPECT_EQ(asPitchAccent(*edge.front())->positions, QList<quint8>{0});

    // The reading is a key too, and reaches both 橋 and 端.
    EXPECT_EQ(store.find(normalizeKey(QStringLiteral("はし"))).size(), 2U);
}

TEST(DictYomitanImporter, ImportsFromAZipArchive)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString archive = directory.filePath(QStringLiteral("dictionary.zip"));
    ASSERT_TRUE(packYomitanFixture(fixture(QStringLiteral("yomitan_v3")), archive));

    const YomitanIndex index = YomitanImporter::readIndex(archive);
    ASSERT_TRUE(index.valid);
    EXPECT_EQ(index.title, QStringLiteral("Marupop Test Dictionary"));

    const QString extractDirectory = directory.filePath(QStringLiteral("extracted"));
    YomitanImporter importer(DictType::YomitanWord);
    importer.setExtractDirectory(extractDirectory);
    const ImportResult result =
        runImport(importer, archive, directory.filePath(QStringLiteral("zip.db")), DictType::YomitanWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 3);
    EXPECT_EQ(importer.resolvedPath(), extractDirectory);
    EXPECT_TRUE(QFile::exists(extractDirectory + QStringLiteral("/term_bank_1.json")));
}

TEST(DictYomitanImporter, ReportsAnErrorWhenNoBankIsPresent)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString empty = directory.filePath(QStringLiteral("empty"));
    ASSERT_TRUE(QDir().mkpath(empty));

    YomitanImporter importer(DictType::YomitanWord);
    const ImportResult result =
        runImport(importer, empty, directory.filePath(QStringLiteral("none.db")), DictType::YomitanWord);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.errorString.contains(QStringLiteral("term_bank_*.json")));
}

TEST(DictStructuredContent, RendersAStringUnchanged)
{
    const StructuredContentResult result = renderStructuredContent(QJsonValue(QStringLiteral("a & b")), QString());
    EXPECT_EQ(result.plainText, QStringLiteral("a & b"));
    EXPECT_EQ(result.richText, QStringLiteral("a &amp; b"));
    EXPECT_TRUE(result.images.isEmpty());
}

TEST(DictStructuredContent, SkipsADeinflectionArray)
{
    const QJsonArray deinflection{QStringLiteral("走る"), QStringLiteral("v5r")};
    const StructuredContentResult result = renderStructuredContent(deinflection, QString());
    EXPECT_TRUE(result.richText.isEmpty());
    EXPECT_TRUE(result.plainText.isEmpty());
}

TEST(DictStructuredContent, CollapsesAnUnstyledSpan)
{
    QJsonObject node;
    node.insert(QStringLiteral("tag"), QStringLiteral("span"));
    node.insert(QStringLiteral("content"), QStringLiteral("plain"));
    const StructuredContentResult result = renderStructuredContent(node, QString());
    EXPECT_EQ(result.richText, QStringLiteral("plain"));
}

TEST(DictStructuredContent, MarksABorderQtCannotDraw)
{
    QJsonObject style;
    style.insert(QStringLiteral("borderStyle"), QStringLiteral("solid"));
    QJsonObject node;
    node.insert(QStringLiteral("tag"), QStringLiteral("span"));
    node.insert(QStringLiteral("style"), style);
    node.insert(QStringLiteral("content"), QStringLiteral("boxed"));

    const StructuredContentResult result = renderStructuredContent(node, QString());
    EXPECT_TRUE(result.plainText.contains(QStringLiteral("[boxed]")));
}

// An unordered list nested in an ordered one renders bullets and leaves the outer counter alone:
// without the sentinel push, the ul items continued the ol numbering and the item after the ul
// carried the number the nested items had reached.
TEST(DictStructuredContent, BulletsANestedUnorderedListInsideAnOrderedOne)
{
    const auto item = [](const QJsonValue &content) {
        QJsonObject node;
        node.insert(QStringLiteral("tag"), QStringLiteral("li"));
        node.insert(QStringLiteral("content"), content);
        return node;
    };

    QJsonObject nested;
    nested.insert(QStringLiteral("tag"), QStringLiteral("ul"));
    nested.insert(QStringLiteral("content"),
                  QJsonArray{item(QJsonValue(QStringLiteral("b"))), item(QJsonValue(QStringLiteral("c")))});

    QJsonObject outer;
    outer.insert(QStringLiteral("tag"), QStringLiteral("ol"));
    outer.insert(
        QStringLiteral("content"),
        QJsonArray{item(QJsonArray{QJsonValue(QStringLiteral("a")), nested}), item(QJsonValue(QStringLiteral("d")))});

    const StructuredContentResult result = renderStructuredContent(outer, QString());
    EXPECT_TRUE(result.plainText.contains(QStringLiteral("1. a"))) << result.plainText.toStdString();
    EXPECT_TRUE(result.plainText.contains(QString::fromUtf8("\u2022 b"))) << result.plainText.toStdString();
    EXPECT_TRUE(result.plainText.contains(QString::fromUtf8("\u2022 c"))) << result.plainText.toStdString();
    EXPECT_TRUE(result.plainText.contains(QStringLiteral("2. d"))) << result.plainText.toStdString();
    EXPECT_FALSE(result.plainText.contains(QStringLiteral("2. b")));
    EXPECT_FALSE(result.plainText.contains(QStringLiteral("4. d")));
}

TEST(DictStructuredContent, NumbersOrderedListItems)
{
    QJsonArray items;
    for (const QString &text : {QStringLiteral("one"), QStringLiteral("two")}) {
        QJsonObject item;
        item.insert(QStringLiteral("tag"), QStringLiteral("li"));
        item.insert(QStringLiteral("content"), text);
        items.append(item);
    }
    QJsonObject list;
    list.insert(QStringLiteral("tag"), QStringLiteral("ol"));
    list.insert(QStringLiteral("content"), items);

    const StructuredContentResult result = renderStructuredContent(list, QString());
    EXPECT_TRUE(result.plainText.contains(QStringLiteral("1. one")));
    EXPECT_TRUE(result.plainText.contains(QStringLiteral("2. two")));
}

// The header cell shape 四字熟語辞典オンライン publishes, which spans the rows of its group. A span of
// 1 is the default, and the cell carrying one is written bare.
TEST(DictStructuredContent, KeepsTheSpanOfATableCell)
{
    QJsonObject header;
    header.insert(QStringLiteral("tag"), QStringLiteral("th"));
    header.insert(QStringLiteral("rowSpan"), 4);
    header.insert(QStringLiteral("content"), QStringLiteral("類義語"));
    QJsonObject cell;
    cell.insert(QStringLiteral("tag"), QStringLiteral("td"));
    cell.insert(QStringLiteral("colSpan"), 2);
    cell.insert(QStringLiteral("content"), QStringLiteral("wide"));
    QJsonObject single;
    single.insert(QStringLiteral("tag"), QStringLiteral("td"));
    single.insert(QStringLiteral("rowSpan"), 1);
    single.insert(QStringLiteral("content"), QStringLiteral("one"));
    QJsonObject row;
    row.insert(QStringLiteral("tag"), QStringLiteral("tr"));
    row.insert(QStringLiteral("content"), QJsonArray{header, cell, single});
    QJsonObject table;
    table.insert(QStringLiteral("tag"), QStringLiteral("table"));
    table.insert(QStringLiteral("content"), row);

    const QString rich = renderStructuredContent(table, QString()).richText;
    EXPECT_TRUE(rich.contains(QStringLiteral("<th rowspan=\"4\">"))) << rich.toStdString();
    EXPECT_TRUE(rich.contains(QStringLiteral("<td colspan=\"2\">"))) << rich.toStdString();
    EXPECT_TRUE(rich.contains(QStringLiteral("<td>one"))) << rich.toStdString();
}

// surasura's あへあへ row holds one glossary element, an empty <ul>. It renders to markup and to no
// text, and a row whose only definition is that markup was imported as an entry with a blank card.
TEST(DictYomitanImporter, DropsAGlossaryElementThatEnclosesNoText)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));
    ASSERT_TRUE(QDir().mkpath(source));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/index.json"), R"({"title":"Empty","revision":"1","format":3})"));
    ASSERT_TRUE(writeFile(source + QStringLiteral("/term_bank_1.json"),
                          QStringLiteral(R"([
["あへあへ","あへあへ","","",0,[{"type":"structured-content","content":[{"tag":"ul","data":{"content":"glossaryShortDefinition"},"style":{"listStyleType":"circle"},"content":[]}]}],0,""],
["すらすら","すらすら","","",0,[{"type":"structured-content","content":[{"tag":"ul","content":[]}]},"smoothly"],0,""]
])")
                              .toUtf8()));

    const QString path = directory.filePath(QStringLiteral("empty.db"));
    YomitanImporter importer(DictType::YomitanWord);
    const ImportResult result = runImport(importer, source, path, DictType::YomitanWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 1);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_TRUE(store.find(normalizeKey(QStringLiteral("あへあへ"))).empty());
    const auto surasura = store.find(normalizeKey(QStringLiteral("すらすら")));
    ASSERT_EQ(surasura.size(), 1U);
    EXPECT_EQ(asYomitanTerm(*surasura.front())->definitionsPlain, QList<QString>{QStringLiteral("smoothly")});
}
