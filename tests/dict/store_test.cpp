// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// StoreWriter to Store round trips: the records come back, the keys reach them, the meta table
// survives, identical payloads share one row, and a schema version the build does not read is
// reported as needing a re-import.
#include "dict/codec.h"
#include "dict/keyfilter.h"
#include "dict/records.h"
#include "dict/store.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <sqlite3.h>

using namespace maru::dict;

namespace
{

Record jmdictRecord(const QString &spelling, const QString &reading, qint32 entryId)
{
    JmdictRecord source;
    source.entryId = entryId;
    source.primarySpelling = spelling;
    source.readings = {reading};
    source.definitions = {{QStringLiteral("gloss for ") + spelling}};
    source.wordClasses.sharedByAllSenses = {QStringLiteral("n")};

    Record record;
    record.type = DictType::JMdict;
    record.data = source;
    return record;
}

std::vector<QString> keys(std::initializer_list<QString> values)
{
    return {values};
}

} // namespace

TEST(DictStore, WritesAndReadsRecords)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("store.db"));

    {
        StoreWriter writer;
        QHash<QString, QString> meta;
        meta.insert(QString(metakeys::title), QStringLiteral("Test dictionary"));
        ASSERT_TRUE(writer.begin(path, DictType::JMdict, meta));

        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る"), QStringLiteral("はしる")})),
                  0);
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("歩く"), QStringLiteral("あるく"), 2),
                                   keys({QStringLiteral("歩く"), QStringLiteral("あるく")})),
                  0);
        EXPECT_EQ(writer.recordCount(), 2);
        EXPECT_EQ(writer.keyCount(), 4);
        // はしる is the longest key, 3 UTF-16 code units.
        EXPECT_EQ(writer.maxKeyLength(), 3);
        ASSERT_TRUE(writer.finish());
    }

    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(QFile::exists(keyFilterPathFor(path)));
    EXPECT_FALSE(QFile::exists(path + QStringLiteral(".tmp")));

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.dictType(), DictType::JMdict);
    EXPECT_EQ(store.recordCount(), 2);
    EXPECT_EQ(store.keyCount(), 4);
    EXPECT_EQ(store.maxKeyLength(), 3);
    EXPECT_EQ(store.meta(metakeys::title), QStringLiteral("Test dictionary"));
    EXPECT_FALSE(store.meta(metakeys::importedAt).isEmpty());

    const auto bySpelling = store.find(QStringLiteral("走る"));
    ASSERT_EQ(bySpelling.size(), 1U);
    ASSERT_NE(asJmdict(*bySpelling.front()), nullptr);
    EXPECT_EQ(asJmdict(*bySpelling.front())->entryId, 1);
    EXPECT_EQ(asJmdict(*bySpelling.front())->primarySpelling, QStringLiteral("走る"));
    EXPECT_GT(bySpelling.front()->id, 0);

    const auto byReading = store.find(QStringLiteral("はしる"));
    ASSERT_EQ(byReading.size(), 1U);
    EXPECT_EQ(byReading.front()->id, bySpelling.front()->id);

    EXPECT_TRUE(store.find(QStringLiteral("およぐ")).empty());
    EXPECT_TRUE(store.find(QString()).empty());
}

TEST(DictStore, DeduplicatesIdenticalPayloads)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("dedup.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        const Record record = jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1);
        const qint64 first = writer.addRecord(record, keys({QStringLiteral("走る")}));
        const qint64 second = writer.addRecord(record, keys({QStringLiteral("奔る")}));
        EXPECT_EQ(first, second);
        EXPECT_EQ(writer.recordCount(), 1);
        EXPECT_EQ(writer.keyCount(), 2);
        ASSERT_TRUE(writer.finish());
    }

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.recordCount(), 1);
    ASSERT_EQ(store.find(QStringLiteral("走る")).size(), 1U);
    ASSERT_EQ(store.find(QStringLiteral("奔る")).size(), 1U);
    EXPECT_EQ(store.find(QStringLiteral("走る")).front()->id, store.find(QStringLiteral("奔る")).front()->id);
}

TEST(DictStore, OneKeyReachesEveryRecordUnderIt)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("multi.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(
            writer.addRecord(jmdictRecord(QStringLiteral("日"), QStringLiteral("ひ"), 1), keys({QStringLiteral("日")})),
            0);
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("日"), QStringLiteral("にち"), 2),
                                   keys({QStringLiteral("日")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto records = store.find(QStringLiteral("日"));
    ASSERT_EQ(records.size(), 2U);
    EXPECT_LT(records.front()->id, records.back()->id);
}

TEST(DictStore, RecordCacheReturnsTheSameInstance)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cache.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto first = store.find(QStringLiteral("走る"));
    const auto second = store.find(QStringLiteral("走る"));
    ASSERT_EQ(first.size(), 1U);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(first.front().get(), second.front().get());

    // A capacity of zero disables the cache, and a decode then produces a new instance.
    store.setRecordCacheCapacity(0);
    const auto third = store.find(QStringLiteral("走る"));
    const auto fourth = store.find(QStringLiteral("走る"));
    ASSERT_EQ(third.size(), 1U);
    ASSERT_EQ(fourth.size(), 1U);
    EXPECT_NE(third.front().get(), fourth.front().get());
    EXPECT_EQ(asJmdict(*third.front())->primarySpelling, asJmdict(*fourth.front())->primarySpelling);
}

TEST(DictStore, ReportsNeedsReimportOnASchemaMismatch)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("stale.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    // Rewrite the schema_version row the way an older build would have left it.
    sqlite3 *database = nullptr;
    ASSERT_EQ(sqlite3_open(QFile::encodeName(path).constData(), &database), SQLITE_OK);
    ASSERT_EQ(
        sqlite3_exec(database, "UPDATE meta SET value = '0' WHERE key = 'schema_version'", nullptr, nullptr, nullptr),
        SQLITE_OK);
    sqlite3_close(database);

    Store store;
    EXPECT_EQ(store.open(path), OpenResult::NeedsReimport);
    EXPECT_FALSE(store.isOpen());
}

TEST(DictStore, ReportsNeedsReimportOnACodecMismatch)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("codec.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    sqlite3 *database = nullptr;
    ASSERT_EQ(sqlite3_open(QFile::encodeName(path).constData(), &database), SQLITE_OK);
    ASSERT_EQ(
        sqlite3_exec(database, "UPDATE meta SET value = '999' WHERE key = 'payload_codec'", nullptr, nullptr, nullptr),
        SQLITE_OK);
    sqlite3_close(database);

    Store store;
    EXPECT_EQ(store.open(path), OpenResult::NeedsReimport);
}

TEST(DictStore, ReportsFailedForAnAbsentFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    Store store;
    EXPECT_EQ(store.open(directory.filePath(QStringLiteral("absent.db"))), OpenResult::Failed);
    EXPECT_TRUE(store.find(QStringLiteral("走る")).empty());
}

TEST(DictStore, AbortLeavesNoFiles)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("aborted.db"));

    StoreWriter writer;
    ASSERT_TRUE(writer.begin(path, DictType::JMdict));
    ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                               keys({QStringLiteral("走る")})),
              0);
    writer.abort();

    EXPECT_FALSE(QFile::exists(path));
    EXPECT_FALSE(QFile::exists(path + QStringLiteral(".tmp")));
}

// A Store is immutable once open, which is what lets a lookup thread read its key filter and its
// meta table with no lock. Reopening one in place would rewrite both under that reader.
TEST(DictStore, RefusesToReopenAnOpenStore)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.db"));
    const QString second = directory.filePath(QStringLiteral("second.db"));

    for (const QString &path : {first, second}) {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    Store store;
    ASSERT_EQ(store.open(first), OpenResult::Ok);
    EXPECT_EQ(store.open(second), OpenResult::Failed);
    // The refusal leaves the first database open and answering.
    EXPECT_EQ(store.path(), first);
    EXPECT_EQ(store.find(QStringLiteral("走る")).size(), 1U);
}

// finish() renames the temporary database onto the target with rename(2), which replaces it in one
// step. Removing the target first, as the writer used to, destroys a working dictionary whenever
// the rename that follows fails.
TEST(DictStore, AFailedDatabaseRenameKeepsThePreviousStore)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("replaced.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("歩く"), QStringLiteral("あるく"), 2),
                                   keys({QStringLiteral("歩く")})),
                  0);
        // Unlinking the temporary database gives the rename an ENOENT source. sqlite3 keeps
        // writing through its open descriptor, so finish() reaches the rename and fails there.
        ASSERT_TRUE(QFile::remove(path + QStringLiteral(".tmp")));
        EXPECT_FALSE(writer.finish());
    }

    // The first import is still the store on disk, with its own sidecar.
    EXPECT_TRUE(QFile::exists(keyFilterPathFor(path)));
    EXPECT_FALSE(QFile::exists(keyFilterPathFor(path) + QStringLiteral(".tmp")));
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("走る")).size(), 1U);
    EXPECT_TRUE(store.find(QStringLiteral("歩く")).empty());
}

// The sidecar rename runs after the database is already in place. A failure there keeps the new
// database and removes the sidecar the previous import left, because a sidecar that indexes the
// previous key set rejects keys the new database holds.
TEST(DictStore, AFailedSidecarRenameKeepsTheNewDatabaseAndDropsTheStaleSidecar)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sidecar.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }
    ASSERT_TRUE(QFile::remove(keyFilterPathFor(path)));
    // rename(2) onto a directory reports EISDIR, which is the failure the second rename has to
    // survive.
    ASSERT_TRUE(QDir().mkpath(keyFilterPathFor(path)));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("歩く"), QStringLiteral("あるく"), 2),
                                   keys({QStringLiteral("歩く")})),
                  0);
        EXPECT_TRUE(writer.finish());
    }

    EXPECT_FALSE(QFile::exists(keyFilterPathFor(path) + QStringLiteral(".tmp")));
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("歩く")).size(), 1U);
    EXPECT_TRUE(store.find(QStringLiteral("走る")).empty());
}

TEST(DictStore, KeyFilterPathReplacesTheSuffix)
{
    EXPECT_EQ(keyFilterPathFor(QStringLiteral("/tmp/abc.db")), QStringLiteral("/tmp/abc.keys"));
    EXPECT_EQ(keyFilterPathFor(QStringLiteral("/tmp/abc")), QStringLiteral("/tmp/abc.keys"));
}

TEST(DictStore, MissesAreRejectedByTheKeyFilter)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("filter.db"));

    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        ASSERT_GE(writer.addRecord(jmdictRecord(QStringLiteral("走る"), QStringLiteral("はしる"), 1),
                                   keys({QStringLiteral("走る")})),
                  0);
        ASSERT_TRUE(writer.finish());
    }

    KeyFilter filter;
    ASSERT_TRUE(filter.open(keyFilterPathFor(path)));
    EXPECT_TRUE(filter.contains(QStringLiteral("走る")));
    EXPECT_FALSE(filter.contains(QStringLiteral("歩く")));

    // With the sidecar removed the store still answers every query, through SQLite alone.
    ASSERT_TRUE(QFile::remove(keyFilterPathFor(path)));
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("走る")).size(), 1U);
    EXPECT_TRUE(store.find(QStringLiteral("歩く")).empty());
}
