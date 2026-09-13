// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictui/adddictionarydialog.h"

#include "core/logging.h"
#include "dict/dictionarymanager.h"
#include "dictui/dictionarymodel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <KLocalizedString>
#include <KMessageWidget>
#include <KUrlRequester>

namespace maru
{

namespace
{

constexpr int formatRole = Qt::UserRole;
constexpr int builtInIdRole = Qt::UserRole + 1;
constexpr int builtInTypeRole = Qt::UserRole + 2;

// The types a Yomitan source can be registered as, in the order the "Treat as" combo lists them.
// KanjiComponents and the three EDRDG types are absent on purpose: neither is reachable from a
// Yomitan folder.
const QList<dict::DictType> &yomitanTypes()
{
    static const QList<dict::DictType> types{
        dict::DictType::YomitanWord,
        dict::DictType::YomitanKanji,
        dict::DictType::YomitanKanjiWordSchema,
        dict::DictType::YomitanName,
        dict::DictType::YomitanPitchAccent,
        dict::DictType::YomitanFrequency,
        dict::DictType::YomitanKanjiFrequency,
        dict::DictType::YomitanOther,
    };
    return types;
}

QString canonical(const QString &path)
{
    const QString resolved = QFileInfo(path).canonicalFilePath();
    return resolved.isEmpty() ? path : resolved;
}

} // namespace

AddDictionaryDialog::AddDictionaryDialog(dict::DictionaryManager &manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(i18nc("@title:window", "Add Dictionary"));
    setObjectName(QStringLiteral("addDictionaryDialog"));
    buildUi();
    populateFormats();
    onFormatChanged();
}

AddDictionaryDialog::~AddDictionaryDialog() = default;

void AddDictionaryDialog::buildUi()
{
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    m_message = new KMessageWidget(this);
    m_message->setObjectName(QStringLiteral("messageWidget"));
    m_message->setWordWrap(true);
    m_message->setCloseButtonVisible(false);
    m_message->hide();
    layout->addWidget(m_message);

    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_formatCombo = new QComboBox(this);
    m_formatCombo->setObjectName(QStringLiteral("formatCombo"));
    form->addRow(i18nc("@label:listbox", "Format:"), m_formatCombo);

    m_pathRequester = new KUrlRequester(this);
    m_pathRequester->setObjectName(QStringLiteral("pathRequester"));
    m_pathLabel = new QLabel(i18nc("@label:textbox", "Location:"), this);
    form->addRow(m_pathLabel, m_pathRequester);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    form->addRow(i18nc("@label:textbox", "Name:"), m_nameEdit);

    m_treatAsCombo = new QComboBox(this);
    m_treatAsCombo->setObjectName(QStringLiteral("treatAsCombo"));
    for (const dict::DictType type : yomitanTypes())
        m_treatAsCombo->addItem(localizedDictTypeName(type), QVariant::fromValue(static_cast<int>(type)));
    m_treatAsLabel =
        new QLabel(i18nc("@label:listbox what kind of dictionary the source holds", "Dictionary type:"), this);
    form->addRow(m_treatAsLabel, m_treatAsCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &AddDictionaryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AddDictionaryDialog::reject);
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, &AddDictionaryDialog::onFormatChanged);
    connect(m_pathRequester, &KUrlRequester::textChanged, this, &AddDictionaryDialog::onPathChanged);
    connect(m_pathRequester, &KUrlRequester::urlSelected, this, &AddDictionaryDialog::onPathChanged);
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this] {
        m_nameEdited = true;
        revalidate();
    });
    connect(m_treatAsCombo, &QComboBox::currentIndexChanged, this, &AddDictionaryDialog::revalidate);
}

void AddDictionaryDialog::populateFormats()
{
    m_formatCombo->addItem(i18nc("@item:inlistbox dictionary format", "Yomitan / Yomichan (folder or .zip)"),
                           static_cast<int>(Format::Yomitan));
    m_formatCombo->addItem(i18nc("@item:inlistbox dictionary format", "Custom word list (.txt)"),
                           static_cast<int>(Format::CustomWord));
    m_formatCombo->addItem(i18nc("@item:inlistbox dictionary format", "Custom name list (.txt)"),
                           static_cast<int>(Format::CustomName));

    // Only the built-ins that have not been downloaded yet: one that is already present is
    // re-fetched through Check for Updates on its own row, not by adding a second copy.
    const QList<dict::Dictionary *> entries = m_manager.dictionaries();
    for (const dict::Dictionary *entry : entries) {
        if (!entry->builtIn || !entry->sourcePath.isEmpty())
            continue;
        m_formatCombo->addItem(
            i18nc("@item:inlistbox a built-in dictionary offered for download", "Download %1", entry->name),
            static_cast<int>(Format::BuiltIn));
        const int row = m_formatCombo->count() - 1;
        m_formatCombo->setItemData(row, entry->id, builtInIdRole);
        m_formatCombo->setItemData(row, static_cast<int>(entry->type), builtInTypeRole);
    }
}

bool AddDictionaryDialog::selectFormat(Format format)
{
    const int row = m_formatCombo->findData(static_cast<int>(format), formatRole);
    if (row < 0)
        return false;
    m_formatCombo->setCurrentIndex(row);
    return true;
}

bool AddDictionaryDialog::selectBuiltIn(dict::DictType type)
{
    for (int row = 0; row < m_formatCombo->count(); ++row) {
        if (m_formatCombo->itemData(row, formatRole).toInt() != static_cast<int>(Format::BuiltIn))
            continue;
        if (m_formatCombo->itemData(row, builtInTypeRole).toInt() == static_cast<int>(type)) {
            m_formatCombo->setCurrentIndex(row);
            return true;
        }
    }
    return false;
}

AddDictionaryDialog::Format AddDictionaryDialog::currentFormat() const
{
    return static_cast<Format>(m_formatCombo->currentData(formatRole).toInt());
}

dict::DictType AddDictionaryDialog::selectedType() const
{
    switch (currentFormat()) {
    case Format::Yomitan:
        return static_cast<dict::DictType>(m_treatAsCombo->currentData().toInt());
    case Format::CustomWord:
        return dict::DictType::CustomWord;
    case Format::CustomName:
        return dict::DictType::CustomName;
    case Format::BuiltIn:
        return static_cast<dict::DictType>(m_formatCombo->currentData(builtInTypeRole).toInt());
    }
    return dict::DictType::YomitanWord;
}

void AddDictionaryDialog::onFormatChanged()
{
    const Format format = currentFormat();
    const bool yomitan = format == Format::Yomitan;
    const bool builtIn = format == Format::BuiltIn;

    m_pathLabel->setVisible(!builtIn);
    m_pathRequester->setVisible(!builtIn);
    m_treatAsLabel->setVisible(yomitan);
    m_treatAsCombo->setVisible(yomitan);

    if (yomitan) {
        // Both, so the same field accepts the unpacked folder and the .zip Yomitan publishes.
        m_pathRequester->setMode(KFile::Directory | KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
        m_pathRequester->setNameFilters({i18nc("@item:inlistbox file filter", "Yomitan dictionary archive (*.zip)"),
                                         i18nc("@item:inlistbox file filter", "All files (*)")});
    } else if (!builtIn) {
        m_pathRequester->setMode(KFile::File | KFile::LocalOnly);
        m_pathRequester->setNameFilters({i18nc("@item:inlistbox file filter", "Text file (*.txt)"),
                                         i18nc("@item:inlistbox file filter", "All files (*)")});
    }

    if (builtIn && !m_nameEdited) {
        const dict::Dictionary *entry = m_manager.dictionary(m_formatCombo->currentData(builtInIdRole).toUuid());
        m_nameEdit->setText(entry != nullptr ? entry->name : QString());
    }
    m_nameEdit->setReadOnly(builtIn);

    refreshFromSource();
}

QString AddDictionaryDialog::localPath() const
{
    const QUrl url = m_pathRequester->url();
    if (url.isLocalFile())
        return url.toLocalFile();
    // A path typed by hand that KUrlRequester could not turn into a URL is still the path the
    // user meant, and dict::Dictionary::sourcePath is a plain string either way.
    return m_pathRequester->text();
}

void AddDictionaryDialog::onPathChanged()
{
    refreshFromSource();
}

void AddDictionaryDialog::refreshFromSource()
{
    const QString path = localPath();
    if (currentFormat() != Format::Yomitan || path.isEmpty() || !QFileInfo::exists(path)) {
        m_index = {};
        m_inspectedPath.clear();
        m_banksFound = false;
        if (!m_nameEdited && currentFormat() != Format::BuiltIn && !path.isEmpty())
            m_nameEdit->setText(QFileInfo(path).completeBaseName());
        revalidate();
        return;
    }

    if (m_inspectedPath == path) {
        revalidate();
        return;
    }
    m_inspectedPath = path;

    m_index = dict::YomitanImporter::readIndex(path);
    const QList<dict::DictType> detected = dict::YomitanImporter::detectTypes(path);
    m_banksFound = !detected.isEmpty();

    if (!m_nameEdited) {
        const QString title =
            m_index.valid && !m_index.title.isEmpty() ? m_index.title : QFileInfo(path).completeBaseName();
        m_nameEdit->setText(title);
    }
    if (!detected.isEmpty()) {
        const int row = m_treatAsCombo->findData(static_cast<int>(detected.constFirst()));
        if (row >= 0)
            m_treatAsCombo->setCurrentIndex(row);
    }

    revalidate();
}

QString AddDictionaryDialog::validationError() const
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return i18nc("@info", "Enter a name for the dictionary.");

    const dict::Dictionary *sameName = m_manager.dictionaryNamed(name);
    if (sameName != nullptr && (currentFormat() != Format::BuiltIn || !sameName->builtIn))
        return xi18nc("@info", "A dictionary named <resource>%1</resource> already exists. Choose another name.", name);

    if (currentFormat() == Format::BuiltIn)
        return {};

    const QString path = localPath();
    if (path.isEmpty())
        return i18nc("@info", "Select a dictionary file or folder.");

    const bool custom = currentFormat() != Format::Yomitan;
    if (!QFileInfo::exists(path)) {
        if (!custom)
            return xi18nc("@info", "<filename>%1</filename> does not exist.", path);
        // A custom list is created empty and filled through Add Word, so a path that is not
        // there yet is a valid choice as long as its folder is.
        if (!QFileInfo::exists(QFileInfo(path).absolutePath()))
            return xi18nc("@info", "The folder of <filename>%1</filename> does not exist.", path);
    }

    const QString resolved = canonical(path);
    const QList<dict::Dictionary *> entries = m_manager.dictionaries();
    for (const dict::Dictionary *entry : entries) {
        if (!entry->sourcePath.isEmpty() && canonical(entry->sourcePath) == resolved) {
            return xi18nc("@info", "<resource>%1</resource> already uses <filename>%2</filename>.", entry->name, path);
        }
    }

    if (currentFormat() == Format::Yomitan && !m_banksFound) {
        return xi18nc("@info",
                      "No Yomitan bank files found in <filename>%1</filename>. Select the folder containing "
                      "<filename>index.json</filename> or the downloaded .zip file.",
                      path);
    }

    return {};
}

QString AddDictionaryDialog::validationWarning() const
{
    if (currentFormat() != Format::Yomitan || !m_banksFound || m_index.valid)
        return {};
    return xi18nc("@info",
                  "Could not read dictionary metadata from <filename>index.json</filename>. Import can continue.");
}

void AddDictionaryDialog::revalidate()
{
    const QString error = validationError();
    // Nothing has been chosen yet, so there is nothing to complain about: an error banner on a
    // dialog the user has not touched reads as a fault rather than as guidance.
    if (currentFormat() != Format::BuiltIn && localPath().isEmpty() && !m_nameEdited) {
        m_message->hide();
        m_okButton->setEnabled(false);
        return;
    }
    const QString warning = error.isEmpty() ? validationWarning() : QString();

    if (!error.isEmpty()) {
        m_message->setMessageType(KMessageWidget::Error);
        m_message->setText(error);
        m_message->show();
    } else if (!warning.isEmpty()) {
        m_message->setMessageType(KMessageWidget::Warning);
        m_message->setText(warning);
        m_message->show();
    } else {
        m_message->hide();
    }
    m_okButton->setEnabled(error.isEmpty());
}

void AddDictionaryDialog::accept()
{
    revalidate();
    if (!validationError().isEmpty())
        return;

    if (currentFormat() == Format::BuiltIn) {
        m_createdId = m_formatCombo->currentData(builtInIdRole).toUuid();
        m_needsDownload = true;
        QDialog::accept();
        return;
    }

    const QString path = localPath();
    if (currentFormat() != Format::Yomitan && !QFileInfo::exists(path)) {
        // Creating the file here rather than at the first Add Word keeps the import job that
        // follows from failing on a source that is not there.
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            m_message->setMessageType(KMessageWidget::Error);
            m_message->setText(
                xi18nc("@info", "<filename>%1</filename> could not be created: %2", path, file.errorString()));
            m_message->show();
            return;
        }
    }

    dict::Dictionary dictionary;
    dictionary.type = selectedType();
    dictionary.name = m_nameEdit->text().trimmed();
    dictionary.sourcePath = path;
    if (currentFormat() == Format::Yomitan && m_index.valid) {
        dictionary.revision = m_index.revision;
        dictionary.autoUpdatable = m_index.isUpdatable;
        if (!m_index.indexUrl.isEmpty())
            dictionary.updateUrl = QUrl(m_index.indexUrl);
        if (m_index.frequencyMode == QLatin1String("occurrence-based"))
            dictionary.options.higherValueMeansHigherFrequency = true;
    }

    const dict::Dictionary *stored = m_manager.add(dictionary);
    if (stored == nullptr) {
        qCWarning(logDict) << "The dictionary manager refused to add" << dictionary.name;
        return;
    }
    m_createdId = stored->id;
    m_needsDownload = false;
    QDialog::accept();
}

} // namespace maru
