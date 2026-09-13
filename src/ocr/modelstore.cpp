// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/modelstore.h"

#include "core/logging.h"
#include "core/settings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <KLocalizedString>

namespace maru::ocr
{

namespace
{

// The repository name of the recognition models carries "txt" where the detection repository
// carries "text". The spelling is upstream's and both are reproduced exactly.
const QString kDetectionRepository = QStringLiteral("rtr46/meiki.text.detect.v0");
const QString kRecognitionRepository = QStringLiteral("rtr46/meiki.txt.recognition.v0");

// Bytes read per hashing step. 1 MiB keeps the peak resident size of a verification pass flat.
constexpr qint64 kHashChunk = 1 << 20;

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kHashChunk);
        if (chunk.isEmpty()) {
            break;
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

QUrl ModelInfo::url() const
{
    return QUrl(QStringLiteral("https://huggingface.co/") + repository + QStringLiteral("/resolve/") + commit +
                QLatin1Char('/') + fileName);
}

const QList<ModelInfo> &ModelStore::models()
{
    static const QList<ModelInfo> table = {
        ModelInfo{.role = ModelRole::Detection,
                  .repository = kDetectionRepository,
                  .fileName = QStringLiteral("meiki.text.detect.v0.1.960x544.onnx"),
                  .bytes = 14503825,
                  .sha256 = QStringLiteral("40b6a016667745cae7d3055929ae3b8b1e7716aac795f5904cd3c2c7c3b8404b"),
                  .commit = QStringLiteral("a9cffa4f60cbf72ddb87edf19c6f98a01cd042e6"),
                  .required = true},
        ModelInfo{.role = ModelRole::SmallDetection,
                  .repository = kDetectionRepository,
                  .fileName = QStringLiteral("meiki.text.detect.v0.1.320x192.onnx"),
                  .bytes = 14084361,
                  .sha256 = QStringLiteral("8cdc5daa5c13a5f93adb40ee2e53ad1a3d2f7e372584c5c665b038adeb74ae6d"),
                  .commit = QStringLiteral("a9cffa4f60cbf72ddb87edf19c6f98a01cd042e6"),
                  .required = false},
        ModelInfo{.role = ModelRole::HorizontalRecognition,
                  .repository = kRecognitionRepository,
                  .fileName = QStringLiteral("meiki.text.rec.v0.960x32.onnx"),
                  .bytes = 18593254,
                  .sha256 = QStringLiteral("3e96bc772fbee9717e536a6353032bb944c3382dd2f6960ef4890decda43b000"),
                  .commit = QStringLiteral("a28cf5874dc2438ebb1c86336be26bcec51e3375"),
                  .required = true},
        ModelInfo{.role = ModelRole::VerticalRecognition,
                  .repository = kRecognitionRepository,
                  .fileName = QStringLiteral("meiki.text.rec.v0.vertical.32x480.onnx"),
                  .bytes = 12872961,
                  .sha256 = QStringLiteral("2c2a83a23bc3b7e6c63962175f507ecc6c5e85cc174f17bdec37d9bbd0bf895a"),
                  .commit = QStringLiteral("a28cf5874dc2438ebb1c86336be26bcec51e3375"),
                  .required = true},
    };
    return table;
}

const ModelInfo &ModelStore::model(ModelRole role)
{
    for (const ModelInfo &info : models()) {
        if (info.role == role) {
            return info;
        }
    }
    return models().constFirst();
}

QString ModelStore::licenseNotice()
{
    return i18nc("@info:tooltip",
                 "Text recognition models by rtr46 are downloaded from Hugging Face. License: GNU Lesser General "
                 "Public License, version 3.");
}

ModelStore::ModelStore(QObject *parent)
    : QObject(parent)
{}

QString ModelStore::directory() const
{
    return m_directory.isEmpty() ? settings::modelDirectory() : m_directory;
}

void ModelStore::setDirectory(const QString &directory)
{
    m_directory = directory;
}

QList<ModelStatus> ModelStore::statusIn(const QString &directory, bool verifyChecksums)
{
    QList<ModelStatus> result;
    for (const ModelInfo &info : models()) {
        ModelStatus status;
        status.info = info;
        status.path = directory + QLatin1Char('/') + info.fileName;
        const QFileInfo file(status.path);
        if (!file.exists()) {
            status.state = ModelState::Missing;
            result.append(status);
            continue;
        }
        status.bytesOnDisk = file.size();
        if (status.bytesOnDisk != info.bytes) {
            // A truncated download and an HTML error page both land here, which is why the
            // length is checked before the digest: it costs one stat() rather than a full read.
            status.state = ModelState::SizeMismatch;
            result.append(status);
            continue;
        }
        if (!verifyChecksums) {
            status.state = ModelState::Present;
            result.append(status);
            continue;
        }
        status.state = hashFile(status.path) == info.sha256 ? ModelState::Verified : ModelState::ChecksumMismatch;
        result.append(status);
    }
    return result;
}

QList<ModelStatus> ModelStore::status(bool verifyChecksums) const
{
    return statusIn(directory(), verifyChecksums);
}

bool ModelStore::requiredModelsPresentIn(const QString &directory)
{
    const QList<ModelStatus> statuses = statusIn(directory, false);
    return std::ranges::all_of(statuses, [](const ModelStatus &status) {
        return !status.info.required || status.state == ModelState::Present || status.state == ModelState::Verified;
    });
}

bool ModelStore::requiredModelsPresent() const
{
    return requiredModelsPresentIn(directory());
}

ModelDownloadJob *ModelStore::createDownloadJob(bool includeSmallDetector)
{
    QList<ModelInfo> missing;
    for (const ModelStatus &status : status(false)) {
        if (!status.info.required && !includeSmallDetector) {
            continue;
        }
        if (status.state != ModelState::Present && status.state != ModelState::Verified) {
            missing.append(status.info);
        }
    }
    if (missing.isEmpty()) {
        return nullptr;
    }
    return new ModelDownloadJob(missing, directory(), this);
}

void ModelStore::checkForUpdates()
{
    if (m_pendingChecks > 0) {
        return;
    }
    m_checkError.clear();
    const QStringList repositories = {kDetectionRepository, kRecognitionRepository};
    m_pendingChecks = static_cast<int>(repositories.size());
    for (const QString &repository : repositories) {
        QNetworkRequest request(QUrl(QStringLiteral("https://huggingface.co/api/models/") + repository));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_manager.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, repository] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                m_checkError = reply->errorString();
            } else {
                const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
                const QString remote = object.value(QStringLiteral("sha")).toString();
                QString pinned;
                for (const ModelInfo &info : models()) {
                    if (info.repository == repository) {
                        pinned = info.commit;
                        break;
                    }
                }
                if (!remote.isEmpty() && remote != pinned) {
                    qCDebug(logOcr) << repository << "moved from" << pinned << "to" << remote;
                    Q_EMIT updateAvailable(repository, remote);
                }
            }
            if (--m_pendingChecks == 0) {
                Q_EMIT updateCheckFinished(m_checkError.isEmpty(), m_checkError);
            }
        });
    }
}

ModelDownloadJob::ModelDownloadJob(QList<ModelInfo> models, QString directory, QObject *parent)
    : KJob(parent)
    , m_models(std::move(models))
    , m_directory(std::move(directory))
{
    setCapabilities(KJob::Killable);
    for (const ModelInfo &info : std::as_const(m_models)) {
        m_totalBytes += info.bytes;
    }
    setTotalAmount(KJob::Bytes, m_totalBytes);
}

ModelDownloadJob::~ModelDownloadJob() = default;

void ModelDownloadJob::start()
{
    // KJob requires start() to return before the job runs, so the first request is posted.
    QTimer::singleShot(0, this, &ModelDownloadJob::startNext);
}

bool ModelDownloadJob::doKill()
{
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        const QString path = m_file->fileName();
        m_file->close();
        m_file = nullptr;
        QFile::remove(path);
    }
    return true;
}

void ModelDownloadJob::fail(const QString &message)
{
    if (m_file) {
        const QString path = m_file->fileName();
        m_file->close();
        m_file = nullptr;
        QFile::remove(path);
    }
    setError(KJob::UserDefinedError);
    setErrorText(message);
    emitResult();
}

void ModelDownloadJob::startNext()
{
    if (m_index >= m_models.size()) {
        emitResult();
        return;
    }
    const ModelInfo &info = m_models.at(m_index);
    if (!QDir().mkpath(m_directory)) {
        fail(i18nc("@info", "Could not create the model folder %1.", m_directory));
        return;
    }

    const QString partPath = m_directory + QLatin1Char('/') + info.fileName + QStringLiteral(".part");
    m_file = std::make_unique<QFile>(partPath);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(i18nc("@info", "The file %1 could not be opened for writing.", partPath));
        return;
    }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);

    QNetworkRequest request(info.url());
    // Hugging Face answers with a 302 to a CDN host. The policy is the Qt 6 default and is set
    // here so a change of the default cannot break the download.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_manager.get(request);
    Q_EMIT description(
        this, i18nc("@title:window", "Download Text Recognition Models"), {i18nc("@label", "File"), info.fileName});

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        if (m_file->write(chunk) != chunk.size()) {
            const QString message = m_file->errorString();
            m_reply->abort();
            fail(message);
            return;
        }
        m_hash->addData(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 /*total*/) {
        const qint64 processed = m_completedBytes + received;
        setProcessedAmount(KJob::Bytes, processed);
        if (m_totalBytes > 0) {
            setPercent(static_cast<unsigned long>((processed * 100) / m_totalBytes));
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownloadJob::finishCurrent);
}

void ModelDownloadJob::finishCurrent()
{
    if (m_reply == nullptr) {
        return;
    }
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    const ModelInfo info = m_models.at(m_index);

    if (reply->error() != QNetworkReply::NoError) {
        fail(i18nc("@info", "%1 could not be downloaded: %2", info.fileName, reply->errorString()));
        return;
    }
    const qint64 written = m_file->size();
    m_file->close();
    const QString partPath = m_file->fileName();
    const QString digest = QString::fromLatin1(m_hash->result().toHex());
    m_file = nullptr;
    m_hash = nullptr;

    if (written != info.bytes) {
        QFile::remove(partPath);
        fail(i18nc(
            "@info", "Incorrect file size for %1: %2 bytes (expected %3 bytes).", info.fileName, written, info.bytes));
        return;
    }
    if (digest != info.sha256) {
        // A corrupt ONNX file surfaces much later as an opaque ONNX Runtime error, so it is
        // rejected here rather than written under its final name.
        QFile::remove(partPath);
        fail(i18nc("@info", "The checksum of %1 does not match the expected value.", info.fileName));
        return;
    }
    const QString finalPath = m_directory + QLatin1Char('/') + info.fileName;
    QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
        fail(i18nc("@info", "%1 could not be renamed to %2.", partPath, finalPath));
        return;
    }

    m_completedBytes += info.bytes;
    setProcessedAmount(KJob::Bytes, m_completedBytes);
    ++m_index;
    startNext();
}

} // namespace maru::ocr
