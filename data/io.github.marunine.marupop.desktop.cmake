# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
[Desktop Entry]
Name=MaruPop
GenericName=Japanese Popup Dictionary
Comment=Look up Japanese words under the mouse pointer
# KWin maps the caller's /proc/pid/exe to this entry's Exec to grant the interfaces below;
# the path must be absolute and match the installed binary exactly. CMake substitutes the directory the configured CMAKE_INSTALL_PREFIX
# installs the binary into, so a prefix other than /usr keeps the match.
Exec=${KDE_INSTALL_FULL_BINDIR}/marupop
Icon=${MARUPOP_APPLICATION_ID}
Type=Application
Terminal=false
Categories=Qt;KDE;Utility;Education;Languages;
Keywords=japanese;dictionary;ocr;popup;yomitan;jmdict;
StartupNotify=false
NoDisplay=false
# ScreenShot2 captures the region around the pointer for OCR.
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
# The right-click menu on the launcher entry and the task manager button. The verb reaches the
# resident process through KDBusService(Unique) rather than starting a second one.
#
# Deliberately without X-KDE-Shortcuts. kglobalacceld's detectAppsWithShortcuts() builds a
# KServiceActionComponent for any entry or action carrying that key, and this application
# already registers its shortcuts in-process under the component name that is this entry's base
# name. The two would appear as separate components in System Settings offering the same
# actions under competing keys, and a press on the service one would take the KIO launch path
# into a process that is already running.
Actions=ToggleScanning;

[Desktop Action ToggleScanning]
Name=Toggle Scanning
Exec=${KDE_INSTALL_FULL_BINDIR}/marupop --toggle-scanning
