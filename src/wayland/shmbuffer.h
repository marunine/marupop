// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// One shared-memory wl_buffer a zwlr_screencopy_frame_v1 is copied into, reused across grabs.
//
// zwlr_screencopy_frame_v1 sends the buffer shape it requires in its buffer event and copies
// into whatever wl_buffer the client attaches. Allocating one buffer per grab would cost one
// memfd, one mmap and one wl_shm_pool per pointer move; reuse() keeps the mapping while the
// shape is unchanged, which it is for every grab of one scan-region size on one output.
#pragma once

#include <QImage>
#include <QSize>

#include <cstddef>

struct wl_buffer;
struct wl_shm;

namespace maru::wl
{

class ShmBuffer
{
    Q_DISABLE_COPY_MOVE(ShmBuffer)

public:
    ShmBuffer();
    ~ShmBuffer();

    // Allocates a buffer of size device pixels in the wl_shm format the caller was offered, with
    // stride bytes per row. A call whose four arguments equal the current ones keeps the
    // mapping and answers true. False leaves the object empty and raises a qCWarning.
    bool reset(wl_shm *shm, QSize size, quint32 format, int stride);
    // Releases the mapping, the pool and the buffer. Called by the destructor.
    void clear();

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] wl_buffer *buffer() const;
    [[nodiscard]] QSize size() const;
    [[nodiscard]] quint32 format() const;
    [[nodiscard]] int stride() const;

    // A QImage over the mapping, without a copy: the returned image is valid while the buffer
    // holds its mapping and while no further reset() has run. QImage::copy() is what detaches
    // it. Null for a format outside the two wl_shm formats every compositor offers,
    // WL_SHM_FORMAT_XRGB8888 and WL_SHM_FORMAT_ARGB8888.
    [[nodiscard]] QImage image() const;

    // The QImage format for a wl_shm format code, or QImage::Format_Invalid. Public so a caller
    // can reject an offered format before allocating.
    [[nodiscard]] static QImage::Format imageFormatFor(quint32 shmFormat);

private:
    wl_buffer *m_buffer = nullptr;
    void *m_data = nullptr;
    std::size_t m_mappedBytes = 0;
    QSize m_size;
    quint32 m_format = 0;
    int m_stride = 0;
};

} // namespace maru::wl
