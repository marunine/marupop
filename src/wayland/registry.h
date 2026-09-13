// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The extra Wayland globals marupop binds beside the ones Qt's platform plugin binds for it.
//
// Three protocols reach a wlroots-family compositor that KDE Plasma covers over D-Bus:
// zwlr_screencopy_manager_v1 for region pixels, hyprland_lock_notifier_v1 for the session lock
// state and hyprland_global_shortcuts_manager_v1 for the three hotkeys. All three are bound on
// the wl_display QNativeInterface::QWaylandApplication::display() returns, on that display's
// default event queue, which Qt's plugin already reads and dispatches on the GUI thread: a
// listener callback therefore runs where a queued slot runs, and no second connection, no second
// event queue and no QtWaylandClient private header is involved.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>

// The libwayland types this header names. They are C struct tags in the library's own
// lower_case spelling, which is not the CamelCase the tree's readability-identifier-naming
// option requires of a class.
// NOLINTBEGIN(readability-identifier-naming)
struct wl_display;
struct wl_interface;
struct wl_output;
struct wl_registry;
struct wl_seat;
struct wl_shm;
// NOLINTEND(readability-identifier-naming)

class QScreen;

namespace maru::wl
{

class Registry : public QObject
{
    Q_OBJECT

public:
    // The registry of the running QGuiApplication's Wayland connection, or nullptr where the
    // platform plugin is not the Wayland one. The first call binds the registry and runs one
    // wl_display_roundtrip(), so the global set is complete when it returns. The instance is
    // parented to qApp.
    [[nodiscard]] static Registry *instance();

    [[nodiscard]] wl_display *display() const;

    // True where the compositor advertises interface, which is the name string of a
    // wl_interface, such as "zwlr_screencopy_manager_v1".
    [[nodiscard]] bool has(const QByteArray &interface) const;
    // The version the compositor advertises for interface, or 0 for one it does not advertise.
    [[nodiscard]] quint32 version(const QByteArray &interface) const;
    // Every advertised interface name, sorted. For a diagnostic and for a test.
    [[nodiscard]] QByteArrayList interfaces() const;

    // Binds interface at the lower of the advertised version and maxVersion, and answers the new
    // proxy. nullptr where the compositor advertises no such interface. The caller owns the
    // proxy and destroys it through the interface's own destructor request.
    [[nodiscard]] void *bind(const wl_interface *interface, quint32 maxVersion);

    // wl_shm, bound once and owned by the registry, for the buffers a screencopy frame is
    // copied into. nullptr on a compositor advertising none, which no Wayland compositor is.
    [[nodiscard]] wl_shm *shm();

    // The wl_output behind screen, from QNativeInterface::QWaylandScreen. nullptr for a null
    // screen and on a non-Wayland platform.
    [[nodiscard]] static wl_output *outputFor(const QScreen *screen);
    // The seat Qt's plugin holds, which hyprland_global_shortcuts_v1 needs none of and
    // ext_image_copy_capture_manager_v1 needs one of. nullptr on a non-Wayland platform.
    [[nodiscard]] static wl_seat *seat();

    // One wl_display_roundtrip() on the default queue, which is what turns a request already
    // written into a reply already dispatched. Used by the initial bind and by a caller that has
    // to observe an event before returning.
    void roundtrip();

Q_SIGNALS:
    // A global appeared or went away after the constructor's roundtrip.
    void interfacesChanged();

private:
    explicit Registry(wl_display *display, QObject *parent);
    ~Registry() override;

    void onGlobal(quint32 name, const char *interface, quint32 version);
    void onGlobalRemove(quint32 name);

    struct Global
    {
        quint32 name = 0;
        quint32 version = 0;
    };

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    QHash<QByteArray, Global> m_globals;

    friend struct RegistryListener;
};

} // namespace maru::wl
