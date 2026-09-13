// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "core/enums.h"
#include "ocr/ocrtypes.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace maru::ocr
{

class Backend;
class OcrWorker;

// Recognition on one dedicated worker thread named marupop-ocr, with a queue that holds one
// request. A request arriving while another one runs replaces whatever was queued, because the
// scan loop only ever wants the newest frame; the running request still delivers.
//
// The single thread is a requirement rather than a simplification: Chrome Screen AI's
// PerformOCR drops results when two calls overlap, and two concurrent meikiocr inferences at 6
// intra-op threads each oversubscribe the machine and slow both down.
class OcrService : public QObject
{
    Q_OBJECT

public:
    explicit OcrService(QObject *parent = nullptr);
    ~OcrService() override;

    // Starts the worker thread and loads the backend the current settings select, which costs
    // about 0.3 s for meikiocr and happens off the calling thread.
    void start();

    // Selects the backend. Automatic resolves to meikiocr where the models are present, to
    // Screen AI where its component is installed, and to no backend otherwise.
    void setEngine(OcrEngine engine);
    [[nodiscard]] OcrEngine engine() const;

    // The name of the backend that loaded, empty while none has. Callable from any thread.
    [[nodiscard]] QString activeBackendName() const;

    // The backends that could load right now, by display name: meikiocr where the models are
    // present, Chrome Screen AI where its component is installed. Both read the settings, so
    // both are called from the thread that owns them.
    [[nodiscard]] static QStringList availableBackends();
    // The backend engine resolves to right now, empty where none can load. This is what the
    // settings dialog shows beside the Automatic entry.
    [[nodiscard]] static QString backendNameFor(OcrEngine engine);
    [[nodiscard]] bool isReady() const;

    // callback runs on the calling thread once the worker is done. A callback whose request
    // was still queued when a newer one arrived is dropped; a callback whose service was
    // destroyed meanwhile is dropped as well.
    void recognize(const QImage &image, std::function<void(Result)> callback);

    // Installs a backend directly, which is what a test injects a fake through. The next
    // setEngine() call replaces it.
    void setBackend(std::unique_ptr<Backend> backend);

Q_SIGNALS:
    void backendChanged(const QString &name);
    void availabilityChanged(bool ready);

private:
    struct Pending
    {
        QImage image;
        std::function<void(Result)> callback;
        QThread *thread = nullptr;
    };

    void drain();
    void publish(const QString &name, bool ready);

    QThread *m_thread = nullptr;
    OcrWorker *m_worker = nullptr;
    // Reset to 0 by the destructor: a callback still in flight sees the sentinel and returns
    // without touching the destroyed service.
    std::shared_ptr<std::atomic<quint64>> m_generation;

    mutable QMutex m_mutex;
    std::optional<Pending> m_pending;
    QString m_backendName;
    bool m_ready = false;
    OcrEngine m_engine = OcrEngine::Automatic;
};

} // namespace maru::ocr
