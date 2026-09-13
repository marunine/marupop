// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "keyfilter.h"

#include "core/logging.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <xxhash.h>

namespace maru::dict
{

namespace
{

// Keys are hashed over their UTF-8 bytes, which is the same representation sqlite3_bind_text
// receives, so the sidecar and the search_key column agree on what a key is without a second
// encoding rule.
quint64 hashUtf8(const QByteArray &utf8)
{
    return XXH3_64bits(utf8.constData(), static_cast<size_t>(utf8.size()));
}

} // namespace

quint64 keyFilterHash(QStringView normalizedKey)
{
    return hashUtf8(normalizedKey.toUtf8());
}

void KeyFilterWriter::reserve(qsizetype keyCount)
{
    if (keyCount > 0)
        m_hashes.reserve(static_cast<size_t>(keyCount));
}

void KeyFilterWriter::add(QStringView normalizedKey)
{
    addHash(keyFilterHash(normalizedKey), static_cast<int>(normalizedKey.size()));
}

void KeyFilterWriter::addHash(quint64 hash, int keyLength)
{
    m_hashes.push_back(hash);
    m_maxKeyLength = std::max(m_maxKeyLength, keyLength);
}

bool KeyFilterWriter::write(const QString &path)
{
    std::ranges::sort(m_hashes);
    const auto duplicates = std::ranges::unique(m_hashes);
    m_hashes.erase(duplicates.begin(), duplicates.end());
    m_distinctCount = static_cast<qsizetype>(m_hashes.size());

    // QSaveFile renames into place only on commit(), so an interrupted write leaves the previous
    // sidecar rather than a truncated one.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(logDict) << "Cannot write the key filter" << path << file.errorString();
        return false;
    }

    KeyFilterHeader header;
    header.count = static_cast<quint64>(m_hashes.size());
    header.maxKeyLength = static_cast<quint32>(m_maxKeyLength);
    if (file.write(reinterpret_cast<const char *>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header))) {
        qCWarning(logDict) << "Short write of the key filter header" << path;
        return false;
    }

    const auto payloadBytes = static_cast<qint64>(m_hashes.size()) * static_cast<qint64>(sizeof(quint64));
    if (payloadBytes > 0 && file.write(reinterpret_cast<const char *>(m_hashes.data()), payloadBytes) != payloadBytes) {
        qCWarning(logDict) << "Short write of the key filter body" << path;
        return false;
    }

    if (!file.commit()) {
        qCWarning(logDict) << "Cannot commit the key filter" << path << file.errorString();
        return false;
    }
    return true;
}

KeyFilter::KeyFilter() = default;

KeyFilter::~KeyFilter()
{
    close();
}

bool KeyFilter::open(const QString &path)
{
    close();

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qCWarning(logDict) << "Cannot open the key filter" << path << file->errorString();
        return false;
    }

    const qint64 size = file->size();
    if (std::cmp_less(size, sizeof(KeyFilterHeader))) {
        qCWarning(logDict) << "Key filter" << path << "is shorter than its header";
        return false;
    }

    uchar *mapping = file->map(0, size);
    if (mapping == nullptr) {
        qCWarning(logDict) << "Cannot map the key filter" << path << file->errorString();
        return false;
    }

    KeyFilterHeader header{};
    std::memcpy(&header, mapping, sizeof(header));
    // The count is tested against the bytes the file has rather than by computing the size the
    // count implies: sizeof(KeyFilterHeader) + count * sizeof(quint64) is 64-bit unsigned
    // arithmetic, and a corrupt count of 2^61 wraps that product to 0, which passes a size
    // comparison and yields a span over unmapped memory. size is at least sizeof(KeyFilterHeader)
    // here, so the subtraction cannot underflow.
    const quint64 available = (static_cast<quint64>(size) - sizeof(KeyFilterHeader)) / sizeof(quint64);
    if (header.magic != keyFilterMagic || header.version != keyFilterVersion || header.count > available) {
        qCWarning(logDict,
                  "Key filter %s carries magic 0x%08x version %u count %llu over %lld bytes",
                  qUtf8Printable(path),
                  header.magic,
                  header.version,
                  static_cast<unsigned long long>(header.count),
                  static_cast<long long>(size));
        file->unmap(mapping);
        return false;
    }

    m_file = file.release();
    m_mapping = mapping;
    m_hashes = std::span<const quint64>(reinterpret_cast<const quint64 *>(mapping + sizeof(KeyFilterHeader)),
                                        static_cast<size_t>(header.count));
    m_maxKeyLength = static_cast<int>(header.maxKeyLength);
    return true;
}

void KeyFilter::close()
{
    if (m_file != nullptr) {
        if (m_mapping != nullptr)
            m_file->unmap(m_mapping);
        delete m_file;
    }
    m_file = nullptr;
    m_mapping = nullptr;
    m_hashes = {};
    m_maxKeyLength = 0;
}

bool KeyFilter::contains(QStringView normalizedKey) const
{
    if (m_hashes.empty())
        return false;
    if (m_maxKeyLength > 0 && normalizedKey.size() > static_cast<qsizetype>(m_maxKeyLength))
        return false;
    return containsHash(keyFilterHash(normalizedKey));
}

bool KeyFilter::containsHash(quint64 hash) const
{
    return std::ranges::binary_search(m_hashes, hash);
}

} // namespace maru::dict
