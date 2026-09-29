// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/application.h"

#include "app/lookupwindow.h"
#include "app/modeldownloaddialog.h"
#include "app/notifier.h"
#include "app/settingsdialog.h"
#include "app/shortcutregistry.h"
#include "app/trayicon.h"
#include "capture/framesource.h"
#include "core/logging.h"
#include "core/settings.h"
#include "cursor/cursortracker.h"
#include "cursor/lockwatcher.h"
#include "deconj/deconjugator.h"
#include "dict/dictionarydownloadjob.h"
#include "dict/dictionaryimportjob.h"
#include "dict/dictionarymanager.h"
#include "dict/updatecheckjob.h"
#include "dictui/dictionarymanagerdialog.h"
#include "lookup/engine.h"
#include "lookup/popupadapter.h"
#include "ocr/modelstore.h"
#include "ocr/ocrservice.h"
#include "platform/backend.h"
#include "platform/desktop.h"
#include "popup/popupwindow.h"
#include "scan/scancontroller.h"
#include "scan/wordcopier.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include <KConfigDialog>
#include <KLocalizedString>

#include <algorithm>

namespace maru
{

namespace
{

constexpr QLatin1StringView toggleScanningOption("toggle-scanning");
constexpr QLatin1StringView settingsOption("settings");
constexpr QLatin1StringView lookupWindowOption("lookup-window");

// Six hours. A dictionary's own autoUpdateAfterDays decides whether it is due; this is only
// how often the question is asked, and a session that stays up for a week would otherwise ask
// it once.
constexpr int kUpdateCheckIntervalMs = 6 * 60 * 60 * 1000;
// How long a hotkey's confirmation stays in the tray tooltip.
constexpr int kStatusFlashMs = 4000;

// The LookupOcrVariants and LookupVariant* settings, as the engine takes them.
[[nodiscard]] lookup::VariantOptions variantOptionsFromSettings()
{
    lookup::VariantOptions options;
    options.enabled = PopSettings::lookupOcrVariants();
    options.confidenceGate = static_cast<float>(PopSettings::lookupVariantConfidenceGate());
    options.withoutConfidences = PopSettings::lookupVariantWithoutConfidences();
    options.maxKeyLength = PopSettings::lookupVariantMaxKeyLength();
    options.maxKeys = PopSettings::lookupVariantMaxKeys();
    options.acceptance = PopSettings::lookupVariantCommonWordsOnly() ? lookup::VariantAcceptance::CommonWords
                                                                     : lookup::VariantAcceptance::AnyWord;
    options.shortAndHiraganaMatches = PopSettings::lookupVariantShortAndHiragana();
    options.nameDictionaries = PopSettings::lookupVariantNameDictionaries();
    options.rankFirst = PopSettings::lookupVariantRankFirst();
    return options;
}

} // namespace

Application::Application(QObject *parent)
    : QObject(parent)
{}

Application::~Application() = default;

TrayIcon *Application::trayIcon() const
{
    return m_tray;
}

LookupWindow *Application::lookupWindow() const
{
    return m_lookupWindow.get();
}

void Application::start()
{
    buildPipeline();

    m_tray = new TrayIcon(this);
#ifdef Q_OS_WIN
    m_notifier->setTrayIcon(m_tray);
#endif
    connect(m_tray, &TrayIcon::toggleScanningRequested, this, &Application::toggleScanning);
    connect(m_tray, &TrayIcon::lookupWindowRequested, this, &Application::toggleLookupWindow);
    connect(m_tray, &TrayIcon::settingsRequested, this, &Application::showSettings);
    connect(m_tray, &TrayIcon::aboutRequested, this, &Application::showAbout);
    connect(m_tray, &TrayIcon::dictionariesRequested, this, &Application::showDictionaryManager);
    connect(m_tray, &TrayIcon::quitRequested, qApp, &QCoreApplication::quit);
    m_tray->setScanning(PopSettings::scanningEnabled());
    m_tray->setVisible(PopSettings::showTrayIcon());

    wireScanning();
    wireHotkeys();

    // Startup reconciles the setting with the entry on disk. Application::applySettings() is
    // the path that installs or removes the entry, and compares against the value recorded
    // here to tell an autostart change from any other setting the dialog wrote.
    adoptAutostartState();
    m_autostartApplied = PopSettings::autostart();

    checkCaptureAvailability();
    maybeRunFirstRun();

    // The recognition worker loads the backend the settings select, which costs about 0.3 s
    // and happens off this thread.
    m_ocr->start();
    if (PopSettings::scanningEnabled()) {
        setScanning(true);
    }

    // The first check runs from the event loop rather than from here, so a start is not held up
    // by a network round trip per dictionary.
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(kUpdateCheckIntervalMs);
    connect(m_updateTimer, &QTimer::timeout, this, &Application::checkDictionaryUpdates);
    m_updateTimer->start();
    QTimer::singleShot(0, this, &Application::checkDictionaryUpdates);
}

void Application::buildPipeline()
{
    m_notifier = new Notifier(this);

    m_dictionaries = new dict::DictionaryManager(this);
    if (!m_dictionaries->load()) {
        qCWarning(logApp) << "the dictionary list could not be read; starting from the built-in seeds";
    }
    m_dictionaries->seedBuiltIns();
    if (!m_dictionaries->save()) {
        qCWarning(logApp) << "the dictionary list could not be written";
    }
    if (!m_dictionaries->loadWordClassTable()) {
        qCDebug(logApp) << "no word-class table yet; JMdict has not been imported";
    }

    QString rulesError;
    if (std::optional<deconj::RuleSet> rules = deconj::RuleSet::loadEmbedded(&rulesError)) {
        m_rules = std::make_shared<deconj::RuleSet>(std::move(*rules));
    } else {
        // Every lookup still works; only the conjugated forms stop resolving, so this is a
        // degraded start rather than a fatal one.
        qCWarning(logApp) << "the deconjugation rules could not be loaded:" << rulesError;
    }

    if (m_rules) {
        m_engine = std::make_shared<lookup::Engine>(*m_dictionaries, *m_rules);
        m_engine->setVariantOptions(variantOptionsFromSettings());
        m_lookupFunction = [engine = m_engine](const lookup::Request &request) {
            return engine->lookup(request);
        };
    }
    if (!m_lookupFunction) {
        qCWarning(logApp) << "no lookup engine; every lookup answers empty";
        m_lookupFunction = [](const lookup::Request &) {
            return lookup::Response{};
        };
    }
    // The response carries LookupWindowMaxResults results while the lookup window is open, and
    // the popup lists its own MaxResults of them.
    m_popupAdapter = [](const lookup::Response &response) {
        return lookup::toPopupModel(lookup::firstResults(response, PopSettings::maxResults()));
    };

    m_models = new ocr::ModelStore(this);
    m_ocr = new ocr::OcrService(this);
    // The four session-dependent services, chosen by platform::detect(). Everything below this
    // line reaches them through cursor::CursorTracker, capture::FrameSource,
    // cursor::LockWatcher and maru::ShortcutRegistry.
    m_platform = new platform::Backend(this);
    m_popup = std::make_unique<popup::PopupWindow>();

    // A pixel source that composites MaruPop's own windows needs to be told where the popup is,
    // so it can report the rectangle as Frame::occluded, and the popup needs to stay off the
    // text it answers for, so that rectangle covers no part of the resolved paragraph.
    if (m_platform->capturesOwnWindows()) {
        popup::PopupWindow *popup = m_popup.get();
        m_platform->frames()->setOcclusionProvider([popup] {
            return popup->occlusionRect();
        });
        m_popup->setAvoidsText(true);
    }

    m_scan = new scan::ScanController(*m_platform->tracker(), *m_platform->frames(), *m_ocr, m_lookupFunction, this);
    m_scan->setLockWatcher(m_platform->lockWatcher());

    // What the pipeline was built with, which is the first thing a bug report needs and the
    // only place the enabled dictionary set is visible without opening the manager.
    QStringList opened;
    const QList<dict::Dictionary *> enabled = m_dictionaries->enabledDictionaries();
    opened.reserve(enabled.size());
    for (const dict::Dictionary *entry : enabled) {
        opened.append(entry->name + QStringLiteral(" (%1)").arg(entry->recordCount));
    }
    qCInfo(logApp).noquote() << "dictionaries:"
                             << (opened.isEmpty() ? QStringLiteral("none opened") : opened.join(QStringLiteral(", ")));
}

void Application::wireScanning()
{
    connect(m_scan, &scan::ScanController::scanningChanged, this, [this](bool scanning) {
        m_tray->setScanning(scanning);
        // The pointer source's 8 ms rate is the most expensive thing in the pipeline while
        // nothing is being looked up, so it runs only while scanning does.
        if (scanning) {
            m_platform->startTracking();
        } else {
            m_platform->stopTracking();
            m_popup->hidePopup();
        }
    });
    connect(m_scan, &scan::ScanController::statusChanged, this, [this](const QString &status) {
        m_tray->setStatusText(status);
    });
    connect(m_scan, &scan::ScanController::error, this, [this](const QString &message) {
        reportFailure(i18nc("@title:window", "Scanning Failed"), message);
    });
    connect(m_scan,
            &scan::ScanController::lookupReady,
            this,
            [this](const lookup::Response &response, const scan::HitContext &context) {
                // A lookup that ran on the snapshot before a dictionary change can deliver a
                // Store the change released. Such a response returns here before the popup, the
                // retained response and the lookup window receive it. The rescan that the change
                // started answers from the current snapshot.
                if (holdsReplacedStore(response)) {
                    return;
                }
                m_lastResponse = response;
                m_lastHit = context;
                // A closed window is handed m_lastResponse by the next showLookupWindow() instead.
                if (m_lookupWindowOpen) {
                    m_lookupWindow->setLookup(response, context);
                }
                // A suppressed card drops the model, so the adapter is not run for it.
                if (m_popup->isSuppressed()) {
                    return;
                }
                m_popup->setModel(m_popupAdapter(response));
                // The paragraph the card answers for, which it is placed off on a session whose
                // pixel source composites the card into the next grab. The value is kept for the
                // hitMoved() samples that follow, which carry no rectangle.
                m_popup->setAvoidRect(context.paragraphRectLogical);
                m_popup->showNear(context.anchorLogical, context.anchorScreen);
            });
    connect(m_scan, &scan::ScanController::hitMoved, this, [this](QPoint cursorLogical, QScreen *screen) {
        // The card already carries the response for the character under the pointer, so this call
        // re-anchors it and renders nothing. PopupWindow::isFrozen() is what holds a pinned card
        // still, for both this call and the setModel() above.
        m_popup->showNear(cursorLogical, screen);
    });
    connect(m_scan, &scan::ScanController::nothingUnderCursor, this, [this](QPoint) {
        // A pinned card is the user reading it; the pointer leaving the text is not a reason to
        // take it away.
        if (!m_popup->isPinned()) {
            m_popup->hidePopup();
        }
    });

    connect(m_platform->tracker(),
            &cursor::CursorTracker::availabilityChanged,
            this,
            [this](bool available, const QString &reason) {
                if (available) {
                    // Uncovers whatever the controller last reported.
                    m_tray->setStatusMessage(QString());
                    return;
                }
                // Stopping the source reports it as unavailable too, so scanning turned off by
                // the user arrives here as well, and a toggle the user made is not a failure to
                // report.
                if (!m_scan->isScanning()) {
                    return;
                }
                // Without a pointer source there is no pointer position at all on Wayland, so
                // this is the one failure that stops everything rather than degrading it.
                reportFailure(i18nc("@title:window", "Pointer Tracking Unavailable"), reason);
            });

    // A compositor configuration line the session is missing, reported once. On Hyprland the
    // line is the `no_screen_share` layer rule, without which the popup's own text reaches the
    // recognition pass.
    connect(
        m_platform, &platform::Backend::configurationMissing, this, [this](const QString &header, const QString &line) {
            reportFailure(i18nc("@title:window", "Compositor Configuration Missing"),
                          header + QLatin1Char('\n') + line);
        });

    connect(m_dictionaries, &dict::DictionaryManager::changed, this, [this] {
        // The engine reads no manager state of its own: the dictionary set it answers from is
        // pushed here, on the GUI thread, and setDictionaries() drops the cached results the
        // previous set produced.
        if (m_engine) {
            m_engine->setDictionaries(m_dictionaries->snapshot(), m_dictionaries->wordClasses());
        }
        // Releases the Store of a reimported or removed dictionary. A response that holds one
        // keeps its files open (DictionaryManager::isCurrentStore()).
        if (holdsReplacedStore(m_lastResponse)) {
            m_lastResponse = {};
            m_lastHit = {};
        }
        if (m_lookupWindow && holdsReplacedStore(m_lookupWindow->response())) {
            m_lookupWindow->clearLookup();
        }
        m_scan->forceRescan();
    });
}

bool Application::holdsReplacedStore(const lookup::Response &response) const
{
    return std::ranges::any_of(response.results, [this](const lookup::Result &result) {
        return result.dictionary.store != nullptr && !m_dictionaries->isCurrentStore(result.dictionary.store.get());
    });
}

void Application::wireHotkeys()
{
    m_hotkeys = m_platform->shortcuts();
    connect(m_hotkeys, &ShortcutRegistry::triggered, this, [this](const QString &id) {
        if (id == QLatin1StringView("toggle-scanning")) {
            toggleScanning();
        } else if (id == QLatin1StringView("copy-word")) {
            copyCurrentWord();
        } else if (id == QLatin1StringView("pin-popup")) {
            togglePinned();
        }
    });
    m_hotkeys->registerActions();
}

void Application::addCommandLineOptions(QCommandLineParser &parser)
{
    parser.addOption(QCommandLineOption{QString{toggleScanningOption}, i18n("Toggle scanning")});
    parser.addOption(QCommandLineOption{QString{settingsOption}, i18n("Open the settings window")});
    parser.addOption(QCommandLineOption{QString{lookupWindowOption}, i18n("Open the lookup window")});
}

bool Application::handleCommandLine(const QStringList &arguments)
{
    QCommandLineParser parser;
    addCommandLineOptions(parser);
    // A forwarded command line carries the options KAboutData registered as well, which this
    // parser does not know. parse() reports them and leaves the options it does know set;
    // process() would exit the resident process over one of them.
    parser.parse(arguments);
    if (parser.isSet(QString{toggleScanningOption})) {
        toggleScanning();
        return true;
    }
    if (parser.isSet(QString{settingsOption})) {
        showSettings();
        return true;
    }
    if (parser.isSet(QString{lookupWindowOption})) {
        showLookupWindow();
        return true;
    }
    return false;
}

void Application::activate(const QStringList &arguments)
{
    if (handleCommandLine(arguments)) {
        return;
    }
    // A verbless activation is what the launcher entry and a System Settings "_launch"
    // shortcut send. The application has no main window, so the settings window is the
    // visible answer.
    showSettings();
}

void Application::setScanning(bool scanning)
{
    if (m_scan != nullptr) {
        // The controller writes the state through to the settings and reports it back through
        // scanningChanged(), which is what moves the tray.
        m_scan->setScanning(scanning);
        return;
    }
    settings::persistScanningEnabled(scanning);
    if (m_tray != nullptr) {
        m_tray->setScanning(scanning);
    }
}

void Application::toggleScanning()
{
    setScanning(!(m_scan != nullptr ? m_scan->isScanning() : PopSettings::scanningEnabled()));
}

void Application::copyCurrentWord()
{
    const QString text = scan::wordToCopy(m_lastResponse, m_lastHit, settings::copyWordMode());
    if (text.isEmpty()) {
        return;
    }
    scan::copyToClipboard(text);
    // No notification: the shortcut is pressed while reading, and a toast over the text being
    // read is worse than no confirmation at all.
    flashStatus(i18nc("@info:tooltip after the copy shortcut", "Copied %1", text));
}

void Application::togglePinned()
{
    m_popup->setPinned(!m_popup->isPinned());
}

void Application::toggleLookupWindow()
{
    if (m_lookupWindow && m_lookupWindow->isVisible()) {
        m_lookupWindow->close();
        return;
    }
    showLookupWindow();
}

void Application::showLookupWindow()
{
    if (!m_lookupWindow) {
        m_lookupWindow = std::make_unique<LookupWindow>();
        connect(m_lookupWindow.get(), &LookupWindow::visibilityChanged, this, [this](bool visible) {
            m_lookupWindowOpen = visible;
            if (m_tray != nullptr) {
                m_tray->setLookupWindowVisible(visible);
            }
            applyLookupWindowState();
        });
        connect(m_lookupWindow.get(), &LookupWindow::sentenceCopied, this, [this](const QString &sentence) {
            flashStatus(i18nc("@info:tooltip after the copy shortcut", "Copied %1", sentence));
        });
    }
    if (!m_lastResponse.results.isEmpty()) {
        m_lookupWindow->setLookup(m_lastResponse, m_lastHit);
    }
    m_lookupWindow->show();
    m_lookupWindow->raise();
    m_lookupWindow->activateWindow();
}

void Application::applyLookupWindowState()
{
    if (m_scan != nullptr) {
        m_scan->setMinimumResults(m_lookupWindowOpen ? PopSettings::lookupWindowMaxResults() : 0);
    }
    if (m_popup) {
        m_popup->setSuppressed(m_lookupWindowOpen && PopSettings::hidePopupWhileLookupWindowOpen());
    }
}

void Application::showSettings()
{
    if (KConfigDialog::showDialog(QStringLiteral("marupop-settings"))) {
        return;
    }
    auto *dialog = new SettingsDialog(m_hotkeys);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &KConfigDialog::settingsChanged, this, &Application::applySettings);
    connect(dialog, &SettingsDialog::manageDictionariesRequested, this, &Application::showDictionaryManager);
    connect(dialog, &SettingsDialog::modelsDownloaded, this, [this] {
        m_notifier->modelsDownloaded();
        m_ocr->setEngine(settings::ocrEngine());
    });
    dialog->show();
}

void Application::showDictionaryManager()
{
    if (!m_dictionaryDialog.isNull()) {
        m_dictionaryDialog->show();
        m_dictionaryDialog->raise();
        m_dictionaryDialog->activateWindow();
        return;
    }
    auto *dialog = new DictionaryManagerDialog(*m_dictionaries, nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dictionaryDialog = dialog;
    dialog->show();
}

void Application::showAbout()
{
    // One dialog: a second request raises the open one rather than stacking a copy the user has
    // to close twice.
    if (!m_aboutDialog.isNull()) {
        m_aboutDialog->show();
        m_aboutDialog->raise();
        m_aboutDialog->activateWindow();
        return;
    }
    QDialog *dialog = platform::createAboutDialog();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_aboutDialog = dialog;
    dialog->show();
}

void Application::maybeRunFirstRun()
{
    if (PopSettings::firstRunCompleted()) {
        return;
    }
    // Nothing to ask for where the models are already in place: an installation restored from a
    // backup, a second profile on the same machine, or a directory the user filled by hand
    // would otherwise open a dialog whose only offer is a zero-byte download.
    if (m_models->requiredModelsPresent()) {
        PopSettings::setFirstRunCompleted(true);
        PopSettings::self()->save();
        return;
    }
    auto *dialog = new ModelDownloadDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, [this, dialog](int) {
        if (dialog->downloadSucceeded()) {
            m_notifier->modelsDownloaded();
        }
        // Whatever the answer was, it is in the settings by now: the engine choice and the
        // model directory are what the recognition service reads.
        applySettings();
    });
    dialog->show();
}

void Application::checkCaptureAvailability()
{
    // The gate differs per session and the backend owns which one applies. On KDE, KWin matches
    // a caller's /proc/pid/exe against an installed desktop entry and grants
    // org.kde.KWin.ScreenShot2 from its X-KDE-DBUS-Restricted-Interfaces; a build tree, a second
    // prefix or a stale entry leaves the match failing. On Hyprland, ecosystem:enforce_permissions
    // is false by default and the check is whether zwlr_screencopy_manager_v1 was bound. Either
    // way the only symptom of a failure is a capture that produces nothing.
    const platform::CaptureReport report = m_platform->captureReport();
    if (report.authorized) {
        return;
    }
    for (const QString &line : report.lines) {
        qCWarning(logApp).noquote() << line;
    }
    reportFailure(i18nc("@title:window", "Screen Capture Unavailable"), report.remedy);
}

void Application::checkDictionaryUpdates()
{
    const QList<dict::Dictionary *> due = m_dictionaries->dictionariesDueForUpdate();
    for (const dict::Dictionary *dictionary : due) {
        dict::UpdateCheckJob *job = m_dictionaries->createUpdateCheckJob(dictionary->id);
        if (job == nullptr) {
            continue;
        }
        // Not registered with the job tracker: a conditional GET is not a transfer, and four of
        // them at every start would fill the notification area's job list with nothing.
        connect(job, &KJob::result, this, [this, job] {
            onUpdateCheckFinished(job);
        });
        job->start();
    }
}

void Application::onUpdateCheckFinished(dict::UpdateCheckJob *job)
{
    if (job->error() != 0) {
        qCWarning(logApp) << "the update check failed:" << job->errorString();
        return;
    }
    if (!job->updateAvailable()) {
        return;
    }
    const dict::Dictionary *dictionary = m_dictionaries->dictionary(job->dictionaryId());
    if (dictionary == nullptr) {
        return;
    }
    m_notifier->dictionaryUpdateAvailable(dictionary->name);
    startDictionaryDownload(dictionary->id);
}

void Application::startDictionaryDownload(const QUuid &id)
{
    dict::DictionaryDownloadJob *job = m_dictionaries->createDownloadJob(id);
    if (job == nullptr) {
        return;
    }
    // A dictionary dump is tens of megabytes, so it belongs in Plasma's transfer list beside
    // every other download.
    platform::registerJob(job);
    connect(job, &KJob::result, this, [this, id, job] {
        if (job->error() != 0) {
            reportFailure(i18nc("@title:window", "Dictionary Download Failed"), job->errorString());
            return;
        }
        if (job->notModified()) {
            return;
        }
        startDictionaryImport(id);
    });
    job->start();
}

void Application::startDictionaryImport(const QUuid &id)
{
    dict::DictionaryImportJob *job = m_dictionaries->createImportJob(id);
    if (job == nullptr) {
        return;
    }
    platform::registerJob(job);
    connect(job, &KJob::result, this, [this, id, job] {
        if (job->error() != 0) {
            reportFailure(i18nc("@title:window", "Dictionary Import Failed"), job->errorString());
            return;
        }
        if (!m_dictionaries->applyImportResult(id, job)) {
            reportFailure(i18nc("@title:window", "Dictionary Import Failed"),
                          i18nc("@info", "The imported dictionary could not be opened."));
            return;
        }
        if (!m_dictionaries->save()) {
            qCWarning(logApp) << "the dictionary list could not be written after an import";
        }
        const dict::Dictionary *dictionary = m_dictionaries->dictionary(id);
        m_notifier->dictionaryImported(dictionary != nullptr ? dictionary->name : QString(),
                                       static_cast<int>(job->result().recordCount));
    });
    job->start();
}

void Application::applySettings()
{
    if (m_tray != nullptr) {
        m_tray->setVisible(PopSettings::showTrayIcon());
    }
    if (m_scan != nullptr) {
        m_scan->applySettings();
    }
    if (m_ocr != nullptr) {
        m_ocr->setEngine(settings::ocrEngine());
    }
    if (m_popup) {
        m_popup->applyTheme();
        m_popup->applyRenderOptions();
    }
    if (m_lookupWindow) {
        m_lookupWindow->applySettings();
    }
    // After the scan controller re-read MaxResults, which the minimum count is compared against.
    applyLookupWindowState();
    if (m_engine) {
        // The lookup cache is keyed by span and category; the category, the result count and
        // the search length all just moved.
        m_engine->setVariantOptions(variantOptionsFromSettings());
        m_engine->invalidateCache();
    }
    if (m_models != nullptr) {
        m_models->setDirectory(settings::modelDirectory());
    }
    if (m_hotkeys != nullptr) {
        m_hotkeys->registerActions();
    }
    // KConfigDialog emits settingsChanged for a change to any setting, so the autostart entry
    // is rewritten only when the autostart setting itself moved. Rewriting it on an unrelated
    // change would also report a missing installation to a user who never asked for autostart
    // in that dialog.
    if (PopSettings::autostart() != m_autostartApplied && applyAutostart()) {
        m_autostartApplied = PopSettings::autostart();
    }
}

void Application::reportFailure(const QString &title, const QString &message)
{
    // Failures never open a modal dialog: they land in the tray tooltip and on the failed
    // notification channel that NotifyOnError leaves on by default.
    qCWarning(logApp) << title << message;
    if (m_tray != nullptr) {
        m_tray->setStatusMessage(message);
    }
    if (m_notifier != nullptr) {
        m_notifier->failure(title, message);
    }
}

void Application::flashStatus(const QString &message)
{
    if (m_tray == nullptr) {
        return;
    }
    m_tray->setStatusMessage(message);
    if (m_statusFlash == nullptr) {
        m_statusFlash = new QTimer(this);
        m_statusFlash->setSingleShot(true);
        m_statusFlash->setInterval(kStatusFlashMs);
        connect(m_statusFlash, &QTimer::timeout, this, [this] {
            m_tray->setStatusMessage(QString());
        });
    }
    m_statusFlash->start();
}

} // namespace maru
