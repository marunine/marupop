// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Configure Dictionary dialog: that a type only sees the options that apply to it, that
// touching an option which rewrites the store says so, and that OK writes through the manager.
#include "dict/dictionarymanager.h"
#include "dictui/configuredictionarydialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>

#include <KMessageWidget>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

struct Fixture
{
    QTemporaryDir directory{QDir::homePath() + QLatin1String("/dictui-configure-XXXXXX")};
    dict::DictionaryManager manager{directory.path(), nullptr};

    dict::Dictionary *add(dict::DictType type, bool autoUpdatable = false)
    {
        dict::Dictionary entry;
        entry.name = QStringLiteral("Entry %1").arg(static_cast<int>(type));
        entry.type = type;
        entry.sourcePath = directory.path();
        entry.autoUpdatable = autoUpdatable;
        return manager.add(entry);
    }
};

template <typename T>
T *widget(const QDialog &dialog, const QString &name)
{
    return dialog.findChild<T *>(name);
}

} // namespace

TEST(ConfigureDictionaryDialogTest, showsOnlyTheOptionsThatApplyToTheType)
{
    Fixture fixture;
    const QUuid jmdict = fixture.add(dict::DictType::JMdict)->id;
    ConfigureDictionaryDialog forJmdict(fixture.manager, jmdict);
    EXPECT_NE(widget<QCheckBox>(forJmdict, QStringLiteral("wordClassInfoBox")), nullptr);
    EXPECT_NE(widget<QCheckBox>(forJmdict, QStringLiteral("crossReferencesBox")), nullptr);
    EXPECT_NE(widget<QCheckBox>(forJmdict, QStringLiteral("properNameEntriesBox")), nullptr);
    EXPECT_NE(widget<QCheckBox>(forJmdict, QStringLiteral("excludeFromAllBox")), nullptr);
    EXPECT_EQ(widget<QCheckBox>(forJmdict, QStringLiteral("showImagesBox")), nullptr);
    EXPECT_EQ(widget<QCheckBox>(forJmdict, QStringLiteral("higherValueMeansHigherFrequencyBox")), nullptr);
    EXPECT_EQ(widget<QSpinBox>(forJmdict, QStringLiteral("autoUpdateSpin")), nullptr);

    const QUuid pitch = fixture.add(dict::DictType::YomitanPitchAccent)->id;
    ConfigureDictionaryDialog forPitch(fixture.manager, pitch);
    EXPECT_EQ(widget<QCheckBox>(forPitch, QStringLiteral("wordClassInfoBox")), nullptr);
    EXPECT_EQ(widget<QCheckBox>(forPitch, QStringLiteral("newlineBetweenDefinitionsBox")), nullptr);
    EXPECT_EQ(widget<QCheckBox>(forPitch, QStringLiteral("excludeFromAllBox")), nullptr);

    const QUuid frequency = fixture.add(dict::DictType::YomitanFrequency, true)->id;
    ConfigureDictionaryDialog forFrequency(fixture.manager, frequency);
    EXPECT_NE(widget<QCheckBox>(forFrequency, QStringLiteral("higherValueMeansHigherFrequencyBox")), nullptr);
    EXPECT_EQ(widget<QCheckBox>(forFrequency, QStringLiteral("wordClassInfoBox")), nullptr);
    // autoUpdatable is what reveals the Updates group, per section 8.1's AutoUpdateAfterNDays row.
    EXPECT_NE(widget<QSpinBox>(forFrequency, QStringLiteral("autoUpdateSpin")), nullptr);

    const QUuid yomitan = fixture.add(dict::DictType::YomitanWord)->id;
    ConfigureDictionaryDialog forYomitan(fixture.manager, yomitan);
    EXPECT_NE(widget<QCheckBox>(forYomitan, QStringLiteral("showImagesBox")), nullptr);
    EXPECT_NE(widget<QCheckBox>(forYomitan, QStringLiteral("newlineBetweenDefinitionsBox")), nullptr);
}

TEST(ConfigureDictionaryDialogTest, warnsWhenAnOptionThatRewritesTheStoreIsTouched)
{
    Fixture fixture;
    const QUuid id = fixture.add(dict::DictType::JMdict)->id;
    ConfigureDictionaryDialog dialog(fixture.manager, id);

    auto *notice = widget<KMessageWidget>(dialog, QStringLiteral("reimportMessage"));
    ASSERT_NE(notice, nullptr);
    EXPECT_TRUE(notice->isHidden());

    auto *properNames = widget<QCheckBox>(dialog, QStringLiteral("properNameEntriesBox"));
    ASSERT_NE(properNames, nullptr);
    properNames->setChecked(false);
    EXPECT_FALSE(notice->isHidden());

    // Putting it back removes the notice: only a real change forces the import.
    properNames->setChecked(true);
    EXPECT_TRUE(notice->isHidden());

    // A render-time option never asks for one.
    widget<QCheckBox>(dialog, QStringLiteral("miscInfoBox"))->setChecked(false);
    EXPECT_TRUE(notice->isHidden());
}

TEST(ConfigureDictionaryDialogTest, appliesEverythingThroughTheManager)
{
    Fixture fixture;
    const QUuid id = fixture.add(dict::DictType::JMdict)->id;
    ConfigureDictionaryDialog dialog(fixture.manager, id);

    widget<QLineEdit>(dialog, QStringLiteral("nameEdit"))->setText(QStringLiteral("Renamed"));
    widget<QCheckBox>(dialog, QStringLiteral("enabledBox"))->setChecked(false);
    widget<QCheckBox>(dialog, QStringLiteral("excludeFromAllBox"))->setChecked(true);
    widget<QCheckBox>(dialog, QStringLiteral("miscInfoBox"))->setChecked(false);
    dialog.accept();

    const dict::Dictionary *entry = fixture.manager.dictionary(id);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->name, QStringLiteral("Renamed"));
    EXPECT_FALSE(entry->enabled);
    EXPECT_TRUE(entry->options.excludeFromAll);
    EXPECT_FALSE(entry->options.miscInfo);
    EXPECT_TRUE(entry->options.properNameEntries);
    EXPECT_FALSE(dialog.requiresReimport());
}

TEST(ConfigureDictionaryDialogTest, reportsThatAReimportIsNeeded)
{
    Fixture fixture;
    const QUuid id = fixture.add(dict::DictType::JMdict)->id;
    ConfigureDictionaryDialog dialog(fixture.manager, id);
    widget<QCheckBox>(dialog, QStringLiteral("properNameEntriesBox"))->setChecked(false);
    dialog.accept();

    EXPECT_TRUE(dialog.requiresReimport());
    EXPECT_FALSE(fixture.manager.dictionary(id)->options.properNameEntries);
}

TEST(ConfigureDictionaryDialogTest, writesTheUpdateInterval)
{
    Fixture fixture;
    const QUuid id = fixture.add(dict::DictType::YomitanWord, true)->id;
    ConfigureDictionaryDialog dialog(fixture.manager, id);
    auto *spin = widget<QSpinBox>(dialog, QStringLiteral("autoUpdateSpin"));
    ASSERT_NE(spin, nullptr);
    EXPECT_EQ(spin->minimum(), 0);
    EXPECT_EQ(spin->maximum(), 365);
    spin->setValue(14);
    dialog.accept();

    EXPECT_EQ(fixture.manager.dictionary(id)->options.autoUpdateAfterDays, 14);
    EXPECT_FALSE(dialog.requiresReimport());
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
