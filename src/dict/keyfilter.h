// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The per-dictionary key-hash sidecar rejects candidates before SQLite is queried.
// It stores sorted XXH3-64 hashes of distinct search keys in a read-only mapping, so a miss
// costs a binary search over clean pages. A hash collision only causes an unnecessary
// SQLite query; the store still checks the exact key.
#pragma once

#include <QString>
#include <QStringView>

#include <span>
#include <vector>

class QFile;

namespace maru::dict
{

// Written into the header so a file from another build or another byte order is rejected rather
// than mis-read. The value is the ASCII bytes "MKF1" read as a host-order quint32.
inline constexpr quint32 keyFilterMagic = 0x3146'4B4DU;

// Bumped when the header or the array layout changes.
inline constexpr quint32 keyFilterVersion = 1;

// 24 bytes, so the hash array that follows starts 8-byte aligned.
struct KeyFilterHeader
{
    quint32 magic = keyFilterMagic;
    quint32 version = keyFilterVersion;
    quint64 count = 0;        // number of quint64 hashes that follow
    quint32 maxKeyLength = 0; // longest key in UTF-16 code units
    quint32 reserved = 0;
};

static_assert(sizeof(KeyFilterHeader) == 24);

// The hash of one normalized key. Exposed so StoreWriter can hash a key once and hand the value
// to both the writer and its own bookkeeping.
[[nodiscard]] quint64 keyFilterHash(QStringView normalizedKey);

// Collects key hashes during import and writes the sidecar. Hash storage costs eight bytes
// per distinct key, plus container overhead while collecting.
class KeyFilterWriter
{
public:
    void reserve(qsizetype keyCount);

    // Adds one key. Duplicates are removed by write(), so a caller that already deduplicates its
    // keys pays nothing extra and one that does not stays correct.
    void add(QStringView normalizedKey);

    // Adds a hash keyFilterHash() already produced, together with the key length it came from.
    void addHash(quint64 hash, int keyLength);

    [[nodiscard]] qsizetype rawCount() const
    {
        return static_cast<qsizetype>(m_hashes.size());
    }

    [[nodiscard]] int maxKeyLength() const
    {
        return m_maxKeyLength;
    }

    // Sorts, deduplicates and writes the sidecar to path. Returns false when the file cannot be
    // created or the write is short. The distinct key count is available from distinctCount()
    // afterwards.
    [[nodiscard]] bool write(const QString &path);

    [[nodiscard]] qsizetype distinctCount() const
    {
        return m_distinctCount;
    }

private:
    std::vector<quint64> m_hashes;
    int m_maxKeyLength = 0;
    qsizetype m_distinctCount = 0;
};

// A mapped sidecar. The mapping is read-only and never dirtied, so its pages are shared with any
// other process holding the same dictionary and are evictable under memory pressure.
class KeyFilter
{
public:
    KeyFilter();
    ~KeyFilter();

    KeyFilter(const KeyFilter &) = delete;
    KeyFilter &operator=(const KeyFilter &) = delete;

    // Maps path. Returns false when the file is absent, shorter than its header claims, or
    // carries a different magic or version.
    [[nodiscard]] bool open(const QString &path);
    void close();

    [[nodiscard]] bool isOpen() const
    {
        return m_hashes.data() != nullptr;
    }

    // Whether the store may hold normalizedKey. False is definitive; true costs one point query
    // that may return no rows.
    [[nodiscard]] bool contains(QStringView normalizedKey) const;

    // Whether the store may hold the key that hashed to hash.
    [[nodiscard]] bool containsHash(quint64 hash) const;

    [[nodiscard]] qsizetype count() const
    {
        return static_cast<qsizetype>(m_hashes.size());
    }

    [[nodiscard]] int maxKeyLength() const
    {
        return m_maxKeyLength;
    }

private:
    QFile *m_file = nullptr;
    uchar *m_mapping = nullptr;
    std::span<const quint64> m_hashes;
    int m_maxKeyLength = 0;
};

} // namespace maru::dict
