// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "updatecheckjob.h"

#include "core/logging.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <KLocalizedString>

namespace maru::dict
{

UpdateCheckJob::UpdateCheckJob(const Dictionary &dictionary, QObject *parent)
    : KJob(parent)
    , m_id(dictionary.id)
    , m_url(dictionary.updateUrl)
    , m_localRevision(dictionary.revision)
    , m_localPath(dictionary.sourcePath)
    , m_yomitan(m_url.path().endsWith(QLatin1String(".json")))
{
    setCapabilities(Killable);
}

UpdateCheckJob::~UpdateCheckJob() = default;

void UpdateCheckJob::setNetworkAccessManager(QNetworkAccessManager *manager)
{
    m_manager = manager;
}

void UpdateCheckJob::start()
{
    QTimer::singleShot(0, this, &UpdateCheckJob::run);
}

void UpdateCheckJob::run()
{
    if (!m_url.isValid() || m_url.isEmpty()) {
        setError(NotUpdatable);
        setErrorText(i18n("This dictionary has no update source"));
        emitResult();
        return;
    }

    if (m_manager == nullptr)
        m_manager = new QNetworkAccessManager(this);

    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_yomitan) {
        const QFileInfo localFile(m_localPath);
        if (localFile.exists())
            request.setHeader(QNetworkRequest::IfModifiedSinceHeader, localFile.lastModified(QTimeZone::UTC));
    }

    // A built-in dump is checked with a conditional GET rather than a HEAD: the EDRDG server
    // answers 304 to the conditional GET, and a HEAD would download nothing but also report
    // nothing about the local file's freshness.
    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateCheckJob::onFinished);
}

void UpdateCheckJob::onFinished()
{
    if (m_reply == nullptr)
        return;

    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError replyError = m_reply->error();
    const QString errorString = m_reply->errorString();
    const QByteArray body = m_yomitan ? m_reply->readAll() : QByteArray();
    const QVariant modified = m_reply->header(QNetworkRequest::LastModifiedHeader);
    if (modified.isValid())
        m_remoteModified = modified.toDateTime();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (status == 304) {
        m_updateAvailable = false;
        emitResult();
        return;
    }

    if (replyError != QNetworkReply::NoError) {
        setError(NetworkError);
        setErrorText(i18n("Cannot check %1 for updates: %2", m_url.toString(), errorString));
        emitResult();
        return;
    }

    if (m_yomitan) {
        const QJsonDocument document = QJsonDocument::fromJson(body);
        m_remoteRevision = document.object().value(QLatin1String("revision")).toString();
        m_updateAvailable = !m_remoteRevision.isEmpty() && m_remoteRevision != m_localRevision;
    } else {
        // A 200 answer to the conditional GET means the server holds a copy newer than the local
        // file, or that no local file exists to compare against.
        m_updateAvailable = true;
    }

    qCInfo(logDictImport) << m_url.toString() << (m_updateAvailable ? "has an update" : "is up to date");
    emitResult();
}

bool UpdateCheckJob::doKill()
{
    if (m_reply != nullptr)
        m_reply->abort();
    return true;
}

} // namespace maru::dict
