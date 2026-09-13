// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictionarydownloadjob.h"

#include "core/logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <KCompressionDevice>
#include <KLocalizedString>

namespace maru::dict
{

DictionaryDownloadJob::DictionaryDownloadJob(QUrl url, QString targetPath, QObject *parent)
    : KJob(parent)
    , m_url(std::move(url))
    , m_targetPath(std::move(targetPath))
    , m_partPath(m_targetPath + QLatin1String(".part"))
{
    setCapabilities(Killable);
}

DictionaryDownloadJob::~DictionaryDownloadJob() = default;

void DictionaryDownloadJob::setConditional(bool conditional)
{
    m_conditional = conditional;
}

void DictionaryDownloadJob::setNetworkAccessManager(QNetworkAccessManager *manager)
{
    // run() parents the manager it creates to the job, so a manager whose parent is this job is
    // one the job created and is now dropping.
    if (m_manager != nullptr && m_manager->parent() == this)
        delete m_manager;
    m_manager = manager;
}

void DictionaryDownloadJob::start()
{
    // KJob's contract is that start() returns immediately and the work begins on the next turn of
    // the event loop, so a caller can connect to result() after calling it.
    QTimer::singleShot(0, this, &DictionaryDownloadJob::run);
}

void DictionaryDownloadJob::run()
{
    QDir().mkpath(QFileInfo(m_targetPath).absolutePath());

    m_partFile = std::make_unique<QFile>(m_partPath);
    if (!m_partFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(WriteError);
        setErrorText(i18n("Cannot write to %1: %2", m_partPath, m_partFile->errorString()));
        emitResult();
        return;
    }

    if (m_manager == nullptr)
        m_manager = new QNetworkAccessManager(this);

    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    const QFileInfo targetInfo(m_targetPath);
    if (m_conditional && targetInfo.exists()) {
        request.setHeader(QNetworkRequest::IfModifiedSinceHeader, targetInfo.lastModified(QTimeZone::UTC));
    }

    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &DictionaryDownloadJob::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &DictionaryDownloadJob::onFinished);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0)
            setTotalAmount(Bytes, static_cast<qulonglong>(total));
        setProcessedAmount(Bytes, static_cast<qulonglong>(received));
        if (total > 0)
            setPercent(static_cast<unsigned long>(received * 100 / total));
    });
}

void DictionaryDownloadJob::onReadyRead()
{
    if (m_reply == nullptr || m_partFile == nullptr)
        return;
    const QByteArray chunk = m_reply->readAll();
    if (!chunk.isEmpty() && m_partFile->write(chunk) != chunk.size()) {
        setError(WriteError);
        setErrorText(i18n("Cannot write to %1: %2", m_partPath, m_partFile->errorString()));
        m_reply->abort();
    }
}

bool DictionaryDownloadJob::finishDownloadedFile()
{
    m_partFile->close();

    QFile part(m_partPath);
    if (!part.open(QIODevice::ReadOnly)) {
        setError(WriteError);
        setErrorText(i18n("Cannot read the downloaded file %1", m_partPath));
        return false;
    }

    const QByteArray magic = part.peek(2);
    const bool compressed =
        magic.size() == 2 && static_cast<quint8>(magic[0]) == 0x1F && static_cast<quint8>(magic[1]) == 0x8B;

    if (compressed) {
        const QString temporaryPath = m_targetPath + QLatin1String(".tmp");
        QFile output(temporaryPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setError(WriteError);
            setErrorText(i18n("Cannot write to %1: %2", temporaryPath, output.errorString()));
            return false;
        }

        KCompressionDevice decompressor(&part, false, KCompressionDevice::GZip);
        if (!decompressor.open(QIODevice::ReadOnly)) {
            setError(DecompressError);
            setErrorText(i18n("Cannot decompress %1", m_url.toString()));
            return false;
        }

        QByteArray buffer;
        buffer.resize(1 << 16);
        while (true) {
            const qint64 read = decompressor.read(buffer.data(), buffer.size());
            if (read < 0) {
                setError(DecompressError);
                setErrorText(i18n("Cannot decompress %1", m_url.toString()));
                return false;
            }
            if (read == 0)
                break;
            if (output.write(buffer.constData(), read) != read) {
                setError(WriteError);
                setErrorText(i18n("Cannot write to %1: %2", temporaryPath, output.errorString()));
                return false;
            }
        }
        output.close();
        part.close();
        QFile::remove(m_partPath);
        QFile::remove(m_targetPath);
        if (!QFile::rename(temporaryPath, m_targetPath)) {
            setError(WriteError);
            setErrorText(i18n("Cannot move %1 into place", temporaryPath));
            return false;
        }
    } else {
        part.close();
        QFile::remove(m_targetPath);
        if (!QFile::rename(m_partPath, m_targetPath)) {
            setError(WriteError);
            setErrorText(i18n("Cannot move %1 into place", m_partPath));
            return false;
        }
    }
    return true;
}

void DictionaryDownloadJob::onFinished()
{
    if (m_reply == nullptr)
        return;

    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QVariant modified = m_reply->header(QNetworkRequest::LastModifiedHeader);
    if (modified.isValid())
        m_remoteModified = modified.toDateTime();

    const bool aborted = m_reply->error() == QNetworkReply::OperationCanceledError;
    const QString networkError = m_reply->errorString();
    const QNetworkReply::NetworkError replyError = m_reply->error();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (status == 304) {
        m_notModified = true;
        m_partFile->close();
        QFile::remove(m_partPath);
        m_partFile = nullptr;
        qCInfo(logDictImport) << m_url.toString() << "is up to date";
        emitResult();
        return;
    }

    if (replyError != QNetworkReply::NoError) {
        m_partFile->close();
        QFile::remove(m_partPath);
        m_partFile = nullptr;
        if (error() == NoError && !aborted) {
            setError(NetworkError);
            setErrorText(i18n("Cannot download %1: %2", m_url.toString(), networkError));
        }
        emitResult();
        return;
    }

    if (error() == NoError)
        (void)finishDownloadedFile();
    m_partFile = nullptr;
    if (error() != NoError)
        QFile::remove(m_partPath);
    emitResult();
}

bool DictionaryDownloadJob::doKill()
{
    // QNetworkReply::abort() emits finished() synchronously, and onFinished() calls emitResult().
    // KJob::kill() then emits result() a second time, and a handler that starts an import on the
    // first emission runs on a download the user cancelled. Dropping the connections first leaves
    // KJob::kill() as the one path that finishes the job.
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_partFile != nullptr) {
        m_partFile->close();
        m_partFile = nullptr;
    }
    QFile::remove(m_partPath);
    return true;
}

} // namespace maru::dict
