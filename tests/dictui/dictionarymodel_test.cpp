// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The table model over dict::DictionaryManager: that the rows follow the manager's list, that the
// check state writes the enabled flag back, that a move renumbers the priorities, and that a
// dictionary whose source is gone is marked.
#include "dict/dictionarymanager.h"
#include "dictui/dictionarymodel.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// A manager rooted under the test HOME, so nothing is written into the tester's data directory.
struct Fixture
{
    QTemporaryDir directory{QDir::homePath() + QLatin1String("/dictui-model-XXXXXX")};
    dict::DictionaryManager manager{directory.path(), nullptr};
    DictionaryModel model{manager};

    dict::Dictionary *add(const QString &name, dict::DictType type = dict::DictType::YomitanWord)
    {
        dict::Dictionary entry;
        entry.name = name;
        entry.type = type;
        entry.sourcePath = directory.path();
        entry.recordCount = 12345;
        return manager.add(entry);
    }
};

} // namespace

TEST(DictionaryModelTest, rowsFollowTheManager)
{
    Fixture fixture;
    EXPECT_EQ(fixture.model.rowCount(), 0);
    EXPECT_EQ(fixture.model.columnCount(), DictionaryModel::ColumnCount);

    dict::Dictionary *first = fixture.add(QStringLiteral("First"));
    ASSERT_EQ(fixture.model.rowCount(), 1);
    dict::Dictionary *second = fixture.add(QStringLiteral("Second"));
    ASSERT_EQ(fixture.model.rowCount(), 2);

    EXPECT_EQ(fixture.model.index(0, DictionaryModel::NameColumn).data().toString(), QStringLiteral("First"));
    EXPECT_EQ(fixture.model.index(1, DictionaryModel::NameColumn).data().toString(), QStringLiteral("Second"));
    EXPECT_EQ(fixture.model.idAt(fixture.model.index(1, 0)), second->id);

    EXPECT_TRUE(fixture.manager.remove(first->id));
    ASSERT_EQ(fixture.model.rowCount(), 1);
    EXPECT_EQ(fixture.model.index(0, DictionaryModel::NameColumn).data().toString(), QStringLiteral("Second"));
}

TEST(DictionaryModelTest, showsTheTypeTheCountAndTheImportState)
{
    Fixture fixture;
    dict::Dictionary *entry = fixture.add(QStringLiteral("Words"), dict::DictType::YomitanPitchAccent);

    EXPECT_EQ(fixture.model.index(0, DictionaryModel::TypeColumn).data().toString(),
              dict::dictTypeName(dict::DictType::YomitanPitchAccent));
    EXPECT_EQ(fixture.model.index(0, DictionaryModel::EntriesColumn).data().toString(), QLocale().toString(12345));
    EXPECT_EQ(fixture.model.index(0, DictionaryModel::UpdatedColumn).data().toString(), QStringLiteral("—"));

    fixture.model.setImportProgress(entry->id, 42);
    EXPECT_TRUE(fixture.model.isImporting(entry->id));
    EXPECT_TRUE(
        fixture.model.index(0, DictionaryModel::EntriesColumn).data().toString().contains(QStringLiteral("42")));
    fixture.model.clearImportProgress(entry->id);
    EXPECT_EQ(fixture.model.index(0, DictionaryModel::EntriesColumn).data().toString(), QLocale().toString(12345));

    // A built-in seed with no source yet reports that instead of a zero.
    dict::Dictionary seed;
    seed.name = QStringLiteral("JMdict");
    seed.type = dict::DictType::JMdict;
    seed.builtIn = true;
    fixture.manager.add(seed);
    EXPECT_EQ(fixture.model.index(1, DictionaryModel::EntriesColumn).data().toString(),
              QStringLiteral("Not downloaded"));
}

TEST(DictionaryModelTest, theCheckStateWritesTheEnabledFlag)
{
    Fixture fixture;
    dict::Dictionary *entry = fixture.add(QStringLiteral("Words"));
    const QModelIndex index = fixture.model.index(0, DictionaryModel::NameColumn);

    EXPECT_EQ(index.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Checked);
    EXPECT_TRUE(fixture.model.flags(index).testFlag(Qt::ItemIsUserCheckable));
    EXPECT_TRUE(fixture.model.flags(index).testFlag(Qt::ItemIsDragEnabled));

    QSignalSpy changed(&fixture.model, &QAbstractItemModel::dataChanged);
    EXPECT_TRUE(fixture.model.setData(index, Qt::Unchecked, Qt::CheckStateRole));
    EXPECT_FALSE(entry->enabled);
    EXPECT_FALSE(changed.isEmpty());
    EXPECT_EQ(index.data(Qt::CheckStateRole).value<Qt::CheckState>(), Qt::Unchecked);
}

TEST(DictionaryModelTest, movingARowRenumbersThePriorities)
{
    Fixture fixture;
    dict::Dictionary *first = fixture.add(QStringLiteral("First"));
    dict::Dictionary *second = fixture.add(QStringLiteral("Second"));
    dict::Dictionary *third = fixture.add(QStringLiteral("Third"));

    QSignalSpy moved(&fixture.model, &QAbstractItemModel::rowsMoved);
    EXPECT_TRUE(fixture.model.moveToBottom(0));
    EXPECT_EQ(moved.size(), 1);

    EXPECT_EQ(fixture.model.index(0, 0).data().toString(), QStringLiteral("Second"));
    EXPECT_EQ(fixture.model.index(2, 0).data().toString(), QStringLiteral("First"));
    EXPECT_EQ(second->priority, 1);
    EXPECT_EQ(third->priority, 2);
    EXPECT_EQ(first->priority, 3);

    EXPECT_TRUE(fixture.model.moveUp(2));
    EXPECT_EQ(fixture.model.index(1, 0).data().toString(), QStringLiteral("First"));
    EXPECT_FALSE(fixture.model.moveUp(0));
    EXPECT_FALSE(fixture.model.moveDown(2));
}

TEST(DictionaryModelTest, marksADictionaryWhoseSourceIsGone)
{
    Fixture fixture;
    dict::Dictionary *entry = fixture.add(QStringLiteral("Words"));
    const QModelIndex index = fixture.model.index(0, DictionaryModel::NameColumn);
    EXPECT_FALSE(index.data(DictionaryModel::SourceMissingRole).toBool());
    EXPECT_TRUE(index.data(Qt::DecorationRole).value<QIcon>().isNull());

    EXPECT_TRUE(fixture.manager.setSourcePath(entry->id, fixture.directory.path() + QLatin1String("/not-here")));

    EXPECT_TRUE(index.data(DictionaryModel::SourceMissingRole).toBool());
    EXPECT_TRUE(index.data(Qt::ToolTipRole).toString().contains(QStringLiteral("not-here")));
    EXPECT_TRUE(index.data(Qt::ForegroundRole).isValid());
}

TEST(DictionaryModelTest, resyncsWhenTheOrderChangesBehindTheModel)
{
    Fixture fixture;
    fixture.add(QStringLiteral("First"));
    dict::Dictionary *second = fixture.add(QStringLiteral("Second"));

    // A reorder that did not go through the model arrives as changed() alone, which is the one
    // case the row order has to be read back from the manager. The same entries in a new order are
    // a layout change, so a persistent index follows its entry to the row it moved to.
    const QPersistentModelIndex first = fixture.model.index(0, 0);
    QSignalSpy reset(&fixture.model, &QAbstractItemModel::modelReset);
    QSignalSpy layout(&fixture.model, &QAbstractItemModel::layoutChanged);
    EXPECT_TRUE(fixture.manager.move(second->id, 0));
    EXPECT_EQ(reset.size(), 0);
    EXPECT_EQ(layout.size(), 1);
    EXPECT_EQ(fixture.model.index(0, 0).data().toString(), QStringLiteral("Second"));
    EXPECT_EQ(first.row(), 1);
    EXPECT_EQ(first.data().toString(), QStringLiteral("First"));
}

TEST(DictionaryModelTest, keepsItsRowsAcrossASaveAndLoadRoundTrip)
{
    Fixture fixture;
    fixture.add(QStringLiteral("First"));
    fixture.add(QStringLiteral("Second"));
    ASSERT_TRUE(fixture.manager.save());

    QSignalSpy reset(&fixture.model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&fixture.model, &QAbstractItemModel::dataChanged);
    ASSERT_TRUE(fixture.manager.load());

    // The ids and the order came back identical, so the rows are redrawn rather than replaced:
    // a reset would drop the view's selection for nothing.
    EXPECT_EQ(reset.size(), 0);
    EXPECT_FALSE(changed.isEmpty());
    EXPECT_EQ(fixture.model.rowCount(), 2);
    EXPECT_EQ(fixture.model.index(0, 0).data().toString(), QStringLiteral("First"));
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
