// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/modeldownloaddialog.h"

#include "app/modelstatuswidget.h"
#include "core/logging.h"
#include "core/settings.h"
#include "ocr/modelstore.h"
#include "ocr/screenaibackend.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <KLocalizedString>

namespace maru
{

ModelDownloadDialog::ModelDownloadDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Set Up Text Recognition"));
    setObjectName(QStringLiteral("modelDownloadDialog"));
    auto *layout = new QVBoxLayout(this);

    auto *explanation = new QLabel(i18nc("@info",
                                         "Download the meikiocr models to recognize Japanese text offline, or use an "
                                         "installed Chrome Screen AI component."),
                                   this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    m_models = new ModelStatusWidget(ModelStatusWidget::Mode::Compact, this);
    layout->addWidget(m_models);

    const QString resourcesDir = settings::screenAiResourcesDir();
    const bool screenAiInstalled = ocr::ScreenAiBackend::isInstalled(resourcesDir);
    m_screenAiState = new QLabel(
        screenAiInstalled
            ? i18nc("@info", "Chrome Screen AI installed in %1.", resourcesDir)
            : i18nc("@info",
                    "Chrome Screen AI not found in %1. This proprietary component requires a separate installation.",
                    resourcesDir),
        this);
    m_screenAiState->setWordWrap(true);
    layout->addWidget(m_screenAiState);

    auto *buttons = new QDialogButtonBox(this);
    m_download = buttons->addButton(i18nc("@action:button", "Download"), QDialogButtonBox::AcceptRole);
    m_download->setIcon(QIcon::fromTheme(QStringLiteral("download")));
    m_download->setObjectName(QStringLiteral("downloadButton"));
    m_download->setToolTip(ocr::ModelStore::licenseNotice());
    m_screenAi = buttons->addButton(i18nc("@action:button", "Use Chrome Screen AI"), QDialogButtonBox::AcceptRole);
    m_screenAi->setObjectName(QStringLiteral("screenAiButton"));
    m_screenAi->setEnabled(screenAiInstalled);
    m_notNow = buttons->addButton(i18nc("@action:button", "Not Now"), QDialogButtonBox::RejectRole);
    m_notNow->setObjectName(QStringLiteral("notNowButton"));
    layout->addWidget(buttons);

    // Neither AcceptRole button may run QDialogButtonBox's own accept(): the download one has
    // to wait for the job, and the Screen AI one has to write the engine first.
    connect(m_download, &QPushButton::clicked, this, [this] {
        m_download->setEnabled(false);
        m_screenAi->setEnabled(false);
        m_models->startDownload();
    });
    connect(m_screenAi, &QPushButton::clicked, this, [this] {
        settings::setOcrEngine(OcrEngine::ScreenAi);
        accept();
    });
    connect(m_notNow, &QPushButton::clicked, this, [this] {
        PopSettings::setModelDownloadDeclined(true);
        reject();
    });
    connect(m_models, &ModelStatusWidget::downloadFinished, this, [this](bool ok, const QString &message) {
        m_downloaded = ok;
        if (!ok) {
            qCWarning(logApp) << "the first-run model download failed:" << message;
            updateButtons();
            return;
        }
        // Explicit rather than Automatic: the user asked for these models, and a later Screen
        // AI install must not silently take the recognition over.
        settings::setOcrEngine(OcrEngine::MeikiOcr);
        accept();
    });
    connect(m_models, &ModelStatusWidget::statusChanged, this, &ModelDownloadDialog::updateButtons);

    updateButtons();
    resize(560, 460);
}

bool ModelDownloadDialog::downloadSucceeded() const
{
    return m_downloaded;
}

void ModelDownloadDialog::updateButtons()
{
    if (m_models->isDownloading()) {
        return;
    }
    const qint64 bytes = m_models->bytesToDownload();
    m_download->setText(
        bytes > 0 ? i18nc("@action:button with the download size", "Download (%1)", QLocale{}.formattedDataSize(bytes))
                  : i18nc("@action:button", "Download"));
    m_download->setEnabled(true);
    m_screenAi->setEnabled(ocr::ScreenAiBackend::isInstalled(settings::screenAiResourcesDir()));
}

void ModelDownloadDialog::done(int result)
{
    // Application::maybeRunFirstRun() opens this dialog on every start where FirstRunCompleted
    // is false, so the flag is written here rather than in the three button handlers: Esc and
    // the window close button reach QDialog::reject(), which calls this function. The write
    // covers the settings each handler made just before, which is why the save() is here too.
    PopSettings::setFirstRunCompleted(true);
    PopSettings::self()->save();
    QDialog::done(result);
}

} // namespace maru
