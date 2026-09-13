// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The download half of a built-in dictionary update.
//
// Ported from JL (Apache-2.0), JL.Core/Utilities/ResourceUpdater.cs: the freshness protocol
// for the EDRDG dumps is an If-Modified-Since header carrying the local file's modification time,
// and a 304 answer means the local copy is current. There is no version file and no checksum.
//
// The response is streamed to <target>.part while it arrives, gunzipped into <target>.tmp when it
// is a gzip member, and renamed onto the target last, so an interrupted download leaves the
// previous file in place.
#pragma once

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <KJob>

#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace maru::dict
{

class DictionaryDownloadJob : public KJob
{
    Q_OBJECT

public:
    enum Error
    {
        NetworkError = UserDefinedError + 1,
        WriteError,
        DecompressError,
    };

    // url is the resource to fetch and targetPath the file to leave behind, uncompressed.
    DictionaryDownloadJob(QUrl url, QString targetPath, QObject *parent = nullptr);
    ~DictionaryDownloadJob() override;

    void start() override;

    // Whether the server answered 304 Not Modified, which leaves the target file untouched.
    [[nodiscard]] bool notModified() const
    {
        return m_notModified;
    }

    [[nodiscard]] QString targetPath() const
    {
        return m_targetPath;
    }

    // The Last-Modified time the server reported, invalid when it sent none.
    [[nodiscard]] QDateTime remoteModified() const
    {
        return m_remoteModified;
    }

    // Sends If-Modified-Since from the target file's modification time. On by default; a first
    // download of a file that does not exist sends no header either way.
    void setConditional(bool conditional);

    // Uses an existing manager rather than one the job creates, which is how a test points the job
    // at a local server. The caller keeps ownership of manager. A manager the job created for an
    // earlier run is deleted here, because that one is parented to the job and would otherwise
    // stay alive until the job is destroyed.
    void setNetworkAccessManager(QNetworkAccessManager *manager);

protected:
    bool doKill() override;

private:
    void run();
    void onReadyRead();
    void onFinished();
    [[nodiscard]] bool finishDownloadedFile();

    QUrl m_url;
    QString m_targetPath;
    QString m_partPath;
    // Either the manager setNetworkAccessManager() was handed, which the caller owns, or the one
    // run() creates with the job as its parent, which the job's destructor takes with it.
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_partFile;
    bool m_conditional = true;
    bool m_notModified = false;
    QDateTime m_remoteModified;
};

} // namespace maru::dict
