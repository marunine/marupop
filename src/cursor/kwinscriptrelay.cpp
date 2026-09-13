// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/kwinscriptrelay.h"

#include "core/logging.h"
#include "core/paths.h"
#include "cursor/cursorsink.h"
#include "marupop_version.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTimer>

#include <KConfigGroup>
#include <KLocalizedString>

#include <algorithm>
#include <utility>

namespace maru::cursor
{

namespace
{

constexpr QLatin1StringView kKWinService("org.kde.KWin");
constexpr QLatin1StringView kScriptingPath("/Scripting");
constexpr QLatin1StringView kScriptingInterface("org.kde.kwin.Scripting");
constexpr QLatin1StringView kScriptInterface("org.kde.kwin.Script");
constexpr QLatin1StringView kKWinPath("/KWin");
constexpr QLatin1StringView kKWinInterface("org.kde.KWin");
constexpr QLatin1StringView kPluginsGroup("Plugins");
constexpr QLatin1StringView kVersionMarker(".marupop-version");

// Workspace::reconfigure() debounces for 200 ms before it emits configChanged, and
// Scripting::start() then walks every installed package. Ten polls at 200 ms cover both with
// margin; the fast path takes over afterwards.
constexpr int kPollIntervalMs = 200;
constexpr int kPollAttempts = 10;
constexpr int kCallTimeoutMs = 5000;
// The one blocking call, in the destructor. A queued asynchronous unload is dropped where the
// process exits before its event loop runs again, which would leave the script relaying into a
// bus name nothing owns.
constexpr int kUnloadTimeoutMs = 1000;

QDBusMessage scriptingMessage(const QString &method)
{
    return QDBusMessage::createMethodCall(kKWinService, kScriptingPath, kScriptingInterface, method);
}

template <typename... Reply, typename Handler>
void callAsync(QObject *context, const QDBusMessage &message, Handler &&handler)
{
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, kCallTimeoutMs), context);
    QObject::connect(watcher,
                     &QDBusPendingCallWatcher::finished,
                     context,
                     [handler = std::forward<Handler>(handler)](QDBusPendingCallWatcher *self) {
                         const QDBusPendingReply<Reply...> reply = *self;
                         self->deleteLater();
                         handler(reply);
                     });
    // The watcher is parented to context and deleted with it, and deletes itself in the handler
    // above; the analyzer models neither and reads the new expression as an allocation nothing
    // frees. The diagnostic is anchored on the closing brace, so the suppression sits here.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
}

QByteArray resourceBytes(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logCursor) << "the KWin script resource" << path << "could not be read";
        return {};
    }
    return file.readAll();
}

// The three files the package consists of, as install-relative path and content.
QList<std::pair<QString, QByteArray>> packageFiles()
{
    return {
        {QStringLiteral("metadata.json"), resourceBytes(QStringLiteral(":/marupop/kwin-script/metadata.json"))},
        {QStringLiteral("contents/code/main.js"), resourceBytes(QStringLiteral(":/marupop/kwin-script/main.js"))},
        // The marker is what makes a MaruPop upgrade rewrite a package whose two files a user
        // edited into something that still parses.
        {QString{kVersionMarker}, QByteArrayLiteral(MARUPOP_VERSION_STRING "\n")},
    };
}

bool fileHolds(const QString &path, const QByteArray &content)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.readAll() == content;
}

bool writeFile(const QString &path, const QByteArray &content)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath())) {
        qCWarning(logCursor) << "creating the directory for" << path << "failed";
        return false;
    }
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(logCursor) << "opening" << path << "failed:" << file.errorString();
        return false;
    }
    file.write(content);
    if (!file.commit()) {
        qCWarning(logCursor) << "writing" << path << "failed:" << file.errorString();
        return false;
    }
    return true;
}

} // namespace

KWinScriptRelay::KWinScriptRelay(QObject *parent)
    : KWinScriptRelay(paths::kwinScriptInstallDir(), KSharedConfig::openConfig(QStringLiteral("kwinrc")), parent)
{}

KWinScriptRelay::KWinScriptRelay(QString installDir, KSharedConfig::Ptr kwinConfig, QObject *parent)
    : CursorTracker(parent)
    , m_installDir(std::move(installDir))
    , m_kwinConfig(std::move(kwinConfig))
    , m_connection(QDBusConnection::sessionBus())
    , m_sink(new CursorSink(this))
{
    connect(m_sink, &CursorSink::positionChanged, this, &CursorTracker::positionChanged);
}

KWinScriptRelay::~KWinScriptRelay()
{
    if (!kwinAvailable()) {
        return;
    }
    QDBusMessage unload = scriptingMessage(QStringLiteral("unloadScript"));
    unload.setArguments({pluginId()});
    QDBusConnection::sessionBus().call(unload, QDBus::Block, kUnloadTimeoutMs);
}

QString KWinScriptRelay::pluginId()
{
    return QStringLiteral("marupopcursor");
}

QString KWinScriptRelay::scriptPath(const QString &installDir)
{
    return installDir + QStringLiteral("/contents/code/main.js");
}

bool KWinScriptRelay::kwinAvailable()
{
    const auto *interface = QDBusConnection::sessionBus().interface();
    return interface != nullptr && interface->isServiceRegistered(kKWinService);
}

bool KWinScriptRelay::packageMatchesResources(const QString &installDir)
{
    const auto files = packageFiles();
    return std::ranges::all_of(files, [&installDir](const auto &file) {
        return !file.second.isEmpty() && fileHolds(installDir + QLatin1Char('/') + file.first, file.second);
    });
}

bool KWinScriptRelay::writePackage(const QString &installDir)
{
    const auto files = packageFiles();
    bool written = true;
    for (const auto &[relative, content] : files) {
        if (content.isEmpty()) {
            qCWarning(logCursor) << "the KWin script package source for" << relative << "is empty";
            written = false;
            continue;
        }
        const QString path = installDir + QLatin1Char('/') + relative;
        if (fileHolds(path, content)) {
            continue;
        }
        written = writeFile(path, content) && written;
    }
    return written;
}

bool KWinScriptRelay::isEnabledInConfig(const KSharedConfig::Ptr &config)
{
    if (!config) {
        return false;
    }
    const KConfigGroup plugins{config, kPluginsGroup};
    return plugins.readEntry(pluginId() + QStringLiteral("Enabled"), false);
}

bool KWinScriptRelay::enableInConfig(const KSharedConfig::Ptr &config)
{
    if (!config) {
        return false;
    }
    if (isEnabledInConfig(config)) {
        // Written only where the value differs, so a running KWin is not asked to reload its
        // configuration for a file that did not change.
        return false;
    }
    KConfigGroup plugins{config, kPluginsGroup};
    plugins.writeEntry(pluginId() + QStringLiteral("Enabled"), true);
    plugins.sync();
    return true;
}

CursorSink *KWinScriptRelay::sink() const
{
    return m_sink;
}

bool KWinScriptRelay::isAvailable() const
{
    return m_available;
}

QString KWinScriptRelay::unavailableReason() const
{
    return m_reason;
}

QString KWinScriptRelay::installDir() const
{
    return m_installDir;
}

void KWinScriptRelay::setConnection(const QDBusConnection &connection)
{
    m_connection = connection;
}

void KWinScriptRelay::setTracking(bool tracking)
{
    CursorTracker::setTracking(tracking);
    // The value the next Update reply carries. The script switches between the 8 ms pump and the
    // 500 ms heartbeat on it, so the change takes effect within one heartbeat.
    m_sink->setTracking(tracking);
}

void KWinScriptRelay::start()
{
    // First, so a script that loads immediately never calls into a path nothing answers.
    if (!m_sink->isRegistered() && !m_sink->registerObject(m_connection)) {
        setAvailability(
            false, i18nc("@info cursor relay status", "The pointer relay could not be exported on the session bus."));
        return;
    }
    watchKWin();

    if (!writePackage(m_installDir)) {
        setAvailability(
            false,
            i18nc("@info cursor relay status", "The KWin script package could not be written to %1.", m_installDir));
        return;
    }
    enableInConfig(m_kwinConfig);

    if (!kwinAvailable()) {
        setAvailability(false,
                        i18nc("@info cursor relay status", "Pointer tracking unavailable: KWin is not running."));
        return;
    }
    reconfigure();
}

void KWinScriptRelay::stop()
{
    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
    }
    if (!kwinAvailable()) {
        return;
    }
    QDBusMessage unload = scriptingMessage(QStringLiteral("unloadScript"));
    unload.setArguments({pluginId()});
    callAsync<bool>(this, unload, [this](const QDBusPendingReply<bool> &reply) {
        if (reply.isError()) {
            qCWarning(logCursor) << "unloadScript failed:" << reply.error().message();
        }
        setAvailability(false, i18nc("@info cursor relay status", "Pointer tracking stopped."), true);
    });
}

void KWinScriptRelay::reconfigure()
{
    // Workspace::reconfigure() debounces 200 ms and then emits configChanged, which is what
    // makes Scripting::start() pick up a package enabled while KWin was already running.
    const QDBusMessage message =
        QDBusMessage::createMethodCall(kKWinService, kKWinPath, kKWinInterface, QStringLiteral("reconfigure"));
    callAsync<>(this, message, [this](const QDBusPendingReply<> &reply) {
        if (reply.isError()) {
            qCWarning(logCursor) << "org.kde.KWin.reconfigure failed:" << reply.error().message();
        }
        startPolling();
    });
}

void KWinScriptRelay::startPolling()
{
    m_pollAttempts = 0;
    if (m_pollTimer == nullptr) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setSingleShot(true);
        m_pollTimer->setInterval(kPollIntervalMs);
        connect(m_pollTimer, &QTimer::timeout, this, &KWinScriptRelay::pollScriptLoaded);
    }
    m_pollTimer->start();
}

void KWinScriptRelay::pollScriptLoaded()
{
    ++m_pollAttempts;
    QDBusMessage message = scriptingMessage(QStringLiteral("isScriptLoaded"));
    message.setArguments({pluginId()});
    callAsync<bool>(this, message, [this](const QDBusPendingReply<bool> &reply) {
        if (!reply.isError() && reply.value()) {
            setAvailability(true, QString{});
            return;
        }
        if (m_pollAttempts < kPollAttempts) {
            m_pollTimer->start();
            return;
        }
        loadScriptDirectly();
    });
}

void KWinScriptRelay::loadScriptDirectly()
{
    // The packaged path did not load within kPollAttempts * kPollIntervalMs. loadScript takes an
    // absolute path to a .js file and no package at all, so it works whatever state the KPackage
    // registry is in; the script it starts is lost on the next KWin restart, which the service
    // watcher covers.
    QDBusMessage message = scriptingMessage(QStringLiteral("loadScript"));
    message.setArguments({scriptPath(m_installDir), pluginId()});
    callAsync<int>(this, message, [this](const QDBusPendingReply<int> &reply) {
        if (reply.isError()) {
            setAvailability(false,
                            i18nc("@info cursor relay status",
                                  "Could not load the pointer tracking script in KWin: %1",
                                  reply.error().message()));
            return;
        }
        const int id = reply.value();
        if (id < 0) {
            // A script with this plugin name is already loaded, which is the answer the poll
            // above was looking for.
            verifyLoaded(i18nc("@info cursor relay status",
                               "The pointer tracking script is registered with KWin but is not running."));
            return;
        }
        runScript(id);
    });
}

void KWinScriptRelay::runScript(int id)
{
    // loadScript() only constructs the script object. Each script registers itself at
    // /Scripting/Script<id> with org.kde.kwin.Script, whose run() evaluates it.
    const QDBusMessage message = QDBusMessage::createMethodCall(
        kKWinService, QStringLiteral("/Scripting/Script%1").arg(id), kScriptInterface, QStringLiteral("run"));
    callAsync<>(this, message, [this](const QDBusPendingReply<> &reply) {
        if (reply.isError()) {
            setAvailability(false,
                            i18nc("@info cursor relay status",
                                  "KWin could not run the pointer relay script: %1",
                                  reply.error().message()));
            return;
        }
        // run() replies success even where the script threw during evaluation, in which case
        // KWin deletes it and writes one kwin_scripting warning to the journal. isScriptLoaded is the observable that
        // distinguishes the two.
        verifyLoaded(i18nc("@info cursor relay status", "Could not start the pointer tracking script in KWin."));
    });
}

void KWinScriptRelay::verifyLoaded(const QString &reasonWhenAbsent)
{
    QDBusMessage message = scriptingMessage(QStringLiteral("isScriptLoaded"));
    message.setArguments({pluginId()});
    callAsync<bool>(this, message, [this, reasonWhenAbsent](const QDBusPendingReply<bool> &reply) {
        const bool loaded = !reply.isError() && reply.value();
        setAvailability(loaded, loaded ? QString{} : reasonWhenAbsent);
    });
}

void KWinScriptRelay::refresh()
{
    if (!kwinAvailable()) {
        setAvailability(false,
                        i18nc("@info cursor relay status", "Pointer tracking unavailable: KWin is not running."));
        return;
    }
    verifyLoaded(isEnabledInConfig(m_kwinConfig)
                     ? i18nc("@info cursor relay status", "The pointer tracking script is not running in KWin.")
                     : i18nc("@info cursor relay status",
                             "Enable the MaruPop cursor relay in System Settings > Window Management > KWin Scripts."));
}

void KWinScriptRelay::setAvailability(bool available, const QString &reason, bool expected)
{
    if (m_availabilityReported && available == m_available && reason == m_reason) {
        return;
    }
    m_availabilityReported = true;
    m_available = available;
    m_reason = reason;
    if (available) {
        qCInfo(logCursor) << "the pointer relay is delivering positions";
    } else if (expected) {
        // stop() is the user turning scanning off, not a failure.
        qCInfo(logCursor) << "the pointer relay is stopped:" << reason;
    } else {
        qCWarning(logCursor) << "pointer relay unavailable:" << reason;
    }
    Q_EMIT availabilityChanged(available, reason);
}

void KWinScriptRelay::watchKWin()
{
    if (m_watcher != nullptr) {
        return;
    }
    // A KWin restart drops every runtime-loaded script and re-runs Scripting::start() for the
    // packaged ones. Redoing the bootstrap on re-registration covers both paths.
    m_watcher = new QDBusServiceWatcher(
        QString{kKWinService}, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        qCDebug(logCursor) << "org.kde.KWin reappeared; redoing the relay bootstrap";
        reconfigure();
    });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        setAvailability(false,
                        i18nc("@info cursor relay status", "Pointer tracking unavailable: KWin is not running."));
    });
}

} // namespace maru::cursor
