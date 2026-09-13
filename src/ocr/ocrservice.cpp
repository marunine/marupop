// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/ocrservice.h"

#include "core/logging.h"
#include "core/settings.h"
#include "ocr/backend.h"
#include "ocr/grouping.h"
#include "ocr/meikiocrbackend.h"
#include "ocr/screenaibackend.h"

#include <QAbstractEventDispatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QThread>

#include <KLocalizedString>

namespace maru::ocr
{

// Lives on the marupop-ocr thread and owns both backends. Settings are read on the caller's
// thread and passed in, because KConfig is not safe to read from two threads.
class OcrWorker : public QObject
{
public:
    struct Configuration
    {
        OcrEngine engine = OcrEngine::Automatic;
        QString modelDirectory;
        QString screenAiResourcesDir;
        MeikiOcrBackend::Options meikiOptions;
    };

    void configure(const Configuration &configuration)
    {
        m_injected.reset();
        m_active = nullptr;
        switch (configuration.engine) {
        case OcrEngine::MeikiOcr:
            m_active = loadMeiki(configuration);
            break;
        case OcrEngine::ScreenAi:
            m_active = loadScreenAi(configuration);
            break;
        case OcrEngine::Automatic:
            // meikiocr first: it reports per-character boxes for vertical text, which is what
            // a manga or a visual novel needs, and it needs no proprietary component.
            if (MeikiOcrBackend::modelsPresent(configuration.modelDirectory)) {
                m_active = loadMeiki(configuration);
            }
            if (m_active == nullptr && ScreenAiBackend::isInstalled(configuration.screenAiResourcesDir)) {
                m_active = loadScreenAi(configuration);
            }
            break;
        }
    }

    void install(std::unique_ptr<Backend> backend)
    {
        m_injected = std::move(backend);
        m_active = nullptr;
        if (m_injected && m_injected->initialize()) {
            m_active = m_injected.get();
        }
    }

    [[nodiscard]] QString backendName() const
    {
        return m_active != nullptr ? m_active->name() : QString{};
    }

    [[nodiscard]] bool isReady() const
    {
        return m_active != nullptr && m_active->isReady();
    }

    [[nodiscard]] Result run(const QImage &image)
    {
        if (m_active == nullptr) {
            Result result;
            result.sourceSize = image.size();
            result.errorMessage =
                i18nc("@info", "Text recognition unavailable. Configure a recognition engine in Settings.");
            return result;
        }
        Result result = m_active->recognize(image);
        // Both backends report lines; the grouping is one implementation shared by both.
        result.paragraphs = groupLines(result.lines, result.sourceSize);
        return result;
    }

private:
    Backend *loadMeiki(const Configuration &configuration)
    {
        m_meiki = std::make_unique<MeikiOcrBackend>(configuration.modelDirectory, configuration.meikiOptions);
        if (!m_meiki->initialize()) {
            m_meiki.reset();
            return nullptr;
        }
        return m_meiki.get();
    }

    Backend *loadScreenAi(const Configuration &configuration)
    {
        if (!m_screenAi) {
            m_screenAi = std::make_unique<ScreenAiBackend>();
        }
        m_screenAi->setResourcesDir(configuration.screenAiResourcesDir);
        if (!m_screenAi->initialize()) {
            return nullptr;
        }
        return m_screenAi.get();
    }

    std::unique_ptr<MeikiOcrBackend> m_meiki;
    std::unique_ptr<ScreenAiBackend> m_screenAi;
    std::unique_ptr<Backend> m_injected;
    Backend *m_active = nullptr;
};

OcrService::OcrService(QObject *parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_worker(new OcrWorker)
    , m_generation(std::make_shared<std::atomic<quint64>>(1))
{
    m_thread->setObjectName(QStringLiteral("marupop-ocr"));
    m_worker->moveToThread(m_thread);
    m_thread->start();
    m_engine = settings::ocrEngine();
}

OcrService::~OcrService()
{
    // The sentinel every in-flight callback tests before it runs.
    m_generation->store(0);
    m_thread->quit();
    // A recognition pass takes up to 150 ms on a dense page; the wait lets it unwind so the
    // Screen AI component is never torn down mid-call.
    if (!m_thread->wait(5000)) {
        qCWarning(logOcr) << "the OCR thread did not stop in time";
        m_thread->terminate();
        m_thread->wait();
    }
    delete m_worker;
    delete m_thread;
}

void OcrService::start()
{
    setEngine(settings::ocrEngine());
}

OcrEngine OcrService::engine() const
{
    QMutexLocker lock(&m_mutex);
    return m_engine;
}

void OcrService::setEngine(OcrEngine engine)
{
    {
        QMutexLocker lock(&m_mutex);
        m_engine = engine;
    }
    // Settings are read here, on the caller's thread, and handed to the worker as values.
    OcrWorker::Configuration configuration;
    configuration.engine = engine;
    configuration.modelDirectory = settings::modelDirectory();
    configuration.screenAiResourcesDir = settings::screenAiResourcesDir();
    configuration.meikiOptions = MeikiOcrBackend::optionsFromSettings();

    QMetaObject::invokeMethod(
        m_worker,
        [this, configuration] {
            m_worker->configure(configuration);
            const QString name = m_worker->backendName();
            const bool ready = m_worker->isReady();
            QMetaObject::invokeMethod(
                this,
                [this, name, ready] {
                    publish(name, ready);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void OcrService::setBackend(std::unique_ptr<Backend> backend)
{
    Backend *raw = backend.release();
    // Blocking, so a test can call recognize() on the next line and know which backend runs.
    QMetaObject::invokeMethod(
        m_worker,
        [this, raw] {
            m_worker->install(std::unique_ptr<Backend>(raw));
        },
        Qt::BlockingQueuedConnection);
    const QString name = m_worker->backendName();
    const bool ready = m_worker->isReady();
    publish(name, ready);
}

void OcrService::publish(const QString &name, bool ready)
{
    bool nameChanged = false;
    bool readyChanged = false;
    {
        QMutexLocker lock(&m_mutex);
        nameChanged = m_backendName != name;
        readyChanged = m_ready != ready;
        m_backendName = name;
        m_ready = ready;
    }
    if (nameChanged) {
        Q_EMIT backendChanged(name);
    }
    if (readyChanged) {
        Q_EMIT availabilityChanged(ready);
    }
}

QStringList OcrService::availableBackends()
{
    QStringList backends;
    if (MeikiOcrBackend::modelsPresent(settings::modelDirectory())) {
        backends.append(MeikiOcrBackend::displayName());
    }
    if (ScreenAiBackend::isInstalled(settings::screenAiResourcesDir())) {
        backends.append(ScreenAiBackend::displayName());
    }
    return backends;
}

QString OcrService::backendNameFor(OcrEngine engine)
{
    switch (engine) {
    case OcrEngine::MeikiOcr:
        return MeikiOcrBackend::modelsPresent(settings::modelDirectory()) ? MeikiOcrBackend::displayName() : QString{};
    case OcrEngine::ScreenAi:
        return ScreenAiBackend::isInstalled(settings::screenAiResourcesDir()) ? ScreenAiBackend::displayName()
                                                                              : QString{};
    case OcrEngine::Automatic:
        return availableBackends().value(0);
    }
    return {};
}

QString OcrService::activeBackendName() const
{
    QMutexLocker lock(&m_mutex);
    return m_backendName;
}

bool OcrService::isReady() const
{
    QMutexLocker lock(&m_mutex);
    return m_ready;
}

void OcrService::recognize(const QImage &image, std::function<void(Result)> callback)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_pending.has_value()) {
            qCDebug(logOcr) << "dropping a queued recognition request in favour of a newer frame";
        }
        m_pending = Pending{.image = image, .callback = std::move(callback), .thread = QThread::currentThread()};
    }
    QMetaObject::invokeMethod(
        m_worker,
        [this] {
            drain();
        },
        Qt::QueuedConnection);
}

void OcrService::drain()
{
    Pending request;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_pending.has_value()) {
            // A newer request was already taken by an earlier drain call.
            return;
        }
        request = std::move(*m_pending);
        m_pending.reset();
    }

    Result result = m_worker->run(request.image);
    if (m_generation->load() == 0) {
        return;
    }
    if (!request.callback) {
        return;
    }

    // The dispatcher of the calling thread is a QObject living in that thread, which is what
    // makes the queued call land there rather than on the service's own thread.
    QObject *context = QAbstractEventDispatcher::instance(request.thread);
    if (context == nullptr) {
        context = this;
    }
    QMetaObject::invokeMethod(
        context,
        [generation = m_generation, callback = std::move(request.callback), result = std::move(result)] {
            if (generation->load() == 0) {
                return;
            }
            callback(result);
        },
        Qt::QueuedConnection);
}

} // namespace maru::ocr
