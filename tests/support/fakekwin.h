// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A stand-in for org.kde.KWin on a private session bus, covering the two interfaces MaruPop
// calls: org.kde.KWin.ScreenShot2 at /org/kde/KWin/ScreenShot2 and org.kde.kwin.Scripting at
// /Scripting, plus org.kde.KWin.reconfigure at /KWin.
//
// The fake answers what a test sets rather than what a compositor renders, which is what makes
// three classes of case reachable that a live KWin never produces on demand:
// org.kde.KWin.ScreenShot2.Error.NoAuthorized, org.kde.KWin.ScreenShot2.Error.Cancelled, and a
// reply describing more bytes than the pipe delivered. The last one is the KWin 6.7.4 defect
// src/capture/kwingrabber.cpp works around: the compositor writes
// pixels through a QFile on a non-blocking pipe and loses up to 16384 bytes of the tail when
// the final flush fails.
//
// Registration is refused on any bus other than a private one; see privatebus.h.
#pragma once

#include <QDBusContext>
#include <QDBusUnixFileDescriptor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace maru::test
{

// org.kde.KWin.ScreenShot2 at /org/kde/KWin/ScreenShot2. Every capture method writes
// FakeScreenShot2::image to the file descriptor it is passed and replies with the vardict that
// describes it.
// QObject and QDBusContext are the two bases Qt requires for an exported object that raises a
// D-Bus error: QDBusContext::sendErrorReply() reads the message the connection is dispatching.
// NOLINTNEXTLINE(misc-multiple-inheritance)
class FakeScreenShot2 : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.ScreenShot2")

public:
    // The D-Bus error each capture method raises instead of writing pixels.
    enum class Failure
    {
        None,
        NoAuthorized, // KWin rejects a caller whose executable matches no installed desktop entry
        Cancelled,    // KWin cancels a grab superseded by another one
        Unknown,      // any other D-Bus error, which KWinGrab reports as Error::DBus
    };

    explicit FakeScreenShot2(QObject *parent = nullptr);
    ~FakeScreenShot2() override;

    // The pixels every capture method writes. Defaults to a 64x64 Format_RGBA8888 image whose
    // red channel is the column index and whose green channel is the row index, so a test can
    // assert on a pixel value without carrying a fixture file.
    QImage image;

    // The value written as "scale" in the reply, which KWinGrab applies as the image's device
    // pixel ratio. Defaults to 1.0.
    qreal scale = 1.0;

    // The error raised instead of a reply. Defaults to Failure::None.
    Failure failure = Failure::None;

    // Bytes of the tail left unwritten while the reply still describes the whole image, which
    // is the KWin 6.7.4 flush loss. Defaults to 0.
    qsizetype withholdBytes = 0;

    // The last call, for assertions on what the production code sent.
    [[nodiscard]] QString lastMethod() const;
    [[nodiscard]] QVariantList lastArguments() const;
    [[nodiscard]] QVariantMap lastOptions() const;
    [[nodiscard]] int callCount() const;

    // The six method names are the ones org.kde.KWin.ScreenShot2 defines, and D-Bus dispatches
    // on the exact name, so the project's camelBack rule cannot apply to them.
    // NOLINTBEGIN(readability-identifier-naming)
public Q_SLOTS:
    Q_SCRIPTABLE QVariantMap
    CaptureArea(int x, int y, uint width, uint height, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    Q_SCRIPTABLE QVariantMap CaptureWorkspace(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    Q_SCRIPTABLE QVariantMap CaptureScreen(const QString &name,
                                           const QVariantMap &options,
                                           const QDBusUnixFileDescriptor &pipe);
    Q_SCRIPTABLE QVariantMap CaptureActiveScreen(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    Q_SCRIPTABLE QVariantMap CaptureWindow(const QString &handle,
                                           const QVariantMap &options,
                                           const QDBusUnixFileDescriptor &pipe);
    Q_SCRIPTABLE QVariantMap CaptureActiveWindow(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe);
    // NOLINTEND(readability-identifier-naming)

private:
    QVariantMap serve(const QString &method,
                      const QVariantList &arguments,
                      const QVariantMap &options,
                      const QDBusUnixFileDescriptor &pipe);

    QString m_lastMethod;
    QVariantList m_lastArguments;
    QVariantMap m_lastOptions;
    int m_callCount = 0;
};

// org.kde.kwin.Scripting at /Scripting. loadScript() returns an increasing identifier and
// registers a FakeScript at /Scripting/Script<id>, which is the path KWinScriptRelay calls
// run() on.
// QObject and QDBusContext are the two bases Qt requires for an exported object that raises a
// D-Bus error: QDBusContext::sendErrorReply() reads the message the connection is dispatching.
// NOLINTNEXTLINE(misc-multiple-inheritance)
class FakeScripting : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")

public:
    explicit FakeScripting(QObject *parent = nullptr);
    ~FakeScripting() override;

    // False makes isScriptLoaded() answer false for every name, which is the compositor state
    // in which KWinScriptRelay falls back to loadScriptDirectly(). Defaults to true, so a
    // script becomes loaded as soon as loadScript() is called for it.
    bool acceptsLoad = true;

    // The plugin names passed to loadScript(), in call order.
    [[nodiscard]] QStringList loadedScripts() const;

    // The object paths whose run() was called, in call order.
    [[nodiscard]] QStringList runScripts() const;

    // The plugin names passed to unloadScript(), in call order.
    [[nodiscard]] QStringList unloadedScripts() const;

    // Marks a plugin name as loaded without a loadScript() call, which is the state a
    // compositor is in after KWin has read the package from disk on its own.
    void markLoaded(const QString &pluginName);

public Q_SLOTS:
    // The two-argument form alone. The one production call site is
    // KWinScriptRelay::loadScriptDirectly(), which sets the arguments to
    // {scriptPath(m_installDir), pluginId()} (src/cursor/kwinscriptrelay.cpp:352); the packaged
    // path reaches KWin through KPackage and reconfigure() rather than through loadScript. D-Bus
    // dispatches on the signature, so a one-argument overload was never reached, and it recorded
    // the file path in m_loadCalls where every assertion here reads a plugin name.
    Q_SCRIPTABLE int loadScript(const QString &filePath, const QString &pluginName);
    Q_SCRIPTABLE bool isScriptLoaded(const QString &pluginName);
    Q_SCRIPTABLE bool unloadScript(const QString &pluginName);

    // Called by the FakeScript object registered at path, which loadScript() creates.
    void recordRun(const QString &path);

private:
    QStringList m_loaded;
    QStringList m_run;
    QStringList m_unloaded;
    QStringList m_loadCalls;
    int m_nextId = 0;
};

// org.kde.KWin at /KWin, for the reconfigure() call KWinScriptRelay makes after it enables the
// plugin in kwinrc.
class FakeKWinCore : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")

public:
    explicit FakeKWinCore(QObject *parent = nullptr);
    ~FakeKWinCore() override;

    [[nodiscard]] int reconfigureCount() const;

public Q_SLOTS:
    Q_SCRIPTABLE void reconfigure();

private:
    int m_reconfigureCount = 0;
};

// Owns the three objects above and the org.kde.KWin bus name. isRegistered() is false where the
// bus refused the name or where privateBusAvailable() is false, and every case that needs the
// fake skips on it.
class FakeKWin : public QObject
{
    Q_OBJECT

public:
    explicit FakeKWin(QObject *parent = nullptr);
    ~FakeKWin() override;

    [[nodiscard]] bool isRegistered() const;

    // The reason isRegistered() is false, for a GTEST_SKIP() message.
    [[nodiscard]] QString skipReason() const;

    [[nodiscard]] FakeScreenShot2 *screenShot() const;
    [[nodiscard]] FakeScripting *scripting() const;
    [[nodiscard]] FakeKWinCore *core() const;

private:
    FakeScreenShot2 *m_screenShot = nullptr;
    FakeScripting *m_scripting = nullptr;
    FakeKWinCore *m_core = nullptr;
    bool m_registered = false;
    QString m_skipReason;
};

} // namespace maru::test
