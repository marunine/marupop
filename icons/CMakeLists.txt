# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Monochrome Breeze-style app icon. Every source carries the current-color-scheme stylesheet,
# which KIconLoader rewrites at load time, so one file serves the light and dark themes -- and
# the tray, which recolours its icon to whatever the panel needs.
#
# ecm_install_icons() sorts <size>-<group>-<name>.<ext> into the freedesktop.org theme layout
# and refreshes the icon cache after the install, which a hand-written foreach() over
# hicolor/<size>/apps would do neither of.
#
# The first name is the application id, which is what an AppStream front end and Flathub look
# the icon up under, and what the desktop entry's Icon= names. The -paused name is the second
# icon TrayIcon::setScanning() swaps to, installed in the same five sizes so the swap resolves
# in the hicolor theme rather than falling back to the missing-icon glyph.
ecm_install_icons(
    ICONS
        16-apps-${MARUPOP_APPLICATION_ID}.svg
        22-apps-${MARUPOP_APPLICATION_ID}.svg
        32-apps-${MARUPOP_APPLICATION_ID}.svg
        48-apps-${MARUPOP_APPLICATION_ID}.svg
        sc-apps-${MARUPOP_APPLICATION_ID}.svg
        16-apps-${MARUPOP_APPLICATION_ID}-paused.svg
        22-apps-${MARUPOP_APPLICATION_ID}-paused.svg
        32-apps-${MARUPOP_APPLICATION_ID}-paused.svg
        48-apps-${MARUPOP_APPLICATION_ID}-paused.svg
        sc-apps-${MARUPOP_APPLICATION_ID}-paused.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)
