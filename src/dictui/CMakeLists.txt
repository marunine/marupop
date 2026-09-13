# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The Manage Dictionaries dialog and its sub-dialogs. Every KF6 component this module uses
# (KIOWidgets, KIOGui, JobWidgets, WidgetsAddons, XmlGui, ColorScheme, CoreAddons, I18n) is
# already PUBLIC on marupop_lib in src/CMakeLists.txt, so there is no target_link_libraries()
# line here.
target_sources(marupop_lib PRIVATE
    addcustomentrydialog.cpp
    addcustomentrydialog.h
    adddictionarydialog.cpp
    adddictionarydialog.h
    configuredictionarydialog.cpp
    configuredictionarydialog.h
    dictionarymanagerdialog.cpp
    dictionarymanagerdialog.h
    dictionarymodel.cpp
    dictionarymodel.h)
