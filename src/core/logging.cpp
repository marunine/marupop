// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "core/logging.h"

#include <array>

namespace maru
{

// Every category defaults to QtWarningMsg. The two-argument Q_LOGGING_CATEGORY enables every
// level, which for a tray-resident process that grabs and recognizes on every pointer move means
// a few debug lines a second in the journal for the life of the session. Info and debug are
// turned on the way any Qt application turns them on: QT_LOGGING_RULES="marupop.*=true", a rule
// in qtlogging.ini, or kdebugsettings against the marupop.categories file.

Q_LOGGING_CATEGORY(logApp, "marupop.app", QtWarningMsg)
Q_LOGGING_CATEGORY(logCapture, "marupop.capture", QtWarningMsg)
Q_LOGGING_CATEGORY(logCore, "marupop.core", QtWarningMsg)
Q_LOGGING_CATEGORY(logCursor, "marupop.cursor", QtWarningMsg)
Q_LOGGING_CATEGORY(logDeconj, "marupop.deconj", QtWarningMsg)
Q_LOGGING_CATEGORY(logDict, "marupop.dict", QtWarningMsg)
Q_LOGGING_CATEGORY(logDictImport, "marupop.dictimport", QtWarningMsg)
Q_LOGGING_CATEGORY(logJp, "marupop.jp", QtWarningMsg)
Q_LOGGING_CATEGORY(logKWinGrabber, "marupop.kwingrabber", QtWarningMsg)
Q_LOGGING_CATEGORY(logLookup, "marupop.lookup", QtWarningMsg)
Q_LOGGING_CATEGORY(logMeikiOcr, "marupop.meikiocr", QtWarningMsg)
Q_LOGGING_CATEGORY(logOcr, "marupop.ocr", QtWarningMsg)
Q_LOGGING_CATEGORY(logPlatform, "marupop.platform", QtWarningMsg)
Q_LOGGING_CATEGORY(logPopup, "marupop.popup", QtWarningMsg)
Q_LOGGING_CATEGORY(logScan, "marupop.scan", QtWarningMsg)
Q_LOGGING_CATEGORY(logScreenAi, "marupop.screenai", QtWarningMsg)
Q_LOGGING_CATEGORY(logScreenAiProto, "marupop.screenaiproto", QtWarningMsg)
Q_LOGGING_CATEGORY(logWayland, "marupop.wayland", QtWarningMsg)

QStringList logCategoryNames()
{
    const std::array categories{&logApp(),
                                &logCapture(),
                                &logCore(),
                                &logCursor(),
                                &logDeconj(),
                                &logDict(),
                                &logDictImport(),
                                &logJp(),
                                &logKWinGrabber(),
                                &logLookup(),
                                &logMeikiOcr(),
                                &logOcr(),
                                &logPlatform(),
                                &logPopup(),
                                &logScan(),
                                &logScreenAi(),
                                &logScreenAiProto(),
                                &logWayland()};
    QStringList names;
    names.reserve(static_cast<qsizetype>(categories.size()));
    for (const QLoggingCategory *category : categories) {
        names.append(QString::fromLatin1(category->categoryName()));
    }
    names.sort();
    return names;
}

} // namespace maru
