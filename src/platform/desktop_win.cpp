// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "core/logging.h"
#include "platform/desktop.h"

#include <QDir>
#include <QMessageBox>

#include <KAboutData>
#include <KLocalizedString>
#include <kcoreaddons_version.h>

#include <shlobj.h>
#include <windows.h>

namespace maru::platform
{

QDialog *createAboutDialog(QWidget *parent)
{
    const KAboutData about = KAboutData::applicationData();
    QStringList paragraphs;
    paragraphs.append(QStringLiteral("<h3>%1 %2</h3>").arg(about.displayName().toHtmlEscaped(), about.version()));
    paragraphs.append(QStringLiteral("<p>%1</p>").arg(about.shortDescription().toHtmlEscaped()));
    paragraphs.append(QStringLiteral("<p>%1</p>").arg(about.copyrightStatement().toHtmlEscaped()));
    if (!about.licenses().isEmpty()) {
        paragraphs.append(QStringLiteral("<p>%1</p>")
                              .arg(i18nc("@info about dialog",
                                         "License: %1",
                                         about.licenses().constFirst().name(KAboutLicense::FullName))
                                       .toHtmlEscaped()));
    }
    if (!about.homepage().isEmpty()) {
        paragraphs.append(QStringLiteral("<p><a href=\"%1\">%1</a></p>").arg(about.homepage().toHtmlEscaped()));
    }
    paragraphs.append(QStringLiteral("<p>%1</p>")
                          .arg(i18nc("@info about dialog",
                                     "Built with Qt %1 and KDE Frameworks %2.",
                                     QStringLiteral(QT_VERSION_STR),
                                     QStringLiteral(KCOREADDONS_VERSION_STRING))
                                   .toHtmlEscaped()));

    auto *box = new QMessageBox(QMessageBox::NoIcon,
                                i18nc("@title:window", "About %1", about.displayName()),
                                paragraphs.join(QString{}),
                                QMessageBox::Close,
                                parent);
    box->setTextFormat(Qt::RichText);
    box->setTextInteractionFlags(Qt::TextBrowserInteraction);
    box->setWindowModality(Qt::NonModal);
    return box;
}

void registerJob(KJob * /*job*/) {}

void revealInFileManager(const QString &path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(native.c_str());
    if (item == nullptr) {
        qCWarning(logPlatform) << "ILCreateFromPathW failed for" << path;
        return;
    }
    // SHOpenFolderAndSelectItems requires COM on the calling thread. Qt initializes the GUI thread
    // as a single-threaded apartment, so CoInitializeEx returns S_FALSE on the GUI thread. S_FALSE
    // counts as success and requires a matching CoUninitialize().
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HRESULT opened = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    if (FAILED(opened)) {
        qCWarning(logPlatform) << "SHOpenFolderAndSelectItems failed with" << Qt::hex << static_cast<quint32>(opened);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    ILFree(item);
}

} // namespace maru::platform
