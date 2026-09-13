// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "wayland/registry.h"

#include "core/logging.h"

#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qscreen_platform.h>

#include <algorithm>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

namespace maru::wl
{

namespace
{

// The wl_display of the running application, or nullptr where the platform plugin is not the
// Wayland one. QGuiApplication::nativeInterface() answers nullptr for a plugin that implements
// no such interface, which is what the offscreen and xcb plugins do.
wl_display *applicationDisplay()
{
    if (qGuiApp == nullptr) {
        return nullptr;
    }
    auto *application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    return application == nullptr ? nullptr : application->display();
}

} // namespace

// The two wl_registry events, forwarded to the Registry the listener's user data names. A
// struct rather than two free functions, so the private member functions stay private.
struct RegistryListener
{
    static void global(void *data, wl_registry * /*registry*/, quint32 name, const char *interface, quint32 version)
    {
        static_cast<Registry *>(data)->onGlobal(name, interface, version);
    }

    static void globalRemove(void *data, wl_registry * /*registry*/, quint32 name)
    {
        static_cast<Registry *>(data)->onGlobalRemove(name);
    }
};

namespace
{

const wl_registry_listener kRegistryListener = {
    .global = &RegistryListener::global,
    .global_remove = &RegistryListener::globalRemove,
};

} // namespace

Registry *Registry::instance()
{
    // A QPointer rather than a raw one: the registry is parented to qGuiApp, so it is destroyed
    // with the application, and a caller running after that has to receive nullptr rather than a
    // dangling pointer. attempted keeps a non-Wayland platform from repeating the probe on every
    // call, and after the application is gone it is what makes every further call answer nullptr.
    static bool attempted = false;
    static QPointer<Registry> registry;
    if (attempted) {
        return registry.data();
    }
    attempted = true;

    wl_display *display = applicationDisplay();
    if (display == nullptr) {
        qCDebug(logWayland) << "no Wayland display; the Wayland backends are unavailable";
        return nullptr;
    }
    registry = new Registry(display, qGuiApp);
    return registry.data();
}

Registry::Registry(wl_display *display, QObject *parent)
    : QObject(parent)
    , m_display(display)
{
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    // The constructor is what the callers' has() answers are read against, so the global set has
    // to be complete before it returns.
    roundtrip();
    qCDebug(logWayland) << "bound the Wayland registry with" << m_globals.size() << "globals";
}

Registry::~Registry()
{
    if (m_shm != nullptr) {
        wl_shm_destroy(m_shm);
    }
    if (m_registry != nullptr) {
        wl_registry_destroy(m_registry);
    }
}

wl_display *Registry::display() const
{
    return m_display;
}

bool Registry::has(const QByteArray &interface) const
{
    return m_globals.contains(interface);
}

quint32 Registry::version(const QByteArray &interface) const
{
    return m_globals.value(interface).version;
}

QByteArrayList Registry::interfaces() const
{
    QByteArrayList names = m_globals.keys();
    std::ranges::sort(names);
    return names;
}

void *Registry::bind(const wl_interface *interface, quint32 maxVersion)
{
    if (interface == nullptr || m_registry == nullptr) {
        return nullptr;
    }
    const QByteArray name = QByteArray::fromRawData(interface->name, qstrlen(interface->name));
    const auto found = m_globals.constFind(name);
    if (found == m_globals.constEnd()) {
        return nullptr;
    }
    const quint32 version = qMin(found->version, maxVersion);
    return wl_registry_bind(m_registry, found->name, interface, version);
}

wl_shm *Registry::shm()
{
    if (m_shm == nullptr) {
        m_shm = static_cast<wl_shm *>(bind(&wl_shm_interface, 1));
    }
    return m_shm;
}

wl_output *Registry::outputFor(const QScreen *screen)
{
    if (screen == nullptr) {
        return nullptr;
    }
    // const_cast: nativeInterface() is non-const on QScreen and the interface it answers with
    // holds no mutable state of its own.
    auto *native = const_cast<QScreen *>(screen)->nativeInterface<QNativeInterface::QWaylandScreen>();
    return native == nullptr ? nullptr : native->output();
}

wl_seat *Registry::seat()
{
    if (qGuiApp == nullptr) {
        return nullptr;
    }
    auto *application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    return application == nullptr ? nullptr : application->seat();
}

void Registry::roundtrip()
{
    if (m_display != nullptr) {
        wl_display_roundtrip(m_display);
    }
}

void Registry::onGlobal(quint32 name, const char *interface, quint32 version)
{
    m_globals.insert(QByteArray(interface), Global{.name = name, .version = version});
    Q_EMIT interfacesChanged();
}

void Registry::onGlobalRemove(quint32 name)
{
    for (auto it = m_globals.begin(); it != m_globals.end(); ++it) {
        if (it->name == name) {
            m_globals.erase(it);
            Q_EMIT interfacesChanged();
            return;
        }
    }
}

} // namespace maru::wl
