# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The application shell: the tray item, the settings window, the lookup window, the three global
# shortcuts, the notification channels and the first-run model prompt. Everything here lives
# directly in the maru namespace.
target_sources(marupop_lib PRIVATE
    application.cpp
    application.h
    hotkeyregistry.cpp
    hotkeyregistry.h
    lookupwindow.cpp
    lookupwindow.h
    modeldownloaddialog.cpp
    modeldownloaddialog.h
    modelstatuswidget.cpp
    modelstatuswidget.h
    notifier.cpp
    notifier.h
    pathrequester.cpp
    pathrequester.h
    settingsdialog.cpp
    settingsdialog.h
    shortcutbutton.cpp
    shortcutbutton.h
    shortcutregistry.cpp
    shortcutregistry.h
    trayicon.cpp
    trayicon.h
    wlrshortcuts.cpp
    wlrshortcuts.h)

# KF6::KIOCore supplies KIO::getJobTracker(), which the model download and the dictionary
# update chain register their jobs with so Plasma's transfer applet shows them; KF6::KIOGui
# supplies KIO::highlightInFileManager() for the model folder button; KF6::JobWidgets is what
# puts a widget-based tracker behind getJobTracker() where no job service is running.
# KF6::GlobalAccel carries the three hotkeys, KF6::Notifications the notifyrc events,
# KF6::WidgetsAddons KMessageWidget and KColorButton, and KF6::StatusNotifierItem the tray item.
# All of them are already on marupop_lib's PUBLIC link line from src/CMakeLists.txt; they are
# named here as well so the dependency is recorded where the source that needs it is.
target_link_libraries(marupop_lib PRIVATE
    KF6::GlobalAccel
    KF6::JobWidgets
    KF6::KIOCore
    KF6::KIOGui
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::WidgetsAddons)
