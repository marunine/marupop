// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The two waits a suite with an event loop needs. Every asynchronous result in the tree arrives
// through the event loop -- a D-Bus reply, a queued invokeMethod from the OCR worker, a QTimer
// the throttle armed -- so a suite either runs the loop until a condition holds or runs it for a
// fixed time.
//
// One implementation, because a timeout convention fixed in one suite has to reach the others.
#pragma once

#include <QEventLoop>
#include <QTimer>

#include <functional>

namespace maru::test
{

// Runs the event loop until predicate answers true or timeoutMs elapses.
// An already satisfied predicate returns immediately. A timer-driven event loop
// waits for work instead of busy-spinning around processEvents(), which returns
// as soon as its queue is empty. This leaves CPU time for worker threads and D-Bus.
// The one-millisecond poll also notices predicates changed without a Qt event.
inline bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    if (predicate()) {
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, &predicate] {
        if (predicate()) {
            loop.quit();
        }
    });
    poll.start(1);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return predicate();
}

// Runs the event loop for milliseconds, for the assertion that something did not happen.
inline void pumpFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace maru::test
