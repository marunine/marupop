// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The two timers the scan loop runs on, kept apart from ScanController so both are testable
// without a capture stack behind them.
//
// Throttle is trailing-edge rather than leading-edge-only: a pointer that stops moving inside
// the throttle window still gets one scan, which is the case that matters most because it is
// exactly what a reader does before looking a word up. Coalescing pointer events limits
// recognition work while preserving a final scan at the settled position.
#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

namespace maru::scan
{

// Fires triggered() at most once per interval: immediately when the previous fire is older
// than the interval, and otherwise once at the end of the current window, with every request
// arriving meanwhile coalesced into that one fire.
class Throttle : public QObject
{
    Q_OBJECT

public:
    // Milliseconds on a monotonic scale. The default reads QDateTime::currentMSecsSinceEpoch();
    // a test substitutes a counter so the elapsed-time decisions stop depending on wall time.
    using Clock = std::function<qint64()>;

    explicit Throttle(QObject *parent = nullptr);
    ~Throttle() override;

    void setClock(Clock clock);
    // Values below 1 ms are clamped to 1 ms: a zero interval would make every request fire and
    // turn the 8 ms pointer pump into an 8 ms scan loop.
    void setIntervalMs(int intervalMs);
    [[nodiscard]] int intervalMs() const;

    void request();
    // Drops a scheduled trailing fire. The next request() starts a new window.
    void cancel();
    // True while a trailing fire is scheduled.
    [[nodiscard]] bool isPending() const;

Q_SIGNALS:
    void triggered();

private:
    void fire();

    Clock m_clock;
    QTimer m_timer;
    int m_intervalMs = 200;
    qint64 m_lastFireMs = 0;
    bool m_hasFired = false;
};

// A QTimer that emits tick(), so ScanController holds one type for both its timers and a test
// can drive the poll without reaching into a private member.
class PeriodicPoller : public QObject
{
    Q_OBJECT

public:
    explicit PeriodicPoller(QObject *parent = nullptr);
    ~PeriodicPoller() override;

    // Applied to a running timer immediately, which is what makes a settings change take
    // effect without a scanning toggle.
    void setIntervalMs(int intervalMs);
    [[nodiscard]] int intervalMs() const;

    // Starts the timer, or restarts a running one from zero, which is how ScanController counts
    // the interval from the last scan that returned.
    void start();
    void stop();
    [[nodiscard]] bool isActive() const;

Q_SIGNALS:
    void tick();

private:
    QTimer m_timer;
};

} // namespace maru::scan
