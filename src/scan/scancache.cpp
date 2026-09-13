// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "scan/scancache.h"

#include "ocr/resolution.h"

#include <algorithm>
#include <utility>

namespace maru::scan
{

ScanCache::ScanCache(int capacity)
    : m_capacity(std::max(capacity, 1))
{}

const CachedScan *ScanCache::find(QRect logicalRect, quint64 hash)
{
    const auto match = std::ranges::find_if(m_entries, [&](const CachedScan &entry) {
        return entry.hash == hash && entry.logicalRect == logicalRect;
    });
    if (match == m_entries.end()) {
        return nullptr;
    }
    m_entries.splice(m_entries.begin(), m_entries, match);
    return &m_entries.front();
}

void ScanCache::insert(QRect logicalRect, quint64 hash, qreal scale, ocr::Result result, QRect occluded)
{
    m_entries.remove_if([&](const CachedScan &entry) {
        return entry.hash == hash && entry.logicalRect == logicalRect;
    });
    const double extent = ocr::medianCharExtent(result);
    m_entries.push_front(CachedScan{.logicalRect = logicalRect,
                                    .hash = hash,
                                    .scale = scale,
                                    .result = std::move(result),
                                    .medianCharExtent = extent,
                                    .occluded = occluded});
    while (std::cmp_greater(m_entries.size(), m_capacity)) {
        m_entries.pop_back();
    }
}

void ScanCache::clear()
{
    m_entries.clear();
}

void ScanCache::invalidate(QRect logicalRect)
{
    if (logicalRect.isEmpty()) {
        return;
    }
    m_entries.remove_if([&](const CachedScan &entry) {
        return entry.logicalRect.intersects(logicalRect);
    });
}

int ScanCache::size() const
{
    return static_cast<int>(m_entries.size());
}

int ScanCache::capacity() const
{
    return m_capacity;
}

} // namespace maru::scan
