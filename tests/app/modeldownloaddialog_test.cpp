// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The first-run prompt over an empty model directory, which is the state every first run is
// in. The download itself is a 46 MB transfer from huggingface.co and runs only under
// MARUPOP_NETWORK=1; what is covered without it is the state the dialog reports and the two
// settings each answer writes.
#include "app/modeldownloaddialog.h"
#include "app/modelstatuswidget.h"
#include "core/settings.h"

#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// An empty directory as the model folder, so nothing on the tester's machine is read and the
// dialog sees the state a first run sees.
class EmptyModelDirectory
{
public:
    EmptyModelDirectory()
        : m_previousDirectory(PopSettings::modelDirectory())
        , m_previousFirstRun(PopSettings::firstRunCompleted())
        , m_previousDeclined(PopSettings::modelDownloadDeclined())
    {
        PopSettings::setModelDirectory(m_directory.path());
        PopSettings::setFirstRunCompleted(false);
        PopSettings::setModelDownloadDeclined(false);
    }

    ~EmptyModelDirectory()
    {
        PopSettings::setModelDirectory(m_previousDirectory);
        PopSettings::setFirstRunCompleted(m_previousFirstRun);
        PopSettings::setModelDownloadDeclined(m_previousDeclined);
        PopSettings::self()->save();
    }

    EmptyModelDirectory(const EmptyModelDirectory &) = delete;
    EmptyModelDirectory &operator=(const EmptyModelDirectory &) = delete;

    [[nodiscard]] QString path() const
    {
        return m_directory.path();
    }

private:
    QTemporaryDir m_directory;
    QString m_previousDirectory;
    bool m_previousFirstRun = false;
    bool m_previousDeclined = false;
};

} // namespace

TEST(ModelDownloadDialogTest, reportsEveryModelAsMissingOverAnEmptyFolder)
{
    const EmptyModelDirectory directory;
    ModelDownloadDialog dialog;

    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("modelTable"));
    ASSERT_NE(table, nullptr);
    // Four models: detection, the optional small detector, horizontal and vertical recognition.
    ASSERT_EQ(table->rowCount(), 4);
    for (int row = 0; row < table->rowCount(); ++row) {
        ASSERT_NE(table->item(row, 2), nullptr);
        EXPECT_TRUE(table->item(row, 2)->text().contains(QStringLiteral("Missing")) ||
                    table->item(row, 2)->text().contains(QStringLiteral("Not installed")))
            << row << ": " << table->item(row, 2)->text().toStdString();
    }
}

TEST(ModelDownloadDialogTest, namesTheDownloadSizeOnTheButton)
{
    const EmptyModelDirectory directory;
    ModelDownloadDialog dialog;

    auto *download = dialog.findChild<QPushButton *>(QStringLiteral("downloadButton"));
    ASSERT_NE(download, nullptr);
    EXPECT_TRUE(download->isEnabled());
    // The three required models are tens of megabytes, so the label carries the figure the
    // user decides on rather than a bare verb. QLocale spells the unit, so the check is for a
    // parenthesised size rather than for one spelling of "MB".
    EXPECT_TRUE(download->text().contains(QLatin1Char('(')) && download->text().contains(QLatin1Char('B')))
        << download->text().toStdString();
}

TEST(ModelDownloadDialogTest, recordsTheDeclineSoThePromptIsNotRepeated)
{
    const EmptyModelDirectory directory;
    ModelDownloadDialog dialog;
    auto *notNow = dialog.findChild<QPushButton *>(QStringLiteral("notNowButton"));
    ASSERT_NE(notNow, nullptr);

    notNow->click();

    EXPECT_TRUE(PopSettings::modelDownloadDeclined());
    // Both settings, so neither the first-run dialog nor the download prompt returns at the
    // next start.
    EXPECT_TRUE(PopSettings::firstRunCompleted());
    EXPECT_EQ(dialog.result(), QDialog::Rejected);
}

// Esc and the window close button both reach QDialog::reject(). Application::maybeRunFirstRun()
// reopens the dialog at every start while FirstRunCompleted is false, so a dismissal records
// the first run as the three buttons do.
TEST(ModelDownloadDialogTest, recordsTheFirstRunWhenTheDialogIsDismissed)
{
    const EmptyModelDirectory directory;
    ModelDownloadDialog dialog;

    dialog.reject();

    EXPECT_TRUE(PopSettings::firstRunCompleted());
    EXPECT_EQ(dialog.result(), QDialog::Rejected);
    // ModelDownloadDeclined records the "Not Now" answer, and a dismissal answers nothing, so
    // the settings page keeps offering the download.
    EXPECT_FALSE(PopSettings::modelDownloadDeclined());

    // The window close button reaches QDialog::closeEvent(), which calls reject(). QWidget::
    // close() delivers QCloseEvent to a shown widget, so the dialog is shown first.
    PopSettings::setFirstRunCompleted(false);
    ModelDownloadDialog closed;
    closed.show();
    closed.close();
    EXPECT_TRUE(PopSettings::firstRunCompleted());
    EXPECT_FALSE(PopSettings::modelDownloadDeclined());
}

// The Screen AI component is proprietary and is not installed on a test machine, so the answer
// that needs it is offered but refused.
TEST(ModelDownloadDialogTest, offersScreenAiOnlyWhereItIsInstalled)
{
    const EmptyModelDirectory directory;
    const QString previous = PopSettings::screenAiResourcesDir();
    QTemporaryDir empty;
    PopSettings::setScreenAiResourcesDir(empty.path());

    ModelDownloadDialog dialog;
    auto *screenAi = dialog.findChild<QPushButton *>(QStringLiteral("screenAiButton"));
    ASSERT_NE(screenAi, nullptr);
    EXPECT_FALSE(screenAi->isEnabled());

    PopSettings::setScreenAiResourcesDir(previous);
}

TEST(ModelStatusWidgetTest, downloadsTheModelsWhenTheNetworkIsAvailable)
{
    if (qEnvironmentVariable("MARUPOP_NETWORK") != QStringLiteral("1")) {
        GTEST_SKIP() << "a 46 MB download from huggingface.co; set MARUPOP_NETWORK=1 to run it";
    }
    const EmptyModelDirectory directory;
    ModelStatusWidget widget{ModelStatusWidget::Mode::Compact};
    ASSERT_FALSE(widget.requiredModelsPresent());

    QSignalSpy finished{&widget, &ModelStatusWidget::downloadFinished};
    widget.startDownload();
    ASSERT_TRUE(finished.wait(600000));
    EXPECT_TRUE(finished.constFirst().at(0).toBool()) << finished.constFirst().at(1).toString().toStdString();
    EXPECT_TRUE(widget.requiredModelsPresent());
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
