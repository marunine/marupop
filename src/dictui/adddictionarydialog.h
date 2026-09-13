// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Add Dictionary dialog reads index.json and bank file names when a path is
// chosen, preselects the dictionary type and reports validation problems inline.
// Confirmation hands a new dict::Dictionary to the manager dialog.
#pragma once

#include "dict/dicttypes.h"
#include "dict/importers/yomitanimporter.h"

#include <QDialog>
#include <QString>
#include <QUuid>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class KMessageWidget;
class KUrlRequester;

namespace maru::dict
{
class DictionaryManager;
}

namespace maru
{

class AddDictionaryDialog : public QDialog
{
    Q_OBJECT

public:
    // What the Format combo offers. A built-in entry names a dictionary the manager has already
    // seeded and only has to download, so it carries no path field.
    enum class Format
    {
        Yomitan = 0,
        CustomWord,
        CustomName,
        BuiltIn,
    };
    Q_ENUM(Format)

    explicit AddDictionaryDialog(dict::DictionaryManager &manager, QWidget *parent = nullptr);
    ~AddDictionaryDialog() override;

    // Preselects one of the three file-backed formats. False when the combo does not offer it.
    bool selectFormat(Format format);

    // Preselects the download entry of a built-in that has not been fetched yet. False when the
    // manager has no such entry, which is what the Add menu filters on already.
    bool selectBuiltIn(dict::DictType type);

    // The entry the dialog created or selected, null until OK was accepted.
    [[nodiscard]] QUuid createdId() const
    {
        return m_createdId;
    }

    // Whether the entry has no local source yet, so the caller runs a download job before the
    // import job.
    [[nodiscard]] bool needsDownload() const
    {
        return m_needsDownload;
    }

    // The type the user settled on, which is what the import job is created for.
    [[nodiscard]] dict::DictType selectedType() const;

    void accept() override;

private:
    void buildUi();
    void populateFormats();
    [[nodiscard]] Format currentFormat() const;
    void onFormatChanged();
    void onPathChanged();
    void refreshFromSource();
    // The first problem that stops the entry from being created, empty when there is none.
    [[nodiscard]] QString validationError() const;
    // A problem worth reporting that does not stop the entry from being created.
    [[nodiscard]] QString validationWarning() const;
    void revalidate();
    [[nodiscard]] QString localPath() const;

    dict::DictionaryManager &m_manager;

    QComboBox *m_formatCombo = nullptr;
    QLabel *m_pathLabel = nullptr;
    KUrlRequester *m_pathRequester = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_treatAsLabel = nullptr;
    QComboBox *m_treatAsCombo = nullptr;
    KMessageWidget *m_message = nullptr;
    QPushButton *m_okButton = nullptr;

    // Set once the user types in the Name field, after which index.json no longer overwrites it.
    bool m_nameEdited = false;
    dict::YomitanIndex m_index;
    QString m_inspectedPath;
    bool m_banksFound = false;

    QUuid m_createdId;
    bool m_needsDownload = false;
};

} // namespace maru
