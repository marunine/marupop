// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictionaryimportjob.h"

#include "core/logging.h"
#include "dict/store.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <KLocalizedString>

namespace maru::dict
{

// Runs the importer on the worker thread. The progress callback the importer receives posts to the
// worker's own queue, so the callback itself never touches a GUI object; the job connects to
// progressed() with the default automatic connection, which is queued across the thread boundary.
class ImportWorker : public QObject
{
    Q_OBJECT

public:
    ImportWorker(Importer *importer,
                 QString source,
                 QString databasePath,
                 DictType type,
                 QHash<QString, QString> meta,
                 std::shared_ptr<std::atomic_bool> cancel)
        : m_importer(importer)
        , m_source(std::move(source))
        , m_databasePath(std::move(databasePath))
        , m_type(type)
        , m_meta(std::move(meta))
        , m_cancel(std::move(cancel))
    {}

public Q_SLOTS:

    void work()
    {
        ImportResult result;
        StoreWriter writer;
        if (!writer.begin(m_databasePath, m_type, m_meta)) {
            result.errorString = QStringLiteral("Cannot create the dictionary database at %1").arg(m_databasePath);
            Q_EMIT finished(result);
            return;
        }

        result = m_importer->import(
            m_source,
            writer,
            [this](int percent, const QString &message) {
                Q_EMIT progressed(percent, message);
            },
            *m_cancel);
        Q_EMIT finished(result);
    }

    // moc names a signal's parameters _t1 and _t2 in the definition it generates, so a named
    // declaration disagrees with it. The names are given as comments instead.
Q_SIGNALS:
    void progressed(int /*percent*/, const QString & /*message*/);
    void finished(const maru::dict::ImportResult & /*result*/);

private:
    Importer *m_importer;
    QString m_source;
    QString m_databasePath;
    DictType m_type;
    QHash<QString, QString> m_meta;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

DictionaryImportJob::DictionaryImportJob(
    std::unique_ptr<Importer> importer, QString source, QString databasePath, DictType type, QObject *parent)
    : KJob(parent)
    , m_importer(std::move(importer))
    , m_source(std::move(source))
    , m_databasePath(std::move(databasePath))
    , m_type(type)
    , m_cancel(std::make_shared<std::atomic_bool>(false))
{
    qRegisterMetaType<maru::dict::ImportResult>();
    setCapabilities(Killable);
}

DictionaryImportJob::~DictionaryImportJob()
{
    if (m_thread != nullptr) {
        m_cancel->store(true, std::memory_order_relaxed);
        m_thread->quit();
        m_thread->wait();
    }
}

void DictionaryImportJob::setStoreMeta(const QHash<QString, QString> &meta)
{
    m_meta = meta;
}

void DictionaryImportJob::start()
{
    QTimer::singleShot(0, this, &DictionaryImportJob::run);
}

void DictionaryImportJob::run()
{
    setTotalAmount(Items, 100);

    m_thread = new QThread(this);
    m_worker = new ImportWorker(m_importer.get(), m_source, m_databasePath, m_type, m_meta, m_cancel);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &ImportWorker::work);
    connect(m_worker, &ImportWorker::progressed, this, [this](int percent, const QString &message) {
        setProcessedAmount(Items, static_cast<qulonglong>(percent));
        setPercent(static_cast<unsigned long>(percent));
        Q_EMIT progress(percent, message);
    });
    connect(m_worker, &ImportWorker::finished, this, &DictionaryImportJob::onWorkerFinished);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

void DictionaryImportJob::onWorkerFinished(const ImportResult &result)
{
    m_result = result;
    m_worker = nullptr;
    if (m_thread != nullptr) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }

    if (result.cancelled) {
        setError(KilledJobError);
        emitResult();
        return;
    }
    if (!result.ok) {
        setError(ImportFailed);
        setErrorText(result.errorString.isEmpty() ? i18n("Cannot import the dictionary") : result.errorString);
        emitResult();
        return;
    }

    setPercent(100);
    qCInfo(logDictImport,
           "%s imported %lld records and %lld keys into %s",
           qUtf8Printable(m_importer->name()),
           static_cast<long long>(result.recordCount),
           static_cast<long long>(result.keyCount),
           qUtf8Printable(m_databasePath));
    emitResult();
}

bool DictionaryImportJob::doKill()
{
    m_cancel->store(true, std::memory_order_relaxed);
    if (m_thread != nullptr) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    }
    return true;
}

} // namespace maru::dict

#include "dictionaryimportjob.moc"
