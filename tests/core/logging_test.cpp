// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Checks the three places a logging category has to appear against each other: the
// Q_LOGGING_CATEGORY macros in the sources, maru::logCategoryNames(), and the shipped
// .categories file. kdebugsettings reads only the last, so a category missing from it cannot
// be switched on from the UI, and a category listed there but gone from the code is a dead
// entry. The source scan is what keeps logCategoryNames(), itself a hand-kept list, honest.
#include "core/logging.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

constexpr QLatin1StringView kNamePrefix("marupop.");
constexpr QLatin1StringView kDefinitionFile("core/logging.cpp");

// Q_LOGGING_CATEGORY(identifier, "name") occurrences, as (file, name) pairs.
QList<std::pair<QString, QString>> categoryDefinitions(const QString &sourceDir)
{
    // Delimited raw string: the pattern itself contains the )" sequence.
    static const QRegularExpression macro{QStringLiteral(R"re(Q_LOGGING_CATEGORY\(\s*\w+\s*,\s*"([^"]+)")re")};
    QList<std::pair<QString, QString>> found;
    QDirIterator files{
        sourceDir, {QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files, QDirIterator::Subdirectories};
    while (files.hasNext()) {
        const QString path = files.next();
        QFile file{path};
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        QRegularExpressionMatchIterator matches = macro.globalMatch(text);
        while (matches.hasNext()) {
            const QString relative = QDir{sourceDir}.relativeFilePath(path);
            found.append({relative, matches.next().captured(1)});
        }
    }
    return found;
}

// The first field of every line that is neither blank nor a comment.
QStringList declaredCategories(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList names;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        names.append(trimmed.section(QLatin1Char(' '), 0, 0));
    }
    names.sort();
    return names;
}

class CategoriesFileTest : public testing::Test
{
protected:
    CategoriesFileTest()
        : path{QString::fromUtf8(MARUPOP_CATEGORIES_PATH)}
    {}

    void SetUp() override
    {
        ASSERT_TRUE(QFile::exists(path)) << path.toStdString();
        declared = declaredCategories(path);
        ASSERT_FALSE(declared.isEmpty()) << "no categories parsed from " << path.toStdString();
    }

    QString path;
    QStringList declared;
};

} // namespace

TEST_F(CategoriesFileTest, listsExactlyTheCategoriesTheCodeDefines)
{
    // One expectation per name rather than one over the two lists: gtest has no printer for
    // QStringList and reports a mismatched pair as two screens of byte objects.
    const QStringList defined = logCategoryNames();
    for (const QString &name : defined) {
        EXPECT_TRUE(declared.contains(name)) << name.toStdString() << " is missing from " << path.toStdString();
    }
    for (const QString &name : declared) {
        EXPECT_TRUE(defined.contains(name)) << name.toStdString() << " is listed but no longer defined in code";
    }
}

TEST_F(CategoriesFileTest, namesEveryCategoryAfterTheApplication)
{
    // kdebugsettings groups by prefix; a category outside marupop.* lands somewhere else.
    for (const QString &name : logCategoryNames()) {
        EXPECT_TRUE(name.startsWith(kNamePrefix)) << name.toStdString() << " is outside marupop.*";
    }
}

TEST(LoggingSourceTest, definesEveryCategoryInOneFile)
{
    // A Q_LOGGING_CATEGORY anywhere else compiles and logs, and would be invisible to both
    // logCategoryNames() and the .categories file.
    const QList<std::pair<QString, QString>> definitions = categoryDefinitions(QString::fromUtf8(MARUPOP_SOURCE_DIR));
    ASSERT_FALSE(definitions.isEmpty()) << "no Q_LOGGING_CATEGORY found under " << MARUPOP_SOURCE_DIR;
    for (const auto &[file, name] : definitions) {
        EXPECT_EQ(file, QString{kDefinitionFile}) << name.toStdString() << " is defined in " << file.toStdString()
                                                  << " rather than " << kDefinitionFile.data();
    }
}

TEST(LoggingSourceTest, returnsEveryDefinedCategoryFromLogCategoryNames)
{
    // logCategoryNames() lists the categories one by one, so a definition added without a
    // matching entry would otherwise pass every other check here.
    const QList<std::pair<QString, QString>> definitions = categoryDefinitions(QString::fromUtf8(MARUPOP_SOURCE_DIR));
    const QStringList reported = logCategoryNames();
    QStringList defined;
    for (const auto &[file, name] : definitions) {
        defined.append(name);
        EXPECT_TRUE(reported.contains(name)) << name.toStdString() << " is missing from logCategoryNames()";
    }
    for (const QString &name : reported) {
        EXPECT_TRUE(defined.contains(name)) << name.toStdString() << " is reported but no longer defined";
    }
}

TEST_F(CategoriesFileTest, givesEveryLineADescriptionAndAnIdentifier)
{
    QFile file{path};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const qsizetype identifier = trimmed.indexOf(QLatin1StringView(" IDENTIFIER ["));
        EXPECT_GT(identifier, 0) << trimmed.toStdString() << " has no IDENTIFIER field";
        EXPECT_TRUE(trimmed.endsWith(QLatin1Char(']'))) << trimmed.toStdString() << " has an unclosed IDENTIFIER";
        // Between the name and IDENTIFIER, so a bare name with no description fails here.
        const qsizetype nameEnd = trimmed.indexOf(QLatin1Char(' '));
        EXPECT_GT(identifier, nameEnd) << trimmed.toStdString() << " has no description";
    }
}
