// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/desktop.h"

#include <QUrl>

#include <KAboutApplicationDialog>
#include <KAboutData>
#include <KIO/JobTracker>
#include <KIO/OpenFileManagerWindowJob>
#include <KJobTrackerInterface>

namespace maru::platform
{

QDialog *createAboutDialog(QWidget *parent)
{
    return new KAboutApplicationDialog(KAboutData::applicationData(), parent);
}

void registerJob(KJob *job)
{
    KIO::getJobTracker()->registerJob(job);
}

void revealInFileManager(const QString &path)
{
    KIO::highlightInFileManager({QUrl::fromLocalFile(path)});
}

} // namespace maru::platform
