// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The meikiocr model files: what is on disk, and the job that fetches what is not.
#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace maru::ocr
{
class ModelDownloadJob;
class ModelStore;
} // namespace maru::ocr

namespace maru
{

class PathRequester;

// The model table shared by the Text Recognition settings page and the first-run dialog. The
// weights are LGPL-3.0 by rtr46 and are downloaded rather than shipped, so both places have to
// show the same thing: which files are missing, how much the download costs, and who wrote
// them.
class ModelStatusWidget : public QWidget
{
    Q_OBJECT

public:
    // Settings adds the model directory row and the four maintenance buttons; Compact shows
    // the table and the progress row alone, which is what the first-run dialog embeds under
    // its own buttons.
    enum class Mode
    {
        Settings,
        Compact,
    };

    explicit ModelStatusWidget(Mode mode = Mode::Settings, QWidget *parent = nullptr);
    ~ModelStatusWidget() override;

    // Reads the directory again and redraws the table. Cheap: it stats four files and computes
    // no digest.
    void refresh();
    // Re-reads every present file in full and shows the SHA-256 verdict, which costs about
    // 46 MB of disk reads.
    void verify();

    // The directory the table is read from. Settings mode follows the path requester; both
    // modes fall back to settings::modelDirectory().
    [[nodiscard]] QString directory() const;

    // True where the three required models are present with the expected length.
    [[nodiscard]] bool requiredModelsPresent() const;
    // Bytes a download would fetch right now, which is what the button label names.
    [[nodiscard]] qint64 bytesToDownload() const;

    // Starts the download of everything that is missing, or of every model when force is set.
    // The job is registered with the KIO job tracker, so Plasma shows it beside every other
    // transfer. Does nothing while a job is already running.
    void startDownload(bool force = false);
    [[nodiscard]] bool isDownloading() const;
    void cancelDownload();

Q_SIGNALS:
    // ok is false for a cancelled or a failed download; message is empty when ok.
    void downloadFinished(bool ok, const QString &message);
    // The table was redrawn: the buttons of an embedding dialog follow the model state.
    void statusChanged();

private:
    void buildTable(bool verifyChecksums);
    void updateButtons();
    void showInFileManager() const;

    Mode m_mode = Mode::Settings;
    ocr::ModelStore *m_store = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_notice = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_redownload = nullptr;
    QPushButton *m_checkUpdates = nullptr;
    QPushButton *m_verify = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_openDirectory = nullptr;
    QProgressBar *m_progress = nullptr;
    PathRequester *m_directory = nullptr;
    QPointer<ocr::ModelDownloadJob> m_job;
};

} // namespace maru
