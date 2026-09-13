// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Optional benchmarks read user-supplied dictionaries; enable MARUPOP_REAL_DATA=1.
// See docs/TESTING.md for input variables. No dictionary corpus is bundled.
#include "dict/dictionary.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/importers/yomitanimporter.h"
#include "dict/keynorm.h"
#include "dict/lookupsupport.h"
#include "dict/records.h"
#include "dict/store.h"
#include "dict/wordclasses.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <random>

#ifdef __GLIBC__
#include <malloc.h>
#endif

using namespace maru::dict;

namespace
{

const QString jlResources = qEnvironmentVariable("MARUPOP_DICTIONARY_RESOURCES");
const QString yomitanRoot = qEnvironmentVariable("MARUPOP_YOMITAN_ROOT");

bool realDataEnabled()
{
    return qEnvironmentVariable("MARUPOP_REAL_DATA") == QLatin1String("1");
}

bool inputsPresent()
{
    return QFileInfo::exists(jlResources) && QFileInfo::exists(yomitanRoot);
}

#define SKIP_UNLESS_REAL_DATA()                                                                                        \
    do {                                                                                                               \
        if (!inputsPresent())                                                                                          \
            GTEST_SKIP() << "set MARUPOP_DICTIONARY_RESOURCES and MARUPOP_YOMITAN_ROOT to existing input directories"; \
        if (!realDataEnabled())                                                                                        \
            GTEST_SKIP() << "set MARUPOP_REAL_DATA=1 to run the real-data imports";                                    \
    } while (false)

// The peak resident set of this process, in bytes, from /proc/self/status.
qint64 peakResidentBytes()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("VmHWM:"))
            continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() >= 2)
            return fields.at(1).toLongLong() * 1024;
    }
    return 0;
}

qint64 currentResidentBytes()
{
#ifdef __GLIBC__
    // The import allocated hundreds of megabytes that the allocator has not returned to the
    // kernel. Trimming first is what makes the number the working set of the open stores rather
    // than the high-water mark of the process.
    malloc_trim(0);
#endif
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("VmRSS:"))
            continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() >= 2)
            return fields.at(1).toLongLong() * 1024;
    }
    return 0;
}

double megabytes(qint64 bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void reportStore(const QString &label, const QString &path, const ImportResult &result, qint64 elapsedMs)
{
    const qint64 databaseBytes = QFileInfo(path).size();
    const qint64 sidecarBytes = QFileInfo(keyFilterPathFor(path)).size();
    std::printf("%-42s records %9lld keys %9lld  %6.1f s  db %7.1f MB  keys %5.1f MB\n",
                qUtf8Printable(label),
                static_cast<long long>(result.recordCount),
                static_cast<long long>(result.keyCount),
                static_cast<double>(elapsedMs) / 1000.0,
                megabytes(databaseBytes),
                megabytes(sidecarBytes));
}

// Microseconds per find(), sorted, so the caller can read a percentile off it.
std::vector<double> timeFinds(const Store &store, const QList<QString> &keys)
{
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(keys.size()));
    for (const QString &key : keys) {
        QElapsedTimer timer;
        timer.start();
        const auto records = store.find(key);
        samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1000.0);
        (void)records;
    }
    std::ranges::sort(samples);
    return samples;
}

double percentile(const std::vector<double> &sorted, double fraction)
{
    if (sorted.empty())
        return 0.0;
    const auto index = static_cast<size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

// 200 keys the store holds, taken from its own search_key table through a sample of the source
// text, and 200 keys it does not.
struct BenchmarkKeys
{
    QList<QString> hits;
    QList<QString> misses;
};

BenchmarkKeys buildBenchmarkKeys(const Store &store, const QList<QString> &candidates, int count)
{
    BenchmarkKeys keys;
    // A fixed seed makes the measured sample set reproducible across runs.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937 generator(20260905);
    std::uniform_int_distribution<int> characters(0x4E00, 0x9FA0);

    QList<QString> shuffled = candidates;
    std::shuffle(shuffled.begin(), shuffled.end(), generator);
    for (const QString &candidate : shuffled) {
        if (keys.hits.size() >= count)
            break;
        if (!store.find(candidate).empty())
            keys.hits.append(candidate);
    }

    while (keys.misses.size() < count) {
        QString key;
        for (int i = 0; i < 6; ++i)
            key.append(QChar(static_cast<char16_t>(characters(generator))));
        if (store.find(key).empty())
            keys.misses.append(key);
    }
    return keys;
}

class RealData : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        directory = new QTemporaryDir;
    }

    static void TearDownTestSuite()
    {
        delete directory;
        directory = nullptr;
    }

    static QString path(const QString &name)
    {
        return directory->filePath(name);
    }

    static QTemporaryDir *directory;
};

QTemporaryDir *RealData::directory = nullptr;

} // namespace

TEST_F(RealData, ImportsJmdict)
{
    SKIP_UNLESS_REAL_DATA();

    const QString source = jlResources + QStringLiteral("/JMdict.xml");
    ASSERT_TRUE(QFileInfo::exists(source));
    const QString database = path(QStringLiteral("jmdict.db"));

    JmdictImporter importer(true);
    StoreWriter writer;
    ASSERT_TRUE(writer.begin(database, DictType::JMdict));

    std::atomic_bool cancel{false};
    QElapsedTimer timer;
    timer.start();
    const ImportResult result = importer.import(source, writer, {}, cancel);
    const qint64 elapsed = timer.elapsed();
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();

    reportStore(QStringLiteral("JMdict.xml"), database, result, elapsed);
    std::printf("  peak RSS %.1f MB, word-class keys %lld\n",
                megabytes(peakResidentBytes()),
                static_cast<long long>(importer.wordClassTable().keyCount()));

    EXPECT_GT(result.recordCount, 250000);
    EXPECT_GT(result.keyCount, 400000);
    EXPECT_GT(importer.wordClassTable().keyCount(), 35000);

    ASSERT_TRUE(importer.wordClassTable().save(path(QStringLiteral("jmdict.pos"))));

    Store store;
    ASSERT_EQ(store.open(database), OpenResult::Ok);
    EXPECT_FALSE(store.find(normalizeKey(QStringLiteral("走る"))).empty());
    EXPECT_FALSE(store.find(normalizeKey(QStringLiteral("食べる"))).empty());
    EXPECT_FALSE(store.find(kanjiExamplesKey(QStringLiteral("日"))).empty());
}

TEST_F(RealData, ComparesTheWordClassTableAgainstJlPoSJson)
{
    SKIP_UNLESS_REAL_DATA();

    const QString posPath = jlResources + QStringLiteral("/PoS.json");
    if (!QFileInfo::exists(posPath))
        GTEST_SKIP() << "PoS.json is absent from the configured inputs";

    WordClassTable table;
    ASSERT_TRUE(table.load(path(QStringLiteral("jmdict.pos")))) << "run ImportsJmdict first";

    QFile file(posPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QJsonObject reference = QJsonDocument::fromJson(file.readAll()).object();
    ASSERT_FALSE(reference.isEmpty());

    // 100 spellings drawn deterministically from JL's own table.
    const QStringList referenceKeys = reference.keys();
    // A fixed seed makes the compared sample set reproducible across runs.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937 generator(20260905);
    std::uniform_int_distribution<int> pick(0, static_cast<int>(referenceKeys.size()) - 1);

    int compared = 0;
    int agreed = 0;
    QStringList disagreements;
    while (compared < 100) {
        const QString &key = referenceKeys.at(pick(generator));
        const QJsonArray entries = reference.value(key).toArray();
        if (entries.isEmpty())
            continue;
        ++compared;

        bool allFound = true;
        for (const auto &entryValue : entries) {
            const QJsonObject entry = entryValue.toObject();
            const QString spelling = entry.value(QStringLiteral("S")).toString();
            const QJsonArray classes = entry.value(QStringLiteral("C")).toArray();
            const QJsonArray readings = entry.value(QStringLiteral("R")).toArray();
            const QString reading = readings.isEmpty() ? QString() : readings.first().toString();
            for (const auto &wordClass : classes) {
                if (!table.containsTag(spelling, reading, wordClass.toString())) {
                    allFound = false;
                    if (disagreements.size() < 10) {
                        disagreements.append(spelling + QLatin1Char('/') + reading + QLatin1Char('/') +
                                             wordClass.toString());
                    }
                }
            }
        }
        if (allFound)
            ++agreed;
    }

    std::printf("word classes: %d of %d sampled JL PoS.json spellings reproduced\n", agreed, compared);
    for (const QString &disagreement : disagreements)
        std::printf("  missing %s\n", qUtf8Printable(disagreement));
    // JL's table and the supplied JMdict may use different revisions, so a small number of
    // headwords can legitimately differ.
    EXPECT_GE(agreed, compared * 9 / 10);
}

TEST_F(RealData, ImportsAGrammarDictionary)
{
    SKIP_UNLESS_REAL_DATA();

    const QString source = yomitanRoot + QStringLiteral("/grammar");
    if (!QFileInfo::exists(source))
        GTEST_SKIP() << "the grammar dictionary is absent from the configured inputs";

    const QString database = path(QStringLiteral("grammar.db"));
    YomitanImporter importer(DictType::YomitanOther);
    StoreWriter writer;
    ASSERT_TRUE(writer.begin(database, DictType::YomitanOther));

    std::atomic_bool cancel{false};
    QElapsedTimer timer;
    timer.start();
    const ImportResult result = importer.import(source, writer, {}, cancel);
    const qint64 elapsed = timer.elapsed();
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    reportStore(QStringLiteral("[Grammar] 02. E de wakaru"), database, result, elapsed);
    EXPECT_GT(result.recordCount, 1000);
}

TEST_F(RealData, ImportsAMonolingualDictionary)
{
    SKIP_UNLESS_REAL_DATA();

    const QString source = yomitanRoot + QStringLiteral("/monolingual");
    if (!QFileInfo::exists(source))
        GTEST_SKIP() << "the monolingual dictionary is absent from the configured inputs";

    const QString database = path(QStringLiteral("sanseido.db"));
    YomitanImporter importer(DictType::YomitanWord);
    StoreWriter writer;
    ASSERT_TRUE(writer.begin(database, DictType::YomitanWord));

    std::atomic_bool cancel{false};
    QElapsedTimer timer;
    timer.start();
    const ImportResult result = importer.import(source, writer, {}, cancel);
    const qint64 elapsed = timer.elapsed();
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    reportStore(QString::fromUtf8("三省堂国語辞典 第八版"), database, result, elapsed);
    EXPECT_GT(result.recordCount, 50000);

    Store store;
    ASSERT_EQ(store.open(database), OpenResult::Ok);
    const auto records = store.find(normalizeKey(QString::fromUtf8("走る")));
    ASSERT_FALSE(records.empty());
    const YomitanTermRecord *record = asYomitanTerm(*records.front());
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->definitions.isEmpty());
    // Structured content survives as rich text rather than being flattened.
    bool sawMarkup = false;
    for (const QString &definition : record->definitions) {
        if (definition.contains(QLatin1Char('<')))
            sawMarkup = true;
    }
    EXPECT_TRUE(sawMarkup);
}

TEST_F(RealData, ImportsTheJpdbFrequencyList)
{
    SKIP_UNLESS_REAL_DATA();

    const QString source = yomitanRoot + QStringLiteral("/frequency");
    if (!QFileInfo::exists(source))
        GTEST_SKIP() << "the JPDB frequency list is absent from the configured inputs";

    const QString database = path(QStringLiteral("jpdb.db"));
    YomitanImporter importer(DictType::YomitanFrequency);
    StoreWriter writer;
    ASSERT_TRUE(writer.begin(database, DictType::YomitanFrequency));

    std::atomic_bool cancel{false};
    QElapsedTimer timer;
    timer.start();
    const ImportResult result = importer.import(source, writer, {}, cancel);
    const qint64 elapsed = timer.elapsed();
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    reportStore(QStringLiteral("JPDB v2.2 frequency"), database, result, elapsed);
    EXPECT_GT(result.recordCount, 100000);

    Store store;
    ASSERT_EQ(store.open(database), OpenResult::Ok);
    // The ㋕ display values parse to a rank rather than to zero, which is the bug meikipop has.
    const auto records = store.find(normalizeKey(QString::fromUtf8("の")));
    ASSERT_FALSE(records.empty());
    EXPECT_GT(asFrequency(*records.front())->frequency, 0);
    EXPECT_GT(store.meta(metakeys::maxFrequency).toInt(), 0);
}

TEST_F(RealData, ImportsTheNhkPitchDictionary)
{
    SKIP_UNLESS_REAL_DATA();

    const QString source = yomitanRoot + QStringLiteral("/pitch");
    if (!QFileInfo::exists(source))
        GTEST_SKIP() << "the NHK pitch dictionary is absent from the configured inputs";

    const QString database = path(QStringLiteral("nhk.db"));
    YomitanImporter importer(DictType::YomitanPitchAccent);
    StoreWriter writer;
    ASSERT_TRUE(writer.begin(database, DictType::YomitanPitchAccent));

    std::atomic_bool cancel{false};
    QElapsedTimer timer;
    timer.start();
    const ImportResult result = importer.import(source, writer, {}, cancel);
    const qint64 elapsed = timer.elapsed();
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    reportStore(QStringLiteral("[Pitch] NHK2016"), database, result, elapsed);
    EXPECT_GT(result.recordCount, 10000);

    auto store = std::make_shared<Store>();
    ASSERT_EQ(store->open(database), OpenResult::Ok);
    DictionaryHandle handle;
    handle.type = DictType::YomitanPitchAccent;
    handle.store = std::move(store);
    const QList<std::optional<quint8>> positions =
        pitchPositionsFor(handle, QString::fromUtf8("箸"), {QString::fromUtf8("はし")});
    ASSERT_EQ(positions.size(), 1);
    EXPECT_TRUE(positions.first().has_value());
}

TEST_F(RealData, MeasuresTheLookupLatency)
{
    SKIP_UNLESS_REAL_DATA();

    struct Target
    {
        QString label;
        QString database;
    };

    const QList<Target> targets{
        {.label = QStringLiteral("JMdict"), .database = path(QStringLiteral("jmdict.db"))},
        {.label = QString::fromUtf8("三省堂国語辞典"), .database = path(QStringLiteral("sanseido.db"))},
    };

    // The candidate keys are the normalized prefixes of a paragraph of running text, which is what
    // the lookup engine produces on a hover.
    const QString sample =
        QString::fromUtf8("彼女は毎日図書館に通って本を読んでいる。天気が良い日には公園を散歩しながら考え事をする"
                          "のが好きだと言っていた。今日も朝から晩まで勉強を続けている。");
    QList<QString> candidates;
    for (qsizetype start = 0; start < sample.size(); ++start) {
        for (qsizetype length = 1; length <= 12 && start + length <= sample.size(); ++length)
            candidates.append(normalizeKey(QStringView(sample).mid(start, length)));
    }

    for (const Target &target : targets) {
        if (!QFileInfo::exists(target.database))
            continue;

        Store store;
        ASSERT_EQ(store.open(target.database), OpenResult::Ok);

        const BenchmarkKeys keys = buildBenchmarkKeys(store, candidates, 200);
        if (keys.hits.size() < 20)
            continue;

        // Warm the caches with one full pass before measuring.
        (void)timeFinds(store, keys.hits);
        (void)timeFinds(store, keys.misses);

        const std::vector<double> hits = timeFinds(store, keys.hits);
        const std::vector<double> misses = timeFinds(store, keys.misses);
        std::printf("%-24s hits %3zu p50 %6.2f us p99 %7.2f us | misses p50 %5.2f us p99 %5.2f us\n",
                    qUtf8Printable(target.label),
                    hits.size(),
                    percentile(hits, 0.5),
                    percentile(hits, 0.99),
                    percentile(misses, 0.5),
                    percentile(misses, 0.99));

        // The design budget is under 5 ms per lookup across every dictionary, so one warm find()
        // has three orders of magnitude of headroom.
        EXPECT_LT(percentile(hits, 0.99), 5000.0);
        EXPECT_LT(percentile(misses, 0.99), 100.0);
    }

    std::printf("resident set with the stores open: %.1f MB\n", megabytes(currentResidentBytes()));
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
