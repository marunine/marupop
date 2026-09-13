// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/modelstatuswidget.h"

#include "app/pathrequester.h"
#include "core/logging.h"
#include "core/paths.h"
#include "core/settings.h"
#include "ocr/modelstore.h"

#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <KIO/JobTracker>
#include <KIO/OpenFileManagerWindowJob>
#include <KJobTrackerInterface>
#include <KLocalizedString>

namespace maru
{

namespace
{

QString roleLabel(ocr::ModelRole role)
{
    switch (role) {
    case ocr::ModelRole::Detection:
        return i18nc("@item:intable a text-recognition model", "Text detection");
    case ocr::ModelRole::SmallDetection:
        return i18nc("@item:intable a text-recognition model", "Text detection (small)");
    case ocr::ModelRole::HorizontalRecognition:
        return i18nc("@item:intable a text-recognition model", "Horizontal recognition");
    case ocr::ModelRole::VerticalRecognition:
        return i18nc("@item:intable a text-recognition model", "Vertical recognition");
    }
    return {};
}

QString stateLabel(const ocr::ModelStatus &status)
{
    switch (status.state) {
    case ocr::ModelState::Missing:
        return status.info.required ? i18nc("@item:intable model file state", "Missing")
                                    : i18nc("@item:intable model file state", "Not installed (optional)");
    case ocr::ModelState::SizeMismatch:
        return i18nc("@item:intable model file state", "Incomplete");
    case ocr::ModelState::Present:
        return i18nc("@item:intable model file state", "Installed");
    case ocr::ModelState::Verified:
        return i18nc("@item:intable model file state", "Verified");
    case ocr::ModelState::ChecksumMismatch:
        return i18nc("@item:intable model file state", "Damaged");
    }
    return {};
}

} // namespace

ModelStatusWidget::ModelStatusWidget(Mode mode, QWidget *parent)
    : QWidget(parent)
    , m_mode(mode)
    , m_store(new ocr::ModelStore(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    m_notice->setTextFormat(Qt::PlainText);
    m_notice->hide();
    layout->addWidget(m_notice);

    m_table = new QTableWidget(0, 3, this);
    // Not a kcfg_ name: the config manager binds by object name, and this table edits no entry.
    m_table->setObjectName(QStringLiteral("modelTable"));
    m_table->setHorizontalHeaderLabels(
        {i18nc("@title:column", "Model"), i18nc("@title:column", "Size"), i18nc("@title:column", "Status")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    // Four rows and a header: a scroll area over five rows is wasted space in both hosts.
    m_table->setMinimumHeight(150);
    layout->addWidget(m_table);

    auto *progressRow = new QHBoxLayout;
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("modelProgress"));
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    progressRow->addWidget(m_progress, 1);
    m_cancel =
        new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-cancel")), i18nc("@action:button", "Cancel"), this);
    m_cancel->setVisible(false);
    connect(m_cancel, &QPushButton::clicked, this, &ModelStatusWidget::cancelDownload);
    progressRow->addWidget(m_cancel);
    layout->addLayout(progressRow);

    if (m_mode == Mode::Settings) {
        auto *buttons = new QHBoxLayout;
        m_download = new QPushButton(QIcon::fromTheme(QStringLiteral("download")), QString(), this);
        m_download->setToolTip(ocr::ModelStore::licenseNotice());
        connect(m_download, &QPushButton::clicked, this, [this] {
            startDownload(false);
        });
        buttons->addWidget(m_download);
        m_redownload = new QPushButton(
            QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action:button", "Download Again"), this);
        m_redownload->setToolTip(ocr::ModelStore::licenseNotice());
        connect(m_redownload, &QPushButton::clicked, this, [this] {
            startDownload(true);
        });
        buttons->addWidget(m_redownload);
        m_checkUpdates = new QPushButton(
            QIcon::fromTheme(QStringLiteral("update-none")), i18nc("@action:button", "Check for Updates"), this);
        connect(m_checkUpdates, &QPushButton::clicked, this, [this] {
            m_checkUpdates->setEnabled(false);
            m_store->checkForUpdates();
        });
        buttons->addWidget(m_checkUpdates);
        m_verify =
            new QPushButton(QIcon::fromTheme(QStringLiteral("checkmark")), i18nc("@action:button", "Verify"), this);
        connect(m_verify, &QPushButton::clicked, this, &ModelStatusWidget::verify);
        buttons->addWidget(m_verify);
        buttons->addStretch();
        layout->addLayout(buttons);

        auto *directoryRow = new QHBoxLayout;
        directoryRow->addWidget(new QLabel(i18nc("@label:textbox", "Model folder:"), this));
        m_directory = new PathRequester(PathRequester::Kind::Directory, this);
        // Bound by KConfigDialogManager on the settings page; harmless in any other host.
        m_directory->setObjectName(QStringLiteral("kcfg_ModelDirectory"));
        m_directory->setPlaceholderText(paths::modelsDir());
        connect(m_directory, &PathRequester::pathChanged, this, &ModelStatusWidget::refresh);
        directoryRow->addWidget(m_directory, 1);
        m_openDirectory = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")),
                                          i18nc("@action:button", "Show in File Manager"),
                                          this);
        connect(m_openDirectory, &QPushButton::clicked, this, &ModelStatusWidget::showInFileManager);
        directoryRow->addWidget(m_openDirectory);
        layout->addLayout(directoryRow);

        connect(m_store, &ocr::ModelStore::updateAvailable, this, [this](const QString &repository, const QString &) {
            m_notice->setText(i18nc("@info", "An update for %1 is available.", repository));
            m_notice->show();
        });
        connect(m_store, &ocr::ModelStore::updateCheckFinished, this, [this](bool ok, const QString &message) {
            m_checkUpdates->setEnabled(true);
            if (!ok) {
                m_notice->setText(i18nc("@info", "Update check failed: %1", message));
                m_notice->show();
            }
        });
    }

    refresh();
}

ModelStatusWidget::~ModelStatusWidget() = default;

QString ModelStatusWidget::directory() const
{
    if (m_directory != nullptr && !m_directory->path().isEmpty()) {
        return paths::expandPath(m_directory->path());
    }
    return settings::modelDirectory();
}

void ModelStatusWidget::refresh()
{
    buildTable(false);
}

void ModelStatusWidget::verify()
{
    buildTable(true);
}

void ModelStatusWidget::buildTable(bool verifyChecksums)
{
    const QList<ocr::ModelStatus> statuses = ocr::ModelStore::statusIn(directory(), verifyChecksums);
    m_table->setRowCount(static_cast<int>(statuses.size()));
    for (int row = 0; row < statuses.size(); ++row) {
        const ocr::ModelStatus &status = statuses.at(row);
        auto *name = new QTableWidgetItem(roleLabel(status.info.role));
        name->setToolTip(status.path);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(QLocale{}.formattedDataSize(status.info.bytes)));
        m_table->setItem(row, 2, new QTableWidgetItem(stateLabel(status)));
    }
    updateButtons();
    Q_EMIT statusChanged();
}

bool ModelStatusWidget::requiredModelsPresent() const
{
    return ocr::ModelStore::requiredModelsPresentIn(directory());
}

qint64 ModelStatusWidget::bytesToDownload() const
{
    const QList<ocr::ModelStatus> statuses = ocr::ModelStore::statusIn(directory());
    qint64 bytes = 0;
    for (const ocr::ModelStatus &status : statuses) {
        // The optional small detector is not part of what a plain Download fetches, so it is
        // not part of the figure the button names either.
        if (status.info.required && status.state != ocr::ModelState::Present &&
            status.state != ocr::ModelState::Verified) {
            bytes += status.info.bytes;
        }
    }
    return bytes;
}

void ModelStatusWidget::updateButtons()
{
    if (m_mode != Mode::Settings) {
        return;
    }
    const bool busy = isDownloading();
    const qint64 bytes = bytesToDownload();
    m_download->setText(
        bytes > 0 ? i18nc("@action:button with the download size", "Download (%1)", QLocale{}.formattedDataSize(bytes))
                  : i18nc("@action:button", "Download"));
    m_download->setEnabled(!busy && bytes > 0);
    m_redownload->setEnabled(!busy);
    m_verify->setEnabled(!busy);
}

void ModelStatusWidget::startDownload(bool force)
{
    if (isDownloading()) {
        return;
    }
    m_store->setDirectory(directory());
    ocr::ModelDownloadJob *job = nullptr;
    if (force) {
        // Every model, the optional small detector included: a re-download is the answer to a
        // damaged file, and the user has no way to say which one it is.
        job = new ocr::ModelDownloadJob(ocr::ModelStore::models(), directory(), this);
    } else {
        job = m_store->createDownloadJob();
    }
    if (job == nullptr) {
        Q_EMIT downloadFinished(true, QString());
        return;
    }
    m_job = job;
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_cancel->setVisible(true);
    updateButtons();

    connect(job, &KJob::percentChanged, this, [this](KJob *, unsigned long percent) {
        m_progress->setValue(static_cast<int>(percent));
    });
    connect(job, &KJob::result, this, [this](KJob *finished) {
        m_progress->setVisible(false);
        m_cancel->setVisible(false);
        const bool ok = finished->error() == 0;
        if (!ok) {
            qCWarning(logApp) << "the model download failed:" << finished->errorString();
        }
        refresh();
        Q_EMIT downloadFinished(ok, ok ? QString() : finished->errorString());
    });
    // Plasma's job applet shows the transfer beside every other one, which is where a user
    // looks for a 46 MB download that is taking its time.
    KIO::getJobTracker()->registerJob(job);
    job->start();
}

bool ModelStatusWidget::isDownloading() const
{
    return !m_job.isNull();
}

void ModelStatusWidget::cancelDownload()
{
    if (!m_job.isNull()) {
        m_job->kill(KJob::EmitResult);
    }
}

void ModelStatusWidget::showInFileManager() const
{
    const QString path = directory();
    // The directory is created on demand by the download, so a first run has none to open yet.
    QDir{}.mkpath(path);
    KIO::highlightInFileManager({QUrl::fromLocalFile(path)});
}

} // namespace maru
