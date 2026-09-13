// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "yomitanfixture.h"

#include <QDir>

#include <KZip>

namespace maru::test
{

bool packYomitanFixture(const QString &sourceDirectory, const QString &archivePath)
{
    KZip zip(archivePath);
    if (!zip.open(QIODevice::WriteOnly))
        return false;
    const QDir directory(sourceDirectory);
    const QStringList files = directory.entryList(QDir::Files);
    for (const QString &name : files) {
        if (!zip.addLocalFile(directory.filePath(name), name))
            return false;
    }
    return zip.close();
}

bool packWrappedYomitanFixture(const QString &sourceDirectory,
                               const QString &archivePath,
                               const QString &wrapper,
                               const QStringList &siblings)
{
    KZip zip(archivePath);
    if (!zip.open(QIODevice::WriteOnly))
        return false;
    if (!zip.addLocalDirectory(sourceDirectory, wrapper))
        return false;
    for (const QString &sibling : siblings) {
        if (!zip.writeFile(sibling + QLatin1String("/._index.json"), QByteArrayLiteral("\x00\x05\x16\x07")))
            return false;
    }
    return zip.close();
}

} // namespace maru::test
