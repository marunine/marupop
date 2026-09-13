// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictui/configuredictionarydialog.h"

#include "dict/dictionarymanager.h"
#include "dictui/dictionarymodel.h"

#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <KLocalizedString>
#include <KMessageWidget>

namespace maru
{

namespace
{

using dict::DictOptions;
using dict::DictType;

bool isYomitanType(DictType type)
{
    switch (type) {
    case DictType::YomitanWord:
    case DictType::YomitanKanji:
    case DictType::YomitanKanjiWordSchema:
    case DictType::YomitanName:
    case DictType::YomitanPitchAccent:
    case DictType::YomitanOther:
    case DictType::YomitanFrequency:
    case DictType::YomitanKanjiFrequency:
        return true;
    default:
        return false;
    }
}

// Definition controls apply to entry types that render definition text. Frequency
// and pitch rows annotate entries, and kanji/name/component rows use other fields.
bool hasDefinitions(DictType type)
{
    if (dict::isPitchAccentType(type) || dict::isFrequencyType(type))
        return false;
    switch (type) {
    case DictType::Kanjidic:
    case DictType::CustomName:
    case DictType::KanjiComponents:
        return false;
    default:
        return true;
    }
}

// Images apply to Yomitan term-shaped types and the custom name list, which is
// the only custom format carrying an image path.
bool hasImages(DictType type)
{
    switch (type) {
    case DictType::YomitanWord:
    case DictType::YomitanKanji:
    case DictType::YomitanKanjiWordSchema:
    case DictType::YomitanName:
    case DictType::YomitanOther:
    case DictType::CustomName:
        return true;
    default:
        return false;
    }
}

bool answersCombinedResults(DictType type)
{
    return !dict::isPitchAccentType(type) && !dict::isFrequencyType(type) && type != DictType::KanjiComponents;
}

bool isJmdict(DictType type)
{
    return type == DictType::JMdict;
}

// One checkbox the dialog may build. applies limits it to the relevant dictionary types.
struct BoolOption
{
    QLatin1StringView objectName;
    QString label;
    QString tooltip;
    bool DictOptions::*field;
    bool (*applies)(DictType);
    // Changing it rewrites the store rather than the rendering.
    bool reimport;
};

QList<BoolOption> displayOptions()
{
    return {
        {.objectName = QLatin1StringView("newlineBetweenDefinitionsBox"),
         .label = i18nc("@option:check", "Show definitions on separate lines"),
         .tooltip = {},
         .field = &DictOptions::newlineBetweenDefinitions,
         .applies = &hasDefinitions,
         .reimport = false},
        {.objectName = QLatin1StringView("wordClassInfoBox"),
         .label = i18nc("@option:check", "Show part-of-speech tags"),
         .tooltip = i18nc("@info:tooltip", "JMdict tags, such as n (noun) or v5s (godan verb ending in su)."),
         .field = &DictOptions::wordClassInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("dialectInfoBox"),
         .label = i18nc("@option:check", "Show dialect tags"),
         .tooltip = {},
         .field = &DictOptions::dialectInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("primarySpellingOrthographyInfoBox"),
         .label = i18nc("@option:check", "Show headword orthography tags"),
         .tooltip = {},
         .field = &DictOptions::primarySpellingOrthographyInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("alternativeSpellingOrthographyInfoBox"),
         .label = i18nc("@option:check", "Show alternative spelling tags"),
         .tooltip = {},
         .field = &DictOptions::alternativeSpellingOrthographyInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("readingOrthographyInfoBox"),
         .label = i18nc("@option:check", "Show reading orthography tags"),
         .tooltip = {},
         .field = &DictOptions::readingOrthographyInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("fieldInfoBox"),
         .label = i18nc("@option:check", "Show field tags"),
         .tooltip = i18nc("@info:tooltip", "Subject tags, such as comp (computing) or sports."),
         .field = &DictOptions::fieldInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("spellingRestrictionInfoBox"),
         .label = i18nc("@option:check", "Show spelling restrictions"),
         .tooltip = i18nc("@info:tooltip", "For example “only applies to 或る”."),
         .field = &DictOptions::spellingRestrictionInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("extraDefinitionInfoBox"),
         .label = i18nc("@option:check", "Show definition notes"),
         .tooltip = i18nc("@info:tooltip", "For example “often derogatory”."),
         .field = &DictOptions::extraDefinitionInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("miscInfoBox"),
         .label = i18nc("@option:check", "Show miscellaneous tags"),
         .tooltip =
             i18nc("@info:tooltip", "Usage tags, such as uk (usually kana), col (colloquial), or euph (euphemistic)."),
         .field = &DictOptions::miscInfo,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("loanwordEtymologyBox"),
         .label = i18nc("@option:check", "Show loanword etymology"),
         .tooltip = {},
         .field = &DictOptions::loanwordEtymology,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("crossReferencesBox"),
         .label = i18nc("@option:check", "Show cross-references"),
         .tooltip = {},
         .field = &DictOptions::crossReferences,
         .applies = &isJmdict,
         .reimport = false},
        {.objectName = QLatin1StringView("showImagesBox"),
         .label = i18nc("@option:check", "Show images"),
         .tooltip = {},
         .field = &DictOptions::showImages,
         .applies = &hasImages,
         .reimport = false},
        {.objectName = QLatin1StringView("higherValueMeansHigherFrequencyBox"),
         .label = i18nc("@option:check", "Use occurrence counts"),
         .tooltip = i18nc("@info:tooltip",
                          "Higher counts indicate more frequent words. With this option disabled, values are ranks: "
                          "lower ranks indicate more frequent words."),
         .field = &DictOptions::higherValueMeansHigherFrequency,
         .applies = &dict::isFrequencyType,
         .reimport = false},
        {.objectName = QLatin1StringView("properNameEntriesBox"),
         .label = i18nc("@option:check", "Import proper name entries"),
         .tooltip = i18nc("@info:tooltip", "JMdict proper names can duplicate entries in JMnedict."),
         .field = &DictOptions::properNameEntries,
         .applies = &isJmdict,
         .reimport = true},
    };
}

} // namespace

ConfigureDictionaryDialog::ConfigureDictionaryDialog(dict::DictionaryManager &manager, const QUuid &id, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_id(id)
{
    setObjectName(QStringLiteral("configureDictionaryDialog"));

    const dict::Dictionary *entry = m_manager.dictionary(id);
    if (entry == nullptr) {
        setWindowTitle(i18nc("@title:window", "Configure Dictionary"));
        return;
    }
    m_original = entry->options;
    m_originalSource = entry->sourcePath;
    setWindowTitle(i18nc("@title:window", "Configure Dictionary — %1", entry->name));
    buildUi(*entry);
}

ConfigureDictionaryDialog::~ConfigureDictionaryDialog() = default;

void ConfigureDictionaryDialog::buildUi(const dict::Dictionary &dictionary)
{
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);

    m_reimportMessage = new KMessageWidget(this);
    m_reimportMessage->setObjectName(QStringLiteral("reimportMessage"));
    m_reimportMessage->setMessageType(KMessageWidget::Information);
    m_reimportMessage->setWordWrap(true);
    m_reimportMessage->setCloseButtonVisible(false);
    m_reimportMessage->setText(i18nc("@info", "Applying these changes will reimport the dictionary."));
    m_reimportMessage->hide();
    layout->addWidget(m_reimportMessage);

    auto *general = new QGroupBox(i18nc("@title:group", "General"), this);
    auto *generalForm = new QFormLayout(general);

    m_nameEdit = new QLineEdit(dictionary.name, general);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    generalForm->addRow(i18nc("@label:textbox", "Name:"), m_nameEdit);

    m_enabledBox = new QCheckBox(i18nc("@option:check", "Enable dictionary"), general);
    m_enabledBox->setObjectName(QStringLiteral("enabledBox"));
    m_enabledBox->setChecked(dictionary.enabled);
    generalForm->addRow(QString(), m_enabledBox);

    if (answersCombinedResults(dictionary.type)) {
        auto *exclude = new QCheckBox(i18nc("@option:check", "Exclude from combined results"), general);
        exclude->setObjectName(QStringLiteral("excludeFromAllBox"));
        exclude->setChecked(dictionary.options.excludeFromAll);
        exclude->setToolTip(i18nc("@info:tooltip", "Search this dictionary only when its category is selected."));
        generalForm->addRow(QString(), exclude);
        m_boolOptions.append({exclude, &DictOptions::excludeFromAll});
    }

    auto *sourceRow = new QWidget(general);
    auto *sourceLayout = new QHBoxLayout(sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    m_sourceEdit = new QLineEdit(dictionary.sourcePath, sourceRow);
    m_sourceEdit->setObjectName(QStringLiteral("sourceEdit"));
    m_sourceEdit->setReadOnly(true);
    m_sourceEdit->setPlaceholderText(i18nc("@info:placeholder", "Not downloaded"));
    // A dictionary path is longer than the field, and its interesting half is the tail; the
    // field still opens at the start so the whole value can be read by scrolling one way.
    m_sourceEdit->setCursorPosition(0);
    sourceLayout->addWidget(m_sourceEdit);
    m_changeSourceButton = new QPushButton(i18nc("@action:button", "Change…"), sourceRow);
    m_changeSourceButton->setObjectName(QStringLiteral("changeSourceButton"));
    // Only a Yomitan dictionary is re-pointed by hand. A built-in dump's path is the file its
    // download wrote, and a custom list's path is the file the Add Word dialog appends to.
    m_changeSourceButton->setVisible(isYomitanType(dictionary.type));
    sourceLayout->addWidget(m_changeSourceButton);
    generalForm->addRow(i18nc("@label:textbox", "Source:"), sourceRow);
    connect(m_changeSourceButton, &QPushButton::clicked, this, &ConfigureDictionaryDialog::chooseSource);

    layout->addWidget(general);

    // Two groups rather than one: an option that only changes the rendering is applied the
    // moment OK is pressed, and an option that changes what the store holds costs an import.
    auto *display = new QGroupBox(i18nc("@title:group", "Display"), this);
    auto *displayForm = new QFormLayout(display);
    auto *import = new QGroupBox(i18nc("@title:group", "Import"), this);
    auto *importForm = new QFormLayout(import);
    const QList<BoolOption> options = displayOptions();
    for (const BoolOption &option : options) {
        if (!option.applies(dictionary.type))
            continue;
        QGroupBox *group = option.reimport ? import : display;
        auto *box = new QCheckBox(option.label, group);
        box->setObjectName(QString(option.objectName));
        box->setChecked(dictionary.options.*option.field);
        if (!option.tooltip.isEmpty())
            box->setToolTip(option.tooltip);
        (option.reimport ? importForm : displayForm)->addRow(QString(), box);
        m_boolOptions.append({box, option.field});
        if (option.reimport) {
            m_reimportOptions.append(box);
            connect(box, &QCheckBox::toggled, this, &ConfigureDictionaryDialog::updateReimportNotice);
        }
    }
    display->setVisible(displayForm->rowCount() > 0);
    layout->addWidget(display);
    import->setVisible(importForm->rowCount() > 0);
    layout->addWidget(import);

    if (dictionary.autoUpdatable) {
        auto *updates = new QGroupBox(i18nc("@title:group", "Updates"), this);
        auto *updatesForm = new QFormLayout(updates);
        m_autoUpdateSpin = new QSpinBox(updates);
        m_autoUpdateSpin->setObjectName(QStringLiteral("autoUpdateSpin"));
        m_autoUpdateSpin->setRange(0, 365);
        m_autoUpdateSpin->setValue(dictionary.options.autoUpdateAfterDays);
        m_autoUpdateSpin->setSpecialValueText(i18nc("@item:valuesuffix zero disables the check", "Never"));
        m_autoUpdateSpin->setSuffix(
            i18ncp("@item:valuesuffix", " day", " days", dictionary.options.autoUpdateAfterDays));
        connect(m_autoUpdateSpin, &QSpinBox::valueChanged, this, [this](int value) {
            m_autoUpdateSpin->setSuffix(i18ncp("@item:valuesuffix", " day", " days", value));
        });
        updatesForm->addRow(i18nc("@label:spinbox", "Check for updates every:"), m_autoUpdateSpin);
        layout->addWidget(updates);
    }

    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ConfigureDictionaryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ConfigureDictionaryDialog::reject);
}

void ConfigureDictionaryDialog::chooseSource()
{
    // A Yomitan dictionary is published both unpacked and as a .zip, and no single QFileDialog
    // mode accepts both, so the choice is the first thing the button asks for.
    QMenu menu(this);
    QAction *folder = menu.addAction(i18nc("@action:inmenu", "Choose Folder…"));
    QAction *archive = menu.addAction(i18nc("@action:inmenu", "Choose Archive File…"));
    QAction *chosen = menu.exec(m_changeSourceButton->mapToGlobal(m_changeSourceButton->rect().bottomLeft()));
    if (chosen == nullptr)
        return;

    const QString start = m_sourceEdit->text().isEmpty() ? QDir::homePath() : m_sourceEdit->text();
    QString path;
    if (chosen == folder) {
        path = QFileDialog::getExistingDirectory(this, i18nc("@title:window", "Choose Dictionary Folder"), start);
    } else if (chosen == archive) {
        path = QFileDialog::getOpenFileName(this,
                                            i18nc("@title:window", "Choose Dictionary Archive"),
                                            start,
                                            i18nc("@item:inlistbox file filter", "Yomitan dictionary archive (*.zip)"));
    }
    if (path.isEmpty())
        return;
    m_sourceEdit->setText(path);
    updateReimportNotice();
}

void ConfigureDictionaryDialog::updateReimportNotice()
{
    bool touched = m_sourceEdit != nullptr && m_sourceEdit->text() != m_originalSource;
    for (const auto &[box, field] : m_boolOptions) {
        if (m_reimportOptions.contains(box) && box->isChecked() != m_original.*field)
            touched = true;
    }
    m_reimportMessage->setVisible(touched);
}

void ConfigureDictionaryDialog::accept()
{
    if (m_manager.dictionary(m_id) == nullptr) {
        QDialog::accept();
        return;
    }

    const QString name = m_nameEdit->text().trimmed();
    if (!name.isEmpty())
        m_manager.rename(m_id, name);
    m_manager.setEnabled(m_id, m_enabledBox->isChecked());

    dict::DictOptions options = m_original;
    for (const auto &[box, field] : m_boolOptions)
        options.*field = box->isChecked();
    if (m_autoUpdateSpin != nullptr)
        options.autoUpdateAfterDays = m_autoUpdateSpin->value();

    bool requiresReimport = false;
    m_manager.setOptions(m_id, options, &requiresReimport);

    if (m_sourceEdit->text() != m_originalSource) {
        m_manager.setSourcePath(m_id, m_sourceEdit->text());
        requiresReimport = true;
    }
    m_requiresReimport = requiresReimport;

    QDialog::accept();
}

} // namespace maru
