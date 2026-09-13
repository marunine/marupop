// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Add Word and Add Name dialogs: that the line each one appends is the tab-separated record
// JL's loaders read back.
#include "dict/dictionarymanager.h"
#include "dict/importers/customnameimporter.h"
#include "dict/importers/customwordimporter.h"
#include "dictui/addcustomentrydialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

struct Fixture
{
    QTemporaryDir directory{QDir::homePath() + QLatin1String("/dictui-entry-XXXXXX")};
    dict::DictionaryManager manager{directory.path(), nullptr};

    [[nodiscard]] QString listPath(const QString &name) const
    {
        return directory.path() + QLatin1Char('/') + name;
    }

    dict::Dictionary *addList(dict::DictType type, const QString &fileName)
    {
        QFile file(listPath(fileName));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.close();

        dict::Dictionary entry;
        entry.name = fileName;
        entry.type = type;
        entry.sourcePath = listPath(fileName);
        return manager.add(entry);
    }

    static QString firstLine(const QString &path)
    {
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        return QString::fromUtf8(file.readLine()).trimmed();
    }
};

template <typename T>
T *widget(const QDialog &dialog, const QString &name)
{
    return dialog.findChild<T *>(name);
}

} // namespace

TEST(AddCustomEntryDialogTest, reportsWhenThereIsNoListToWriteTo)
{
    Fixture fixture;
    AddCustomEntryDialog dialog(fixture.manager, AddCustomEntryDialog::Mode::Word);
    EXPECT_FALSE(dialog.hasTargets());
    dialog.accept();
    EXPECT_TRUE(dialog.dictionaryId().isNull());
}

TEST(AddCustomEntryDialogTest, appendsAWordInJlsTabSeparatedFormat)
{
    Fixture fixture;
    dict::Dictionary *list = fixture.addList(dict::DictType::CustomWord, QStringLiteral("custom_words.txt"));

    AddCustomEntryDialog dialog(fixture.manager, AddCustomEntryDialog::Mode::Word);
    ASSERT_TRUE(dialog.hasTargets());
    dialog.setSelectedDictionary(list->id);
    widget<QLineEdit>(dialog, QStringLiteral("spellingsEdit"))->setText(QStringLiteral("引き籠もり;引きこもり"));
    widget<QLineEdit>(dialog, QStringLiteral("readingsEdit"))->setText(QStringLiteral("ひきこもり"));
    widget<QPlainTextEdit>(dialog, QStringLiteral("definitionsEdit"))
        ->setPlainText(QStringLiteral("shut-in\nrecluse;hikikomori"));
    widget<QRadioButton>(dialog, QStringLiteral("nounButton"))->setChecked(true);
    dialog.accept();

    ASSERT_EQ(dialog.dictionaryId(), list->id);
    const QString line = Fixture::firstLine(list->sourcePath);
    EXPECT_EQ(line, dialog.appendedLine());

    const QStringList fields = line.split(QLatin1Char('\t'));
    ASSERT_EQ(fields.size(), 4);
    EXPECT_EQ(fields.at(0), QStringLiteral("引き籠もり;引きこもり"));
    EXPECT_EQ(fields.at(1), QStringLiteral("ひきこもり"));
    // A line break inside a definition is escaped the way the loader un-escapes it.
    EXPECT_EQ(fields.at(2), QStringLiteral("shut-in\\nrecluse;hikikomori"));
    EXPECT_EQ(fields.at(3), QStringLiteral("Noun"));

    // The importer reads the line back into the record the popup renders.
    const dict::CustomWordImporter::ParsedLine parsed = dict::CustomWordImporter::parseLine(line);
    EXPECT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.records.size(), 2);
}

TEST(AddCustomEntryDialogTest, keepsAnExplicitWordClassAsTheFifthField)
{
    Fixture fixture;
    dict::Dictionary *list = fixture.addList(dict::DictType::CustomWord, QStringLiteral("custom_words.txt"));

    AddCustomEntryDialog dialog(fixture.manager, AddCustomEntryDialog::Mode::Word);
    dialog.setSelectedDictionary(list->id);
    widget<QLineEdit>(dialog, QStringLiteral("spellingsEdit"))->setText(QStringLiteral("しけ込む"));
    widget<QPlainTextEdit>(dialog, QStringLiteral("definitionsEdit"))->setPlainText(QStringLiteral("to hole up"));
    widget<QRadioButton>(dialog, QStringLiteral("verbButton"))->setChecked(true);
    widget<QLineEdit>(dialog, QStringLiteral("wordClassesEdit"))->setText(QStringLiteral("v5m;vi"));
    dialog.accept();

    const QStringList fields = Fixture::firstLine(list->sourcePath).split(QLatin1Char('\t'));
    ASSERT_EQ(fields.size(), 5);
    EXPECT_EQ(fields.at(3), QStringLiteral("Verb"));
    EXPECT_EQ(fields.at(4), QStringLiteral("v5m;vi"));
}

TEST(AddCustomEntryDialogTest, refusesAWordWithNoSpellingOrNoDefinition)
{
    Fixture fixture;
    dict::Dictionary *list = fixture.addList(dict::DictType::CustomWord, QStringLiteral("custom_words.txt"));
    AddCustomEntryDialog dialog(fixture.manager, AddCustomEntryDialog::Mode::Word);
    dialog.setSelectedDictionary(list->id);

    dialog.accept();
    EXPECT_TRUE(dialog.dictionaryId().isNull());

    widget<QLineEdit>(dialog, QStringLiteral("spellingsEdit"))->setText(QStringLiteral("猫"));
    dialog.accept();
    EXPECT_TRUE(dialog.dictionaryId().isNull());
    EXPECT_EQ(QFileInfo(list->sourcePath).size(), 0);
}

TEST(AddCustomEntryDialogTest, appendsANameWithItsTypeAndExtraInfo)
{
    Fixture fixture;
    dict::Dictionary *list = fixture.addList(dict::DictType::CustomName, QStringLiteral("custom_names.txt"));

    AddCustomEntryDialog dialog(fixture.manager, AddCustomEntryDialog::Mode::Name);
    ASSERT_TRUE(dialog.hasTargets());
    dialog.setSelectedDictionary(list->id);
    widget<QLineEdit>(dialog, QStringLiteral("spellingEdit"))->setText(QStringLiteral("宮水三葉"));
    widget<QLineEdit>(dialog, QStringLiteral("readingEdit"))->setText(QStringLiteral("みやみずみつは"));
    widget<QComboBox>(dialog, QStringLiteral("nameTypeCombo"))->setCurrentText(QStringLiteral("Female"));
    widget<QPlainTextEdit>(dialog, QStringLiteral("extraInfoEdit"))
        ->setPlainText(QStringLiteral("Protagonist\nKimi no Na wa"));
    dialog.accept();

    ASSERT_EQ(dialog.dictionaryId(), list->id);
    const QStringList fields = Fixture::firstLine(list->sourcePath).split(QLatin1Char('\t'));
    ASSERT_EQ(fields.size(), 4);
    EXPECT_EQ(fields.at(0), QStringLiteral("宮水三葉"));
    EXPECT_EQ(fields.at(1), QStringLiteral("みやみずみつは"));
    EXPECT_EQ(fields.at(2), QStringLiteral("Female"));
    EXPECT_EQ(fields.at(3), QStringLiteral("Protagonist\\nKimi no Na wa"));

    const std::optional<dict::CustomNameRecord> record =
        dict::CustomNameImporter::parseLine(Fixture::firstLine(list->sourcePath));
    ASSERT_TRUE(record.has_value());
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
