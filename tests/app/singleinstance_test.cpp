// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// MARUPOP_TEST_INSTANCE gives the suite an instance name apart from the instance of a running
// MaruPop.
#include "eventloop.h"
#include "platform/singleinstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QThread>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

constexpr char kForwardOption[] = "--forward";

} // namespace

TEST(SingleInstance, secondProcessForwardsItsArgumentsToTheFirst)
{
    platform::SingleInstance first{{QStringLiteral("marupop")}};
    ASSERT_TRUE(first.isPrimary());
    QSignalSpy activated{&first, &platform::SingleInstance::activateRequested};

    QProcess second;
    second.setProgram(QCoreApplication::applicationFilePath());
    second.setArguments({QString::fromLatin1(kForwardOption), QStringLiteral("--settings"), QStringLiteral("日本語")});
    second.start();
    ASSERT_TRUE(second.waitForStarted());
    ASSERT_TRUE(test::waitFor(
        [&] {
            return second.state() == QProcess::NotRunning && !activated.isEmpty();
        },
        10000));
    EXPECT_EQ(second.exitCode(), 0) << "the second process claimed the instance";
    const QStringList forwarded = activated.constFirst().at(0).toStringList();
    ASSERT_EQ(forwarded.size(), 4);
    EXPECT_EQ(forwarded.at(1), QString::fromLatin1(kForwardOption));
    EXPECT_EQ(forwarded.at(2), QStringLiteral("--settings"));
    EXPECT_EQ(forwarded.at(3), QStringLiteral("日本語"));
    EXPECT_EQ(activated.constFirst().at(1).toString(), QDir::currentPath());
}

// A primary whose event loop has ended, as during a quit, leaves the pipe unread. A second process
// started during the quit keeps its command line by claiming the instance after the primary
// releases it.
TEST(SingleInstance, aLaunchDuringTheFirstOnesExitClaimsTheInstanceOnceItIsReleased)
{
    QProcess second;
    {
        platform::SingleInstance first{{QStringLiteral("marupop")}};
        ASSERT_TRUE(first.isPrimary());
        second.setProgram(QCoreApplication::applicationFilePath());
        second.setArguments({QString::fromLatin1(kForwardOption)});
        second.start();
        ASSERT_TRUE(second.waitForStarted());
        QThread::msleep(500);
        EXPECT_EQ(second.state(), QProcess::Running) << "the second process exited before it was acknowledged";
    }
    ASSERT_TRUE(second.waitForFinished(10000));
    EXPECT_EQ(second.exitCode(), 1) << "the second process did not claim the released instance";
}

TEST(SingleInstance, aReleasedInstanceCanBeClaimedAgain)
{
    {
        platform::SingleInstance first{{QStringLiteral("marupop")}};
        ASSERT_TRUE(first.isPrimary());
    }
    platform::SingleInstance again{{QStringLiteral("marupop")}};
    EXPECT_TRUE(again.isPrimary());
}

TEST(SingleInstance, instanceNameCarriesTheTestSuffix)
{
    EXPECT_TRUE(platform::SingleInstance::instanceName().startsWith(QStringLiteral(MARUPOP_APPLICATION_ID)));
    EXPECT_TRUE(platform::SingleInstance::instanceName().endsWith(QStringLiteral("singleinstance-test")));
}

int main(int argc, char **argv)
{
    qputenv("MARUPOP_TEST_INSTANCE", QByteArrayLiteral("singleinstance-test"));
    QCoreApplication app{argc, argv};
    if (argc > 1 && qstrcmp(argv[1], kForwardOption) == 0) {
        const platform::SingleInstance instance{QCoreApplication::arguments()};
        return instance.isPrimary() ? 1 : 0;
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
