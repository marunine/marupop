// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Adds one line to a custom word or name list in JL's tab-separated format.
// Stores are replaced through import rather than edited in place, so the manager
// re-imports the list after the new line is appended.
#pragma once

#include <QDialog>
#include <QString>
#include <QUuid>

class QComboBox;
class QLineEdit;
class QFormLayout;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class KMessageWidget;

namespace maru::dict
{
class DictionaryManager;
}

namespace maru
{

class AddCustomEntryDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode
    {
        Word,
        Name,
    };
    Q_ENUM(Mode)

    AddCustomEntryDialog(dict::DictionaryManager &manager, Mode mode, QWidget *parent = nullptr);
    ~AddCustomEntryDialog() override;

    // Preselects a target, which is the row the manager dialog had selected.
    void setSelectedDictionary(const QUuid &id);

    // Whether the manager holds a custom list of the right kind at all. False leaves the dialog
    // with nothing to write to, and the caller says so instead of showing it.
    [[nodiscard]] bool hasTargets() const;

    // The list the entry was appended to, null until OK was accepted.
    [[nodiscard]] QUuid dictionaryId() const
    {
        return m_dictionaryId;
    }

    // The line that was written, for a caller that logs it and for the test.
    [[nodiscard]] QString appendedLine() const
    {
        return m_appendedLine;
    }

    void accept() override;

private:
    void buildWordFields();
    void buildNameFields();
    void populateTargets();
    [[nodiscard]] QString partOfSpeech() const;
    void showError(const QString &text);

    dict::DictionaryManager &m_manager;
    Mode m_mode;

    KMessageWidget *m_message = nullptr;
    QComboBox *m_targetCombo = nullptr;
    QFormLayout *m_form = nullptr;

    // Word mode.
    QLineEdit *m_spellingsEdit = nullptr;
    QLineEdit *m_readingsEdit = nullptr;
    QPlainTextEdit *m_definitionsEdit = nullptr;
    QRadioButton *m_verbButton = nullptr;
    QRadioButton *m_adjectiveButton = nullptr;
    QRadioButton *m_nounButton = nullptr;
    QRadioButton *m_otherButton = nullptr;
    QLineEdit *m_wordClassesEdit = nullptr;

    // Name mode.
    QLineEdit *m_spellingEdit = nullptr;
    QLineEdit *m_readingEdit = nullptr;
    QComboBox *m_nameTypeCombo = nullptr;
    QPlainTextEdit *m_extraInfoEdit = nullptr;

    QUuid m_dictionaryId;
    QString m_appendedLine;
};

} // namespace maru
