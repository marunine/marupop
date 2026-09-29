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
// AFailedDatabaseRenameKeepsThePreviousStore fails the rename by removing the temporary database
// while sqlite3 holds it open, and Windows refuses the removal of an open file.
#ifndef Q_OS_WIN
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
#endif

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

    {
        // KeyFilter maps the sidecar, and Windows refuses QFile::remove() of a mapped file.
        KeyFilter filter;
        ASSERT_TRUE(filter.open(keyFilterPathFor(path)));
        EXPECT_TRUE(filter.contains(QStringLiteral("走る")));
        EXPECT_FALSE(filter.contains(QStringLiteral("歩く")));
    }

    // With the sidecar removed the store still answers every query, through SQLite alone.
    ASSERT_TRUE(QFile::remove(keyFilterPathFor(path)));
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("走る")).size(), 1U);
    EXPECT_TRUE(store.find(QStringLiteral("歩く")).empty());
}

namespace
{

void writeStore(const QString &path, const QString &word, qint32 id)
{
    StoreWriter writer;
    ASSERT_TRUE(writer.begin(path, DictType::JMdict));
    ASSERT_GE(writer.addRecord(jmdictRecord(word, QStringLiteral("よみ"), id), keys({word})), 0);
    ASSERT_TRUE(writer.finish());
}

// Writes a store holding word into the staged paths of path, the files StoreWriter::finish()
// leaves on Windows when an open Store holds the database at path.
void stageStore(const QString &path, const QString &word, qint32 id)
{
    const QString written = path + QStringLiteral(".written");
    writeStore(written, word, id);
    ASSERT_TRUE(QFile::rename(written, stagedPathFor(path)));
    ASSERT_TRUE(QFile::rename(keyFilterPathFor(written), stagedPathFor(keyFilterPathFor(path))));
}

} // namespace

// sqlite3_open_v2() takes a UTF-8 file name, and QFile::encodeName() on Windows returns the ANSI
// code page encoding. Store::open() passes the path in a file: URI, where # starts a fragment and
// % starts a percent-encoded byte.
TEST(DictStore, OpensAStoreUnderAFolderNameWithUriDelimiters)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString folder = QStringLiteral("辞書 #1 %20");
    ASSERT_TRUE(QDir(directory.path()).mkdir(folder));
    const QString path = directory.filePath(folder + QStringLiteral("/日本語.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("走る")).size(), 1U);
}

// Staged files of an earlier process are promoted on every platform, because no Store holds the
// files they replace.
TEST(DictStore, PromotesAStagedStoreOverAClosedStore)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("closed.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    stageStore(path, QStringLiteral("歩く"), 2);

    EXPECT_EQ(promoteStagedStore(path), StagedStore::Promoted);
    EXPECT_FALSE(QFile::exists(stagedPathFor(path)));
    EXPECT_FALSE(QFile::exists(stagedPathFor(keyFilterPathFor(path))));
    EXPECT_EQ(promoteStagedStore(path), StagedStore::None);
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("歩く")).size(), 1U);
    EXPECT_TRUE(store.find(QStringLiteral("走る")).empty());
}

// A staged store left beside a newer direct reimport would replace the newer database at the next
// promotion.
TEST(DictStore, ADirectReimportDeletesTheStagedFiles)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("restaged.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    stageStore(path, QStringLiteral("歩く"), 2);

    writeStore(path, QStringLiteral("泳ぐ"), 3);
    EXPECT_FALSE(QFile::exists(stagedPathFor(path)));
    EXPECT_FALSE(QFile::exists(stagedPathFor(keyFilterPathFor(path))));
    EXPECT_EQ(promoteStagedStore(path), StagedStore::None);
    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    EXPECT_EQ(store.find(QStringLiteral("泳ぐ")).size(), 1U);
}

TEST(DictStore, RemovesTheStoreFilesAndTheirStagedCopies)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("removed.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    stageStore(path, QStringLiteral("歩く"), 2);

    EXPECT_EQ(removeStoreFiles(path), StoreRemoval::Removed);
    for (const QString &file :
         {path, keyFilterPathFor(path), stagedPathFor(path), stagedPathFor(keyFilterPathFor(path))})
        EXPECT_FALSE(QFile::exists(file)) << file.toStdString();
    // The files are absent, which is the state the call establishes.
    EXPECT_EQ(removeStoreFiles(path), StoreRemoval::Removed);
}

#ifdef Q_OS_WIN

// Windows refuses to replace a database file that an open Store holds.
TEST(DictStore, ReimportingAnOpenStoreStagesItUntilTheStoreCloses)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("open.db"));
    writeStore(path, QStringLiteral("走る"), 1);

    {
        Store open;
        ASSERT_EQ(open.open(path), OpenResult::Ok);
        writeStore(path, QStringLiteral("歩く"), 2);
        EXPECT_TRUE(QFile::exists(stagedPathFor(path)));
        EXPECT_TRUE(QFile::exists(stagedPathFor(keyFilterPathFor(path))));
        EXPECT_EQ(open.find(QStringLiteral("走る")).size(), 1U);
        EXPECT_EQ(promoteStagedStore(path), StagedStore::Pending);
    }

    EXPECT_EQ(promoteStagedStore(path), StagedStore::Promoted);
    EXPECT_FALSE(QFile::exists(stagedPathFor(path)));
    EXPECT_EQ(promoteStagedStore(path), StagedStore::None);
    Store reopened;
    ASSERT_EQ(reopened.open(path), OpenResult::Ok);
    EXPECT_EQ(reopened.find(QStringLiteral("歩く")).size(), 1U);
    EXPECT_TRUE(reopened.find(QStringLiteral("走る")).empty());
}

// The previous key filter lacks the headwords of the new database. Store::find() rejects every key
// that the key filter lacks.
TEST(DictStore, PromotingADatabaseStagedWithoutItsKeyFilterDropsThePreviousFilter)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nokeys.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    ASSERT_TRUE(QFile::exists(keyFilterPathFor(path)));
    {
        Store open;
        ASSERT_EQ(open.open(path), OpenResult::Ok);
        writeStore(path, QStringLiteral("歩く"), 2);
    }
    ASSERT_TRUE(QFile::remove(stagedPathFor(keyFilterPathFor(path))));

    EXPECT_EQ(promoteStagedStore(path), StagedStore::Promoted);
    EXPECT_FALSE(QFile::exists(keyFilterPathFor(path)));
    Store reopened;
    ASSERT_EQ(reopened.open(path), OpenResult::Ok);
    EXPECT_EQ(reopened.find(QStringLiteral("歩く")).size(), 1U);
}

// QFile opens without FILE_SHARE_DELETE, so the open staged key filter cannot move after the
// staged database has moved.
TEST(DictStore, ADatabasePromotedBeforeItsKeyFilterRunsWithoutTheReplacedFilter)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("partial.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    {
        Store open;
        ASSERT_EQ(open.open(path), OpenResult::Ok);
        writeStore(path, QStringLiteral("歩く"), 2);
    }

    {
        QFile stagedKeys(stagedPathFor(keyFilterPathFor(path)));
        ASSERT_TRUE(stagedKeys.open(QIODevice::ReadOnly));
        EXPECT_EQ(promoteStagedStore(path), StagedStore::Pending);
        EXPECT_FALSE(QFile::exists(stagedPathFor(path)));
        EXPECT_FALSE(QFile::exists(keyFilterPathFor(path)));
        Store partial;
        ASSERT_EQ(partial.open(path), OpenResult::Ok);
        EXPECT_EQ(partial.find(QStringLiteral("歩く")).size(), 1U);
    }

    EXPECT_EQ(promoteStagedStore(path), StagedStore::Promoted);
    EXPECT_TRUE(QFile::exists(keyFilterPathFor(path)));
    Store promoted;
    ASSERT_EQ(promoted.open(path), OpenResult::Ok);
    EXPECT_EQ(promoted.find(QStringLiteral("歩く")).size(), 1U);
    EXPECT_TRUE(promoted.find(QStringLiteral("走る")).empty());
}

// Windows refuses to delete the files of an open Store.
TEST(DictStore, RemovingAnOpenStoreWaitsForItToClose)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("removed.db"));
    writeStore(path, QStringLiteral("走る"), 1);
    {
        Store open;
        ASSERT_EQ(open.open(path), OpenResult::Ok);
        EXPECT_EQ(removeStoreFiles(path), StoreRemoval::Pending);
    }
    EXPECT_EQ(removeStoreFiles(path), StoreRemoval::Removed);
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_FALSE(QFile::exists(keyFilterPathFor(path)));
}

#endif
