// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/modelstore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

bool networkTestsEnabled()
{
    return qEnvironmentVariable("MARUPOP_NETWORK") == QLatin1String("1");
}

const ModelStatus &statusOf(const QList<ModelStatus> &all, ModelRole role)
{
    for (const ModelStatus &status : all) {
        if (status.info.role == role) {
            return status;
        }
    }
    return all.constFirst();
}

} // namespace

TEST(ModelStore, tableHoldsTheFourModelsWithTheirPinnedCommits)
{
    ASSERT_EQ(ModelStore::models().size(), 4);
    const ModelInfo &detection = ModelStore::model(ModelRole::Detection);
    EXPECT_EQ(detection.fileName, QStringLiteral("meiki.text.detect.v0.1.960x544.onnx"));
    EXPECT_EQ(detection.repository, QStringLiteral("rtr46/meiki.text.detect.v0"));
    EXPECT_EQ(detection.bytes, 14503825);
    EXPECT_EQ(detection.sha256.size(), 64);
    EXPECT_TRUE(detection.required);

    // The recognition repository spells the middle component "txt", which is upstream's own
    // name and not a transcription error.
    const ModelInfo &vertical = ModelStore::model(ModelRole::VerticalRecognition);
    EXPECT_EQ(vertical.repository, QStringLiteral("rtr46/meiki.txt.recognition.v0"));
    EXPECT_FALSE(ModelStore::model(ModelRole::SmallDetection).required);
}

TEST(ModelStore, urlPinsTheCommitRatherThanMain)
{
    const ModelInfo &detection = ModelStore::model(ModelRole::Detection);
    EXPECT_EQ(detection.url().toString(),
              QStringLiteral("https://huggingface.co/rtr46/meiki.text.detect.v0/resolve/"
                             "a9cffa4f60cbf72ddb87edf19c6f98a01cd042e6/meiki.text.detect.v0.1.960x544.onnx"));
}

TEST(ModelStore, licenseNoticeNamesTheAuthorAndTheLicense)
{
    const QString notice = ModelStore::licenseNotice();
    EXPECT_TRUE(notice.contains(QStringLiteral("rtr46"))) << notice.toStdString();
    EXPECT_TRUE(notice.contains(QStringLiteral("Lesser General Public License"))) << notice.toStdString();
}

TEST(ModelStore, everyModelIsMissingInAnEmptyDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QList<ModelStatus> statuses = ModelStore::statusIn(directory.path(), false);
    ASSERT_EQ(statuses.size(), 4);
    for (const ModelStatus &status : statuses) {
        EXPECT_EQ(status.state, ModelState::Missing);
        EXPECT_EQ(status.bytesOnDisk, 0);
        EXPECT_TRUE(status.path.startsWith(directory.path()));
    }
    EXPECT_FALSE(ModelStore::requiredModelsPresentIn(directory.path()));
}

TEST(ModelStore, aFileOfTheWrongLengthReportsASizeMismatch)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const ModelInfo &detection = ModelStore::model(ModelRole::Detection);
    QFile file(directory.path() + QLatin1Char('/') + detection.fileName);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("<html>404</html>"));
    file.close();

    const QList<ModelStatus> statuses = ModelStore::statusIn(directory.path(), false);
    const ModelStatus &status = statusOf(statuses, ModelRole::Detection);
    EXPECT_EQ(status.state, ModelState::SizeMismatch);
    EXPECT_EQ(status.bytesOnDisk, 16);
    EXPECT_FALSE(ModelStore::requiredModelsPresentIn(directory.path()));
}

TEST(ModelStore, aDownloadJobIsCreatedForTheMissingModelsAlone)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ModelStore store;
    store.setDirectory(directory.path());
    EXPECT_EQ(store.directory(), directory.path());
    EXPECT_FALSE(store.requiredModelsPresent());

    const qint64 requiredBytes = ModelStore::model(ModelRole::Detection).bytes +
                                 ModelStore::model(ModelRole::HorizontalRecognition).bytes +
                                 ModelStore::model(ModelRole::VerticalRecognition).bytes;
    ModelDownloadJob *job = store.createDownloadJob(false);
    ASSERT_NE(job, nullptr);
    EXPECT_EQ(job->totalAmount(KJob::Bytes), requiredBytes);
    delete job;

    // The optional 320x192 detector is added only where the caller asks for it.
    ModelDownloadJob *withSmall = store.createDownloadJob(true);
    ASSERT_NE(withSmall, nullptr);
    EXPECT_EQ(withSmall->totalAmount(KJob::Bytes), requiredBytes + ModelStore::model(ModelRole::SmallDetection).bytes);
    delete withSmall;
}

TEST(ModelStore, aKilledJobLeavesNoPartialFile)
{
    if (!networkTestsEnabled()) {
        GTEST_SKIP() << "set MARUPOP_NETWORK=1 to run the tests that reach Hugging Face";
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto *job = new ModelDownloadJob({ModelStore::model(ModelRole::VerticalRecognition)}, directory.path());
    job->start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
    EXPECT_TRUE(job->kill(KJob::Quietly));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    EXPECT_TRUE(QDir(directory.path()).entryList(QDir::Files).isEmpty());
}

TEST(ModelStore, downloadsAndVerifiesTheVerticalModel)
{
    if (!networkTestsEnabled()) {
        GTEST_SKIP() << "set MARUPOP_NETWORK=1 to run the tests that reach Hugging Face";
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto *job = new ModelDownloadJob({ModelStore::model(ModelRole::VerticalRecognition)}, directory.path());
    bool done = false;
    QObject::connect(job, &KJob::result, [&done](KJob *) {
        done = true;
    });
    job->start();
    const QDeadlineTimer deadline{180000};
    while (!done && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    ASSERT_TRUE(done) << "the download did not finish";
    ASSERT_EQ(job->error(), 0) << job->errorText().toStdString();

    const QList<ModelStatus> statuses = ModelStore::statusIn(directory.path(), true);
    EXPECT_EQ(statusOf(statuses, ModelRole::VerticalRecognition).state, ModelState::Verified);
}

TEST(ModelStore, updateCheckComparesThePinnedCommit)
{
    if (!networkTestsEnabled()) {
        GTEST_SKIP() << "set MARUPOP_NETWORK=1 to run the tests that reach Hugging Face";
    }
    ModelStore store;
    QSignalSpy finished(&store, &ModelStore::updateCheckFinished);
    store.checkForUpdates();
    const QDeadlineTimer deadline{30000};
    while (finished.isEmpty() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    ASSERT_FALSE(finished.isEmpty());
    EXPECT_TRUE(finished.constFirst().at(0).toBool()) << finished.constFirst().at(1).toString().toStdString();
}

int main(int argc, char **argv)
{
    QCoreApplication app{argc, argv}; // PopSettings and QNetworkAccessManager need one
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
