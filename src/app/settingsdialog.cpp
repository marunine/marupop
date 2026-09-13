// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/settingsdialog.h"

#include "app/modelstatuswidget.h"
#include "app/pathrequester.h"
#include "app/shortcutbutton.h"
#include "app/shortcutregistry.h"
#include "core/paths.h"
#include "core/settings.h"
#include "ocr/meikiocrbackend.h"
#include "ocr/screenaibackend.h"
#include "popup/popuppreview.h"
#include "popup/theme.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaProperty>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <KColorButton>
#include <KLocalizedString>
#include <KMessageWidget>

#include <algorithm>

namespace maru
{

namespace
{

// KConfigDialogManager binds a widget whose object name is kcfg_<EntryName> to that entry of
// the KConfigSkeleton the dialog was constructed with. A widget without the prefix is left
// unbound, which is what separates a bound control from a label or a preview.
constexpr QLatin1StringView kConfigPrefix("kcfg_");

QCheckBox *checkBox(const QString &name, const QString &label, QWidget *parent)
{
    auto *box = new QCheckBox(label, parent);
    box->setObjectName(name);
    return box;
}

QSpinBox *spinBox(const QString &name, int minimum, int maximum, QWidget *parent, const QString &suffix = {})
{
    auto *box = new QSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    if (!suffix.isEmpty()) {
        box->setSuffix(suffix);
    }
    return box;
}

QDoubleSpinBox *doubleSpinBox(const QString &name, double minimum, double maximum, QWidget *parent)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(2);
    box->setSingleStep(0.05);
    return box;
}

// A plain combo is bound by index, which is what the Enum entries need: the kcfg choice lists
// mirror core/enums.h in declaration order.
QComboBox *enumCombo(const QString &name, const QStringList &labels, QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->setObjectName(name);
    combo->addItems(labels);
    return combo;
}

// Long pages get a scroll area so the dialog stays usable on a small screen. The config
// manager walks the whole child tree, so the bindings survive the extra level.
QWidget *scrollable(QWidget *content)
{
    auto *area = new QScrollArea(content->parentWidget());
    area->setWidget(content);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    return area;
}

// The value KConfigDialogManager would write through for one bound widget. The manager reads a
// widget's USER property unless the widget carries a kcfg_property, with one exception it
// hard-codes: a QComboBox is bound by index, and its USER property is the current *text*.
QVariant boundValue(const QWidget *widget)
{
    const QVariant custom = widget->property("kcfg_property");
    if (custom.isValid()) {
        return widget->property(custom.toByteArray().constData());
    }
    if (qobject_cast<const QComboBox *>(widget) != nullptr) {
        return widget->property("currentIndex");
    }
    const QMetaProperty user = widget->metaObject()->userProperty();
    return user.isValid() ? user.read(widget) : QVariant{};
}

// Writes every bound widget of page into the settings items, without saving. The caller
// restores the items afterwards; see SettingsDialog::refreshPreview().
void pushWidgetValues(const QWidget *page)
{
    if (page == nullptr) {
        return;
    }
    const QList<QWidget *> widgets = page->findChildren<QWidget *>();
    for (const QWidget *widget : widgets) {
        const QString name = widget->objectName();
        if (!name.startsWith(kConfigPrefix)) {
            continue;
        }
        KConfigSkeletonItem *item = PopSettings::self()->findItem(name.mid(kConfigPrefix.size()));
        const QVariant value = boundValue(widget);
        if (item != nullptr && value.isValid()) {
            item->setProperty(value);
        }
    }
}

} // namespace

SettingsDialog::SettingsDialog(ShortcutRegistry *hotkeys, QWidget *parent)
    // The id has to equal the string Application::showSettings() passes to
    // KConfigDialog::showDialog(), which is how the second request raises this dialog rather
    // than opening a copy.
    : KConfigDialog(parent, QStringLiteral("marupop-settings"), PopSettings::self())
    , m_hotkeys(hotkeys)
{
    setWindowTitle(i18nc("@title:window", "Configure MaruPop"));
    setFaceType(KPageDialog::List);

    const QStringList ids = ShortcutRegistry::actionIds();
    for (const QString &id : ids) {
        m_storedShortcuts.insert(
            id, m_hotkeys != nullptr ? m_hotkeys->shortcut(id) : ShortcutRegistry::defaultShortcut(id));
    }
    m_editedShortcuts = m_storedShortcuts;

    addPage(createGeneralPage(), i18nc("@title settings page", "General"), QStringLiteral("configure"));
    addPage(createScanningPage(), i18nc("@title settings page", "Scanning"), QStringLiteral("document-scan"));
    addPage(createRecognitionPage(), i18nc("@title settings page", "Text Recognition"), QStringLiteral("edit-find"));
    addPage(createLookupPage(), i18nc("@title settings page", "Lookup"), QStringLiteral("accessories-dictionary"));
    addPage(createContentPage(), i18nc("@title settings page", "Popup Content"), QStringLiteral("view-list-details"));
    addPage(createAppearancePage(),
            i18nc("@title settings page", "Popup Appearance"),
            QStringLiteral("preferences-desktop-theme"));
    addPage(createShortcutsPage(), i18nc("@title settings page", "Shortcuts"), QStringLiteral("configure-shortcuts"));

    // Every bound widget moving redraws the preview. KConfigDialog forwards its page managers'
    // KConfigDialogManager::widgetModified as its own, whichever page the widget is on, so a
    // content switch and a color both reach the same slot.
    connect(this, &KConfigDialog::widgetModified, this, &SettingsDialog::refreshPreview);
    refreshPreview();
    resize(880, 640);
}

QWidget *SettingsDialog::createGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *session = new QGroupBox(i18nc("@title:group", "Session"), page);
    auto *sessionForm = new QFormLayout(session);
    sessionForm->addRow(
        QString(),
        checkBox(QStringLiteral("kcfg_ShowTrayIcon"), i18nc("@option:check", "Show icon in system tray"), session));
    sessionForm->addRow(QString(),
                        checkBox(QStringLiteral("kcfg_Autostart"), i18nc("@option:check", "Start at login"), session));
    auto *locked = checkBox(QStringLiteral("kcfg_PauseWhileLocked"),
                            i18nc("@option:check", "Pause scanning when screen is locked"),
                            session);
    locked->setToolTip(i18nc("@info:tooltip", "Scanning resumes when the screen is unlocked."));
    sessionForm->addRow(QString(), locked);
    layout->addWidget(session);

    auto *notifications = new QGroupBox(i18nc("@title:group", "Notifications"), page);
    auto *notificationForm = new QFormLayout(notifications);
    auto *errors = checkBox(
        QStringLiteral("kcfg_NotifyOnError"), i18nc("@option:check", "Show error notifications"), notifications);
    errors->setToolTip(i18nc(
        "@info:tooltip",
        "Report capture, recognition, download, and import errors. Errors also appear in the system tray tooltip."));
    notificationForm->addRow(QString(), errors);
    layout->addWidget(notifications);

    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createScanningPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *triggers = new QGroupBox(i18nc("@title:group", "Triggers"), page);
    auto *triggerForm = new QFormLayout(triggers);
    auto *onMove = checkBox(
        QStringLiteral("kcfg_TriggerOnCursorMove"), i18nc("@option:check", "Scan on mouse movement"), triggers);
    triggerForm->addRow(QString(), onMove);
    auto *throttle = spinBox(QStringLiteral("kcfg_CursorMoveThrottleMs"),
                             50,
                             5000,
                             triggers,
                             i18nc("@item:valuesuffix milliseconds", " ms"));
    triggerForm->addRow(i18nc("@label:spinbox", "Minimum scan interval:"), throttle);
    auto *poll =
        checkBox(QStringLiteral("kcfg_PeriodicPollEnabled"), i18nc("@option:check", "Scan periodically"), triggers);
    triggerForm->addRow(QString(), poll);
    auto *pollInterval = spinBox(QStringLiteral("kcfg_PeriodicPollIntervalMs"),
                                 200,
                                 60000,
                                 triggers,
                                 i18nc("@item:valuesuffix milliseconds", " ms"));
    pollInterval->setToolTip(i18nc("@info:tooltip",
                                   "Scan the area around the mouse pointer at regular intervals. Unchanged areas reuse "
                                   "the previous recognition result."));
    triggerForm->addRow(i18nc("@label:spinbox", "Scan interval:"), pollInterval);
    layout->addWidget(triggers);

    // A throttle the pointer path never reads, and an interval no timer runs at, are dead
    // controls; disabling them says so without hiding the value they hold.
    connect(onMove, &QCheckBox::toggled, throttle, &QWidget::setEnabled);
    throttle->setEnabled(PopSettings::triggerOnCursorMove());
    connect(poll, &QCheckBox::toggled, pollInterval, &QWidget::setEnabled);
    pollInterval->setEnabled(PopSettings::periodicPollEnabled());

    auto *area = new QGroupBox(i18nc("@title:group", "Scan Area"), page);
    auto *areaForm = new QFormLayout(area);
    auto *progressive = checkBox(
        QStringLiteral("kcfg_ProgressiveScanArea"), i18nc("@option:check", "Expand scan area automatically"), area);
    progressive->setToolTip(
        i18nc("@info:tooltip", "Expand the scan area when text reaches its edge or no text is found."));
    areaForm->addRow(QString(), progressive);
    auto *initialWidth = spinBox(
        QStringLiteral("kcfg_InitialScanWidth"), 64, 7680, area, i18nc("@item:valuesuffix logical pixels", " px"));
    auto *initialHeight = spinBox(
        QStringLiteral("kcfg_InitialScanHeight"), 64, 4320, area, i18nc("@item:valuesuffix logical pixels", " px"));
    auto *initialRow = new QHBoxLayout;
    initialRow->addWidget(initialWidth);
    initialRow->addWidget(new QLabel(i18nc("@label between a width and a height", "×"), area));
    initialRow->addWidget(initialHeight);
    initialRow->addStretch();
    areaForm->addRow(i18nc("@label:spinbox", "Initial size:"), initialRow);

    auto *maxWidth =
        spinBox(QStringLiteral("kcfg_MaxScanWidth"), 64, 7680, area, i18nc("@item:valuesuffix logical pixels", " px"));
    auto *maxHeight =
        spinBox(QStringLiteral("kcfg_MaxScanHeight"), 64, 4320, area, i18nc("@item:valuesuffix logical pixels", " px"));
    auto *maxRow = new QHBoxLayout;
    maxRow->addWidget(maxWidth);
    maxRow->addWidget(new QLabel(i18nc("@label between a width and a height", "×"), area));
    maxRow->addWidget(maxHeight);
    maxRow->addStretch();
    areaForm->addRow(i18nc("@label:spinbox", "Maximum size:"), maxRow);
    layout->addWidget(area);

    const auto setLadderEnabled = [maxWidth, maxHeight](bool enabled) {
        maxWidth->setEnabled(enabled);
        maxHeight->setEnabled(enabled);
    };
    connect(progressive, &QCheckBox::toggled, this, setLadderEnabled);
    setLadderEnabled(PopSettings::progressiveScanArea());

    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createRecognitionPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *engine = new QGroupBox(i18nc("@title:group", "Engine"), page);
    auto *engineForm = new QFormLayout(engine);
    auto *combo = enumCombo(QStringLiteral("kcfg_OcrEngine"),
                            {i18nc("@item:inlistbox text-recognition engine", "Automatic"),
                             i18nc("@item:inlistbox text-recognition engine", "meikiocr"),
                             i18nc("@item:inlistbox text-recognition engine", "Chrome Screen AI")},
                            engine);
    engineForm->addRow(i18nc("@label:listbox", "Engine:"), combo);

    // What each engine can do right now, read at construction: both answers are a directory
    // listing, and both change only through this same page.
    const bool modelsPresent = ocr::MeikiOcrBackend::modelsPresent(settings::modelDirectory());
    const bool screenAi = ocr::ScreenAiBackend::isInstalled(settings::screenAiResourcesDir());
    auto *availability = new QLabel(i18nc("@info the state of the two recognition engines",
                                          "meikiocr: %1 · Chrome Screen AI: %2",
                                          modelsPresent ? i18nc("@info model files", "models installed")
                                                        : i18nc("@info model files", "models missing"),
                                          screenAi ? i18nc("@info a component install", "installed")
                                                   : i18nc("@info a component install", "not found")),
                                    engine);
    availability->setWordWrap(true);
    engineForm->addRow(QString(), availability);
    layout->addWidget(engine);

    auto *meiki = new QGroupBox(i18nc("@title:group", "meikiocr"), page);
    auto *meikiForm = new QFormLayout(meiki);
    meikiForm->addRow(i18nc("@label:spinbox", "Inference threads:"),
                      spinBox(QStringLiteral("kcfg_MeikiIntraOpThreads"), 1, 32, meiki));
    meikiForm->addRow(i18nc("@label:spinbox", "Detection threshold:"),
                      doubleSpinBox(QStringLiteral("kcfg_MeikiDetectionThreshold"), 0.0, 1.0, meiki));
    meikiForm->addRow(i18nc("@label:spinbox", "Recognition threshold:"),
                      doubleSpinBox(QStringLiteral("kcfg_MeikiRecognitionThreshold"), 0.0, 1.0, meiki));
    meikiForm->addRow(i18nc("@label:spinbox", "Punctuation confidence factor:"),
                      doubleSpinBox(QStringLiteral("kcfg_MeikiPunctuationConfidenceFactor"), 0.0, 1.0, meiki));
    meikiForm->addRow(QString(),
                      checkBox(QStringLiteral("kcfg_MeikiUseSmallDetector"),
                               i18nc("@option:check", "Use small detection model"),
                               meiki));
    auto *corrections = checkBox(
        QStringLiteral("kcfg_MeikiApplyCorrections"), i18nc("@option:check", "Correct recognition errors"), meiki);
    corrections->setToolTip(i18nc("@info:tooltip",
                                  "Correct punctuation and similar characters after recognition. Correction rules "
                                  "assume a punctuation confidence factor of 0.2."));
    meikiForm->addRow(QString(), corrections);
    auto *gpu = checkBox(QStringLiteral("kcfg_MeikiAllowGpu"), i18nc("@option:check", "Use GPU acceleration"), meiki);
    gpu->setToolTip(
        i18nc("@info:tooltip",
              "GPU acceleration is tested when the engine starts. Recognition uses the CPU if the test fails."));
    meikiForm->addRow(QString(), gpu);
    layout->addWidget(meiki);

    auto *models = new QGroupBox(i18nc("@title:group", "Models"), page);
    auto *modelsLayout = new QVBoxLayout(models);
    auto *modelStatus = new ModelStatusWidget(ModelStatusWidget::Mode::Settings, models);
    connect(modelStatus, &ModelStatusWidget::downloadFinished, this, [this](bool ok, const QString &) {
        if (ok) {
            Q_EMIT modelsDownloaded();
        }
    });
    modelsLayout->addWidget(modelStatus);
    layout->addWidget(models);

    auto *screenAiGroup = new QGroupBox(i18nc("@title:group", "Chrome Screen AI"), page);
    auto *screenAiForm = new QFormLayout(screenAiGroup);
    auto *resources = new PathRequester(PathRequester::Kind::Directory, screenAiGroup);
    resources->setObjectName(QStringLiteral("kcfg_ScreenAiResourcesDir"));
    resources->setPlaceholderText(QStringLiteral("~/.config/screen_ai/resources"));
    screenAiForm->addRow(i18nc("@label:textbox", "Component folder:"), resources);
    auto *screenAiNote = new QLabel(i18nc("@info",
                                          "Chrome Screen AI requires a separate installation. Select the folder "
                                          "containing libchromescreenai.so and its model files."),
                                    screenAiGroup);
    screenAiNote->setWordWrap(true);
    screenAiForm->addRow(QString(), screenAiNote);
    layout->addWidget(screenAiGroup);

    layout->addStretch();
    return scrollable(page);
}

QWidget *SettingsDialog::createLookupPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *search = new QGroupBox(i18nc("@title:group", "Search"), page);
    auto *searchForm = new QFormLayout(search);
    auto *length = spinBox(
        QStringLiteral("kcfg_MaxLookupLength"), 5, 100, search, i18nc("@item:valuesuffix characters", " characters"));
    length->setToolTip(
        i18nc("@info:tooltip", "Maximum number of characters to search, starting at the mouse pointer."));
    searchForm->addRow(i18nc("@label:spinbox", "Text scan length:"), length);
    searchForm->addRow(i18nc("@label:spinbox", "Maximum popup results:"),
                       spinBox(QStringLiteral("kcfg_MaxResults"), 1, 50, search));
    searchForm->addRow(i18nc("@label:listbox", "Dictionary types:"),
                       enumCombo(QStringLiteral("kcfg_LookupCategory"),
                                 {i18nc("@item:inlistbox dictionary types", "All"),
                                  i18nc("@item:inlistbox dictionary types", "Words"),
                                  i18nc("@item:inlistbox dictionary types", "Names"),
                                  i18nc("@item:inlistbox dictionary types", "Kanji")},
                                 search));
    searchForm->addRow(i18nc("@label:listbox", "Copy mode:"),
                       enumCombo(QStringLiteral("kcfg_CopyWordMode"),
                                 {i18nc("@item:inlistbox what to copy", "Matched text"),
                                  i18nc("@item:inlistbox what to copy", "Headword"),
                                  i18nc("@item:inlistbox what to copy", "Reading")},
                                 search));
    layout->addWidget(search);

    // lookup::VariantOptions. Every control below trades words the pass finds for time or for
    // fewer wrong first answers. Defaults are heuristic limits, not an accuracy guarantee.
    auto *lookalike = new QGroupBox(i18nc("@title:group", "Character Variants"), page);
    auto *lookalikeForm = new QFormLayout(lookalike);
    auto *variants = checkBox(
        QStringLiteral("kcfg_LookupOcrVariants"), i18nc("@option:check", "Search character variants"), lookalike);
    variants->setToolTip(i18nc("@info:tooltip",
                               "Substitute one character with a kana variant (は, ば, ぱ; つ, っ) or a similar "
                               "character (カ, 力). Show variants that match more text than the original."));
    lookalikeForm->addRow(QString(), variants);

    auto *gate = doubleSpinBox(QStringLiteral("kcfg_LookupVariantConfidenceGate"), 0.0, 1.0, lookalike);
    gate->setToolTip(i18nc("@info:tooltip",
                           "Substitute characters with recognition confidence at or below the threshold. A value of 1 "
                           "allows substitutions for all characters."));
    lookalikeForm->addRow(i18nc("@label:spinbox", "Maximum confidence:"), gate);

    auto *withoutConfidences = checkBox(QStringLiteral("kcfg_LookupVariantWithoutConfidences"),
                                        i18nc("@option:check", "Use with Chrome Screen AI"),
                                        lookalike);
    withoutConfidences->setToolTip(
        i18nc("@info:tooltip",
              "Chrome Screen AI provides no character confidence scores. All characters are eligible for substitution, "
              "which can produce incorrect matches and increase lookup time."));
    lookalikeForm->addRow(QString(), withoutConfidences);

    auto *keyLength = spinBox(QStringLiteral("kcfg_LookupVariantMaxKeyLength"),
                              0,
                              100,
                              lookalike,
                              i18nc("@item:valuesuffix characters", " characters"));
    keyLength->setSpecialValueText(i18nc("@item:inrange no limit", "Unlimited"));
    keyLength->setToolTip(i18nc(
        "@info:tooltip", "Maximum text length for character substitutions. Longer text is searched as recognized."));
    lookalikeForm->addRow(i18nc("@label:spinbox", "Maximum variant length:"), keyLength);

    auto *keys = spinBox(QStringLiteral("kcfg_LookupVariantMaxKeys"), 0, 100000, lookalike);
    keys->setSpecialValueText(i18nc("@item:inrange no limit", "Unlimited"));
    keys->setToolTip(i18nc("@info:tooltip",
                           "Maximum number of variant spellings to search per lookup. Shorter text is searched first. "
                           "A lower limit can omit longer matches."));
    lookalikeForm->addRow(i18nc("@label:spinbox", "Maximum variants:"), keys);

    auto *common = checkBox(QStringLiteral("kcfg_LookupVariantCommonWordsOnly"),
                            i18nc("@option:check", "Limit to common or katakana words"),
                            lookalike);
    common->setToolTip(
        i18nc("@info:tooltip",
              "Common words have a JMdict priority tag or a frequency rank of 20,000 or less in an enabled frequency "
              "dictionary. Katakana matches require both the matched text and headword to be katakana."));
    lookalikeForm->addRow(QString(), common);

    auto *shortAndHiragana = checkBox(QStringLiteral("kcfg_LookupVariantShortAndHiragana"),
                                      i18nc("@option:check", "Include short and hiragana matches"),
                                      lookalike);
    shortAndHiragana->setToolTip(i18nc("@info:tooltip",
                                       "Include hiragana-only variants and two-character variants other than two "
                                       "kanji. These substitutions can produce incorrect matches."));
    lookalikeForm->addRow(QString(), shortAndHiragana);

    auto *names = checkBox(QStringLiteral("kcfg_LookupVariantNameDictionaries"),
                           i18nc("@option:check", "Search name dictionaries"),
                           lookalike);
    lookalikeForm->addRow(QString(), names);

    auto *rankFirst = checkBox(
        QStringLiteral("kcfg_LookupVariantRankFirst"), i18nc("@option:check", "Prioritize longer variants"), lookalike);
    rankFirst->setToolTip(i18nc("@info:tooltip",
                                "Show longer variant matches before shorter matches of the original text. Otherwise, "
                                "show variants after all original matches."));
    lookalikeForm->addRow(QString(), rankFirst);

    const QList<QWidget *> dependents{
        gate, withoutConfidences, keyLength, keys, common, shortAndHiragana, names, rankFirst};
    const auto enableDependents = [dependents](bool enabled) {
        for (QWidget *widget : dependents)
            widget->setEnabled(enabled);
    };
    connect(variants, &QCheckBox::toggled, this, enableDependents);
    enableDependents(variants->isChecked());
    layout->addWidget(lookalike);

    auto *window = new QGroupBox(i18nc("@title:group", "Lookup Window"), page);
    auto *windowForm = new QFormLayout(window);
    auto *windowResults = spinBox(QStringLiteral("kcfg_LookupWindowMaxResults"), 1, 100, window);
    windowResults->setToolTip(i18nc("@info:tooltip",
                                    "Maximum number of results in the lookup window. Open the window from the system "
                                    "tray menu or by middle-clicking the tray icon."));
    windowForm->addRow(i18nc("@label:spinbox", "Maximum results:"), windowResults);
    windowForm->addRow(QString(),
                       checkBox(QStringLiteral("kcfg_HidePopupWhileLookupWindowOpen"),
                                i18nc("@option:check", "Hide popup when lookup window is open"),
                                window));
    layout->addWidget(window);

    auto *dictionaries = new QGroupBox(i18nc("@title:group", "Dictionaries"), page);
    auto *dictionaryLayout = new QHBoxLayout(dictionaries);
    auto *manage = new QPushButton(QIcon::fromTheme(QStringLiteral("accessories-dictionary")),
                                   i18nc("@action:button", "Manage Dictionaries…"),
                                   dictionaries);
    manage->setObjectName(QStringLiteral("manageDictionariesButton"));
    connect(manage, &QPushButton::clicked, this, &SettingsDialog::manageDictionariesRequested);
    dictionaryLayout->addWidget(manage);
    dictionaryLayout->addStretch();
    layout->addWidget(dictionaries);

    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createContentPage()
{
    auto *page = new QWidget(this);
    m_contentPage = page;
    auto *layout = new QVBoxLayout(page);

    auto *words = new QGroupBox(i18nc("@title:group", "Word Entries"), page);
    auto *wordsLayout = new QVBoxLayout(words);
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_CompactMode"), i18nc("@option:check", "Show definitions on one line"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowAllGlosses"), i18nc("@option:check", "Show all glosses"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowDeconjugation"), i18nc("@option:check", "Show deconjugation"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowPartOfSpeech"), i18nc("@option:check", "Show part-of-speech tags"), words));
    wordsLayout->addWidget(checkBox(
        QStringLiteral("kcfg_ShowTags"), i18nc("@option:check", "Show field, dialect, and miscellaneous tags"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowFrequency"), i18nc("@option:check", "Show frequency rank"), words));
    wordsLayout->addWidget(checkBox(
        QStringLiteral("kcfg_ShowAlternativeSpellings"), i18nc("@option:check", "Show alternative spellings"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowPitchAccent"), i18nc("@option:check", "Show pitch accent"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowOrthographyInfo"), i18nc("@option:check", "Show orthography tags"), words));
    wordsLayout->addWidget(
        checkBox(QStringLiteral("kcfg_ShowDictionaryName"), i18nc("@option:check", "Show dictionary names"), words));
    layout->addWidget(words);

    auto *kanji = new QGroupBox(i18nc("@title:group", "Kanji Entries"), page);
    auto *kanjiLayout = new QVBoxLayout(kanji);
    m_showKanji = checkBox(QStringLiteral("kcfg_ShowKanji"), i18nc("@option:check", "Show kanji entries"), kanji);
    m_showKanji->setToolTip(i18nc("@info:tooltip", "Show kanji details for single-character lookups."));
    kanjiLayout->addWidget(m_showKanji);
    m_kanjiOptions = {
        checkBox(QStringLiteral("kcfg_ShowKanjiExamples"), i18nc("@option:check", "Show example words"), kanji),
        checkBox(QStringLiteral("kcfg_ShowKanjiComponents"), i18nc("@option:check", "Show kanji components"), kanji),
        checkBox(QStringLiteral("kcfg_ShowStrokeCountAndGrade"),
                 i18nc("@option:check", "Show stroke count and grade"),
                 kanji)};
    for (QWidget *option : std::as_const(m_kanjiOptions)) {
        // Indented, so the three read as sub-options of the card rather than as siblings.
        auto *row = new QHBoxLayout;
        row->addSpacing(24);
        row->addWidget(option);
        kanjiLayout->addLayout(row);
    }
    layout->addWidget(kanji);

    const auto updateKanjiOptions = [this](bool enabled) {
        for (QWidget *option : std::as_const(m_kanjiOptions)) {
            option->setEnabled(enabled);
        }
    };
    connect(m_showKanji, &QCheckBox::toggled, this, updateKanjiOptions);
    updateKanjiOptions(PopSettings::showKanji());

    layout->addStretch();
    return scrollable(page);
}

QWidget *SettingsDialog::createAppearancePage()
{
    auto *page = new QWidget(this);
    m_appearancePage = page;
    auto *layout = new QHBoxLayout(page);

    auto *settingsColumn = new QWidget(page);
    auto *column = new QVBoxLayout(settingsColumn);
    column->setContentsMargins(0, 0, 0, 0);

    auto *colors = new QGroupBox(i18nc("@title:group", "Colors"), settingsColumn);
    auto *colorForm = new QFormLayout(colors);
    m_themePreset = enumCombo(QStringLiteral("kcfg_ThemePreset"),
                              {i18nc("@item:inlistbox popup color preset", "Nazeka"),
                               i18nc("@item:inlistbox popup color preset", "Indigo"),
                               i18nc("@item:inlistbox popup color preset", "Slate"),
                               i18nc("@item:inlistbox popup color preset", "Light"),
                               i18nc("@item:inlistbox popup color preset", "Custom")},
                              colors);
    colorForm->addRow(i18nc("@label:listbox", "Preset:"), m_themePreset);

    struct ColorRow
    {
        QLatin1StringView name;
        QString label;
    };

    const QList<ColorRow> colorRows{
        {.name = QLatin1StringView("kcfg_ColorBackground"), .label = i18nc("@label:chooser", "Background:")},
        {.name = QLatin1StringView("kcfg_ColorForeground"), .label = i18nc("@label:chooser", "Text:")},
        {.name = QLatin1StringView("kcfg_ColorHighlightWord"), .label = i18nc("@label:chooser", "Headword:")},
        {.name = QLatin1StringView("kcfg_ColorHighlightReading"), .label = i18nc("@label:chooser", "Reading:")},
    };
    for (const ColorRow &row : colorRows) {
        auto *button = new KColorButton(colors);
        button->setObjectName(QString{row.name});
        colorForm->addRow(row.label, button);
        m_colorButtons.append(button);
    }

    m_opacity = new QSlider(Qt::Horizontal, colors);
    m_opacity->setObjectName(QStringLiteral("kcfg_BackgroundOpacity"));
    m_opacity->setRange(50, 255);
    colorForm->addRow(i18nc("@label:slider", "Opacity:"), m_opacity);
    column->addWidget(colors);

    // Selecting a preset fills the four buttons and the slider from the preset table, without
    // touching marupoprc: the values reach it through Apply like every other widget on the
    // page. QComboBox::activated rather than currentIndexChanged, because the config manager
    // sets the index itself whenever the dialog is shown or reset, and that must not overwrite
    // the stored colors.
    connect(m_themePreset, &QComboBox::activated, this, [this](int index) {
        if (index < 0 || index >= static_cast<int>(ThemePreset::Custom)) {
            return;
        }
        const popup::Theme theme = popup::presetTheme(static_cast<ThemePreset>(index));
        m_fillingPreset = true;
        m_colorButtons.at(0)->setColor(theme.background);
        m_colorButtons.at(1)->setColor(theme.foreground);
        m_colorButtons.at(2)->setColor(theme.highlightWord);
        m_colorButtons.at(3)->setColor(theme.highlightReading);
        m_opacity->setValue(theme.backgroundOpacity);
        m_fillingPreset = false;
        // The five widgets were moved behind the config manager's back, so Apply has to be
        // told, and the preview redrawn.
        settingsChangedSlot();
        refreshPreview();
    });

    // Editing a color by hand is what Custom means. Queued, because KConfigDialogManager loads
    // the five widgets one at a time: a direct connection would compare a half-loaded page
    // against the preset and move a stored preset to Custom before the colors it names had
    // arrived.
    for (KColorButton *button : std::as_const(m_colorButtons)) {
        connect(button, &KColorButton::changed, this, &SettingsDialog::syncPresetToColors, Qt::QueuedConnection);
    }
    connect(m_opacity, &QSlider::valueChanged, this, &SettingsDialog::syncPresetToColors, Qt::QueuedConnection);

    auto *fonts = new QGroupBox(i18nc("@title:group", "Text"), settingsColumn);
    auto *fontForm = new QFormLayout(fonts);
    auto *family = new QFontComboBox(fonts);
    family->setObjectName(QStringLiteral("kcfg_FontFamily"));
    // The entry is a family name, and a QFontComboBox is a QComboBox, which the config manager
    // binds by index. kcfg_property moves the binding onto the text, which is the family.
    family->setProperty("kcfg_property", QByteArray("currentText"));
    family->setWritingSystem(QFontDatabase::Japanese);
    fontForm->addRow(i18nc("@label:listbox", "Font:"), family);
    fontForm->addRow(
        i18nc("@label:spinbox", "Headword size:"),
        spinBox(QStringLiteral("kcfg_FontSizeHeader"), 6, 72, fonts, i18nc("@item:valuesuffix points", " pt")));
    fontForm->addRow(
        i18nc("@label:spinbox", "Definition size:"),
        spinBox(QStringLiteral("kcfg_FontSizeDefinitions"), 6, 72, fonts, i18nc("@item:valuesuffix points", " pt")));
    QSpinBox *lineHeight =
        spinBox(QStringLiteral("kcfg_LineHeight"), 80, 300, fonts, i18nc("@item:valuesuffix percent", " %"));
    lineHeight->setSingleStep(5);
    fontForm->addRow(i18nc("@label:spinbox", "Line height:"), lineHeight);
    fontForm->addRow(
        i18nc("@label:spinbox", "Entry spacing:"),
        spinBox(QStringLiteral("kcfg_EntrySpacing"), 0, 60, fonts, i18nc("@item:valuesuffix logical pixels", " px")));
    column->addWidget(fonts);

    auto *card = new QGroupBox(i18nc("@title:group", "Size & Position"), settingsColumn);
    auto *cardForm = new QFormLayout(card);
    cardForm->addRow(i18nc("@label:listbox", "Position:"),
                     enumCombo(QStringLiteral("kcfg_PopupPositionMode"),
                               {i18nc("@item:inlistbox popup placement", "Flip horizontally and vertically"),
                                i18nc("@item:inlistbox popup placement", "Flip vertically"),
                                i18nc("@item:inlistbox popup placement", "Flip horizontally"),
                                i18nc("@item:inlistbox popup placement", "Visual novel")},
                               card));
    cardForm->addRow(
        i18nc("@label:spinbox", "Maximum width:"),
        spinBox(
            QStringLiteral("kcfg_PopupMaxWidth"), 200, 2000, card, i18nc("@item:valuesuffix logical pixels", " px")));
    cardForm->addRow(
        i18nc("@label:spinbox", "Maximum height:"),
        spinBox(
            QStringLiteral("kcfg_PopupMaxHeight"), 200, 2000, card, i18nc("@item:valuesuffix logical pixels", " px")));
    cardForm->addRow(
        i18nc("@label:spinbox", "Pointer offset:"),
        spinBox(
            QStringLiteral("kcfg_PopupCursorOffset"), 0, 200, card, i18nc("@item:valuesuffix logical pixels", " px")));
    cardForm->addRow(
        i18nc("@label:spinbox", "Corner radius:"),
        spinBox(
            QStringLiteral("kcfg_PopupCornerRadius"), 0, 40, card, i18nc("@item:valuesuffix logical pixels", " px")));
    cardForm->addRow(
        i18nc("@label:spinbox", "Fade duration:"),
        spinBox(QStringLiteral("kcfg_PopupFadeMs"), 0, 1000, card, i18nc("@item:valuesuffix milliseconds", " ms")));
    column->addWidget(card);
    column->addStretch();
    layout->addWidget(scrollable(settingsColumn), 1);

    auto *previewColumn = new QVBoxLayout;
    previewColumn->addWidget(new QLabel(i18nc("@label", "Preview:"), page));
    m_preview = new popup::PopupPreview(page);
    m_preview->setObjectName(QStringLiteral("popupPreview"));
    previewColumn->addWidget(m_preview);
    previewColumn->addStretch();
    layout->addLayout(previewColumn);

    return page;
}

QWidget *SettingsDialog::createShortcutsPage()
{
    if (m_hotkeys != nullptr && !m_hotkeys->editableShortcuts()) {
        return createCompositorShortcutsPage();
    }

    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *hint = new KMessageWidget(page);
    hint->setMessageType(KMessageWidget::Information);
    hint->setCloseButtonVisible(false);
    hint->setWordWrap(true);
    hint->setText(i18nc(
        "@info", "Global shortcuts work in any application. Configure them here or in System Settings under MaruPop."));
    layout->addWidget(hint);

    auto *form = new QFormLayout;
    const QStringList ids = ShortcutRegistry::actionIds();
    for (const QString &id : ids) {
        auto *button = new ShortcutButton(page);
        // Not a kcfg_ name: the three hotkeys are KGlobalAccel actions, not settings entries.
        button->setObjectName(QStringLiteral("shortcut_") + id);
        button->setKeySequence(m_editedShortcuts.value(id).isEmpty() ? QKeySequence{}
                                                                     : m_editedShortcuts.value(id).constFirst());
        connect(button, &ShortcutButton::keySequenceChanged, this, [this, id](const QKeySequence &sequence) {
            m_editedShortcuts.insert(id, sequence.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{sequence});
            // Nothing the config manager owns moved, so Apply has to be told by hand.
            settingsChangedSlot();
        });
        m_shortcutButtons.insert(id, button);
        form->addRow(ShortcutRegistry::actionLabel(id) + QLatin1Char(':'), button);
    }
    layout->addLayout(form);
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createCompositorShortcutsPage()
{
    // hyprland_global_shortcuts_v1 registers an anonymous action: the compositor owns the key
    // sequence and the protocol has no request that sets one, so this page shows the
    // configuration lines to paste rather than three key editors. No ShortcutButton is created, so
    // updateSettings(), updateWidgets() and hasChanged() all see an empty m_shortcutButtons and
    // an m_editedShortcuts equal to m_storedShortcuts, and the page never reports a change.
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *hint = new KMessageWidget(page);
    hint->setMessageType(KMessageWidget::Information);
    hint->setCloseButtonVisible(false);
    hint->setWordWrap(true);
    hint->setText(m_hotkeys->bindingHintHeader());
    layout->addWidget(hint);

    QStringList lines;
    const QStringList ids = ShortcutRegistry::actionIds();
    lines.reserve(ids.size());
    for (const QString &id : ids) {
        const QString line = m_hotkeys->bindingHint(id);
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }

    auto *bindings = new QPlainTextEdit(lines.join(QLatin1Char('\n')), page);
    bindings->setObjectName(QStringLiteral("compositorBindings"));
    bindings->setReadOnly(true);
    bindings->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bindings->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(bindings);

    auto *copy = new QPushButton(
        QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:button", "Copy to Clipboard"), page);
    copy->setObjectName(QStringLiteral("copyBindings"));
    connect(copy, &QPushButton::clicked, this, [bindings] {
        // QClipboard rather than KSystemClipboard: the settings window has the focus while its
        // own button is pressed, which is the one state a plain clipboard write works in.
        QGuiApplication::clipboard()->setText(bindings->toPlainText());
    });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(copy);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *registered = new QLabel(page);
    registered->setWordWrap(true);
    // Three states rather than two: a partial registration is what a duplicate id produces, and
    // telling the user that none of the lines work while two of them do sends them after the
    // wrong problem.
    const auto accepted = std::ranges::count_if(ids, [this](const QString &id) {
        return m_hotkeys->isRegistered(id);
    });
    if (accepted == ids.size()) {
        registered->setText(
            i18ncp("@info", "%1 global shortcut registered.", "%1 global shortcuts registered.", ids.size()));
    } else if (accepted == 0) {
        registered->setText(i18nc("@info", "Global shortcut registration failed."));
    } else {
        registered->setText(i18nc(
            "@info, %1 and %2 are counts of shortcuts", "%1 of %2 global shortcuts registered.", accepted, ids.size()));
    }
    layout->addWidget(registered);

    layout->addStretch();
    return page;
}

void SettingsDialog::showShortcuts()
{
    for (auto it = m_shortcutButtons.cbegin(); it != m_shortcutButtons.cend(); ++it) {
        const QList<QKeySequence> keys = m_editedShortcuts.value(it.key());
        it.value()->setKeySequence(keys.isEmpty() ? QKeySequence{} : keys.constFirst());
    }
}

void SettingsDialog::syncPresetToColors()
{
    if (m_fillingPreset || m_themePreset == nullptr || m_colorButtons.size() != 4) {
        return;
    }
    const int index = m_themePreset->currentIndex();
    if (index < 0 || index >= static_cast<int>(ThemePreset::Custom)) {
        return; // already Custom, which nothing moves away from but the combo itself
    }
    const popup::Theme theme = popup::presetTheme(static_cast<ThemePreset>(index));
    if (m_colorButtons.at(0)->color() == theme.background && m_colorButtons.at(1)->color() == theme.foreground &&
        m_colorButtons.at(2)->color() == theme.highlightWord &&
        m_colorButtons.at(3)->color() == theme.highlightReading && m_opacity->value() == theme.backgroundOpacity) {
        return;
    }
    m_themePreset->setCurrentIndex(static_cast<int>(ThemePreset::Custom));
}

void SettingsDialog::refreshPreview()
{
    if (m_preview == nullptr) {
        return;
    }
    // The widgets are pushed into the settings items, read back by the preview, and the items
    // are restored. Leaving them written would make KConfigDialogManager compare every widget
    // against itself and grey out Apply; a preview that read marupoprc instead would show the
    // last applied appearance rather than the one being edited.
    const QList<KConfigSkeletonItem *> items = PopSettings::self()->items();
    QVariantList saved;
    saved.reserve(items.size());
    for (const KConfigSkeletonItem *item : items) {
        saved.append(item->property());
    }

    pushWidgetValues(m_appearancePage);
    pushWidgetValues(m_contentPage);
    m_preview->applyTheme();
    m_preview->applyRenderOptions();

    for (qsizetype index = 0; index < items.size(); ++index) {
        items.at(index)->setProperty(saved.at(index));
    }
}

void SettingsDialog::updateSettings()
{
    if (m_shortcutButtons.isEmpty()) {
        return;
    }
    for (auto it = m_editedShortcuts.cbegin(); it != m_editedShortcuts.cend(); ++it) {
        if (m_storedShortcuts.value(it.key()) == it.value()) {
            continue;
        }
        if (m_hotkeys != nullptr && !m_hotkeys->setShortcut(it.key(), it.value())) {
            // A refused registration is a sequence another component already holds. The button
            // keeps showing what the user asked for, and the next Apply tries again.
            continue;
        }
        m_storedShortcuts.insert(it.key(), it.value());
    }
    // Whatever the daemon refused is reverted, so the page never claims a binding that is not
    // in force.
    m_editedShortcuts = m_storedShortcuts;
    showShortcuts();
}

void SettingsDialog::updateWidgets()
{
    if (m_shortcutButtons.isEmpty()) {
        return;
    }
    if (m_hotkeys != nullptr) {
        const QStringList ids = ShortcutRegistry::actionIds();
        for (const QString &id : ids) {
            m_storedShortcuts.insert(id, m_hotkeys->shortcut(id));
        }
    }
    m_editedShortcuts = m_storedShortcuts;
    showShortcuts();
}

void SettingsDialog::updateWidgetsDefault()
{
    if (m_shortcutButtons.isEmpty()) {
        return;
    }
    const QStringList ids = ShortcutRegistry::actionIds();
    for (const QString &id : ids) {
        m_editedShortcuts.insert(id, ShortcutRegistry::defaultShortcut(id));
    }
    showShortcuts();
}

bool SettingsDialog::hasChanged()
{
    // KConfigDialog calls this from addPage(), before the constructor has built the later
    // pages, so the page this reports on may not exist yet.
    if (m_shortcutButtons.isEmpty()) {
        return false;
    }
    return m_editedShortcuts != m_storedShortcuts;
}

} // namespace maru
