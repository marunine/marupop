// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// One imported dictionary on disk: a SQLite file holding the records and their search keys, plus
// the key-filter sidecar beside it. Store is the read path and StoreWriter the import path.
//
// The schema is uniform across every dictionary type, with the type-specific structure inside an
// opaque CBOR payload. Frequency dictionaries use the same encoding: a FrequencyRecord payload is a string and an
// integer, 20 bytes of CBOR for a typical entry, and one schema keeps one decoder, one writer and one dedup rule for
// every type.
#pragma once

#include "dict/dicttypes.h"
#include "dict/keyfilter.h"
#include "dict/records.h"

#include <QHash>
#include <QMutex>
#include <QString>

#include <list>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace maru::dict
{

// Bumped when the DDL, the key normalization or the payload codec changes. Written to
// PRAGMA user_version and to the meta row schema_version; Store::open() reports NeedsReimport for
// any other value, which is the whole migration strategy.
inline constexpr int storeSchemaVersion = 1;

// Meta keys StoreWriter writes and Store::meta() reads back.
namespace metakeys
{
inline constexpr QLatin1StringView schemaVersion{"schema_version"};
inline constexpr QLatin1StringView payloadCodec{"payload_codec"};
inline constexpr QLatin1StringView dictType{"dict_type"};
inline constexpr QLatin1StringView title{"title"};
inline constexpr QLatin1StringView revision{"revision"};
inline constexpr QLatin1StringView sourceUrl{"source_url"};
inline constexpr QLatin1StringView sourceFormat{"source_format"};
inline constexpr QLatin1StringView importedAt{"imported_at"};
inline constexpr QLatin1StringView recordCount{"record_count"};
inline constexpr QLatin1StringView keyCount{"key_count"};
inline constexpr QLatin1StringView maxKeyLength{"max_key_length"};
// The JMdict or JMnedict DTD entity map, as a JSON object of short name to description.
inline constexpr QLatin1StringView entities{"entities"};
// The Yomitan tag_bank contents, as a JSON object of tag name to a [category, order, notes,
// score] array.
inline constexpr QLatin1StringView tags{"tags"};
// The corpus maximum of a frequency list, used to normalize a rank for display.
inline constexpr QLatin1StringView maxFrequency{"max_frequency"};
} // namespace metakeys

// The result of opening a store file.
enum class OpenResult
{
    Ok,
    // The file is absent, unreadable, or not a marupop store.
    Failed,
    // The file is a marupop store written by a different schema or codec version. The dictionary
    // has to be imported again from its source.
    NeedsReimport,
};

// The read path over one dictionary. One sqlite3 handle, opened
// SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, and one prepared statement reused for the process
// lifetime.
//
// A Store is immutable once open() has returned OpenResult::Ok. The key filter mapping, m_meta,
// m_path, m_dictType, m_recordCount, m_keyCount and m_maxKeyLength are written during open() and
// read afterwards with no lock. m_mutex guards the sqlite3 connection and the record LRU alone:
// a NOMUTEX connection carries one statement at a time, so find() serializes the query while the
// key-filter rejection ahead of it runs concurrently on every thread. The lookup engine's
// parallelism is across dictionaries, one thread per store.
//
// The invariant is what lets DictionaryManager hand a std::shared_ptr<Store> to a pool thread:
// open() refuses a Store that is already open and close() runs from the destructor alone, so no
// thread can be inside a Store whose state another thread is tearing down. Replacing an imported
// dictionary constructs a new Store rather than reopening this one.
class Store
{
public:
    Store();
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Opens dbPath and the sidecar beside it (the same path with the .db suffix replaced by
    // .keys). A store whose sidecar is missing still opens and answers every query through
    // SQLite; only the miss rejection is lost. Reports OpenResult::Failed for a Store that is
    // already open.
    [[nodiscard]] OpenResult open(const QString &dbPath);

    [[nodiscard]] bool isOpen() const
    {
        return m_db != nullptr;
    }

    // The records stored under normalizedKey, in record id order. Empty when the key filter
    // rejects the key, which is the case for the large majority of candidate keys.
    [[nodiscard]] std::vector<std::shared_ptr<const Record>> find(QStringView normalizedKey) const;

    // The value of one meta row, empty when the row is absent.
    [[nodiscard]] QString meta(QLatin1StringView key) const;

    [[nodiscard]] DictType dictType() const
    {
        return m_dictType;
    }

    [[nodiscard]] qint64 recordCount() const
    {
        return m_recordCount;
    }

    [[nodiscard]] qint64 keyCount() const
    {
        return m_keyCount;
    }

    // The longest key the store holds, in UTF-16 code units. The lookup engine drops a candidate
    // longer than this before hashing it.
    [[nodiscard]] int maxKeyLength() const
    {
        return m_maxKeyLength;
    }

    // Number of decoded records kept alive by the LRU. Defaults to 4096.
    void setRecordCacheCapacity(qsizetype capacity);

    [[nodiscard]] QString path() const
    {
        return m_path;
    }

private:
    // Finalizes the statement, closes the connection and unmaps the sidecar. Called by the
    // destructor, and by open() to drop a partial open that a later step rejected.
    void close();

    [[nodiscard]] std::shared_ptr<const Record> decodeCached(qint64 id, const void *payload, int payloadSize) const;

    mutable QMutex m_mutex;
    sqlite3 *m_db = nullptr;
    sqlite3_stmt *m_findStatement = nullptr;
    QString m_path;
    DictType m_dictType = DictType::JMdict;
    qint64 m_recordCount = 0;
    qint64 m_keyCount = 0;
    int m_maxKeyLength = 0;
    QHash<QString, QString> m_meta;
    KeyFilter m_keyFilter;

    // Records are shared with every caller holding a Response, so eviction only drops the
    // cache's own reference.
    using CacheEntry = std::pair<qint64, std::shared_ptr<const Record>>;
    mutable std::list<CacheEntry> m_lru;
    mutable std::unordered_map<qint64, std::list<CacheEntry>::iterator> m_lruIndex;
    qsizetype m_cacheCapacity = 4096;
};

// The import path. Writes to <path>.tmp and the sidecar to <path minus .db>.keys.tmp, then
// renames both into place with rename(2), which replaces an existing target in one step, so an
// interrupted import leaves the previous store untouched.
class StoreWriter
{
public:
    StoreWriter();
    ~StoreWriter();

    StoreWriter(const StoreWriter &) = delete;
    StoreWriter &operator=(const StoreWriter &) = delete;

    // Creates the temporary database and applies the schema. meta rows are written by finish(),
    // after the counts are known; the rows given here are stored verbatim alongside them.
    [[nodiscard]] bool begin(const QString &dbPath, DictType type, const QHash<QString, QString> &meta = {});

    // Adds one meta row. Overwrites a row begin() carried.
    void setMeta(QLatin1StringView key, const QString &value);

    // Stores record and points every key in keys at it. Records are deduplicated in RAM by the
    // 128-bit hash of their payload, as JL deduplicates with its recordsToKeys map
    // (JL.Core/Dicts/JMdict/JmdictDBManager.cs), so two headwords carrying identical payloads
    // share one row and both key sets. Returns the record id, or -1 on a write failure.
    qint64 addRecord(const Record &record, std::span<const QString> keys);

    // Points existing keys at a record addRecord() already returned an id for.
    void addKeys(qint64 recordId, std::span<const QString> keys);

    [[nodiscard]] qint64 recordCount() const
    {
        return m_recordCount;
    }

    [[nodiscard]] qint64 keyCount() const
    {
        return m_keyCount;
    }

    [[nodiscard]] int maxKeyLength() const
    {
        return m_maxKeyLength;
    }

    // Commits the last transaction, writes the meta rows, runs ANALYZE, writes the sidecar and
    // renames both files into place. Returns false when the database rename fails, which leaves
    // the previous store and its sidecar in place.
    //
    // A sidecar rename that fails after the database rename succeeded returns true and removes
    // the sidecar at the target path: the new database is complete and consistent, and a store
    // with no sidecar answers every query through SQLite. Keeping the previous sidecar instead
    // would reject keys the new database holds, which is the one outcome that answers a lookup
    // wrongly rather than slowly.
    [[nodiscard]] bool finish();

    // Closes and removes the temporary files.
    void abort();

private:
    [[nodiscard]] bool exec(const char *sql);
    [[nodiscard]] bool beginTransaction();
    [[nodiscard]] bool commitTransaction();
    void maybeCommit();

    sqlite3 *m_db = nullptr;
    sqlite3_stmt *m_insertRecord = nullptr;
    sqlite3_stmt *m_insertKey = nullptr;
    sqlite3_stmt *m_insertMeta = nullptr;
    QString m_dbPath;
    QString m_tempDbPath;
    QString m_keysPath;
    DictType m_type = DictType::JMdict;
    QHash<QString, QString> m_meta;

    qint64 m_nextId = 1;
    qint64 m_recordCount = 0;
    qint64 m_keyCount = 0;
    int m_maxKeyLength = 0;
    qint64 m_keysSinceCommit = 0;
    bool m_inTransaction = false;
    bool m_failed = false;

    // 128 bits of XXH3 over the payload. A 64-bit dedup key would merge two distinct records once
    // per 3.7e7 imports of a million records; 128 bits removes the case.
    struct PayloadHash
    {
        quint64 low = 0;
        quint64 high = 0;

        [[nodiscard]] bool operator==(const PayloadHash &other) const = default;
    };

    struct PayloadHasher
    {
        [[nodiscard]] size_t operator()(const PayloadHash &value) const noexcept
        {
            return static_cast<size_t>(value.low ^ (value.high * 0x9E37'79B9'7F4A'7C15ULL));
        }
    };

    std::unordered_map<PayloadHash, qint64, PayloadHasher> m_payloadIds;
    KeyFilterWriter m_keyFilterWriter;
};

// The sidecar path for a store database path: the .db suffix replaced by .keys.
[[nodiscard]] QString keyFilterPathFor(const QString &dbPath);

// Commits a transaction and starts the next one every 200 000 keys, as JL does with
// DBUtils.TransactionBatchSize.
inline constexpr qint64 storeTransactionKeyBatch = 200'000;

} // namespace maru::dict
