#!/bin/sh
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
#
# Rewrites po/marupop.pot from the i18n calls in src/ and the user-visible fields of the three
# templates in data/. `extract-messages.sh --check` extracts to a temporary file and exits 1 when
# the committed template differs, which is what the CI translations job runs.
#
# This replaces KDE's Messages.sh convention, which exists for KDE's scripty robots: they run a
# repository's Messages.sh on invent.kde.org, commit the template into KDE's l10n repository and
# push the finished catalogues back. Nothing services a repository hosted anywhere else, so the
# template is produced here and committed here.
#
# Three passes, because the three input formats have three extraction rules:
#
# 1. C++ sources, with KDE's keyword list. KLocalizedString supplies four call families
#    (i18n, ki18n, xi18n, kxi18n) that gettext knows nothing about, and `--kde` is what makes
#    xgettext read the i18nc() context argument as a msgctxt.
# 2. The desktop entry and the notifyrc, both in Desktop Entry syntax. Their strings never pass
#    through i18n(): a launcher reads Name= out of the entry and KNotification reads Name= out of
#    the notifyrc, so data/CMakeLists.txt merges the translations back as Name[<lang>]= keys.
# 3. The AppStream metainfo, through gettext's `appdata` ITS rules, which pick <name>, <summary>
#    and the <description> block. Discover and GNOME Software read xml:lang attributes out of the
#    installed file, which data/CMakeLists.txt writes back the same way.
#
# The .cmake templates are read rather than the configured files, so an extraction needs no build
# and the file references name a tracked path. Neither substituted variable is translatable.
#
# src/core/marupopsettings.kcfg is not extracted. Its 59 <label> elements are developer
# documentation: marupopsettings.kcfgc leaves SetUserTexts at its false default, so
# kconfig_compiler emits each label as a doc comment on the generated accessor and no label
# reaches a widget. The settings dialog builds its widgets by hand and passes its own i18nc()
# strings. Running extractrc over the file would hand a translator 59 strings no user can see.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
template=$root/po/marupop.pot
version=$(sed -n 's/^project(marupop VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
application_id=$(sed -n 's/^set(MARUPOP_APPLICATION_ID "\([^"]*\)").*/\1/p' "$root/CMakeLists.txt")

extract() {
    output=$1
    staging=$output.xgettext
    cd "$root"
    # --cached --others --exclude-standard, not --cached alone: a source file that has been
    # written but not yet staged is part of this extraction. Reading the index alone omits the
    # i18n calls of every unstaged file from the template.
    #
    # LC_ALL=C because the file order sets the entry order: a UTF-8 locale collates without the
    # path separators, sorting src/dict/updatecheckjob.cpp after src/dictui/ where CI does not.
    #
    # shellcheck disable=SC2046 # the file list has to word-split into separate arguments
    xgettext \
        --from-code=UTF-8 \
        -C \
        --kde \
        -ci18n \
        -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
        -kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3 \
        -kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3 \
        -kkxi18n:1 -kkxi18nc:1c,2 -kkxi18np:1,2 -kkxi18ncp:1c,2,3 \
        -kN_:1 \
        --package-name=marupop \
        --package-version="$version" \
        --copyright-holder="marunine" \
        --msgid-bugs-address=https://github.com/marunine/marupop/issues \
        -o "$staging" \
        $(git ls-files --cached --others --exclude-standard 'src/*.cpp' 'src/*.h' | LC_ALL=C sort)

    # --join-existing appends to the file the pass above wrote, so all three land in one template.
    xgettext --from-code=UTF-8 --join-existing --language=Desktop -o "$staging" \
        "data/$application_id.desktop.cmake" \
        data/marupop.notifyrc.cmake
    xgettext --from-code=UTF-8 --join-existing --language=appdata -o "$staging" \
        "data/$application_id.metainfo.xml.cmake"

    # The SPDX header every other tracked file carries. xgettext writes none, and a leading
    # comment block before the header entry is what the PO format accepts.
    {
        echo "# SPDX-FileCopyrightText: 2026 marunine"
        echo "# SPDX-License-Identifier: LGPL-3.0-only"
        cat "$staging"
    } > "$output"
    rm -f "$staging"
}

if [ "${1-}" = "--check" ]; then
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    extract "$work/fresh.pot"
    # POT-Creation-Date carries the time of the run, so it differs on every extraction and
    # says nothing about whether a string moved.
    grep -v '^"POT-Creation-Date:' "$template" > "$work/committed"
    grep -v '^"POT-Creation-Date:' "$work/fresh.pot" > "$work/extracted"
    if diff -u --label "po/marupop.pot (committed)" --label "po/marupop.pot (extracted)" \
        "$work/committed" "$work/extracted"; then
        exit 0
    fi
    echo "po/marupop.pot is out of date; \`po/extract-messages.sh\` rewrites it." >&2
    exit 1
fi

extract "$template"
printf '%s\n' "$template"
