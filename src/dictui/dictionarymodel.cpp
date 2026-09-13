// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictui/dictionarymodel.h"

#include "core/logging.h"
#include "dict/dictionarymanager.h"

#include <QDataStream>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QMimeData>
#include <QScopedValueRollback>

#include <KColorScheme>
#include <KFormat>
#include <KLocalizedString>

#include <algorithm>

namespace maru
{

namespace
{

// The one payload an internal move carries. A private type keeps the tree from accepting a drag
// out of another view.
constexpr QLatin1StringView rowMimeType("application/x-marupop-dictionary-row");

QColor negativeTextColor()
{
    return KColorScheme(QPalette::Normal, KColorScheme::Window).foreground(KColorScheme::NegativeText).color();
}

} // namespace

QString localizedDictTypeName(dict::DictType type)
{
    switch (type) {
    case dict::DictType::JMdict:
        return i18nc("@item dictionary type", "JMdict");
    case dict::DictType::JMnedict:
        return i18nc("@item dictionary type", "JMnedict");
    case dict::DictType::Kanjidic:
        return i18nc("@item dictionary type", "KANJIDIC2");
    case dict::DictType::YomitanWord:
        return i18nc("@item dictionary type", "Word dictionary (Yomitan)");
    case dict::DictType::YomitanKanji:
        return i18nc("@item dictionary type", "Kanji dictionary (Yomitan)");
    case dict::DictType::YomitanKanjiWordSchema:
        return i18nc("@item dictionary type", "Kanji dictionary in term-bank format (Yomitan)");
    case dict::DictType::YomitanName:
        return i18nc("@item dictionary type", "Name dictionary (Yomitan)");
    case dict::DictType::YomitanPitchAccent:
        return i18nc("@item dictionary type", "Pitch accent (Yomitan)");
    case dict::DictType::YomitanOther:
        return i18nc("@item dictionary type", "Other (Yomitan)");
    case dict::DictType::YomitanFrequency:
        return i18nc("@item dictionary type", "Frequency dictionary (Yomitan)");
    case dict::DictType::YomitanKanjiFrequency:
        return i18nc("@item dictionary type", "Kanji frequency dictionary (Yomitan)");
    case dict::DictType::CustomWord:
        return i18nc("@item dictionary type", "Custom word list");
    case dict::DictType::CustomName:
        return i18nc("@item dictionary type", "Custom name list");
    case dict::DictType::KanjiComponents:
        return i18nc("@item dictionary type", "Kanji components");
    }
    return dict::dictTypeName(type);
}

DictionaryModel::DictionaryModel(dict::DictionaryManager &manager, QObject *parent)
    : QAbstractTableModel(parent)
    , m_manager(manager)
    , m_order(managerOrder())
{
    connect(&m_manager, &dict::DictionaryManager::dictionaryAdded, this, &DictionaryModel::onDictionaryAdded);
    connect(&m_manager, &dict::DictionaryManager::dictionaryRemoved, this, &DictionaryModel::onDictionaryRemoved);
    connect(&m_manager, &dict::DictionaryManager::dictionaryChanged, this, &DictionaryModel::onDictionaryChanged);
    connect(&m_manager, &dict::DictionaryManager::changed, this, &DictionaryModel::onManagerChanged);
    connect(&m_manager, &dict::DictionaryManager::ready, this, &DictionaryModel::onDictionaryChanged);
}

DictionaryModel::~DictionaryModel() = default;

QList<QUuid> DictionaryModel::managerOrder() const
{
    QList<QUuid> order;
    const QList<dict::Dictionary *> entries = m_manager.dictionaries();
    order.reserve(entries.size());
    for (const dict::Dictionary *entry : entries)
        order.append(entry->id);
    return order;
}

int DictionaryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_order.size());
}

int DictionaryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

int DictionaryModel::rowForId(const QUuid &id) const
{
    return static_cast<int>(m_order.indexOf(id));
}

QUuid DictionaryModel::idAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_order.size())
        return {};
    return m_order.at(index.row());
}

dict::Dictionary *DictionaryModel::dictionaryAt(const QModelIndex &index) const
{
    const QUuid id = idAt(index);
    return id.isNull() ? nullptr : m_manager.dictionary(id);
}

QModelIndex DictionaryModel::indexForId(const QUuid &id, int column) const
{
    const int row = rowForId(id);
    return row < 0 ? QModelIndex() : index(row, column);
}

bool DictionaryModel::isSourceMissing(const dict::Dictionary &dictionary)
{
    if (dictionary.sourcePath.isEmpty())
        return false;
    return !QFileInfo::exists(dictionary.sourcePath);
}

QVariant DictionaryModel::data(const QModelIndex &index, int role) const
{
    const dict::Dictionary *entry = dictionaryAt(index);
    if (entry == nullptr)
        return {};

    const bool missing = isSourceMissing(*entry);
    const bool broken = missing || entry->needsReimport;

    switch (role) {
    case IdRole:
        return entry->id;
    case SourceMissingRole:
        return missing;
    default:
        break;
    }

    switch (index.column()) {
    case NameColumn:
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return entry->name;
        case Qt::CheckStateRole:
            return entry->enabled ? Qt::Checked : Qt::Unchecked;
        case Qt::DecorationRole:
            return broken ? QIcon::fromTheme(QStringLiteral("dialog-warning")) : QIcon();
        case Qt::ForegroundRole:
            return broken ? QVariant::fromValue(negativeTextColor()) : QVariant();
        case Qt::ToolTipRole: {
            if (missing) {
                return i18nc("@info:tooltip",
                             "Dictionary source missing: %1. Use Configure Dictionary to select the source, or "
                             "Download to download it again.",
                             entry->sourcePath);
            }
            if (entry->needsReimport) {
                return i18nc("@info:tooltip", "Reimport this dictionary to use it with this version of MaruPop.");
            }
            return entry->name;
        }
        default:
            break;
        }
        break;

    case TypeColumn:
        if (role == Qt::DisplayRole)
            return localizedDictTypeName(entry->type);
        break;

    case EntriesColumn:
        switch (role) {
        case Qt::DisplayRole: {
            const auto progress = m_progress.constFind(entry->id);
            if (progress != m_progress.constEnd())
                return i18nc("@info:status a dictionary import in progress", "Importing… %1%", progress.value());
            if (entry->sourcePath.isEmpty())
                return i18nc("@info:status a built-in dictionary that was never fetched", "Not downloaded");
            return QLocale().toString(entry->recordCount);
        }
        case Qt::TextAlignmentRole:
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        default:
            break;
        }
        break;

    case UpdatedColumn:
        if (role == Qt::DisplayRole) {
            if (!entry->importedAt.isValid())
                return QStringLiteral("—");
            return KFormat().formatRelativeDateTime(entry->importedAt.toLocalTime(), QLocale::ShortFormat);
        }
        break;

    default:
        break;
    }

    return {};
}

bool DictionaryModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    dict::Dictionary *entry = dictionaryAt(index);
    if (entry == nullptr)
        return false;

    if (index.column() == NameColumn && role == Qt::CheckStateRole) {
        const bool enabled = value.value<Qt::CheckState>() == Qt::Checked;
        if (enabled == entry->enabled)
            return true;
        return m_manager.setEnabled(entry->id, enabled);
    }
    if (index.column() == NameColumn && role == Qt::EditRole) {
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || name == entry->name)
            return false;
        return m_manager.rename(entry->id, name);
    }
    return false;
}

QVariant DictionaryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case NameColumn:
        return i18nc("@title:column dictionary name", "Name");
    case TypeColumn:
        return i18nc("@title:column dictionary format", "Type");
    case EntriesColumn:
        return i18nc("@title:column number of records", "Entries");
    case UpdatedColumn:
        return i18nc("@title:column when the dictionary was last imported", "Updated");
    default:
        return {};
    }
}

Qt::ItemFlags DictionaryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;

    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    if (index.column() == NameColumn)
        result |= Qt::ItemIsUserCheckable;
    return result;
}

Qt::DropActions DictionaryModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList DictionaryModel::mimeTypes() const
{
    return {QString(rowMimeType)};
}

QMimeData *DictionaryModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return nullptr;

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << indexes.constFirst().row();

    auto *data = new QMimeData;
    data->setData(QString(rowMimeType), payload);
    return data;
}

bool DictionaryModel::canDropMimeData(
    const QMimeData *data, Qt::DropAction action, int /*row*/, int /*column*/, const QModelIndex & /*parent*/) const
{
    return action == Qt::MoveAction && data != nullptr && data->hasFormat(QString(rowMimeType));
}

bool DictionaryModel::dropMimeData(
    const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;

    QByteArray payload = data->data(QString(rowMimeType));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    int source = -1;
    stream >> source;

    // A drop between two rows arrives as row; a drop onto a row arrives as row -1 with the target
    // in parent, which the tree produces when the pointer is over the middle of an item.
    int destination = row;
    if (destination < 0)
        destination = parent.isValid() ? parent.row() : rowCount();

    return moveRow(source, destination);
}

bool DictionaryModel::moveRow(int from, int destinationChild)
{
    const int count = rowCount();
    if (from < 0 || from >= count || destinationChild < 0 || destinationChild > count)
        return false;
    if (destinationChild == from || destinationChild == from + 1)
        return false;

    const int target = destinationChild > from ? destinationChild - 1 : destinationChild;
    if (!beginMoveRows({}, from, from, {}, destinationChild))
        return false;

    const QUuid id = m_order.at(from);
    m_order.move(from, target);
    {
        const QScopedValueRollback<bool> guard(m_reordering, true);
        if (!m_manager.move(id, target))
            qCWarning(logDict) << "The dictionary manager refused to move" << id;
    }
    endMoveRows();
    return true;
}

bool DictionaryModel::moveUp(int row)
{
    return moveRow(row, row - 1);
}

bool DictionaryModel::moveDown(int row)
{
    return moveRow(row, row + 2);
}

bool DictionaryModel::moveToTop(int row)
{
    return moveRow(row, 0);
}

bool DictionaryModel::moveToBottom(int row)
{
    return moveRow(row, rowCount());
}

void DictionaryModel::setImportProgress(const QUuid &id, int percent)
{
    if (percent < 0) {
        clearImportProgress(id);
        return;
    }
    m_progress.insert(id, percent);
    const int row = rowForId(id);
    if (row >= 0)
        Q_EMIT dataChanged(index(row, EntriesColumn), index(row, EntriesColumn));
}

void DictionaryModel::clearImportProgress(const QUuid &id)
{
    if (!m_progress.remove(id))
        return;
    const int row = rowForId(id);
    if (row >= 0)
        Q_EMIT dataChanged(index(row, EntriesColumn), index(row, EntriesColumn));
}

bool DictionaryModel::isImporting(const QUuid &id) const
{
    return m_progress.contains(id);
}

void DictionaryModel::onDictionaryAdded(const QUuid &id)
{
    if (m_reordering || m_order.contains(id))
        return;

    const QList<QUuid> order = managerOrder();
    const int row = static_cast<int>(order.indexOf(id));
    if (row < 0) {
        resync();
        return;
    }
    beginInsertRows({}, row, row);
    m_order.insert(row, id);
    endInsertRows();
}

void DictionaryModel::onDictionaryRemoved(const QUuid &id)
{
    const int row = rowForId(id);
    if (m_reordering || row < 0)
        return;

    beginRemoveRows({}, row, row);
    m_order.removeAt(row);
    endRemoveRows();
    m_progress.remove(id);
}

void DictionaryModel::onDictionaryChanged(const QUuid &id)
{
    if (m_reordering)
        return;
    const int row = rowForId(id);
    if (row < 0)
        return;
    Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void DictionaryModel::onManagerChanged()
{
    if (m_reordering)
        return;
    // load() and a reorder performed elsewhere replace the whole order without a per-entry
    // signal, which is the only case resync() is needed for. Anything else that reached the
    // manager without naming an entry still gets the rows redrawn, which is one pass over a list
    // that is never longer than a few dozen.
    if (managerOrder() != m_order) {
        resync();
        return;
    }
    if (!m_order.isEmpty())
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
}

void DictionaryModel::resync()
{
    const QList<QUuid> order = managerOrder();
    // A permutation of the same entries is a layout change, which carries the persistent indexes,
    // and with them the view's selection, to the rows their entries moved to.
    if (std::ranges::is_permutation(order, m_order)) {
        Q_EMIT layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        const QModelIndexList from = persistentIndexList();
        QModelIndexList to;
        to.reserve(from.size());
        for (const QModelIndex &persistent : from)
            to.append(index(static_cast<int>(order.indexOf(m_order.at(persistent.row()))), persistent.column()));
        m_order = order;
        changePersistentIndexList(from, to);
        Q_EMIT layoutChanged({}, QAbstractItemModel::VerticalSortHint);
        return;
    }

    beginResetModel();
    m_order = order;
    for (auto it = m_progress.begin(); it != m_progress.end();)
        it = m_order.contains(it.key()) ? std::next(it) : m_progress.erase(it);
    endResetModel();
}

} // namespace maru
