// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// One dictionary import, run on a worker thread and reported as a KJob.
// The manager provides progress and cancellation; KIO::getJobTracker() also exposes
// the job in the Plasma notification area.
#pragma once

#include "dict/dicttypes.h"
#include "dict/importers/importer.h"

#include <QHash>
#include <QString>

#include <KJob>

#include <atomic>
#include <memory>

class QThread;

namespace maru::dict
{

class ImportWorker;

class DictionaryImportJob : public KJob
{
    Q_OBJECT

public:
    enum Error
    {
        ImportFailed = UserDefinedError + 1,
    };

    // importer is taken over by the job and destroyed with it, after the worker thread has
    // finished. source is the file or directory to read and databasePath the store to write.
    DictionaryImportJob(std::unique_ptr<Importer> importer,
                        QString source,
                        QString databasePath,
                        DictType type,
                        QObject *parent = nullptr);
    ~DictionaryImportJob() override;

    // Meta rows written into the store beside the ones the importer supplies: the display name,
    // the revision and the source URL of the dictionary.
    void setStoreMeta(const QHash<QString, QString> &meta);

    void start() override;

    [[nodiscard]] const ImportResult &result() const
    {
        return m_result;
    }

    // The importer the job runs, valid until the job is destroyed. A JMdict import leaves its
    // word-class table on it, which the manager saves after the job succeeds.
    [[nodiscard]] Importer *importer() const
    {
        return m_importer.get();
    }

Q_SIGNALS:
    // Emitted on the job's own thread as the import progresses. percent is also reported through
    // KJob::percentChanged().
    void progress(int percent, const QString &message);

protected:
    bool doKill() override;

private:
    void run();
    void onWorkerFinished(const ImportResult &result);

    std::unique_ptr<Importer> m_importer;
    QString m_source;
    QString m_databasePath;
    DictType m_type;
    QHash<QString, QString> m_meta;
    ImportResult m_result;
    QThread *m_thread = nullptr;
    ImportWorker *m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace maru::dict
