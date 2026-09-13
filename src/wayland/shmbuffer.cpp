// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "wayland/shmbuffer.h"

#include "core/logging.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

namespace maru::wl
{

namespace
{

// An anonymous file of size bytes, sealed against a shrink so the compositor's own mapping stays
// valid. memfd_create is Linux-only, which the whole application is.
int createAnonymousFile(std::size_t size)
{
    const int fd = memfd_create("marupop-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        qCWarning(logWayland) << "memfd_create failed:" << qt_error_string(errno);
        return -1;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        qCWarning(logWayland) << "ftruncate to" << size << "bytes failed:" << qt_error_string(errno);
        close(fd);
        return -1;
    }
    // F_SEAL_SHRINK is what lets the compositor map the file without a race against a client
    // that truncates it; wl_shm requires the pool to keep at least the size it was created with.
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
    return fd;
}

} // namespace

ShmBuffer::ShmBuffer() = default;

ShmBuffer::~ShmBuffer()
{
    clear();
}

QImage::Format ShmBuffer::imageFormatFor(quint32 shmFormat)
{
    // wl_shm's two mandatory formats are little-endian packed 32-bit words, which is the byte
    // order QImage::Format_RGB32 and QImage::Format_ARGB32_Premultiplied have on a little-endian
    // host. wl_shm ARGB8888 is premultiplied, which is what the second maps to.
    switch (shmFormat) {
    case WL_SHM_FORMAT_XRGB8888:
        return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888:
        return QImage::Format_ARGB32_Premultiplied;
    default:
        return QImage::Format_Invalid;
    }
}

bool ShmBuffer::reset(wl_shm *shm, QSize size, quint32 format, int stride)
{
    if (shm == nullptr || size.isEmpty() || stride < size.width() * 4) {
        qCWarning(logWayland) << "refusing a shm buffer of" << size << "at stride" << stride;
        return false;
    }
    if (m_buffer != nullptr && m_size == size && m_format == format && m_stride == stride) {
        return true;
    }
    clear();

    const auto bytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(size.height());
    const int fd = createAnonymousFile(bytes);
    if (fd < 0) {
        return false;
    }
    void *data = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        qCWarning(logWayland) << "mmap of" << bytes << "bytes failed:" << qt_error_string(errno);
        close(fd);
        return false;
    }

    wl_shm_pool *pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(bytes));
    m_buffer = wl_shm_pool_create_buffer(pool, 0, size.width(), size.height(), stride, format);
    // The pool holds no reference the buffer needs: wl_shm keeps the mapping alive for the
    // buffers already created from it.
    wl_shm_pool_destroy(pool);
    close(fd);

    m_data = data;
    m_mappedBytes = bytes;
    m_size = size;
    m_format = format;
    m_stride = stride;
    return true;
}

void ShmBuffer::clear()
{
    if (m_buffer != nullptr) {
        wl_buffer_destroy(m_buffer);
        m_buffer = nullptr;
    }
    if (m_data != nullptr) {
        munmap(m_data, m_mappedBytes);
        m_data = nullptr;
    }
    m_mappedBytes = 0;
    m_size = QSize();
    m_format = 0;
    m_stride = 0;
}

bool ShmBuffer::isValid() const
{
    return m_buffer != nullptr;
}

wl_buffer *ShmBuffer::buffer() const
{
    return m_buffer;
}

QSize ShmBuffer::size() const
{
    return m_size;
}

quint32 ShmBuffer::format() const
{
    return m_format;
}

int ShmBuffer::stride() const
{
    return m_stride;
}

QImage ShmBuffer::image() const
{
    const QImage::Format format = imageFormatFor(m_format);
    if (m_data == nullptr || format == QImage::Format_Invalid) {
        return {};
    }
    return {static_cast<const uchar *>(m_data), m_size.width(), m_size.height(), m_stride, format};
}

} // namespace maru::wl
