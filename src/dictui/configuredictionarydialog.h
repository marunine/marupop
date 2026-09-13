// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Per-dictionary options dialog. It constructs only controls applicable to the
// selected dictionary type.
#pragma once

#include "dict/dictionary.h"

#include <QDialog>
#include <QList>
#include <QUuid>

#include <utility>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class KMessageWidget;

namespace maru::dict
{
class DictionaryManager;
}

namespace maru
{

class ConfigureDictionaryDialog : public QDialog
{
    Q_OBJECT

public:
    ConfigureDictionaryDialog(dict::DictionaryManager &manager, const QUuid &id, QWidget *parent = nullptr);
    ~ConfigureDictionaryDialog() override;

    [[nodiscard]] QUuid dictionaryId() const
    {
        return m_id;
    }

    // Whether what OK applied invalidates the store, so the caller starts an import job. False
    // until the dialog is accepted.
    [[nodiscard]] bool requiresReimport() const
    {
        return m_requiresReimport;
    }

    void accept() override;

private:
    void buildUi(const dict::Dictionary &dictionary);
    void chooseSource();
    void updateReimportNotice();

    dict::DictionaryManager &m_manager;
    QUuid m_id;

    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_enabledBox = nullptr;
    QLineEdit *m_sourceEdit = nullptr;
    QPushButton *m_changeSourceButton = nullptr;
    QSpinBox *m_autoUpdateSpin = nullptr;
    KMessageWidget *m_reimportMessage = nullptr;

    // Every option checkbox the type made applicable, paired with the DictOptions member it
    // edits. A pointer to member keeps the read-back a loop rather than one branch per option.
    QList<std::pair<QCheckBox *, bool dict::DictOptions::*>> m_boolOptions;
    // The subset whose change forces a re-import, which drives the message widget.
    QList<QCheckBox *> m_reimportOptions;

    dict::DictOptions m_original;
    QString m_originalSource;
    bool m_requiresReimport = false;
};

} // namespace maru
