// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Whether a dictionary's source has a newer version, without downloading it.
//
// Two protocols, both from JL (Apache-2.0):
//  - a built-in EDRDG dump answers a conditional GET with 304 Not Modified when the local file is
//    current (JL.Core/Utilities/ResourceUpdater.cs);
//  - a Yomitan dictionary publishes index.json at its indexUrl, and a revision string different
//    from the stored one means a newer release
//    (JL.Core/Dicts/EPWING/Yomichan/EpwingYomichanUtils.cs).
#pragma once

#include "dict/dictionary.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <KJob>

class QNetworkAccessManager;
class QNetworkReply;

namespace maru::dict
{

class UpdateCheckJob : public KJob
{
    Q_OBJECT

public:
    enum Error
    {
        NetworkError = UserDefinedError + 1,
        NotUpdatable,
    };

    UpdateCheckJob(const Dictionary &dictionary, QObject *parent = nullptr);
    ~UpdateCheckJob() override;

    void start() override;

    [[nodiscard]] QUuid dictionaryId() const
    {
        return m_id;
    }

    [[nodiscard]] bool updateAvailable() const
    {
        return m_updateAvailable;
    }

    // The revision index.json reported, for a Yomitan dictionary.
    [[nodiscard]] QString remoteRevision() const
    {
        return m_remoteRevision;
    }

    // The Last-Modified time the server reported, for a built-in dump.
    [[nodiscard]] QDateTime remoteModified() const
    {
        return m_remoteModified;
    }

    void setNetworkAccessManager(QNetworkAccessManager *manager);

protected:
    bool doKill() override;

private:
    void run();
    void onFinished();

    QUuid m_id;
    QUrl m_url;
    QString m_localRevision;
    QString m_localPath;
    bool m_yomitan = false;
    bool m_updateAvailable = false;
    QString m_remoteRevision;
    QDateTime m_remoteModified;
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
};

} // namespace maru::dict
