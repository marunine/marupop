// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The desktop integration calls, with one implementation per platform family.
// The Windows build excludes KIO and KXmlGui.
#pragma once

#include <QString>

class KJob;
class QDialog;
class QWidget;

namespace maru::platform
{

// A new, hidden dialog presenting KAboutData::applicationData(). On Linux the dialog is a
// KAboutApplicationDialog. On Windows the dialog is a QMessageBox.
[[nodiscard]] QDialog *createAboutDialog(QWidget *parent = nullptr);

// Lists job in the desktop's transfer view for the lifetime of job. On Windows the call is empty,
// and each dialog that starts a job shows the progress of the job itself.
void registerJob(KJob *job);

// Opens the file manager on the folder holding path, with path selected.
void revealInFileManager(const QString &path);

} // namespace maru::platform
