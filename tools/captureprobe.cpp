// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// ScreenShot2 latency harness. Samples end at pipe EOF, so they include the complete
// frame transfer rather than just the D-Bus reply.
//
// It calls through maru::capture::KWinGrabber, so the numbers cover the path the application
// takes, including the concurrent drain the pipe protocol needs.
//
// KWin answers every call with NoAuthorized unless an installed desktop entry names this
// binary; tools/install-dev-desktop.sh <build-dir> writes one.
//
// Usage: marupop-captureprobe                        the case matrix
//        marupop-captureprobe X Y W H N [INTERVAL]   one CaptureArea case
#include "capture/framesource.h"
#include "capture/kwingrabber.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace maru::capture;

namespace
{

struct Sample
{
    double replyMs = 0;
    double totalMs = 0;
    double hashMs = 0;
    int width = 0;
    int height = 0;
};

struct Case
{
    QString label;
    QString method;
    QVariantList arguments;
    int iterations = 0;
    int intervalMs = 0;
};

double percentile(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    const double position = quantile * static_cast<double>(values.size() - 1);
    const auto low = static_cast<size_t>(std::floor(position));
    const size_t high = std::min(low + 1, values.size() - 1);
    return values[low] + (position - static_cast<double>(low)) * (values[high] - values[low]);
}

// One case: iterations grabs of one method, timed from the call to the assembled QImage.
class CaseRunner : public QObject
{
public:
    CaseRunner(Case scenario, QObject *parent)
        : QObject(parent)
        , m_case(std::move(scenario))
    {}

    void start(std::function<void()> done)
    {
        m_done = std::move(done);
        m_samples.clear();
        m_completed = 0;
        next();
    }

private:
    void next()
    {
        if (m_completed >= m_case.iterations) {
            report();
            m_done();
            return;
        }
        ++m_completed;
        auto *timer = new QElapsedTimer;
        timer->start();

        KWinGrabber::Options options;
        options.includeCursor = false;
        options.includeOwnWindows = false;
        options.nativeResolution = true;
        KWinGrab *grab = startCase(options);

        connect(grab, &KWinGrab::finished, this, [this, timer](const QImage &image) {
            Sample sample;
            sample.replyMs = static_cast<double>(timer->nsecsElapsed()) / 1e6;
            sample.totalMs = sample.replyMs;
            sample.width = image.width();
            sample.height = image.height();
            QElapsedTimer hashTimer;
            hashTimer.start();
            const quint64 hash = FrameSource::hashImage(image);
            sample.hashMs = static_cast<double>(hashTimer.nsecsElapsed()) / 1e6;
            if (m_completed == 1) {
                std::printf("  [%s] first: %dx%d dpr %.2f xxh3 %016llx\n",
                            qPrintable(m_case.label),
                            image.width(),
                            image.height(),
                            image.devicePixelRatio(),
                            static_cast<unsigned long long>(hash));
            }
            m_samples.push_back(sample);
            delete timer;
            schedule();
        });
        connect(grab, &KWinGrab::failed, this, [this, timer](KWinGrab::Error error, const QString &message) {
            delete timer;
            std::fprintf(
                stderr, "FAIL %s (%d): %s\n", qPrintable(m_case.label), static_cast<int>(error), qPrintable(message));
            // exit() has no Qt equivalent. The failure arrives on the Qt main thread, which is the
            // only thread this probe runs.
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            ::exit(error == KWinGrab::Error::PermissionDenied ? 3 : 1);
        });
    }

    KWinGrab *startCase(const KWinGrabber::Options &options)
    {
        if (m_case.method == QLatin1StringView("CaptureArea")) {
            const QRect area{m_case.arguments.at(0).toInt(),
                             m_case.arguments.at(1).toInt(),
                             m_case.arguments.at(2).toInt(),
                             m_case.arguments.at(3).toInt()};
            return m_grabber.captureArea(area, options);
        }
        if (m_case.method == QLatin1StringView("CaptureScreen")) {
            return m_grabber.captureScreen(m_case.arguments.at(0).toString(), options);
        }
        if (m_case.method == QLatin1StringView("CaptureActiveScreen")) {
            return m_grabber.captureActiveScreen(options);
        }
        return m_grabber.captureWorkspace(options);
    }

    void schedule()
    {
        if (m_case.intervalMs > 0) {
            QTimer::singleShot(m_case.intervalMs, this, [this] {
                next();
            });
        } else {
            next();
        }
    }

    void report() const
    {
        std::vector<double> totals;
        std::vector<double> hashes;
        totals.reserve(m_samples.size());
        hashes.reserve(m_samples.size());
        for (const Sample &sample : m_samples) {
            totals.push_back(sample.totalMs);
            hashes.push_back(sample.hashMs);
        }
        std::printf("  [%-28s] n=%zu  total ms: min %.2f  p50 %.2f  p90 %.2f  max %.2f | xxh3 p50 %.2f\n",
                    qPrintable(m_case.label),
                    m_samples.size(),
                    percentile(totals, 0),
                    percentile(totals, 0.5),
                    percentile(totals, 0.9),
                    percentile(totals, 1.0),
                    percentile(hashes, 0.5));
    }

    Case m_case;
    KWinGrabber m_grabber{this};
    std::vector<Sample> m_samples;
    int m_completed = 0;
    std::function<void()> m_done;
};

std::vector<Case> defaultCases()
{
    return {
        {.label = QStringLiteral("warmup 400x200"),
         .method = QStringLiteral("CaptureArea"),
         .arguments = {100, 100, 400, 200},
         .iterations = 5,
         .intervalMs = 0},
        {.label = QStringLiteral("2x2 px"),
         .method = QStringLiteral("CaptureArea"),
         .arguments = {100, 100, 2, 2},
         .iterations = 30,
         .intervalMs = 0},
        {.label = QStringLiteral("400x200 back-to-back"),
         .method = QStringLiteral("CaptureArea"),
         .arguments = {100, 100, 400, 200},
         .iterations = 40,
         .intervalMs = 0},
        {.label = QStringLiteral("400x200 @200ms"),
         .method = QStringLiteral("CaptureArea"),
         .arguments = {100, 100, 400, 200},
         .iterations = 15,
         .intervalMs = 200},
        {.label = QStringLiteral("1200x700 back-to-back"),
         .method = QStringLiteral("CaptureArea"),
         .arguments = {100, 100, 1200, 700},
         .iterations = 40,
         .intervalMs = 0},
        {.label = QStringLiteral("1200x700 @200ms"),
         .method = QStringLiteral("CaptureArea"),
         .arguments = {100, 100, 1200, 700},
         .iterations = 15,
         .intervalMs = 200},
        {.label = QStringLiteral("CaptureActiveScreen"),
         .method = QStringLiteral("CaptureActiveScreen"),
         .arguments = {},
         .iterations = 20,
         .intervalMs = 0},
        {.label = QStringLiteral("CaptureWorkspace"),
         .method = QStringLiteral("CaptureWorkspace"),
         .arguments = {},
         .iterations = 10,
         .intervalMs = 0},
    };
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    if (!KWinGrabber::serviceAvailable()) {
        std::fprintf(stderr, "FAIL: org.kde.KWin is not on the session bus (not a KWin Wayland session?)\n");
        return 2;
    }

    auto cases = defaultCases();
    if (argc >= 6) {
        cases = {{.label = QStringLiteral("custom"),
                  .method = QStringLiteral("CaptureArea"),
                  .arguments = {QString::fromLocal8Bit(argv[1]).toInt(),
                                QString::fromLocal8Bit(argv[2]).toInt(),
                                QString::fromLocal8Bit(argv[3]).toInt(),
                                QString::fromLocal8Bit(argv[4]).toInt()},
                  .iterations = QString::fromLocal8Bit(argv[5]).toInt(),
                  .intervalMs = argc >= 7 ? QString::fromLocal8Bit(argv[6]).toInt() : 0}};
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        const QRect geometry = screen->geometry();
        std::printf("output %s at %d,%d %dx%d scale %.2f\n",
                    qPrintable(screen->name()),
                    geometry.x(),
                    geometry.y(),
                    geometry.width(),
                    geometry.height(),
                    screen->devicePixelRatio());
    }

    auto *runners = new QObject(&app);
    auto index = std::make_shared<size_t>(0);
    auto runNext = std::make_shared<std::function<void()>>();
    *runNext = [cases, index, runners, runNext]() mutable {
        if (*index >= cases.size()) {
            QCoreApplication::quit();
            return;
        }
        auto *runner = new CaseRunner(cases[*index], runners);
        ++(*index);
        runner->start([runNext] {
            QTimer::singleShot(0, [runNext] {
                (*runNext)();
            });
        });
    };
    QTimer::singleShot(0, [runNext] {
        (*runNext)();
    });
    return QGuiApplication::exec();
}
