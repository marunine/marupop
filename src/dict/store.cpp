// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "store.h"

#include "core/logging.h"
#include "dict/codec.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <cerrno>
#include <cstdio>
#include <sqlite3.h>
#include <utility>
#include <xxhash.h>

namespace maru::dict
{

namespace
{

// page_size 4096 rather than JL's 65536: a point lookup that misses the page cache reads one page
// per B-tree level, so a 64 KiB page costs 16 times the bytes for the same row.
// journal_mode OFF because the file is written once and never updated afterwards.
constexpr const char *createSchemaSql = R"SQL(
PRAGMA page_size = 4096;
PRAGMA journal_mode = OFF;
PRAGMA synchronous = OFF;
PRAGMA temp_store = MEMORY;
PRAGMA cache_size = -200000;
CREATE TABLE meta (
    key   TEXT NOT NULL PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID, STRICT;
CREATE TABLE record (
    id      INTEGER NOT NULL PRIMARY KEY,
    payload BLOB    NOT NULL
) STRICT;
CREATE TABLE search_key (
    key       TEXT    NOT NULL,
    record_id INTEGER NOT NULL,
    PRIMARY KEY (key, record_id)
) WITHOUT ROWID, STRICT;
)SQL";

// The composite primary key of search_key is the index, so the join reads the key rows for one
// key contiguously and then one record row per hit.
constexpr const char *findSql = "SELECT r.id, r.payload FROM search_key k JOIN record r ON r.id = k.record_id "
                                "WHERE k.key = ?";

void bindText(sqlite3_stmt *statement, int index, const QByteArray &utf8)
{
    // SQLITE_STATIC: utf8 outlives the sqlite3_step() that reads it, which the callers guarantee
    // by keeping the QByteArray alive until sqlite3_reset().
    sqlite3_bind_text(statement, index, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_STATIC);
}

QString columnText(sqlite3_stmt *statement, int index)
{
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement, index));
    if (text == nullptr)
        return {};
    return QString::fromUtf8(text, sqlite3_column_bytes(statement, index));
}

// Moves source onto target in one step. QFile::rename() refuses a target that exists and would
// need it removed first, and the window between that removal and a rename that then fails is what
// destroys a working dictionary; rename(2) replaces the target atomically instead.
bool renameOver(const QString &source, const QString &target)
{
    if (std::rename(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) == 0)
        return true;
    // The errno value rather than strerror(), which is not thread safe and whose message adds
    // nothing an import failure report needs beyond the code.
    qCWarning(logDictImport) << "Cannot rename" << source << "to" << target << "errno" << errno;
    return false;
}

} // namespace

QString keyFilterPathFor(const QString &dbPath)
{
    if (dbPath.endsWith(QLatin1String(".db")))
        return dbPath.chopped(3) + QLatin1String(".keys");
    return dbPath + QLatin1String(".keys");
}

Store::Store() = default;

Store::~Store()
{
    close();
}

OpenResult Store::open(const QString &dbPath)
{
    // A Store is immutable once open, which is what lets a lookup thread read it with no lock.
    // Replacing an imported dictionary constructs a new Store instead of reopening this one.
    if (m_db != nullptr) {
        qCWarning(logDict) << "Store" << m_path << "is already open and cannot be reopened on" << dbPath;
        return OpenResult::Failed;
    }

    if (!QFileInfo::exists(dbPath)) {
        qCWarning(logDict) << "Dictionary store" << dbPath << "does not exist";
        return OpenResult::Failed;
    }

    // immutable=1 tells SQLite the file cannot change while open, which removes the locking and
    // the change-counter checks. The file is written once by StoreWriter and renamed into place,
    // so the guarantee holds.
    const QByteArray uri = QByteArray("file:") + QFile::encodeName(dbPath) + "?immutable=1";
    const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI;
    if (sqlite3_open_v2(uri.constData(), &m_db, flags, nullptr) != SQLITE_OK) {
        qCWarning(logDict) << "Cannot open" << dbPath << sqlite3_errmsg(m_db);
        close();
        return OpenResult::Failed;
    }

    // mmap_size lets SQLite read pages straight out of the page cache rather than copying them
    // into its own; cache_size covers the B-tree interior pages the point queries walk.
    static constexpr const char *readPragmas = "PRAGMA query_only = 1;"
                                               "PRAGMA mmap_size = 268435456;"
                                               "PRAGMA cache_size = -16000;"
                                               "PRAGMA temp_store = MEMORY;";
    char *errorMessage = nullptr;
    if (sqlite3_exec(m_db, readPragmas, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        qCWarning(logDict) << "Cannot apply the read pragmas to" << dbPath << errorMessage;
        sqlite3_free(errorMessage);
    }

    sqlite3_stmt *metaStatement = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT key, value FROM meta", -1, &metaStatement, nullptr) != SQLITE_OK) {
        qCWarning(logDict) << "Cannot read the meta table of" << dbPath << sqlite3_errmsg(m_db);
        close();
        return OpenResult::Failed;
    }
    while (sqlite3_step(metaStatement) == SQLITE_ROW)
        m_meta.insert(columnText(metaStatement, 0), columnText(metaStatement, 1));
    sqlite3_finalize(metaStatement);

    const int schemaVersion = m_meta.value(metakeys::schemaVersion).toInt();
    const int payloadCodec = m_meta.value(metakeys::payloadCodec).toInt();
    if (schemaVersion != storeSchemaVersion || payloadCodec != codecVersion) {
        qCInfo(logDict,
               "%s carries schema %d codec %d, this build reads schema %d codec %d: needs reimport",
               qUtf8Printable(dbPath),
               schemaVersion,
               payloadCodec,
               storeSchemaVersion,
               codecVersion);
        close();
        return OpenResult::NeedsReimport;
    }

    if (sqlite3_prepare_v2(m_db, findSql, -1, &m_findStatement, nullptr) != SQLITE_OK) {
        qCWarning(logDict) << "Cannot prepare the lookup statement for" << dbPath << sqlite3_errmsg(m_db);
        close();
        return OpenResult::Failed;
    }

    m_path = dbPath;
    m_dictType = static_cast<DictType>(m_meta.value(metakeys::dictType).toInt());
    m_recordCount = m_meta.value(metakeys::recordCount).toLongLong();
    m_keyCount = m_meta.value(metakeys::keyCount).toLongLong();
    m_maxKeyLength = m_meta.value(metakeys::maxKeyLength).toInt();

    if (!m_keyFilter.open(keyFilterPathFor(dbPath)))
        qCWarning(logDict) << "Dictionary" << dbPath << "has no key filter: every candidate key reaches SQLite";

    return OpenResult::Ok;
}

void Store::close()
{
    QMutexLocker locker(&m_mutex);
    if (m_findStatement != nullptr) {
        sqlite3_finalize(m_findStatement);
        m_findStatement = nullptr;
    }
    if (m_db != nullptr) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
    m_keyFilter.close();
    m_meta.clear();
    m_lru.clear();
    m_lruIndex.clear();
    m_path.clear();
    m_recordCount = 0;
    m_keyCount = 0;
    m_maxKeyLength = 0;
}

void Store::setRecordCacheCapacity(qsizetype capacity)
{
    QMutexLocker locker(&m_mutex);
    m_cacheCapacity = std::max<qsizetype>(0, capacity);
    while (std::cmp_greater(m_lru.size(), m_cacheCapacity)) {
        m_lruIndex.erase(m_lru.back().first);
        m_lru.pop_back();
    }
}

std::shared_ptr<const Record> Store::decodeCached(qint64 id, const void *payload, int payloadSize) const
{
    const auto cached = m_lruIndex.find(id);
    if (cached != m_lruIndex.end()) {
        m_lru.splice(m_lru.begin(), m_lru, cached->second);
        return m_lru.front().second;
    }

    std::optional<Record> decoded =
        decodeRecord(m_dictType, QByteArrayView(static_cast<const char *>(payload), payloadSize));
    if (!decoded.has_value())
        return {};
    decoded->id = id;

    auto shared = std::make_shared<const Record>(std::move(*decoded));
    if (m_cacheCapacity > 0) {
        m_lru.emplace_front(id, shared);
        m_lruIndex.emplace(id, m_lru.begin());
        while (std::cmp_greater(m_lru.size(), m_cacheCapacity)) {
            m_lruIndex.erase(m_lru.back().first);
            m_lru.pop_back();
        }
    }
    return shared;
}

std::vector<std::shared_ptr<const Record>> Store::find(QStringView normalizedKey) const
{
    std::vector<std::shared_ptr<const Record>> records;
    if (normalizedKey.isEmpty())
        return records;

    // The mapping is written by open() and read-only afterwards, so the rejection that answers the
    // large majority of candidate keys runs on every thread at once, ahead of the lock.
    if (m_keyFilter.isOpen() && !m_keyFilter.contains(normalizedKey))
        return records;

    QMutexLocker locker(&m_mutex);
    if (m_findStatement == nullptr)
        return records;

    const QByteArray keyUtf8 = normalizedKey.toUtf8();
    sqlite3_reset(m_findStatement);
    sqlite3_clear_bindings(m_findStatement);
    bindText(m_findStatement, 1, keyUtf8);

    while (sqlite3_step(m_findStatement) == SQLITE_ROW) {
        const qint64 id = sqlite3_column_int64(m_findStatement, 0);
        const void *payload = sqlite3_column_blob(m_findStatement, 1);
        const int payloadSize = sqlite3_column_bytes(m_findStatement, 1);
        if (payload == nullptr || payloadSize <= 0)
            continue;
        if (auto record = decodeCached(id, payload, payloadSize))
            records.push_back(std::move(record));
    }
    sqlite3_reset(m_findStatement);
    return records;
}

QString Store::meta(QLatin1StringView key) const
{
    // m_meta is filled by open() and never written again, so the read needs no lock.
    return m_meta.value(QString(key));
}

StoreWriter::StoreWriter() = default;

StoreWriter::~StoreWriter()
{
    abort();
}

bool StoreWriter::exec(const char *sql)
{
    char *errorMessage = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        qCWarning(logDictImport) << "SQL failed:" << errorMessage;
        sqlite3_free(errorMessage);
        m_failed = true;
        return false;
    }
    return true;
}

bool StoreWriter::begin(const QString &dbPath, DictType type, const QHash<QString, QString> &meta)
{
    abort();

    m_dbPath = dbPath;
    m_tempDbPath = dbPath + QLatin1String(".tmp");
    m_keysPath = keyFilterPathFor(dbPath);
    m_type = type;
    m_meta = meta;
    m_failed = false;

    QFile::remove(m_tempDbPath);

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(QFile::encodeName(m_tempDbPath).constData(), &m_db, flags, nullptr) != SQLITE_OK) {
        qCWarning(logDictImport) << "Cannot create" << m_tempDbPath << sqlite3_errmsg(m_db);
        abort();
        return false;
    }

    if (!exec(createSchemaSql)) {
        abort();
        return false;
    }

    const QByteArray userVersion = QByteArray("PRAGMA user_version = ") + QByteArray::number(storeSchemaVersion);
    if (!exec(userVersion.constData())) {
        abort();
        return false;
    }

    if (sqlite3_prepare_v2(m_db, "INSERT INTO record (id, payload) VALUES (?, ?)", -1, &m_insertRecord, nullptr) !=
            SQLITE_OK ||
        sqlite3_prepare_v2(
            m_db, "INSERT OR IGNORE INTO search_key (key, record_id) VALUES (?, ?)", -1, &m_insertKey, nullptr) !=
            SQLITE_OK ||
        sqlite3_prepare_v2(
            m_db, "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", -1, &m_insertMeta, nullptr) != SQLITE_OK) {
        qCWarning(logDictImport) << "Cannot prepare the import statements" << sqlite3_errmsg(m_db);
        abort();
        return false;
    }

    return beginTransaction();
}

bool StoreWriter::beginTransaction()
{
    if (m_inTransaction)
        return true;
    if (!exec("BEGIN"))
        return false;
    m_inTransaction = true;
    return true;
}

bool StoreWriter::commitTransaction()
{
    if (!m_inTransaction)
        return true;
    m_inTransaction = false;
    return exec("COMMIT");
}

void StoreWriter::maybeCommit()
{
    if (m_keysSinceCommit < storeTransactionKeyBatch)
        return;
    m_keysSinceCommit = 0;
    if (commitTransaction())
        (void)beginTransaction();
}

void StoreWriter::setMeta(QLatin1StringView key, const QString &value)
{
    m_meta.insert(QString(key), value);
}

qint64 StoreWriter::addRecord(const Record &record, std::span<const QString> keys)
{
    if (m_db == nullptr || m_failed)
        return -1;

    const QByteArray payload = encodeRecord(record);
    const XXH128_hash_t digest = XXH3_128bits(payload.constData(), static_cast<size_t>(payload.size()));
    const PayloadHash payloadHash{.low = digest.low64, .high = digest.high64};

    const auto existing = m_payloadIds.find(payloadHash);
    if (existing != m_payloadIds.end()) {
        addKeys(existing->second, keys);
        return existing->second;
    }

    const qint64 id = m_nextId++;
    sqlite3_reset(m_insertRecord);
    sqlite3_bind_int64(m_insertRecord, 1, id);
    sqlite3_bind_blob(m_insertRecord, 2, payload.constData(), static_cast<int>(payload.size()), SQLITE_STATIC);
    if (sqlite3_step(m_insertRecord) != SQLITE_DONE) {
        qCWarning(logDictImport) << "Cannot insert a record" << sqlite3_errmsg(m_db);
        m_failed = true;
        sqlite3_reset(m_insertRecord);
        return -1;
    }
    sqlite3_reset(m_insertRecord);

    m_payloadIds.emplace(payloadHash, id);
    ++m_recordCount;
    addKeys(id, keys);
    return id;
}

void StoreWriter::addKeys(qint64 recordId, std::span<const QString> keys)
{
    if (m_db == nullptr || m_failed || recordId < 0)
        return;

    for (const QString &key : keys) {
        if (key.isEmpty())
            continue;
        const QByteArray keyUtf8 = key.toUtf8();
        sqlite3_reset(m_insertKey);
        bindText(m_insertKey, 1, keyUtf8);
        sqlite3_bind_int64(m_insertKey, 2, recordId);
        if (sqlite3_step(m_insertKey) != SQLITE_DONE) {
            qCWarning(logDictImport) << "Cannot insert a search key" << sqlite3_errmsg(m_db);
            m_failed = true;
            sqlite3_reset(m_insertKey);
            return;
        }
        sqlite3_reset(m_insertKey);

        m_keyFilterWriter.add(key);
        m_maxKeyLength = std::max(m_maxKeyLength, static_cast<int>(key.size()));
        ++m_keyCount;
        ++m_keysSinceCommit;
    }
    maybeCommit();
}

bool StoreWriter::finish()
{
    if (m_db == nullptr || m_failed) {
        abort();
        return false;
    }

    if (!commitTransaction()) {
        abort();
        return false;
    }

    // The sidecar is written first so its distinct key count can be recorded in the meta table:
    // key_count is what the manager shows and what the lookup engine reports, and the distinct
    // count is the number that matches the sidecar.
    if (!m_keyFilterWriter.write(m_keysPath + QLatin1String(".tmp"))) {
        abort();
        return false;
    }

    m_meta.insert(QString(metakeys::schemaVersion), QString::number(storeSchemaVersion));
    m_meta.insert(QString(metakeys::payloadCodec), QString::number(codecVersion));
    m_meta.insert(QString(metakeys::dictType), QString::number(static_cast<int>(m_type)));
    m_meta.insert(QString(metakeys::recordCount), QString::number(m_recordCount));
    m_meta.insert(QString(metakeys::keyCount), QString::number(m_keyFilterWriter.distinctCount()));
    m_meta.insert(QString(metakeys::maxKeyLength), QString::number(m_maxKeyLength));
    if (!m_meta.contains(QString(metakeys::importedAt)))
        m_meta.insert(QString(metakeys::importedAt), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!beginTransaction()) {
        abort();
        return false;
    }
    for (auto entry = m_meta.constBegin(); entry != m_meta.constEnd(); ++entry) {
        const QByteArray key = entry.key().toUtf8();
        const QByteArray value = entry.value().toUtf8();
        sqlite3_reset(m_insertMeta);
        bindText(m_insertMeta, 1, key);
        bindText(m_insertMeta, 2, value);
        if (sqlite3_step(m_insertMeta) != SQLITE_DONE) {
            qCWarning(logDictImport) << "Cannot insert a meta row" << sqlite3_errmsg(m_db);
            abort();
            return false;
        }
        sqlite3_reset(m_insertMeta);
    }
    if (!commitTransaction()) {
        abort();
        return false;
    }

    // ANALYZE writes sqlite_stat1, which the query planner reads on the next open. VACUUM is not
    // run: the file is built in one pass into a fresh database, so it has no free pages to
    // reclaim, and vacuuming a 300 MB store costs a full rewrite for nothing.
    (void)exec("ANALYZE");

    sqlite3_finalize(m_insertRecord);
    sqlite3_finalize(m_insertKey);
    sqlite3_finalize(m_insertMeta);
    m_insertRecord = nullptr;
    m_insertKey = nullptr;
    m_insertMeta = nullptr;
    sqlite3_close_v2(m_db);
    m_db = nullptr;

    // rename(2) replaces the previous database in one step. Removing it first would leave the
    // dictionary with no database at all for the duration of a rename that then fails.
    const QString tempKeysPath = m_keysPath + QLatin1String(".tmp");
    if (!renameOver(m_tempDbPath, m_dbPath)) {
        abort();
        return false;
    }

    // The database at m_dbPath is now the new one. A sidecar left over from the previous import
    // indexes keys that database no longer holds, and Store::find() would reject a candidate the
    // new database answers, so the stale file is removed rather than kept.
    if (!renameOver(tempKeysPath, m_keysPath)) {
        QFile::remove(tempKeysPath);
        if (QFile::exists(m_keysPath) && !QFile::remove(m_keysPath)) {
            qCWarning(logDictImport) << "Cannot remove the stale key filter" << m_keysPath
                                     << "left beside the reimported store" << m_dbPath;
        }
    }

    m_payloadIds.clear();
    m_tempDbPath.clear();
    return true;
}

void StoreWriter::abort()
{
    if (m_insertRecord != nullptr) {
        sqlite3_finalize(m_insertRecord);
        m_insertRecord = nullptr;
    }
    if (m_insertKey != nullptr) {
        sqlite3_finalize(m_insertKey);
        m_insertKey = nullptr;
    }
    if (m_insertMeta != nullptr) {
        sqlite3_finalize(m_insertMeta);
        m_insertMeta = nullptr;
    }
    if (m_db != nullptr) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
    m_inTransaction = false;
    m_payloadIds.clear();
    if (!m_tempDbPath.isEmpty()) {
        QFile::remove(m_tempDbPath);
        QFile::remove(m_keysPath + QLatin1String(".tmp"));
        m_tempDbPath.clear();
    }
}

} // namespace maru::dict
