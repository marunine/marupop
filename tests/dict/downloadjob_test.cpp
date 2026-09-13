// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The download job over file:// URLs, which exercises the streaming, the gunzip step and the
// atomic rename without a network. QNetworkAccessManager serves the local scheme itself.
#include "dict/dictionarydownloadjob.h"
#include "dict/updatecheckjob.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <KCompressionDevice>

#include <gtest/gtest.h>

using namespace maru::dict;

namespace
{

bool runJob(KJob *job)
{
    QSignalSpy finished(job, &KJob::result);
    job->start();
    if (finished.isEmpty() && !finished.wait(30000))
        return false;
    return job->error() == KJob::NoError;
}

bool writeGzip(const QString &path, const QByteArray &content)
{
    KCompressionDevice device(path, KCompressionDevice::GZip);
    if (!device.open(QIODevice::WriteOnly))
        return false;
    const bool written = device.write(content) == content.size();
    device.close();
    return written;
}

// A server that answers with a Content-Length it never satisfies, so a reply against it is
// streaming rather than finished when the kill arrives. That is the state whose abort() emits
// finished() synchronously.
class StallingServer : public QTcpServer
{
public:
    StallingServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = nextPendingConnection();
            m_sockets.append(socket);
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                if (!socket->readAll().endsWith("\r\n\r\n"))
                    return;
                // A Content-Length of 100 MB the body never reaches, delivered 64 KiB every
                // 5 ms: the reply is streaming rather than finished for the whole test.
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 104857600\r\n\r\n");
                auto *pump = new QTimer(socket);
                connect(pump, &QTimer::timeout, socket, [socket] {
                    socket->write(QByteArray(qsizetype{64} * 1024, 'x'));
                    socket->flush();
                });
                pump->start(5);
            });
        });
    }

    ~StallingServer() override
    {
        qDeleteAll(m_sockets);
    }

    StallingServer(const StallingServer &) = delete;
    StallingServer &operator=(const StallingServer &) = delete;

private:
    QList<QTcpSocket *> m_sockets;
};

} // namespace

TEST(DictDownloadJob, DecompressesAGzippedSource)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const QByteArray content = QByteArrayLiteral("<JMdict><entry/></JMdict>\n");
    const QString source = directory.filePath(QStringLiteral("source.xml.gz"));
    ASSERT_TRUE(writeGzip(source, content));

    const QString target = directory.filePath(QStringLiteral("downloaded.xml"));
    auto *job = new DictionaryDownloadJob(QUrl::fromLocalFile(source), target);
    EXPECT_TRUE(runJob(job)) << job->errorText().toStdString();
    EXPECT_FALSE(job->notModified());
    delete job;

    QFile downloaded(target);
    ASSERT_TRUE(downloaded.open(QIODevice::ReadOnly));
    EXPECT_EQ(downloaded.readAll(), content);
    EXPECT_FALSE(QFile::exists(target + QStringLiteral(".part")));
    EXPECT_FALSE(QFile::exists(target + QStringLiteral(".tmp")));
}

TEST(DictDownloadJob, CopiesAnUncompressedSource)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const QByteArray content = QByteArrayLiteral("plain text payload");
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QFile sourceFile(source);
    ASSERT_TRUE(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write(content);
    sourceFile.close();

    const QString target = directory.filePath(QStringLiteral("copied.txt"));
    auto *job = new DictionaryDownloadJob(QUrl::fromLocalFile(source), target);
    EXPECT_TRUE(runJob(job)) << job->errorText().toStdString();
    delete job;

    QFile downloaded(target);
    ASSERT_TRUE(downloaded.open(QIODevice::ReadOnly));
    EXPECT_EQ(downloaded.readAll(), content);
}

TEST(DictDownloadJob, ReportsAnAbsentSource)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const QString target = directory.filePath(QStringLiteral("missing.xml"));
    auto *job = new DictionaryDownloadJob(QUrl::fromLocalFile(directory.filePath(QStringLiteral("absent.gz"))), target);
    EXPECT_FALSE(runJob(job));
    EXPECT_EQ(job->error(), DictionaryDownloadJob::NetworkError);
    EXPECT_FALSE(job->errorText().isEmpty());
    delete job;

    EXPECT_FALSE(QFile::exists(target));
    EXPECT_FALSE(QFile::exists(target + QStringLiteral(".part")));
}

// QNetworkReply::abort() emits finished() synchronously, so onFinished() would call emitResult()
// and KJob::kill() would emit result() a second time. Application starts an import on result(),
// which is what made a cancelled download import anyway.
TEST(DictDownloadJob, KillEmitsResultOnceAndReportsNoSuccess)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    StallingServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        GTEST_SKIP() << "a listening socket on 127.0.0.1 is refused in this environment";

    const QString target = directory.filePath(QStringLiteral("killed.bin"));
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/dump.gz").arg(server.serverPort()));
    auto *job = new DictionaryDownloadJob(url, target);
    job->setAutoDelete(false);
    QSignalSpy finished(job, &KJob::result);

    job->start();
    // Spun until the first chunk has arrived, which is the state whose abort() emits finished()
    // synchronously. The deadline bounds the wait so a stalled loopback fails rather than hangs.
    QElapsedTimer deadline;
    deadline.start();
    while (job->processedAmount(KJob::Bytes) == 0 && deadline.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    ASSERT_GT(job->processedAmount(KJob::Bytes), 0U);
    ASSERT_EQ(finished.count(), 0);

    EXPECT_TRUE(job->kill(KJob::EmitResult));

    // A second emission would arrive on a later turn, so the loop is spun before counting.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
    EXPECT_EQ(finished.count(), 1);
    EXPECT_EQ(job->error(), KJob::KilledJobError);
    EXPECT_FALSE(QFile::exists(target));
    EXPECT_FALSE(QFile::exists(target + QStringLiteral(".part")));
    delete job;
}

TEST(DictUpdateCheckJob, ReportsAnEntryWithNoUpdateSource)
{
    Dictionary dictionary;
    dictionary.type = DictType::YomitanWord;
    dictionary.name = QStringLiteral("Local only");

    auto *job = new UpdateCheckJob(dictionary);
    EXPECT_FALSE(runJob(job));
    EXPECT_EQ(job->error(), UpdateCheckJob::NotUpdatable);
    EXPECT_FALSE(job->updateAvailable());
    delete job;
}

TEST(DictUpdateCheckJob, ReadsTheRevisionOfALocalYomitanIndex)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString indexPath = directory.filePath(QStringLiteral("index.json"));
    QFile index(indexPath);
    ASSERT_TRUE(index.open(QIODevice::WriteOnly));
    index.write(QByteArrayLiteral("{\"title\":\"T\",\"revision\":\"rev-2\"}"));
    index.close();

    Dictionary dictionary;
    dictionary.type = DictType::YomitanWord;
    dictionary.name = QStringLiteral("Yomitan");
    dictionary.revision = QStringLiteral("rev-1");
    dictionary.updateUrl = QUrl::fromLocalFile(indexPath);

    auto *job = new UpdateCheckJob(dictionary);
    EXPECT_TRUE(runJob(job)) << job->errorText().toStdString();
    EXPECT_EQ(job->remoteRevision(), QStringLiteral("rev-2"));
    EXPECT_TRUE(job->updateAvailable());
    delete job;

    dictionary.revision = QStringLiteral("rev-2");
    auto *current = new UpdateCheckJob(dictionary);
    EXPECT_TRUE(runJob(current));
    EXPECT_FALSE(current->updateAvailable());
    delete current;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
