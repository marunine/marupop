// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Every Qt logging category the application defines. One declaration site is what lets
// logging_test check the generated marupop.categories, the file kdebugsettings reads, against
// the set the code actually defines: a category added here without a matching
// marupop_logging_category() line in src/CMakeLists.txt fails the suite.
#pragma once

#include <QLoggingCategory>
#include <QStringList>

namespace maru
{

Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logCapture)
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logCursor)
Q_DECLARE_LOGGING_CATEGORY(logDeconj)
Q_DECLARE_LOGGING_CATEGORY(logDict)
Q_DECLARE_LOGGING_CATEGORY(logDictImport)
Q_DECLARE_LOGGING_CATEGORY(logJp)
Q_DECLARE_LOGGING_CATEGORY(logKWinGrabber)
Q_DECLARE_LOGGING_CATEGORY(logLookup)
Q_DECLARE_LOGGING_CATEGORY(logMeikiOcr)
Q_DECLARE_LOGGING_CATEGORY(logOcr)
Q_DECLARE_LOGGING_CATEGORY(logPlatform)
Q_DECLARE_LOGGING_CATEGORY(logPopup)
Q_DECLARE_LOGGING_CATEGORY(logScan)
Q_DECLARE_LOGGING_CATEGORY(logScreenAi)
Q_DECLARE_LOGGING_CATEGORY(logScreenAiProto)
Q_DECLARE_LOGGING_CATEGORY(logWayland)

// The names the categories above carry, sorted. Read from the categories themselves, so the
// string in each Q_LOGGING_CATEGORY is the one definition of a category name.
[[nodiscard]] QStringList logCategoryNames();

} // namespace maru
