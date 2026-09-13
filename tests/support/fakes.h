// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Shared doubles for ocr::Backend, cursor::CursorTracker and cursor::LockWatcher.
// The OCR and scan suites use the same interface behavior and field names.
// capture::FakeFrameSource is declared in capture/framesource.h.
#pragma once

#include "cursor/cursortracker.h"
#include "cursor/lockwatcher.h"
#include "ocr/backend.h"

#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSemaphore>
#include <QSize>
#include <QString>
#include <QThread>

#include <atomic>

namespace maru::test
{

// Emits positions on demand, at whatever rate the caller chooses. KWinScriptRelay emits at
// 8 ms intervals while the pointer moves.
class FakeTracker : public cursor::CursorTracker
{
public:
    // Emits positionChanged(logical, screen), defaulting screen to the primary QScreen.
    void move(QPoint logical, QScreen *screen = nullptr)
    {
        Q_EMIT positionChanged(logical, screen != nullptr ? screen : QGuiApplication::primaryScreen());
    }
};

// The lock state without a session bus behind it. cursor::LockWatcher::isLocked() is not
// virtual, so this double drives the base class through its lockedChanged signal, which is the
// only member scan::ScanController connects to.
class FakeLock : public cursor::LockWatcher
{
public:
    // The protected base helper, exposed: it records the state and emits lockedChanged() for a
    // change, so a caller drives isLocked() and the signal together.
    using cursor::LockWatcher::setLockedState;

    // No source to re-read. The count is what a suite asserts a query was requested by.
    void query() override
    {
        ++queries;
    }

    int queries = 0;
};

// One horizontal line of a configurable string, with character boxes of a configurable size laid
// out left to right from origin. recognize() runs on the marupop-ocr thread, so a caller assigns
// every field before starting the scan that reads it.
class FakeBackend : public ocr::Backend
{
public:
    [[nodiscard]] QString name() const override
    {
        return backendName;
    }

    bool initialize() override
    {
        m_ready = true;
        return true;
    }

    [[nodiscard]] bool isReady() const override
    {
        return m_ready;
    }

    // Whether every pixel of box inside image is black. A box no part of the image holds counts as
    // black: a detector reports nothing outside the frame it was given either.
    [[nodiscard]] static bool boxIsBlack(const QImage &image, QRect box)
    {
        if (image.isNull()) {
            return false;
        }
        const QRect area = box.intersected(image.rect());
        if (area.isEmpty()) {
            return true;
        }
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                if (image.pixelColor(x, y) != QColor(Qt::black)) {
                    return false;
                }
            }
        }
        return true;
    }

    ocr::Result recognize(const QImage &image) override
    {
        workerThread = QThread::currentThread();
        // Released on every call, blocking or not, so a caller waiting for a recognition to start
        // observes it without a timing assumption. A caller that starts waiting later drains the
        // accumulated count first.
        entered.release(1);
        if (blocking) {
            released.acquire(1);
        }
        ++calls;

        ocr::Result result;
        result.success = true;
        result.backendName = backendName;
        result.sourceSize = image.size();
        if (empty || text.isEmpty()) {
            return result;
        }

        ocr::TextLine line;
        QString recognized;
        for (int index = 0; index < text.size(); ++index) {
            const QRect box{origin.x() + (index * charSize.width()), origin.y(), charSize.width(), charSize.height()};
            if (dropsBoxesOnBlackPixels && boxIsBlack(image, box)) {
                continue;
            }
            recognized.append(text.at(index));
            line.chars.append(ocr::CharBox{
                .codePoint = static_cast<char32_t>(text.at(index).unicode()), .box = box, .confidence = 1.0F});
            line.box = line.box.united(box);
        }
        // The line is dropped rather than carried empty, which is what a detector that found no
        // character does. The text is rebuilt from the boxes that survived, because every backend
        // has to keep text.size() == chars.size() -- the hit test indexes the two with one index.
        if (recognized.isEmpty()) {
            return result;
        }
        line.text = recognized;
        line.confidence = 1.0F;
        result.lines.append(line);
        return result;
    }

    // Every field below is read on the marupop-ocr thread inside recognize() and none is atomic, so
    // each has to hold its final value before the scan that reads it starts. Assigning one while a
    // recognition may be running races on a plain bool and on the QString and QSize reference
    // counts. The two semaphores are the synchronised part, and they are what a case uses to order
    // itself against the worker.
    //
    // The recognized string, one QChar per box, and one box per cell of charSize. The default is
    // six characters, which is wider than any single lookup the suites assert on.
    QString text = QStringLiteral("日本語を読む");
    QPoint origin;
    QSize charSize{20, 20};
    bool empty = false;
    bool blocking = false;
    // Whether a character box lying on black pixels is dropped, the way a real detector finds
    // nothing in a uniform region. Off by default: the layout is the point of this double, and a
    // case that sets no image would otherwise lose every box. A case that needs the region
    // capture::FakeFrameSource blacks out for Frame::occluded to reach the recognition pass as
    // the compositor delivers it -- which is what the `no_screen_share` layer rule produces --
    // turns it on.
    bool dropsBoxesOnBlackPixels = false;
    QString backendName = QStringLiteral("fake");

    // entered is released once per recognize() entry. released is acquired once before
    // recognize() returns while blocking is true, which is how a test holds a recognition open
    // across a superseding request.
    QSemaphore entered;
    QSemaphore released;
    std::atomic<int> calls{0};
    // The thread recognize() last ran on, which ocr::OcrService names marupop-ocr.
    std::atomic<QThread *> workerThread{nullptr};

private:
    bool m_ready = false;
};

} // namespace maru::test
