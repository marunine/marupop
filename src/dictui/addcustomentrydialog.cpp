// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictui/addcustomentrydialog.h"

#include "dict/dictionarymanager.h"
#include "dict/importers/customnameimporter.h"
#include "dict/importers/customwordimporter.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QVBoxLayout>

#include <KLocalizedString>
#include <KMessageWidget>

namespace maru
{

namespace
{

// The list fields of both formats are semicolon separated, so the dialog splits on the same
// character the file uses rather than inventing a second convention.
QList<QString> splitList(const QString &text)
{
    QList<QString> parts;
    const QList<QStringView> pieces = QStringView(text).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QStringView piece : pieces) {
        const QString trimmed = piece.trimmed().toString();
        if (!trimmed.isEmpty())
            parts.append(trimmed);
    }
    return parts;
}

} // namespace

AddCustomEntryDialog::AddCustomEntryDialog(dict::DictionaryManager &manager, Mode mode, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_mode(mode)
{
    setObjectName(QStringLiteral("addCustomEntryDialog"));
    setWindowTitle(mode == Mode::Word ? i18nc("@title:window", "Add Word") : i18nc("@title:window", "Add Name"));

    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);

    m_message = new KMessageWidget(this);
    m_message->setObjectName(QStringLiteral("messageWidget"));
    m_message->setMessageType(KMessageWidget::Error);
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(false);
    m_message->hide();
    layout->addWidget(m_message);

    m_form = new QFormLayout;
    layout->addLayout(m_form);

    m_targetCombo = new QComboBox(this);
    m_targetCombo->setObjectName(QStringLiteral("targetCombo"));
    m_form->addRow(i18nc("@label:listbox", "Add to:"), m_targetCombo);
    populateTargets();

    if (mode == Mode::Word)
        buildWordFields();
    else
        buildNameFields();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddCustomEntryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AddCustomEntryDialog::reject);
}

AddCustomEntryDialog::~AddCustomEntryDialog() = default;

void AddCustomEntryDialog::populateTargets()
{
    const dict::DictType wanted = m_mode == Mode::Word ? dict::DictType::CustomWord : dict::DictType::CustomName;
    const QList<dict::Dictionary *> entries = m_manager.dictionaries();
    for (const dict::Dictionary *entry : entries) {
        if (entry->type == wanted && !entry->sourcePath.isEmpty())
            m_targetCombo->addItem(entry->name, entry->id);
    }
}

bool AddCustomEntryDialog::hasTargets() const
{
    return m_targetCombo->count() > 0;
}

void AddCustomEntryDialog::setSelectedDictionary(const QUuid &id)
{
    const int row = m_targetCombo->findData(id);
    if (row >= 0)
        m_targetCombo->setCurrentIndex(row);
}

void AddCustomEntryDialog::buildWordFields()
{
    m_spellingsEdit = new QLineEdit(this);
    m_spellingsEdit->setObjectName(QStringLiteral("spellingsEdit"));
    m_spellingsEdit->setPlaceholderText(i18nc("@info:placeholder", "Separate spellings with semicolons"));
    m_form->addRow(i18nc("@label:textbox", "Spellings:"), m_spellingsEdit);

    m_readingsEdit = new QLineEdit(this);
    m_readingsEdit->setObjectName(QStringLiteral("readingsEdit"));
    m_readingsEdit->setPlaceholderText(i18nc("@info:placeholder", "Separate readings with semicolons"));
    m_form->addRow(i18nc("@label:textbox", "Readings:"), m_readingsEdit);

    m_definitionsEdit = new QPlainTextEdit(this);
    m_definitionsEdit->setObjectName(QStringLiteral("definitionsEdit"));
    // A semicolon starts the next definition and a line break stays inside the one being typed,
    // which is the file format read back the way it is written: JL's loader splits on ';' and
    // un-escapes \n.
    m_definitionsEdit->setPlaceholderText(
        i18nc("@info:placeholder", "Separate definitions with semicolons. Line breaks are preserved."));
    // Four lines: enough for a definition that wraps, and short enough that the part-of-speech
    // row below it stays in view.
    m_definitionsEdit->setFixedHeight(4 * m_definitionsEdit->fontMetrics().lineSpacing() + 12);
    m_form->addRow(i18nc("@label:textbox", "Definitions:"), m_definitionsEdit);

    auto *classRow = new QWidget(this);
    auto *classLayout = new QHBoxLayout(classRow);
    classLayout->setContentsMargins(0, 0, 0, 0);
    auto *group = new QButtonGroup(this);
    const auto addRadio = [&](QRadioButton *&member, const QString &objectName, const QString &label) {
        member = new QRadioButton(label, classRow);
        member->setObjectName(objectName);
        group->addButton(member);
        classLayout->addWidget(member);
    };
    addRadio(m_verbButton, QStringLiteral("verbButton"), i18nc("@option:radio part of speech", "Verb"));
    addRadio(m_adjectiveButton, QStringLiteral("adjectiveButton"), i18nc("@option:radio part of speech", "Adjective"));
    addRadio(m_nounButton, QStringLiteral("nounButton"), i18nc("@option:radio part of speech", "Noun"));
    addRadio(m_otherButton, QStringLiteral("otherButton"), i18nc("@option:radio part of speech", "Other"));
    m_nounButton->setChecked(true);
    classLayout->addStretch();
    m_form->addRow(i18nc("@label", "Part of speech:"), classRow);

    m_wordClassesEdit = new QLineEdit(this);
    m_wordClassesEdit->setObjectName(QStringLiteral("wordClassesEdit"));
    m_wordClassesEdit->setPlaceholderText(i18nc("@info:placeholder", "For example: v5r;vt"));
    m_wordClassesEdit->setToolTip(
        i18nc("@info:tooltip",
              "JMdict part-of-speech tags used for deconjugation. Leave empty to use the selected part of speech."));
    m_form->addRow(i18nc("@label:textbox", "Part-of-speech tags:"), m_wordClassesEdit);
}

void AddCustomEntryDialog::buildNameFields()
{
    m_spellingEdit = new QLineEdit(this);
    m_spellingEdit->setObjectName(QStringLiteral("spellingEdit"));
    m_form->addRow(i18nc("@label:textbox", "Spelling:"), m_spellingEdit);

    m_readingEdit = new QLineEdit(this);
    m_readingEdit->setObjectName(QStringLiteral("readingEdit"));
    m_form->addRow(i18nc("@label:textbox", "Reading:"), m_readingEdit);

    m_nameTypeCombo = new QComboBox(this);
    m_nameTypeCombo->setObjectName(QStringLiteral("nameTypeCombo"));
    m_nameTypeCombo->setEditable(true);
    m_nameTypeCombo->setInsertPolicy(QComboBox::NoInsert);
    m_nameTypeCombo->addItems({i18nc("@item:inlistbox name type", "Surname"),
                               i18nc("@item:inlistbox name type", "Female"),
                               i18nc("@item:inlistbox name type", "Male"),
                               i18nc("@item:inlistbox name type", "Given"),
                               i18nc("@item:inlistbox name type", "Place"),
                               i18nc("@item:inlistbox name type", "Organization"),
                               i18nc("@item:inlistbox name type", "Other")});
    m_form->addRow(i18nc("@label:listbox", "Name type:"), m_nameTypeCombo);

    m_extraInfoEdit = new QPlainTextEdit(this);
    m_extraInfoEdit->setObjectName(QStringLiteral("extraInfoEdit"));
    m_extraInfoEdit->setFixedHeight(3 * m_extraInfoEdit->fontMetrics().lineSpacing() + 12);
    m_form->addRow(i18nc("@label:textbox", "Notes:"), m_extraInfoEdit);
}

QString AddCustomEntryDialog::partOfSpeech() const
{
    if (m_verbButton->isChecked())
        return QStringLiteral("Verb");
    if (m_adjectiveButton->isChecked())
        return QStringLiteral("Adjective");
    if (m_nounButton->isChecked())
        return QStringLiteral("Noun");
    return QStringLiteral("Other");
}

void AddCustomEntryDialog::showError(const QString &text)
{
    m_message->setMessageType(KMessageWidget::Error);
    m_message->setText(text);
    m_message->show();
}

void AddCustomEntryDialog::accept()
{
    const QUuid target = m_targetCombo->currentData().toUuid();
    const dict::Dictionary *entry = m_manager.dictionary(target);
    if (entry == nullptr || entry->sourcePath.isEmpty()) {
        showError(i18nc("@info", "Select a word or name list."));
        return;
    }

    if (m_mode == Mode::Word) {
        const QList<QString> spellings = splitList(m_spellingsEdit->text());
        if (spellings.isEmpty()) {
            showError(i18nc("@info", "Enter at least one spelling."));
            return;
        }
        const QList<QString> definitions = splitList(m_definitionsEdit->toPlainText());
        if (definitions.isEmpty()) {
            showError(i18nc("@info", "Enter at least one definition."));
            return;
        }
        const QList<QString> readings = splitList(m_readingsEdit->text());
        const QList<QString> wordClasses = splitList(m_wordClassesEdit->text());

        m_appendedLine =
            dict::CustomWordImporter::formatEntry(spellings, readings, definitions, partOfSpeech(), wordClasses);
        if (!dict::CustomWordImporter::appendEntry(
                entry->sourcePath, spellings, readings, definitions, partOfSpeech(), wordClasses)) {
            showError(xi18nc("@info", "<filename>%1</filename> could not be written.", entry->sourcePath));
            return;
        }
    } else {
        const QString spelling = m_spellingEdit->text().trimmed();
        if (spelling.isEmpty()) {
            showError(i18nc("@info", "Enter the spelling of the name."));
            return;
        }
        const QString nameType = m_nameTypeCombo->currentText().trimmed();
        if (nameType.isEmpty()) {
            showError(i18nc("@info", "Choose or type a name type."));
            return;
        }
        const QString reading = m_readingEdit->text().trimmed();
        const QString extraInfo = m_extraInfoEdit->toPlainText().trimmed();

        m_appendedLine = dict::CustomNameImporter::formatEntry(spelling, reading, nameType, extraInfo, QString());
        if (!dict::CustomNameImporter::appendEntry(entry->sourcePath, spelling, reading, nameType, extraInfo, {})) {
            showError(xi18nc("@info", "<filename>%1</filename> could not be written.", entry->sourcePath));
            return;
        }
    }

    m_dictionaryId = target;
    QDialog::accept();
}

} // namespace maru
