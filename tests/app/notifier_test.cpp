// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Checks the shipped notifyrc against the events Notifier raises, and checks which setting
// gates each event. Nothing here sends a KNotification: that would put a popup on the tester's
// desktop, and both failures guarded against here -- an event KNotification discards before it
// reaches the notification server, and an event gated on the wrong setting -- are decided
// before any event is sent.
#include "app/notifier.h"
#include "core/settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>

#include <KConfig>
#include <KConfigGroup>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

constexpr QLatin1StringView kEventGroupPrefix("Event/");

// Opens the file once per test and refuses to run on a missing one: without the check a stale
// MARUPOP_NOTIFYRC_PATH would leave the group-iterating cases passing over an empty config,
// certifying a file that is not there.
class NotifyrcTest : public testing::Test
{
protected:
    NotifyrcTest()
        : path{QString::fromUtf8(MARUPOP_NOTIFYRC_PATH)}
        , config{path, KConfig::SimpleConfig}
    {}

    void SetUp() override
    {
        ASSERT_TRUE(QFile::exists(path)) << path.toStdString();
        ASSERT_FALSE(config.groupList().isEmpty()) << "no groups parsed from " << path.toStdString();
    }

    [[nodiscard]] QString action(const QString &eventId) const
    {
        return config.group(kEventGroupPrefix + eventId).readEntry("Action", QString());
    }

    QString path;
    KConfig config;
};

} // namespace

TEST_F(NotifyrcTest, declaresEveryEventTheNotifierRaises)
{
    const QStringList ids = Notifier::eventIds();
    ASSERT_EQ(ids.size(), static_cast<int>(Notifier::Event::Count));
    for (const QString &eventId : ids) {
        EXPECT_TRUE(config.hasGroup(kEventGroupPrefix + eventId))
            << "no [" << kEventGroupPrefix.data() << eventId.toStdString() << "] group";
    }
}

TEST_F(NotifyrcTest, everyEventDefaultsToAPresentation)
{
    const QStringList ids = Notifier::eventIds();
    for (const QString &eventId : ids) {
        // KNotificationManager drops an event whose Action is empty or None, so the toast never
        // reaches the server no matter what the settings say.
        const QString value = action(eventId);
        EXPECT_FALSE(value.isEmpty()) << eventId.toStdString() << " has no Action";
        EXPECT_NE(value, QLatin1StringView("None")) << eventId.toStdString() << " is presented as None";
        EXPECT_TRUE(value.split(QLatin1Char('|')).contains(QLatin1StringView("Popup")))
            << eventId.toStdString() << " does not pop up: Action=" << value.toStdString();
    }
}

TEST_F(NotifyrcTest, declaresNoEventTheNotifierCannotRaise)
{
    const QStringList known = Notifier::eventIds();
    const QStringList groups = config.groupList();
    for (const QString &group : groups) {
        if (!group.startsWith(kEventGroupPrefix)) {
            continue;
        }
        EXPECT_TRUE(known.contains(group.mid(kEventGroupPrefix.size()))) << "stale event " << group.toStdString();
    }
}

TEST_F(NotifyrcTest, pointsAtTheApplicationId)
{
    // The notification server reads DesktopEntry to find the entry a popup's icon, name and
    // "Configure" action come from, and IconName to draw it. Both name the application id, so
    // an id changed without these following it leaves the popups unattributed and unadorned.
    const KConfigGroup global = config.group(QStringLiteral("Global"));
    EXPECT_EQ(global.readEntry("DesktopEntry", QString()), QStringLiteral(MARUPOP_APPLICATION_ID));
    EXPECT_EQ(global.readEntry("IconName", QString()), QStringLiteral(MARUPOP_APPLICATION_ID));
}

TEST_F(NotifyrcTest, keepsThePopupsSilent)
{
    const QStringList ids = Notifier::eventIds();
    for (const QString &eventId : ids) {
        // A dictionary popping up over the text being read must not also make a noise.
        EXPECT_FALSE(action(eventId).split(QLatin1Char('|')).contains(QLatin1StringView("Sound")))
            << eventId.toStdString() << " plays a sound";
    }
}

namespace
{

// Restores the setting afterwards so the in-process PopSettings singleton hands the same value
// to whatever runs next.
class NotifierGateTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_errors = PopSettings::notifyOnError();
    }

    void TearDown() override
    {
        PopSettings::setNotifyOnError(m_errors);
    }

private:
    bool m_errors = true;
};

} // namespace

TEST_F(NotifierGateTest, gatesOnlyTheFailureChannel)
{
    PopSettings::setNotifyOnError(false);
    EXPECT_FALSE(Notifier::isEnabled(Notifier::Event::Failed));
    // The other three announce work the user asked for -- an import they started, a download
    // they confirmed -- so they carry no setting of their own.
    EXPECT_TRUE(Notifier::isEnabled(Notifier::Event::DictionaryImported));
    EXPECT_TRUE(Notifier::isEnabled(Notifier::Event::DictionaryUpdateAvailable));
    EXPECT_TRUE(Notifier::isEnabled(Notifier::Event::ModelsDownloaded));

    PopSettings::setNotifyOnError(true);
    EXPECT_TRUE(Notifier::isEnabled(Notifier::Event::Failed));
}

TEST_F(NotifierGateTest, reportsFailuresOutOfTheBox)
{
    PopSettings::self()->useDefaults(true);
    const bool errors = PopSettings::notifyOnError();
    PopSettings::self()->useDefaults(false);
    EXPECT_TRUE(errors);
}

int main(int argc, char **argv)
{
    QCoreApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
