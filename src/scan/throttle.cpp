// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "scan/throttle.h"

#include <QDateTime>

#include <algorithm>

namespace maru::scan
{

Throttle::Throttle(QObject *parent)
    : QObject(parent)
    , m_clock([] {
        return QDateTime::currentMSecsSinceEpoch();
    })
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Throttle::fire);
}

Throttle::~Throttle() = default;

void Throttle::setClock(Clock clock)
{
    if (clock) {
        m_clock = std::move(clock);
    }
}

void Throttle::setIntervalMs(int intervalMs)
{
    m_intervalMs = std::max(intervalMs, 1);
}

int Throttle::intervalMs() const
{
    return m_intervalMs;
}

void Throttle::request()
{
    if (m_timer.isActive()) {
        // A trailing fire is already scheduled, and it will carry this request too. Restarting
        // it here would turn the throttle into a debounce and starve a pointer that never
        // stops moving.
        return;
    }

    const qint64 now = m_clock();
    const qint64 sinceLastFire = now - m_lastFireMs;
    if (!m_hasFired || sinceLastFire >= m_intervalMs) {
        fire();
        return;
    }

    const qint64 remaining = m_intervalMs - sinceLastFire;
    m_timer.start(static_cast<int>(remaining));
}

void Throttle::cancel()
{
    m_timer.stop();
}

bool Throttle::isPending() const
{
    return m_timer.isActive();
}

void Throttle::fire()
{
    m_timer.stop();
    m_lastFireMs = m_clock();
    m_hasFired = true;
    Q_EMIT triggered();
}

PeriodicPoller::PeriodicPoller(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(false);
    // A coarse timer, because the poll only asks whether the region still hashes the same and
    // is allowed to drift into a neighbouring wakeup: at the default 2000 ms interval Qt's
    // coarse timer saves the wakeups a precise one would force.
    m_timer.setTimerType(Qt::CoarseTimer);
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &PeriodicPoller::tick);
}

PeriodicPoller::~PeriodicPoller() = default;

void PeriodicPoller::setIntervalMs(int intervalMs)
{
    m_timer.setInterval(std::max(intervalMs, 1));
}

int PeriodicPoller::intervalMs() const
{
    return m_timer.interval();
}

void PeriodicPoller::start()
{
    m_timer.start();
}

void PeriodicPoller::stop()
{
    m_timer.stop();
}

bool PeriodicPoller::isActive() const
{
    return m_timer.isActive();
}

} // namespace maru::scan
