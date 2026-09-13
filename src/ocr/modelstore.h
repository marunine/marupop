// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The meikiocr ONNX models: their pinned public sources, expected byte counts and SHA-256
// digests, local availability, and download jobs. modelstore.cpp defines the integrity pins.
// The weights are LGPL-3.0-only by rtr46, downloaded at runtime. licenseNotice() supplies
// the attribution for the download UI.
#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <KJob>

#include <memory>

class QCryptographicHash;
class QFile;
class QNetworkReply;

namespace maru::ocr
{

// Which model a table entry describes. SmallDetection is the optional 320x192 detector.
enum class ModelRole
{
    Detection,
    SmallDetection,
    HorizontalRecognition,
    VerticalRecognition,
};

struct ModelInfo
{
    ModelRole role = ModelRole::Detection;
    QString repository;
    QString fileName;
    qint64 bytes = 0;
    QString sha256;
    QString commit; // pinned: resolve/main is a moving target on both repositories
    bool required = true;

    // https://huggingface.co/<repository>/resolve/<commit>/<fileName>
    [[nodiscard]] QUrl url() const;
};

enum class ModelState
{
    Missing,          // no file at the expected path
    SizeMismatch,     // a file of the wrong length: a partial or an error page
    Present,          // the length matches and no digest was computed
    Verified,         // the length and the SHA-256 digest both match
    ChecksumMismatch, // the length matches and the SHA-256 digest does not
};

struct ModelStatus
{
    ModelInfo info;
    ModelState state = ModelState::Missing;
    qint64 bytesOnDisk = 0;
    QString path;
};

// Downloads a set of models into one directory, one after another. Each file streams to
// <fileName>.part with the SHA-256 computed as the bytes arrive, and is renamed onto its final
// name only after the length and the digest both match, so a partial download can never be
// mistaken for a complete one.
class ModelDownloadJob : public KJob
{
    Q_OBJECT

public:
    ModelDownloadJob(QList<ModelInfo> models, QString directory, QObject *parent = nullptr);
    ~ModelDownloadJob() override;

    void start() override;

protected:
    bool doKill() override;

private:
    void startNext();
    void finishCurrent();
    void fail(const QString &message);

    QList<ModelInfo> m_models;
    QString m_directory;
    qsizetype m_index = 0;
    qint64 m_totalBytes = 0;
    qint64 m_completedBytes = 0;
    QNetworkAccessManager m_manager;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QCryptographicHash> m_hash;
};

// The model table and the state of the files in one directory.
class ModelStore : public QObject
{
    Q_OBJECT

public:
    explicit ModelStore(QObject *parent = nullptr);

    // The four entries, in the order Detection, SmallDetection, HorizontalRecognition,
    // VerticalRecognition.
    [[nodiscard]] static const QList<ModelInfo> &models();
    [[nodiscard]] static const ModelInfo &model(ModelRole role);

    // The attribution the download dialog and the settings page show.
    [[nodiscard]] static QString licenseNotice();

    // settings::modelDirectory() unless setDirectory() named another one.
    [[nodiscard]] QString directory() const;
    void setDirectory(const QString &directory);

    // One entry per model. verifyChecksums reads every present file in full, which costs
    // roughly 46 MB of disk reads, and is therefore off by default.
    [[nodiscard]] QList<ModelStatus> status(bool verifyChecksums = false) const;
    [[nodiscard]] static QList<ModelStatus> statusIn(const QString &directory, bool verifyChecksums = false);

    // True where the three required models are present with the expected length.
    [[nodiscard]] bool requiredModelsPresent() const;
    [[nodiscard]] static bool requiredModelsPresentIn(const QString &directory);

    // A job for every model that is not Present or Verified. Returns nullptr where nothing is
    // missing. The caller owns the job and starts it.
    [[nodiscard]] ModelDownloadJob *createDownloadJob(bool includeSmallDetector = false);

    // Asks https://huggingface.co/api/models/<repository> for the current commit of each
    // repository and compares it with the pinned commit. Asynchronous.
    void checkForUpdates();

Q_SIGNALS:
    // The repository moved past the pinned commit; remoteCommit is what it points at now.
    void updateAvailable(const QString &repository, const QString &remoteCommit);
    // Emitted once, after every repository has answered.
    void updateCheckFinished(bool ok, const QString &errorMessage);

private:
    QString m_directory;
    QNetworkAccessManager m_manager;
    int m_pendingChecks = 0;
    QString m_checkError;
};

} // namespace maru::ocr
