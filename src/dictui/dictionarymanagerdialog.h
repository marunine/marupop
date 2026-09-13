// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Manage dictionaries with drag reordering, actions, inline messages and job progress.
// Every edit goes through dict::DictionaryManager immediately; its changed() signal
// persists the list, keeping one source of truth while import jobs are active.
// Removal retains files in a temporary directory for the lifetime of the dialog
// and offers undo through the message widget.
#pragma once

#include "dict/dictionary.h"

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QUuid>

#include <functional>
#include <memory>

class QAction;
class QLabel;
class QProgressBar;
class QPushButton;
class QTemporaryDir;
class QTreeView;
class KActionCollection;
class KJob;
class KMessageWidget;

namespace maru::dict
{
class DictionaryManager;
}

namespace maru
{

class DictionaryModel;

class DictionaryManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DictionaryManagerDialog(dict::DictionaryManager &manager, QWidget *parent = nullptr);
    ~DictionaryManagerDialog() override;

    [[nodiscard]] DictionaryModel *model() const
    {
        return m_model;
    }

    [[nodiscard]] QTreeView *view() const
    {
        return m_view;
    }

    [[nodiscard]] KActionCollection *actionCollection() const
    {
        return m_actions;
    }

    // What Remove asks before it deletes anything. The default puts up
    // KMessageBox::questionTwoActions with a destructive Remove button; a test replaces it so the
    // path runs without a modal.
    using RemoveConfirmation = std::function<bool(const dict::Dictionary &)>;
    void setRemoveConfirmation(RemoveConfirmation confirmation);

    // Whether a download, an import or an update check is running or queued.
    [[nodiscard]] bool isBusy() const;

    // Registers every job with KIO::getJobTracker(), so Plasma's transfer applet shows it too.
    // On by default; a test turns it off to keep the tracker's own window out of the run.
    void setJobTrackingEnabled(bool enabled);

    // Starts the import of id, which is also what the Add dialog's result runs.
    void importDictionary(const QUuid &id);

Q_SIGNALS:
    // A dictionary finished importing and is answering lookups. The application turns this into
    // a KNotification when its own window is not in the foreground.
    void dictionaryImported(const QUuid &id);
    void importFailed(const QUuid &id, const QString &message);
    void busyChanged(bool busy);

private:
    // One queued unit of work. The chain a built-in download runs is Download followed by Import
    // on the same id.
    struct Task
    {
        enum class Kind
        {
            Download,
            Import,
            UpdateCheck,
        };
        QUuid id;
        Kind kind = Kind::Import;
    };

    // A removed entry held for the life of the dialog, so the message widget's Undo can put it
    // back. The store files are copied out before dict::DictionaryManager::remove() deletes them.
    struct RemovedDictionary
    {
        dict::Dictionary dictionary;
        int row = 0;
        std::shared_ptr<QTemporaryDir> backup;
    };

    void buildActions();
    void buildUi();
    void buildAddMenu();

    [[nodiscard]] dict::Dictionary *selectedDictionary() const;
    [[nodiscard]] int selectedRow() const;
    void updateActions();
    void selectRow(int row);

    void runAddDialog(int format, dict::DictType builtIn, bool isBuiltIn);
    void sortDictionaries();
    void configureSelected();
    void removeSelected();
    void undoRemove();
    void updateSelected();
    void openContainingFolder();
    void addCustomEntry(dict::DictType type);
    void showContextMenu(const QPoint &position);

    void enqueue(const Task &task);
    void startNextTask();
    void onDownloadFinished(KJob *job, const QUuid &id);
    void onImportFinished(KJob *job, const QUuid &id);
    void onUpdateCheckFinished(KJob *job, const QUuid &id);
    void setBusy(bool busy);

    void showMessage(int messageType, const QString &text, const QList<QAction *> &actions = {});
    void clearMessage();
    void scheduleSave();

    dict::DictionaryManager &m_manager;
    DictionaryModel *m_model = nullptr;
    QTreeView *m_view = nullptr;
    KActionCollection *m_actions = nullptr;
    KMessageWidget *m_message = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QAction *m_addAction = nullptr;
    QAction *m_sortAction = nullptr;
    QAction *m_configureAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_updateAction = nullptr;
    QAction *m_moveUpAction = nullptr;
    QAction *m_moveDownAction = nullptr;
    QAction *m_moveToTopAction = nullptr;
    QAction *m_moveToBottomAction = nullptr;
    QAction *m_openFolderAction = nullptr;
    QAction *m_addWordAction = nullptr;
    QAction *m_addNameAction = nullptr;

    QList<QAction *> m_messageActions;
    QList<Task> m_queue;
    QPointer<KJob> m_job;
    QList<RemovedDictionary> m_removed;
    RemoveConfirmation m_confirmRemove;
    bool m_jobTracking = true;
    bool m_savePending = false;
};

} // namespace maru
