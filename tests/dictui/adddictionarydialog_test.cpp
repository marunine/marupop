// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Add Dictionary dialog: that choosing a Yomitan folder or .zip fills the name from
// index.json and preselects the type the bank files imply, and that every inline validation
// refuses OK with a message rather than creating a broken entry.
#include "dict/dictionarymanager.h"
#include "dict/importers/yomitanimporter.h"
#include "dictui/adddictionarydialog.h"
#include "yomitanfixture.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QTemporaryDir>

#include <KMessageWidget>
#include <KUrlRequester>

#include <gtest/gtest.h>

using namespace maru;
using maru::test::packYomitanFixture;

namespace
{

QString fixturePath(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

struct Fixture
{
    QTemporaryDir directory{QDir::homePath() + QLatin1String("/dictui-add-XXXXXX")};
    dict::DictionaryManager manager{directory.path(), nullptr};
    AddDictionaryDialog dialog{manager};

    template <typename T>
    [[nodiscard]] T *widget(const QString &name) const
    {
        return dialog.findChild<T *>(name);
    }

    void setPath(const QString &path) const
    {
        widget<KUrlRequester>(QStringLiteral("pathRequester"))->setUrl(QUrl::fromLocalFile(path));
    }

    [[nodiscard]] QString message() const
    {
        auto *message = widget<KMessageWidget>(QStringLiteral("messageWidget"));
        return message->isHidden() ? QString() : message->text();
    }
};

} // namespace

TEST(AddDictionaryDialogTest, prefillsTheNameAndTheTypeFromAYomitanFolder)
{
    Fixture fixture;
    const QString source = fixturePath(QStringLiteral("yomitan_v3"));
    ASSERT_TRUE(QFileInfo::exists(source));

    fixture.setPath(source);

    EXPECT_EQ(fixture.widget<QLineEdit>(QStringLiteral("nameEdit"))->text(), QStringLiteral("Marupop Test Dictionary"));

    const QList<dict::DictType> detected = dict::YomitanImporter::detectTypes(source);
    ASSERT_FALSE(detected.isEmpty());
    EXPECT_EQ(fixture.dialog.selectedType(), detected.constFirst());
    EXPECT_TRUE(fixture.message().isEmpty());
}

TEST(AddDictionaryDialogTest, acceptsAYomitanZipArchive)
{
    Fixture fixture;
    // Kept out of Fixture::directory, which is the DictionaryManager's own storage root: the
    // manager writes dictionaries.json and a subdirectory per dictionary into it.
    QTemporaryDir sourceDirectory;
    ASSERT_TRUE(sourceDirectory.isValid());
    const QString archive = sourceDirectory.filePath(QStringLiteral("dictionary.zip"));
    ASSERT_TRUE(packYomitanFixture(fixturePath(QStringLiteral("yomitan_v3")), archive));

    fixture.setPath(archive);

    EXPECT_EQ(fixture.widget<QLineEdit>(QStringLiteral("nameEdit"))->text(), QStringLiteral("Marupop Test Dictionary"));
    EXPECT_EQ(fixture.dialog.selectedType(), dict::DictType::YomitanWord);
    EXPECT_TRUE(fixture.message().isEmpty());

    fixture.dialog.accept();
    ASSERT_FALSE(fixture.dialog.createdId().isNull());
    const dict::Dictionary *entry = fixture.manager.dictionary(fixture.dialog.createdId());
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->sourcePath, archive);
    EXPECT_EQ(entry->revision, QStringLiteral("test-1"));
}

TEST(AddDictionaryDialogTest, createsTheEntryWithTheIndexMetadata)
{
    Fixture fixture;
    fixture.setPath(fixturePath(QStringLiteral("yomitan_v3")));
    fixture.dialog.accept();

    ASSERT_FALSE(fixture.dialog.createdId().isNull());
    EXPECT_FALSE(fixture.dialog.needsDownload());
    const dict::Dictionary *entry = fixture.manager.dictionary(fixture.dialog.createdId());
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name, QStringLiteral("Marupop Test Dictionary"));
    EXPECT_EQ(entry->revision, QStringLiteral("test-1"));
    EXPECT_TRUE(entry->autoUpdatable);
    EXPECT_EQ(entry->updateUrl, QUrl(QStringLiteral("https://example.invalid/index.json")));
}

TEST(AddDictionaryDialogTest, refusesAFolderWithNoBankFiles)
{
    Fixture fixture;
    fixture.setPath(fixture.directory.path());
    EXPECT_TRUE(fixture.message().contains(QStringLiteral("No Yomitan bank files")));

    fixture.dialog.accept();
    EXPECT_TRUE(fixture.dialog.createdId().isNull());
    EXPECT_NE(fixture.dialog.result(), QDialog::Accepted);
}

TEST(AddDictionaryDialogTest, refusesAPathThatDoesNotExist)
{
    Fixture fixture;
    fixture.setPath(fixture.directory.path() + QLatin1String("/not-here"));
    EXPECT_TRUE(fixture.message().contains(QStringLiteral("does not exist")));
}

TEST(AddDictionaryDialogTest, refusesADuplicateNameAndADuplicatePath)
{
    Fixture fixture;
    const QString source = fixturePath(QStringLiteral("yomitan_v3"));
    dict::Dictionary existing;
    existing.name = QStringLiteral("Marupop Test Dictionary");
    existing.type = dict::DictType::YomitanWord;
    existing.sourcePath = source;
    fixture.manager.add(existing);

    fixture.setPath(source);
    EXPECT_TRUE(fixture.message().contains(QStringLiteral("already exists")));

    fixture.widget<QLineEdit>(QStringLiteral("nameEdit"))->setText(QStringLiteral("Another name"));
    // setText() does not count as user editing, so the validation has to be re-run the way a
    // keystroke would.
    Q_EMIT fixture.widget<QLineEdit>(QStringLiteral("nameEdit"))->textEdited(QStringLiteral("Another name"));
    EXPECT_TRUE(fixture.message().contains(QStringLiteral("already uses")));
}

TEST(AddDictionaryDialogTest, offersAndSelectsTheUndownloadedBuiltIns)
{
    Fixture fixture0;
    fixture0.manager.seedBuiltIns();
    AddDictionaryDialog dialog(fixture0.manager);

    EXPECT_TRUE(dialog.selectBuiltIn(dict::DictType::JMdict));
    EXPECT_EQ(dialog.selectedType(), dict::DictType::JMdict);
    dialog.accept();
    EXPECT_TRUE(dialog.needsDownload());
    ASSERT_FALSE(dialog.createdId().isNull());
    EXPECT_EQ(fixture0.manager.dictionary(dialog.createdId())->type, dict::DictType::JMdict);
}

TEST(AddDictionaryDialogTest, createsAnEmptyCustomWordList)
{
    Fixture fixture;
    ASSERT_TRUE(fixture.dialog.selectFormat(AddDictionaryDialog::Format::CustomWord));
    const QString path = fixture.directory.path() + QLatin1String("/my_words.txt");
    fixture.setPath(path);
    fixture.widget<QLineEdit>(QStringLiteral("nameEdit"))->setText(QStringLiteral("My words"));
    Q_EMIT fixture.widget<QLineEdit>(QStringLiteral("nameEdit"))->textEdited(QStringLiteral("My words"));
    ASSERT_TRUE(fixture.message().isEmpty()) << fixture.message().toStdString();

    fixture.dialog.accept();
    ASSERT_FALSE(fixture.dialog.createdId().isNull());
    EXPECT_TRUE(QFileInfo::exists(path));
    EXPECT_EQ(fixture.manager.dictionary(fixture.dialog.createdId())->type, dict::DictType::CustomWord);
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
