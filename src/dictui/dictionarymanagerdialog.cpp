// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictui/dictionarymanagerdialog.h"

#include "core/logging.h"
#include "dict/dictionarydownloadjob.h"
#include "dict/dictionaryimportjob.h"
#include "dict/dictionarymanager.h"
#include "dict/store.h"
#include "dict/updatecheckjob.h"
#include "dictui/addcustomentrydialog.h"
#include "dictui/adddictionarydialog.h"
#include "dictui/configuredictionarydialog.h"
#include "dictui/dictionarymodel.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <KActionCollection>
#include <KIO/JobTracker>
#include <KIO/OpenFileManagerWindowJob>
#include <KJobTrackerInterface>
#include <KLocalizedString>
#include <KMessageBox>
#include <KMessageWidget>
#include <KStandardGuiItem>

#include <algorithm>
#include <ranges>
#include <utility>

namespace maru
{

namespace
{

// Copies a file or a whole directory tree. Used to hold a removed dictionary's store files
// while the dialog is open, so Undo has something to put back. The recursion is the directory
// traversal itself.
// NOLINTNEXTLINE(misc-no-recursion)
bool copyPath(const QString &from, const QString &to)
{
    const QFileInfo info(from);
    if (!info.exists())
        return false;
    if (info.isFile())
        return QFile::copy(from, to);

    QDir().mkpath(to);
    const QDir source(from);
    const QFileInfoList entries = source.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    bool ok = true;
    for (const QFileInfo &entry : entries)
        ok = copyPath(entry.absoluteFilePath(), to + QLatin1Char('/') + entry.fileName()) && ok;
    return ok;
}

QToolButton *actionButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

} // namespace

DictionaryManagerDialog::DictionaryManagerDialog(dict::DictionaryManager &manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setObjectName(QStringLiteral("dictionaryManagerDialog"));
    setWindowTitle(i18nc("@title:window", "Manage Dictionaries"));

    m_confirmRemove = [this](const dict::Dictionary &dictionary) {
        const QString text =
            dictionary.builtIn
                ? xi18nc("@info",
                         "Delete imported data and extracted files for <resource>%1</resource>? The dictionary remains "
                         "in the "
                         "list for download.",
                         dictionary.name)
                : xi18nc(
                      "@info",
                      "Remove <resource>%1</resource> from the list and delete its imported data and extracted files?",
                      dictionary.name);
        return KMessageBox::questionTwoActions(
                   this,
                   text,
                   i18nc("@title:window", "Delete Dictionary"),
                   KGuiItem(i18nc("@action:button", "Delete"), QStringLiteral("edit-delete")),
                   KStandardGuiItem::cancel()) == KMessageBox::PrimaryAction;
    };

    m_model = new DictionaryModel(m_manager, this);
    buildActions();
    buildUi();
    updateActions();

    connect(&m_manager, &dict::DictionaryManager::changed, this, &DictionaryManagerDialog::scheduleSave);
    resize(760, 480);
}

DictionaryManagerDialog::~DictionaryManagerDialog()
{
    if (m_savePending)
        (void)m_manager.save();
}

void DictionaryManagerDialog::setRemoveConfirmation(RemoveConfirmation confirmation)
{
    if (confirmation)
        m_confirmRemove = std::move(confirmation);
}

void DictionaryManagerDialog::setJobTrackingEnabled(bool enabled)
{
    m_jobTracking = enabled;
}

void DictionaryManagerDialog::buildActions()
{
    m_actions = new KActionCollection(this);
    m_actions->setComponentName(QStringLiteral("marupop-dictionaries"));

    const auto add = [this](const QString &name, const QString &text, const QString &icon, auto slot) {
        auto *action = m_actions->addAction(name);
        action->setText(text);
        action->setIcon(QIcon::fromTheme(icon));
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    m_addAction = m_actions->addAction(QStringLiteral("add_dictionary"));
    m_addAction->setText(i18nc("@action", "Add Dictionary…"));
    m_addAction->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));

    m_sortAction = add(QStringLiteral("sort_dictionaries"),
                       i18nc("@action", "Auto-Sort Dictionaries"),
                       QStringLiteral("view-sort-ascending"),
                       &DictionaryManagerDialog::sortDictionaries);
    m_sortAction->setToolTip(
        i18nc("@info:tooltip", "Sort dictionaries by the predefined name order and enable matching dictionaries."));

    m_configureAction = add(QStringLiteral("configure_dictionary"),
                            i18nc("@action", "Configure Dictionary…"),
                            QStringLiteral("document-edit"),
                            &DictionaryManagerDialog::configureSelected);
    m_removeAction = add(QStringLiteral("remove_dictionary"),
                         i18nc("@action", "Delete Dictionary"),
                         QStringLiteral("list-remove"),
                         &DictionaryManagerDialog::removeSelected);
    m_updateAction = add(QStringLiteral("update_dictionary"),
                         i18nc("@action", "Check for Updates"),
                         QStringLiteral("view-refresh"),
                         &DictionaryManagerDialog::updateSelected);

    m_moveUpAction = add(QStringLiteral("move_up"), i18nc("@action", "Move Up"), QStringLiteral("arrow-up"), [this] {
        const int row = selectedRow();
        if (m_model->moveUp(row))
            selectRow(row - 1);
    });
    m_moveDownAction =
        add(QStringLiteral("move_down"), i18nc("@action", "Move Down"), QStringLiteral("arrow-down"), [this] {
            const int row = selectedRow();
            if (m_model->moveDown(row))
                selectRow(row + 1);
        });
    m_moveToTopAction =
        add(QStringLiteral("move_to_top"), i18nc("@action", "Move to Top"), QStringLiteral("go-top"), [this] {
            if (m_model->moveToTop(selectedRow()))
                selectRow(0);
        });
    m_moveToBottomAction =
        add(QStringLiteral("move_to_bottom"), i18nc("@action", "Move to Bottom"), QStringLiteral("go-bottom"), [this] {
            if (m_model->moveToBottom(selectedRow()))
                selectRow(m_model->rowCount() - 1);
        });
    m_openFolderAction = add(QStringLiteral("open_folder"),
                             i18nc("@action", "Open Containing Folder"),
                             QStringLiteral("document-open-folder"),
                             &DictionaryManagerDialog::openContainingFolder);
    m_addWordAction =
        add(QStringLiteral("add_word"), i18nc("@action", "Add Word…"), QStringLiteral("list-add"), [this] {
            addCustomEntry(dict::DictType::CustomWord);
        });
    m_addNameAction =
        add(QStringLiteral("add_name"), i18nc("@action", "Add Name…"), QStringLiteral("list-add"), [this] {
            addCustomEntry(dict::DictType::CustomName);
        });

    KActionCollection::setDefaultShortcut(m_moveUpAction, QKeySequence(Qt::CTRL | Qt::Key_Up));
    KActionCollection::setDefaultShortcut(m_moveDownAction, QKeySequence(Qt::CTRL | Qt::Key_Down));
    KActionCollection::setDefaultShortcut(m_moveToTopAction, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up));
    KActionCollection::setDefaultShortcut(m_moveToBottomAction, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down));
    KActionCollection::setDefaultShortcut(m_removeAction, QKeySequence::Delete);

    m_moveUpAction->setToolTip(i18nc("@info:tooltip", "Move the dictionary up. Ctrl+Shift+Up moves it to the top."));
    m_moveDownAction->setToolTip(
        i18nc("@info:tooltip", "Move the dictionary down. Ctrl+Shift+Down moves it to the bottom."));

    m_actions->addAssociatedWidget(this);
    buildAddMenu();
}

void DictionaryManagerDialog::buildAddMenu()
{
    auto *menu = new QMenu(this);
    connect(menu->addAction(i18nc("@action:inmenu", "Import Yomitan Dictionary…")), &QAction::triggered, this, [this] {
        runAddDialog(static_cast<int>(AddDictionaryDialog::Format::Yomitan), dict::DictType::JMdict, false);
    });

    auto *builtInMenu = menu->addMenu(i18nc("@action:inmenu", "Download Dictionary"));
    const QList<dict::Dictionary *> entries = m_manager.dictionaries();
    for (const dict::Dictionary *entry : entries) {
        if (!entry->builtIn || !entry->sourcePath.isEmpty())
            continue;
        const dict::DictType type = entry->type;
        connect(builtInMenu->addAction(entry->name), &QAction::triggered, this, [this, type] {
            runAddDialog(0, type, true);
        });
    }
    builtInMenu->setEnabled(!builtInMenu->isEmpty());

    connect(menu->addAction(i18nc("@action:inmenu", "Custom Word List…")), &QAction::triggered, this, [this] {
        runAddDialog(static_cast<int>(AddDictionaryDialog::Format::CustomWord), dict::DictType::CustomWord, false);
    });
    connect(menu->addAction(i18nc("@action:inmenu", "Custom Name List…")), &QAction::triggered, this, [this] {
        runAddDialog(static_cast<int>(AddDictionaryDialog::Format::CustomName), dict::DictType::CustomName, false);
    });

    m_addAction->setMenu(menu);
}

void DictionaryManagerDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    m_message = new KMessageWidget(this);
    m_message->setObjectName(QStringLiteral("messageWidget"));
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);

    auto *middle = new QHBoxLayout;
    layout->addLayout(middle, 1);

    m_view = new QTreeView(this);
    m_view->setObjectName(QStringLiteral("dictionaryView"));
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setAlternatingRowColors(true);
    m_view->setUniformRowHeights(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDragDropOverwriteMode(false);
    m_view->setDropIndicatorShown(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->header()->setSectionResizeMode(DictionaryModel::NameColumn, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(DictionaryModel::TypeColumn, QHeaderView::ResizeToContents);
    m_view->header()->setSectionResizeMode(DictionaryModel::EntriesColumn, QHeaderView::ResizeToContents);
    m_view->header()->setSectionResizeMode(DictionaryModel::UpdatedColumn, QHeaderView::ResizeToContents);
    middle->addWidget(m_view, 1);

    auto *buttons = new QVBoxLayout;
    middle->addLayout(buttons);
    for (QAction *action : {m_addAction,
                            m_sortAction,
                            m_configureAction,
                            m_removeAction,
                            m_updateAction,
                            m_moveUpAction,
                            m_moveDownAction,
                            m_addWordAction,
                            m_addNameAction}) {
        auto *button = actionButton(action, this);
        if (action == m_addAction)
            button->setPopupMode(QToolButton::InstantPopup);
        buttons->addWidget(button);
    }
    buttons->addStretch();

    auto *progressRow = new QHBoxLayout;
    layout->addLayout(progressRow);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName(QStringLiteral("progressLabel"));
    progressRow->addWidget(m_progressLabel);
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("progressBar"));
    m_progress->setRange(0, 100);
    progressRow->addWidget(m_progress, 1);
    m_cancelButton = new QPushButton(this);
    m_cancelButton->setObjectName(QStringLiteral("cancelJobButton"));
    KGuiItem::assign(m_cancelButton, KStandardGuiItem::cancel());
    progressRow->addWidget(m_cancelButton);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        m_queue.clear();
        if (m_job)
            m_job->kill(KJob::EmitResult);
    });
    setBusy(false);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    KGuiItem::assign(box->button(QDialogButtonBox::Close), KStandardGuiItem::close());
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(m_view, &QTreeView::doubleClicked, this, &DictionaryManagerDialog::configureSelected);
    connect(m_view, &QWidget::customContextMenuRequested, this, &DictionaryManagerDialog::showContextMenu);
    connect(m_view->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            &DictionaryManagerDialog::updateActions);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &DictionaryManagerDialog::updateActions);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &DictionaryManagerDialog::updateActions);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &DictionaryManagerDialog::updateActions);
    connect(m_model, &QAbstractItemModel::modelReset, this, &DictionaryManagerDialog::updateActions);

    if (m_model->rowCount() > 0)
        selectRow(0);
}

int DictionaryManagerDialog::selectedRow() const
{
    const QModelIndex current = m_view->currentIndex();
    return current.isValid() ? current.row() : -1;
}

dict::Dictionary *DictionaryManagerDialog::selectedDictionary() const
{
    return m_model->dictionaryAt(m_view->currentIndex());
}

void DictionaryManagerDialog::selectRow(int row)
{
    if (row < 0 || row >= m_model->rowCount())
        return;
    m_view->setCurrentIndex(m_model->index(row, DictionaryModel::NameColumn));
}

void DictionaryManagerDialog::updateActions()
{
    const dict::Dictionary *entry = selectedDictionary();
    const int row = selectedRow();
    const bool hasSelection = entry != nullptr;
    const bool importing = hasSelection && m_model->isImporting(entry->id);

    m_sortAction->setEnabled(m_model->rowCount() > 1);
    m_configureAction->setEnabled(hasSelection && !importing);
    m_removeAction->setEnabled(hasSelection && !importing &&
                               (!entry->builtIn || !entry->sourcePath.isEmpty() || entry->recordCount > 0));
    m_moveUpAction->setEnabled(row > 0);
    m_moveDownAction->setEnabled(row >= 0 && row < m_model->rowCount() - 1);
    m_moveToTopAction->setEnabled(row > 0);
    m_moveToBottomAction->setEnabled(row >= 0 && row < m_model->rowCount() - 1);
    m_openFolderAction->setEnabled(hasSelection && !entry->sourcePath.isEmpty() &&
                                   QFileInfo::exists(entry->sourcePath));

    const bool updatable = hasSelection && entry->updateUrl.isValid() && !entry->updateUrl.isEmpty();
    const bool missing = hasSelection && (entry->sourcePath.isEmpty() || DictionaryModel::isSourceMissing(*entry));
    m_updateAction->setEnabled(updatable && !importing);
    m_updateAction->setText(missing ? i18nc("@action", "Download") : i18nc("@action", "Check for Updates"));

    const auto hasCustom = [this](dict::DictType type) {
        const QList<dict::Dictionary *> entries = m_manager.dictionaries();
        return std::ranges::any_of(entries, [type](const dict::Dictionary *candidate) {
            return candidate->type == type && !candidate->sourcePath.isEmpty();
        });
    };
    m_addWordAction->setEnabled(hasCustom(dict::DictType::CustomWord));
    m_addNameAction->setEnabled(hasCustom(dict::DictType::CustomName));
}

void DictionaryManagerDialog::sortDictionaries()
{
    // DictionaryModel answers the reorder with a layout change, so the selection follows its entry.
    m_manager.sortDictionaries();
}

void DictionaryManagerDialog::showContextMenu(const QPoint &position)
{
    QMenu menu(this);
    menu.addAction(m_sortAction);
    menu.addSeparator();
    menu.addAction(m_configureAction);
    menu.addAction(m_updateAction);
    menu.addAction(m_openFolderAction);
    menu.addSeparator();
    menu.addAction(m_moveUpAction);
    menu.addAction(m_moveDownAction);
    menu.addAction(m_moveToTopAction);
    menu.addAction(m_moveToBottomAction);
    menu.addSeparator();
    menu.addAction(m_removeAction);
    menu.exec(m_view->viewport()->mapToGlobal(position));
}

void DictionaryManagerDialog::runAddDialog(int format, dict::DictType builtIn, bool isBuiltIn)
{
    AddDictionaryDialog dialog(m_manager, this);
    if (isBuiltIn) {
        if (!dialog.selectBuiltIn(builtIn))
            return;
    } else {
        dialog.selectFormat(static_cast<AddDictionaryDialog::Format>(format));
    }
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QUuid id = dialog.createdId();
    if (id.isNull())
        return;

    clearMessage();
    scheduleSave();
    const QModelIndex index = m_model->indexForId(id);
    if (index.isValid())
        m_view->setCurrentIndex(index);

    if (dialog.needsDownload())
        enqueue({.id = id, .kind = Task::Kind::Download});
    else
        enqueue({.id = id, .kind = Task::Kind::Import});
}

void DictionaryManagerDialog::configureSelected()
{
    const dict::Dictionary *entry = selectedDictionary();
    if (entry == nullptr)
        return;
    const QUuid id = entry->id;

    ConfigureDictionaryDialog dialog(m_manager, id, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    scheduleSave();
    if (dialog.requiresReimport())
        enqueue({.id = id, .kind = Task::Kind::Import});
}

void DictionaryManagerDialog::removeSelected()
{
    dict::Dictionary *entry = selectedDictionary();
    if (entry == nullptr || !m_confirmRemove(*entry))
        return;

    const QUuid id = entry->id;
    const QString name = entry->name;
    const bool builtIn = entry->builtIn;

    RemovedDictionary removed;
    removed.row = selectedRow();
    removed.dictionary = *entry;
    removed.dictionary.store.reset();

    // A built-in entry survives the removal and only loses its data, which the same download
    // brings back, so nothing is copied out for it.
    if (!builtIn) {
        removed.backup = std::make_shared<QTemporaryDir>();
        if (removed.backup->isValid()) {
            const QString directory = m_manager.directory();
            const QString database = dict::databasePathFor(directory, id);
            const QList<QString> files{database,
                                       dict::keyFilterPathFor(database),
                                       dict::wordClassTablePathFor(directory, id),
                                       dict::sourceDirectoryFor(directory, id)};
            for (const QString &file : files) {
                if (QFileInfo::exists(file))
                    copyPath(file, removed.backup->filePath(QFileInfo(file).fileName()));
            }
        }
    }

    if (!m_manager.remove(id))
        return;
    scheduleSave();

    if (builtIn) {
        showMessage(KMessageWidget::Positive,
                    xi18nc("@info", "Imported data for <resource>%1</resource> deleted.", name));
        return;
    }

    m_removed.append(std::move(removed));
    auto *undo = new QAction(i18nc("@action:button", "Undo"), this);
    connect(undo, &QAction::triggered, this, &DictionaryManagerDialog::undoRemove);
    showMessage(KMessageWidget::Positive, xi18nc("@info", "<resource>%1</resource> was removed.", name), {undo});
}

void DictionaryManagerDialog::undoRemove()
{
    if (m_removed.isEmpty())
        return;
    const RemovedDictionary removed = m_removed.takeLast();

    if (removed.backup && removed.backup->isValid()) {
        const QDir backup(removed.backup->path());
        const QFileInfoList entries = backup.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries)
            copyPath(entry.absoluteFilePath(), m_manager.directory() + QLatin1Char('/') + entry.fileName());
    }

    const dict::Dictionary *restored = m_manager.add(removed.dictionary);
    if (restored == nullptr)
        return;
    m_manager.move(restored->id, removed.row);
    scheduleSave();
    clearMessage();
    selectRow(removed.row);
}

void DictionaryManagerDialog::updateSelected()
{
    const dict::Dictionary *entry = selectedDictionary();
    if (entry == nullptr)
        return;
    const bool missing = entry->sourcePath.isEmpty() || DictionaryModel::isSourceMissing(*entry);
    enqueue({.id = entry->id, .kind = missing ? Task::Kind::Download : Task::Kind::UpdateCheck});
}

void DictionaryManagerDialog::openContainingFolder()
{
    const dict::Dictionary *entry = selectedDictionary();
    if (entry == nullptr || entry->sourcePath.isEmpty())
        return;
    KIO::highlightInFileManager({QUrl::fromLocalFile(entry->sourcePath)});
}

void DictionaryManagerDialog::addCustomEntry(dict::DictType type)
{
    const auto mode =
        type == dict::DictType::CustomWord ? AddCustomEntryDialog::Mode::Word : AddCustomEntryDialog::Mode::Name;
    AddCustomEntryDialog dialog(m_manager, mode, this);
    if (!dialog.hasTargets()) {
        showMessage(KMessageWidget::Information,
                    type == dict::DictType::CustomWord
                        ? i18nc("@info", "Add a custom word list first: Add Dictionary, then Custom Word List.")
                        : i18nc("@info", "Add a custom name list first: Add Dictionary, then Custom Name List."));
        return;
    }
    const dict::Dictionary *selected = selectedDictionary();
    if (selected != nullptr && selected->type == type)
        dialog.setSelectedDictionary(selected->id);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // dict::Store has no insert path, so the appended line reaches lookups through a re-import of
    // the list. These files hold a few hundred entries, which imports in single-digit
    // milliseconds.
    enqueue({.id = dialog.dictionaryId(), .kind = Task::Kind::Import});
}

void DictionaryManagerDialog::importDictionary(const QUuid &id)
{
    enqueue({.id = id, .kind = Task::Kind::Import});
}

void DictionaryManagerDialog::enqueue(const Task &task)
{
    if (task.id.isNull())
        return;
    m_queue.append(task);
    setBusy(true);
    startNextTask();
}

void DictionaryManagerDialog::startNextTask()
{
    if (m_job != nullptr)
        return;

    // A loop rather than a tail call: an entry that went away and a task whose job the manager
    // refuses to build are both skipped, and a queue of them must not grow the stack.
    Task task;
    KJob *job = nullptr;
    QString name;
    while (job == nullptr) {
        if (m_queue.isEmpty()) {
            setBusy(false);
            return;
        }
        task = m_queue.takeFirst();
        const dict::Dictionary *entry = m_manager.dictionary(task.id);
        if (entry == nullptr)
            continue;
        name = entry->name;

        switch (task.kind) {
        case Task::Kind::Download:
            job = m_manager.createDownloadJob(task.id);
            m_progressLabel->setText(i18nc("@info:progress", "Downloading %1…", name));
            break;
        case Task::Kind::Import:
            job = m_manager.createImportJob(task.id);
            m_progressLabel->setText(i18nc("@info:progress", "Importing %1…", name));
            break;
        case Task::Kind::UpdateCheck:
            job = m_manager.createUpdateCheckJob(task.id);
            m_progressLabel->setText(i18nc("@info:progress", "Checking %1 for updates…", name));
            break;
        }

        if (job == nullptr) {
            showMessage(KMessageWidget::Error,
                        xi18nc("@info", "Select a source for <resource>%1</resource> in Configure Dictionary.", name));
        }
    }

    m_job = job;
    m_progress->setValue(0);
    if (task.kind == Task::Kind::Import)
        m_model->setImportProgress(task.id, 0);

    const QUuid id = task.id;
    connect(job, &KJob::percentChanged, this, [this](KJob *, unsigned long percent) {
        m_progress->setValue(static_cast<int>(percent));
    });
    if (auto *import = qobject_cast<dict::DictionaryImportJob *>(job)) {
        connect(import, &dict::DictionaryImportJob::progress, this, [this, id](int percent, const QString &) {
            m_model->setImportProgress(id, percent);
        });
    }
    connect(job, &KJob::result, this, [this, id, kind = task.kind](KJob *finished) {
        switch (kind) {
        case Task::Kind::Download:
            onDownloadFinished(finished, id);
            break;
        case Task::Kind::Import:
            onImportFinished(finished, id);
            break;
        case Task::Kind::UpdateCheck:
            onUpdateCheckFinished(finished, id);
            break;
        }
        m_job = nullptr;
        startNextTask();
    });

    if (m_jobTracking)
        KIO::getJobTracker()->registerJob(job);
    setBusy(true);
    job->start();
}

void DictionaryManagerDialog::onDownloadFinished(KJob *job, const QUuid &id)
{
    const dict::Dictionary *entry = m_manager.dictionary(id);
    const QString name = entry != nullptr ? entry->name : QString();

    if (job->error() != KJob::NoError) {
        if (job->error() != KJob::KilledJobError) {
            auto *retry = new QAction(i18nc("@action:button", "Retry"), this);
            connect(retry, &QAction::triggered, this, [this, id] {
                clearMessage();
                enqueue({.id = id, .kind = Task::Kind::Download});
            });
            showMessage(
                KMessageWidget::Error,
                xi18nc("@info", "<resource>%1</resource> could not be downloaded: %2", name, job->errorString()),
                {retry});
        }
        return;
    }

    auto *download = qobject_cast<dict::DictionaryDownloadJob *>(job);
    if (download == nullptr)
        return;

    const bool hadData = entry != nullptr && entry->recordCount > 0;
    m_manager.setSourcePath(id, download->targetPath());
    scheduleSave();

    if (download->notModified() && hadData) {
        showMessage(KMessageWidget::Positive, xi18nc("@info", "<resource>%1</resource> is already up to date.", name));
        return;
    }
    m_queue.prepend({.id = id, .kind = Task::Kind::Import});
}

void DictionaryManagerDialog::onImportFinished(KJob *job, const QUuid &id)
{
    m_model->clearImportProgress(id);
    const dict::Dictionary *entry = m_manager.dictionary(id);
    const QString name = entry != nullptr ? entry->name : QString();

    auto *import = qobject_cast<dict::DictionaryImportJob *>(job);
    if (job->error() != KJob::NoError || import == nullptr) {
        if (job->error() == KJob::KilledJobError)
            return;
        const QString reason = job->errorString();
        QList<QAction *> actions;
        if (entry != nullptr && (entry->sourcePath.isEmpty() || DictionaryModel::isSourceMissing(*entry))) {
            auto *choose = new QAction(i18nc("@action:button", "Configure Dictionary…"), this);
            connect(choose, &QAction::triggered, this, [this, id] {
                clearMessage();
                const QModelIndex index = m_model->indexForId(id);
                if (index.isValid())
                    m_view->setCurrentIndex(index);
                configureSelected();
            });
            actions.append(choose);
        } else {
            auto *retry = new QAction(i18nc("@action:button", "Retry"), this);
            connect(retry, &QAction::triggered, this, [this, id] {
                clearMessage();
                enqueue({.id = id, .kind = Task::Kind::Import});
            });
            actions.append(retry);
        }
        showMessage(KMessageWidget::Error,
                    xi18nc("@info", "<resource>%1</resource> could not be imported: %2", name, reason),
                    actions);
        Q_EMIT importFailed(id, reason);
        return;
    }

    if (!m_manager.applyImportResult(id, import)) {
        showMessage(KMessageWidget::Error,
                    xi18nc("@info", "<resource>%1</resource> was imported but could not be opened.", name));
        Q_EMIT importFailed(id, import->result().errorString);
        return;
    }
    scheduleSave();
    showMessage(KMessageWidget::Positive,
                xi18nc("@info",
                       "<resource>%1</resource> was imported: %2 entries.",
                       name,
                       QLocale().toString(import->result().recordCount)));
    Q_EMIT dictionaryImported(id);
}

void DictionaryManagerDialog::onUpdateCheckFinished(KJob *job, const QUuid &id)
{
    const dict::Dictionary *entry = m_manager.dictionary(id);
    const QString name = entry != nullptr ? entry->name : QString();

    if (job->error() != KJob::NoError) {
        if (job->error() != KJob::KilledJobError) {
            showMessage(
                KMessageWidget::Error,
                xi18nc(
                    "@info", "<resource>%1</resource> could not be checked for updates: %2", name, job->errorString()));
        }
        return;
    }

    auto *check = qobject_cast<dict::UpdateCheckJob *>(job);
    if (check == nullptr || !check->updateAvailable()) {
        showMessage(KMessageWidget::Positive, xi18nc("@info", "<resource>%1</resource> is up to date.", name));
        return;
    }

    auto *download = new QAction(i18nc("@action:button", "Download"), this);
    connect(download, &QAction::triggered, this, [this, id] {
        clearMessage();
        enqueue({.id = id, .kind = Task::Kind::Download});
    });
    showMessage(KMessageWidget::Information,
                xi18nc("@info", "An update for <resource>%1</resource> is available.", name),
                {download});
}

void DictionaryManagerDialog::setBusy(bool busy)
{
    const bool running = busy && (m_job != nullptr || !m_queue.isEmpty());
    m_progress->setVisible(running);
    m_progressLabel->setVisible(running);
    m_cancelButton->setVisible(running);
    if (!running)
        m_progressLabel->clear();
    Q_EMIT busyChanged(running);
}

bool DictionaryManagerDialog::isBusy() const
{
    return m_job != nullptr || !m_queue.isEmpty();
}

void DictionaryManagerDialog::showMessage(int messageType, const QString &text, const QList<QAction *> &actions)
{
    clearMessage();
    m_message->setMessageType(static_cast<KMessageWidget::MessageType>(messageType));
    m_message->setText(text);
    for (QAction *action : actions) {
        m_message->addAction(action);
        m_messageActions.append(action);
    }
    m_message->show();
}

void DictionaryManagerDialog::clearMessage()
{
    for (QAction *action : std::as_const(m_messageActions)) {
        m_message->removeAction(action);
        action->deleteLater();
    }
    m_messageActions.clear();
    m_message->hide();
}

void DictionaryManagerDialog::scheduleSave()
{
    if (m_savePending)
        return;
    m_savePending = true;
    // One write per event-loop turn: a reorder emits changed() from move() and again from
    // renumber()'s caller, and the list file is rewritten whole either way.
    QTimer::singleShot(0, this, [this] {
        m_savePending = false;
        (void)m_manager.save();
    });
}

} // namespace maru
