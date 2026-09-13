// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The first-run prompt: MaruPop recognizes nothing until a text-recognition backend exists,
// and neither of the two ships with it.
#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

namespace maru
{

class ModelStatusWidget;

// Offers the three answers a first run has: download the meikiocr weights (46 MB, LGPL-3.0 by
// rtr46), use a Chrome Screen AI component the user already installed, or neither. Every exit
// path writes FirstRunCompleted, so the dialog is shown once; the "Not Now" button also writes
// ModelDownloadDeclined, which is what keeps the settings page from offering the download as
// though it had never been asked. A dismissal through Esc or through the window close button
// leaves ModelDownloadDeclined at its stored value, because that setting records the "Not Now"
// answer.
class ModelDownloadDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ModelDownloadDialog(QWidget *parent = nullptr);

    // The engine the user chose, written to the settings by accept(). Automatic where nothing
    // was chosen.
    [[nodiscard]] bool downloadSucceeded() const;

    // Writes FirstRunCompleted and saves the settings, then closes with result. QDialog routes
    // accept(), reject(), Esc and the window close button through this one function, which is
    // what records the first run on every exit path.
    void done(int result) override;

private:
    void updateButtons();

    ModelStatusWidget *m_models = nullptr;
    QLabel *m_screenAiState = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_screenAi = nullptr;
    QPushButton *m_notNow = nullptr;
    bool m_downloaded = false;
};

} // namespace maru
