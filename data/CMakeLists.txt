# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Each of the three templates here is built in two steps: configure_file() substitutes the
# install prefix and the application id, then msgfmt writes the translations of its
# user-visible fields back into it. The install rules name the translated file, and
# marupop_translate_data_file() below falls back to the configured one where msgfmt is absent.
#
# The two steps are separate because the strings in these files reach no .mo catalogue. A
# launcher reads Name= out of the desktop entry, KNotification reads Name= out of the notifyrc,
# and Discover reads <name> out of the metainfo, each in the installed file itself, so a
# translation has to be a Name[<lang>]= key or an xml:lang attribute in that file.

# msgfmt reads a flat directory of <lang>.po named by a LINGUAS file; ki18n_install() reads
# po/<lang>/marupop.po. This writes the flat form into the build tree from the nested one.
# CONFIGURE_DEPENDS, because a translation arrives as a new file in a tree that is already
# configured. Without it a `cmake --build` after `po/<lang>/marupop.po` is added re-runs nothing,
# LINGUAS stays empty, and the three merges install their templates in English with no
# diagnostic. The re-configure it forces also re-runs the glob inside ki18n_install(po), which
# would otherwise miss the same file.
file(GLOB po_language_paths CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/po/*/marupop.po)
set(flat_po_dir ${CMAKE_CURRENT_BINARY_DIR}/po)
file(MAKE_DIRECTORY ${flat_po_dir})
set(po_languages "")
foreach(po_path IN LISTS po_language_paths)
    get_filename_component(po_language ${po_path} DIRECTORY)
    get_filename_component(po_language ${po_language} NAME)
    configure_file(${po_path} ${flat_po_dir}/${po_language}.po COPYONLY)
    list(APPEND po_languages ${po_language})
endforeach()
string(JOIN " " linguas ${po_languages})
file(WRITE ${flat_po_dir}/LINGUAS "${linguas}\n")

# Absent, the three files are installed as configure_file() left them, in the source language.
# Reported rather than silent, because that is a package whose launcher entry and notification
# rows are English whatever catalogues it ships.
find_program(MSGFMT_EXECUTABLE msgfmt)
if(NOT MSGFMT_EXECUTABLE AND po_languages)
    message(WARNING "msgfmt was not found; the desktop entry, the notifyrc and the metainfo are "
                    "installed untranslated")
endif()

# marupop_translate_data_file(<configured> <translated> <msgfmt-mode-argument>...)
#
# The mode arguments are msgfmt's own: --desktop for Desktop Entry syntax, `--xml -L appdata` for
# the AppStream metainfo. An empty LINGUAS makes msgfmt copy the template unchanged, so the
# no-translation case produces the same bytes the install rule used to name.
function(marupop_translate_data_file configured translated)
    if(NOT MSGFMT_EXECUTABLE)
        configure_file(${configured} ${translated} COPYONLY)
        return()
    endif()
    add_custom_command(
        OUTPUT ${translated}
        COMMAND ${MSGFMT_EXECUTABLE} ${ARGN} -d ${flat_po_dir} --template=${configured} -o ${translated}
        DEPENDS ${configured} ${po_language_paths} ${flat_po_dir}/LINGUAS
        COMMENT "Merging translations into ${translated}"
        VERBATIM)
endfunction()

# The desktop entry is generated rather than shipped literally: KWin grants the restricted
# Wayland globals and D-Bus interfaces by matching a caller's /proc/pid/exe against the Exec of
# an installed entry, so an Exec that names a directory the binary is not in costs the
# application its capture permissions, with no error a user could act on. Spectacle generates
# its entry for the same reason (spectacle/desktop/org.kde.spectacle.desktop.cmake).
# The templates write ${KDE_INSTALL_FULL_BINDIR} and ${MARUPOP_APPLICATION_ID}, so the calls
# leave @ONLY off.
# Substitution happens at configure time, which fixes the Exec path to the configured
# CMAKE_INSTALL_PREFIX: `cmake --install --prefix <other>` moves the binary and leaves this
# entry naming the configured directory. DESTDIR is unaffected, being a staging root the
# installed package is unpacked from.
set(configured_desktop ${CMAKE_CURRENT_BINARY_DIR}/${MARUPOP_APPLICATION_ID}.desktop.configured)
set(translated_desktop ${CMAKE_CURRENT_BINARY_DIR}/${MARUPOP_APPLICATION_ID}.desktop)
configure_file(${MARUPOP_APPLICATION_ID}.desktop.cmake ${configured_desktop})
marupop_translate_data_file(${configured_desktop} ${translated_desktop} --desktop)
install(FILES ${translated_desktop} DESTINATION ${KDE_INSTALL_APPDIR})

# The AppStream component. It is what puts the application in Discover, GNOME Software and a
# Flathub listing, so without it the only route to MaruPop is knowing the repository URL.
set(configured_metainfo
    ${CMAKE_CURRENT_BINARY_DIR}/${MARUPOP_APPLICATION_ID}.metainfo.xml.configured)
set(translated_metainfo ${CMAKE_CURRENT_BINARY_DIR}/${MARUPOP_APPLICATION_ID}.metainfo.xml)
configure_file(${MARUPOP_APPLICATION_ID}.metainfo.xml.cmake ${configured_metainfo})
marupop_translate_data_file(${configured_metainfo} ${translated_metainfo} --xml -L appdata)
install(FILES ${translated_metainfo} DESTINATION ${KDE_INSTALL_METAINFODIR})

# The file name is the KAboutData component name, which KNotification looks the events up by,
# and stays marupop: renaming it would take marupoprc and ~/.config/marupop with it. Only the
# DesktopEntry and IconName inside it carry the application id.
set(configured_notifyrc ${CMAKE_CURRENT_BINARY_DIR}/marupop.notifyrc.configured)
set(translated_notifyrc ${CMAKE_CURRENT_BINARY_DIR}/marupop.notifyrc)
configure_file(marupop.notifyrc.cmake ${configured_notifyrc})
marupop_translate_data_file(${configured_notifyrc} ${translated_notifyrc} --desktop)
install(FILES ${translated_notifyrc} DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

# The KWin script package, installed whole. KPackage::PackageLoader searches
# QStandardPaths::GenericDataLocation for kwin/scripts/<pluginId>, and ${KDE_INSTALL_DATADIR}
# is one of those directories, so a system install registers the relay with KWin without the
# application writing anything: cursor::KWinScriptRelay then only has to enable it in kwinrc.
# The package is installed under its plugin id, which the loader builds the script path from,
# and the same three files are compiled into the binary through data/marupop.qrc for a
# build-tree run.
install(DIRECTORY kwin-script/marupopcursor/
        DESTINATION ${KDE_INSTALL_DATADIR}/kwin/scripts/marupopcursor)

# The custom commands above produce nothing unless a target asks for their outputs, and the
# install rules alone do not. ALL puts the three merges in the default build, which is also what
# gives the two validation tests below a file to read.
add_custom_target(marupop_data ALL
                  DEPENDS ${translated_desktop} ${translated_metainfo} ${translated_notifyrc})

# A misspelled variable in the template expands to nothing, which leaves a relative Exec that
# no validator rejects. On the [Desktop Entry] line that costs the application its capture
# permissions, since KWin resolves a caller's /proc/pid/exe against KService::exec(); on a
# [Desktop Action] line it leaves a launcher menu entry that runs the wrong thing or nothing.
# Both are checked. Read back at configure time, where the answer is immediate. string(FIND)
# rather than a regex match, so a prefix containing a regex metacharacter is compared literally.
# The configured file rather than the translated one, which exists only after a build.
set(expected_exec "Exec=${KDE_INSTALL_FULL_BINDIR}/marupop")
file(STRINGS ${configured_desktop} generated_execs REGEX "^Exec=")
# An empty list would pass the loop below without checking anything, which is the case a
# renamed or lowercased Exec key produces.
if(NOT generated_execs)
    message(FATAL_ERROR "the generated desktop entry has no Exec= line")
endif()
foreach(line IN LISTS generated_execs)
    string(FIND "${line}" "${expected_exec}" position)
    if(NOT position EQUAL 0)
        message(FATAL_ERROR "the generated desktop entry has '${line}', "
                            "expected a line starting with '${expected_exec}'")
    endif()
endforeach()

# ECM's appstreamtest validates whatever install_manifest.txt names, so it covers only an
# install that has actually been made into the configured prefix, and reports "Could not find"
# and passes otherwise. These two run against the translated files, which are the ones installed,
# so a broken metainfo, a broken desktop entry or a merge that damaged either fails a plain
# `ctest`.
if(BUILD_TESTING)
    # Reported when absent rather than skipped in silence, which would leave `ctest` green over
    # a file nothing had looked at.
    find_program(APPSTREAMCLI_EXECUTABLE appstreamcli)
    if(APPSTREAMCLI_EXECUTABLE)
        add_test(NAME metainfo_validate
                 COMMAND ${APPSTREAMCLI_EXECUTABLE} validate --no-net --pedantic ${translated_metainfo})
    else()
        message(WARNING "appstreamcli was not found; the metainfo file is left unvalidated")
    endif()
    find_program(DESKTOP_FILE_VALIDATE_EXECUTABLE desktop-file-validate)
    if(DESKTOP_FILE_VALIDATE_EXECUTABLE)
        add_test(NAME desktop_file_validate
                 COMMAND ${DESKTOP_FILE_VALIDATE_EXECUTABLE} ${translated_desktop})
    else()
        message(WARNING "desktop-file-validate was not found; the desktop entry is left unvalidated")
    endif()
endif()
