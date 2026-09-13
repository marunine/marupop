// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Packs a Yomitan fixture directory into the .zip shapes a published dictionary takes, so the
// archive paths of YomitanImporter and of the Add Dictionary dialog are covered without a binary
// checked into tests/data.
//
// Shared by dict_yomitanimporter_test and dictui_adddictionarydialog_test, which assert over the
// same two shapes from opposite ends: the importer over detectTypes() and import(), the dialog
// over the message widget and the entry it creates.
#pragma once

#include <QString>
#include <QStringList>

namespace maru::test
{

// Packs the files directly inside sourceDirectory into archivePath, at its root. Returns false
// when the archive cannot be written. Subdirectories of
// sourceDirectory are left out, which drops the img/ of tests/data/dict/yomitan_v3.
[[nodiscard]] bool packYomitanFixture(const QString &sourceDirectory, const QString &archivePath);

// Packs sourceDirectory into archivePath recursively, inside one directory named wrapper, which
// is the second shape a published dictionary takes. Returns false when the archive cannot be
// written.
//
// Each name in siblings becomes a second top-level directory holding one file named
// ._index.json, which is what the macOS Finder writes into __MACOSX. A sibling is what separates
// "the wrapper is the only top-level directory" from "the wrapper is the only top-level directory
// holding index.json".
[[nodiscard]] bool packWrappedYomitanFixture(const QString &sourceDirectory,
                                             const QString &archivePath,
                                             const QString &wrapper,
                                             const QStringList &siblings = {});

} // namespace maru::test
