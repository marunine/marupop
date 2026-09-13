// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The dictionary list as a table model, over the live dict::DictionaryManager.
//
// JL rebuilds its whole ListBox ItemsSource after every edit
// (JL.Windows/GUI/Dictionary/ManageDictionariesWindow.xaml.cs), which drops the selection and the
// scroll position on each toggle. This mirrors the manager's own signals onto the fine-grained
// model signals instead, so a check-state toggle emits dataChanged for one cell and a reorder
// emits beginMoveRows.
#pragma once

#include "dict/dictionary.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QUuid>

namespace maru::dict
{
class DictionaryManager;
}

namespace maru
{

// The Type column's text. dict::dictTypeName() carries the same English strings but is not
// translatable at the call site, because it is reached with a runtime value; this repeats them
// as i18nc() literals so the extractor sees them.
[[nodiscard]] QString localizedDictTypeName(dict::DictType type);

class DictionaryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn = 0,
        TypeColumn,
        EntriesColumn,
        UpdatedColumn,
        ColumnCount,
    };

    enum Role
    {
        // The dict::Dictionary::id of the row, for a view that has to name an entry.
        IdRole = Qt::UserRole + 1,
        // True while the source file or directory the entry names is gone.
        SourceMissingRole,
    };

    explicit DictionaryModel(dict::DictionaryManager &manager, QObject *parent = nullptr);
    ~DictionaryModel() override;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Internal-move drag reordering. The payload is the source row alone: the model is not a drop
    // target for anything else, so nothing is gained by encoding the cells.
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData *mimeData(const QModelIndexList &indexes) const override;
    [[nodiscard]] bool canDropMimeData(
        const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;
    bool
    dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

    // destinationChild is a beginMoveRows() destination: the row the moved entry ends up before,
    // in the coordinates the list has now. Calls dict::DictionaryManager::move().
    bool moveRow(int from, int destinationChild);
    bool moveUp(int row);
    bool moveDown(int row);
    bool moveToTop(int row);
    bool moveToBottom(int row);

    [[nodiscard]] QUuid idAt(const QModelIndex &index) const;
    [[nodiscard]] dict::Dictionary *dictionaryAt(const QModelIndex &index) const;
    [[nodiscard]] QModelIndex indexForId(const QUuid &id, int column = NameColumn) const;

    // Replaces the Entries cell of id with an import progress reading. A percent below zero
    // clears it and restores the record count.
    void setImportProgress(const QUuid &id, int percent);
    void clearImportProgress(const QUuid &id);
    [[nodiscard]] bool isImporting(const QUuid &id) const;

    // Whether the source path of dictionary is absent from disk. Empty for a built-in that was
    // never downloaded, which is reported as missing only once it has been imported.
    [[nodiscard]] static bool isSourceMissing(const dict::Dictionary &dictionary);

private:
    void onDictionaryAdded(const QUuid &id);
    void onDictionaryRemoved(const QUuid &id);
    void onDictionaryChanged(const QUuid &id);
    void onManagerChanged();
    void resync();
    [[nodiscard]] int rowForId(const QUuid &id) const;
    [[nodiscard]] QList<QUuid> managerOrder() const;

    dict::DictionaryManager &m_manager;
    QList<QUuid> m_order;
    QHash<QUuid, int> m_progress;
    // Set while this model is the one mutating the manager, so the manager's own change signals
    // do not reorder a list that is already mid-move.
    bool m_reordering = false;
};

} // namespace maru
