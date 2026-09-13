// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Manage Dictionaries dialog: that it builds, that the action column follows the selection,
// that Remove asks first and can be undone, and that an import job started from the dialog runs
// to completion and leaves the record count in the row.
//
// The confirmation is injected rather than clicked, because KMessageBox::questionTwoActions()
// spins its own event loop and would hang an offscreen run.
#include "dict/dictionarymanager.h"
#include "dictui/dictionarymanagerdialog.h"
#include "dictui/dictionarymodel.h"

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeView>

#include <KActionCollection>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

QString fixturePath(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

struct Fixture
{
    QTemporaryDir directory{QDir::homePath() + QLatin1String("/dictui-dialog-XXXXXX")};
    dict::DictionaryManager manager{directory.path(), nullptr};
    DictionaryManagerDialog dialog{manager};

    Fixture()
    {
        // KIO's dynamic job tracker would put its own progress window on screen and talk to
        // D-Bus, neither of which belongs in a unit test.
        dialog.setJobTrackingEnabled(false);
        dialog.setRemoveConfirmation([](const dict::Dictionary &) {
            return true;
        });
    }

    [[nodiscard]] QAction *action(const QString &name) const
    {
        return dialog.actionCollection()->action(name);
    }

    dict::Dictionary *add(const QString &name, dict::DictType type, const QString &source)
    {
        dict::Dictionary entry;
        entry.name = name;
        entry.type = type;
        entry.sourcePath = source;
        return manager.add(entry);
    }

    void select(int row) const
    {
        dialog.view()->setCurrentIndex(dialog.model()->index(row, DictionaryModel::NameColumn));
    }

    // Pumps the event loop until the dialog has no job left, or the deadline passes.
    [[nodiscard]] bool waitForIdle(int milliseconds = 60000) const
    {
        QDeadlineTimer deadline(milliseconds);
        while (dialog.isBusy() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !dialog.isBusy();
    }
};

} // namespace

TEST(DictionaryManagerDialogTest, buildsWithTheStandardActions)
{
    Fixture fixture;
    EXPECT_EQ(fixture.dialog.windowTitle(), QStringLiteral("Manage Dictionaries"));
    for (const QString &name : {QStringLiteral("add_dictionary"),
                                QStringLiteral("sort_dictionaries"),
                                QStringLiteral("configure_dictionary"),
                                QStringLiteral("remove_dictionary"),
                                QStringLiteral("update_dictionary"),
                                QStringLiteral("move_up"),
                                QStringLiteral("move_down"),
                                QStringLiteral("move_to_top"),
                                QStringLiteral("move_to_bottom"),
                                QStringLiteral("open_folder"),
                                QStringLiteral("add_word"),
                                QStringLiteral("add_name")}) {
        EXPECT_NE(fixture.action(name), nullptr) << name.toStdString();
    }
    EXPECT_NE(fixture.action(QStringLiteral("add_dictionary"))->menu(), nullptr);
}

TEST(DictionaryManagerDialogTest, autoSortsTheDictionaryListAndKeepsTheSelection)
{
    Fixture fixture;
    fixture.add(QStringLiteral("Unknown"), dict::DictType::YomitanWord, fixture.directory.path());
    dict::Dictionary *other =
        fixture.add(QStringLiteral("Other frequency list"), dict::DictType::YomitanFrequency, fixture.directory.path());
    dict::Dictionary *ja =
        fixture.add(QStringLiteral("新明解第八版"), dict::DictType::YomitanWord, fixture.directory.path());
    fixture.select(0);

    ASSERT_TRUE(fixture.action(QStringLiteral("sort_dictionaries"))->isEnabled());
    fixture.action(QStringLiteral("sort_dictionaries"))->trigger();

    ASSERT_EQ(fixture.dialog.model()->rowCount(), 3);
    EXPECT_EQ(fixture.dialog.model()->index(0, DictionaryModel::NameColumn).data().toString(),
              QStringLiteral("新明解第八版"));
    EXPECT_EQ(fixture.dialog.model()->index(1, DictionaryModel::NameColumn).data().toString(),
              QStringLiteral("Unknown"));
    EXPECT_EQ(fixture.dialog.model()->index(2, DictionaryModel::NameColumn).data().toString(),
              QStringLiteral("Other frequency list"));
    EXPECT_TRUE(ja->enabled);
    EXPECT_TRUE(other->enabled);
    EXPECT_EQ(fixture.dialog.view()->currentIndex().data().toString(), QStringLiteral("Unknown"));
    EXPECT_EQ(ja->priority, 1);
    EXPECT_EQ(fixture.manager.dictionaryNamed(QStringLiteral("Unknown"))->priority, 2);
    EXPECT_EQ(other->priority, 3);
}

TEST(DictionaryManagerDialogTest, actionsFollowTheSelection)
{
    Fixture fixture;
    EXPECT_FALSE(fixture.action(QStringLiteral("configure_dictionary"))->isEnabled());
    EXPECT_FALSE(fixture.action(QStringLiteral("remove_dictionary"))->isEnabled());
    EXPECT_FALSE(fixture.action(QStringLiteral("move_up"))->isEnabled());

    fixture.add(QStringLiteral("First"), dict::DictType::YomitanWord, fixture.directory.path());
    fixture.add(QStringLiteral("Second"), dict::DictType::YomitanWord, fixture.directory.path());
    fixture.select(0);

    EXPECT_TRUE(fixture.action(QStringLiteral("configure_dictionary"))->isEnabled());
    EXPECT_TRUE(fixture.action(QStringLiteral("remove_dictionary"))->isEnabled());
    EXPECT_TRUE(fixture.action(QStringLiteral("open_folder"))->isEnabled());
    EXPECT_FALSE(fixture.action(QStringLiteral("move_up"))->isEnabled());
    EXPECT_TRUE(fixture.action(QStringLiteral("move_down"))->isEnabled());
    // No update URL, so nothing to check.
    EXPECT_FALSE(fixture.action(QStringLiteral("update_dictionary"))->isEnabled());

    fixture.select(1);
    EXPECT_TRUE(fixture.action(QStringLiteral("move_up"))->isEnabled());
    EXPECT_FALSE(fixture.action(QStringLiteral("move_down"))->isEnabled());
    // Neither custom list exists yet.
    EXPECT_FALSE(fixture.action(QStringLiteral("add_word"))->isEnabled());
}

TEST(DictionaryManagerDialogTest, theUpdateActionSwitchesToDownloadWhenTheSourceIsGone)
{
    Fixture fixture;
    dict::Dictionary *entry = fixture.add(QStringLiteral("JMdict"), dict::DictType::JMdict, fixture.directory.path());
    entry->updateUrl = QUrl(QStringLiteral("https://example.invalid/JMdict_e.gz"));
    fixture.select(0);
    EXPECT_EQ(fixture.action(QStringLiteral("update_dictionary"))->text(), QStringLiteral("Check for Updates"));

    ASSERT_TRUE(fixture.manager.setSourcePath(entry->id, fixture.directory.path() + QLatin1String("/gone.xml")));
    fixture.select(0);
    EXPECT_EQ(fixture.action(QStringLiteral("update_dictionary"))->text(), QStringLiteral("Download"));
}

TEST(DictionaryManagerDialogTest, removeAsksFirstAndCanBeUndone)
{
    Fixture fixture;
    fixture.add(QStringLiteral("First"), dict::DictType::YomitanWord, fixture.directory.path());
    dict::Dictionary *second =
        fixture.add(QStringLiteral("Second"), dict::DictType::YomitanWord, fixture.directory.path());
    const QUuid removedId = second->id;
    fixture.add(QStringLiteral("Third"), dict::DictType::YomitanWord, fixture.directory.path());
    fixture.select(1);

    bool asked = false;
    fixture.dialog.setRemoveConfirmation([&asked](const dict::Dictionary &) {
        asked = true;
        return false;
    });
    fixture.action(QStringLiteral("remove_dictionary"))->trigger();
    EXPECT_TRUE(asked);
    EXPECT_EQ(fixture.dialog.model()->rowCount(), 3);

    fixture.dialog.setRemoveConfirmation([](const dict::Dictionary &) {
        return true;
    });
    fixture.action(QStringLiteral("remove_dictionary"))->trigger();
    ASSERT_EQ(fixture.dialog.model()->rowCount(), 2);
    EXPECT_EQ(fixture.manager.dictionary(removedId), nullptr);

    // The message widget carries the Undo affordance; triggering it restores the entry with its
    // identity and its place in the order.
    auto *message = fixture.dialog.findChild<QWidget *>(QStringLiteral("messageWidget"));
    ASSERT_NE(message, nullptr);
    ASSERT_EQ(message->actions().size(), 1);
    message->actions().constFirst()->trigger();

    ASSERT_EQ(fixture.dialog.model()->rowCount(), 3);
    ASSERT_NE(fixture.manager.dictionary(removedId), nullptr);
    EXPECT_EQ(fixture.dialog.model()->index(1, DictionaryModel::NameColumn).data().toString(),
              QStringLiteral("Second"));
}

TEST(DictionaryManagerDialogTest, importsAYomitanDictionaryAndShowsItsRecordCount)
{
    Fixture fixture;
    const QString source = fixturePath(QStringLiteral("yomitan_v3"));
    ASSERT_TRUE(QFileInfo::exists(source)) << source.toStdString();

    dict::Dictionary *entry = fixture.add(QStringLiteral("Fixture"), dict::DictType::YomitanWord, source);
    const QUuid id = entry->id;

    QSignalSpy imported(&fixture.dialog, &DictionaryManagerDialog::dictionaryImported);
    fixture.dialog.importDictionary(id);
    EXPECT_TRUE(fixture.dialog.isBusy());
    ASSERT_TRUE(fixture.waitForIdle());

    ASSERT_EQ(imported.size(), 1);
    EXPECT_EQ(imported.constFirst().constFirst().toUuid(), id);
    EXPECT_GT(fixture.manager.dictionary(id)->recordCount, 0);
    EXPECT_FALSE(fixture.dialog.model()->isImporting(id));

    const QString entries = fixture.dialog.model()->index(0, DictionaryModel::EntriesColumn).data().toString();
    EXPECT_EQ(entries, QLocale().toString(fixture.manager.dictionary(id)->recordCount));
    // The dialog persists through the manager, so the list file names the import.
    EXPECT_TRUE(QFileInfo::exists(fixture.directory.path() + QLatin1String("/dictionaries.json")));
}

TEST(DictionaryManagerDialogTest, reportsAnImportOfAMissingSource)
{
    Fixture fixture;
    dict::Dictionary *entry = fixture.add(
        QStringLiteral("Broken"), dict::DictType::YomitanWord, fixture.directory.path() + QLatin1String("/not-here"));
    QSignalSpy failed(&fixture.dialog, &DictionaryManagerDialog::importFailed);
    fixture.dialog.importDictionary(entry->id);
    ASSERT_TRUE(fixture.waitForIdle());

    EXPECT_EQ(failed.size(), 1);
    auto *message = fixture.dialog.findChild<QWidget *>(QStringLiteral("messageWidget"));
    ASSERT_NE(message, nullptr);
    ASSERT_EQ(message->actions().size(), 1);
    EXPECT_EQ(message->actions().constFirst()->text(), QStringLiteral("Configure Dictionary…"));
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
