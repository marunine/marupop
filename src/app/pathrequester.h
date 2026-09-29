// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QLineEdit;
class QToolButton;

namespace maru
{

// A line edit with a browse button whose value is a plain path string. Copied from
// marusnap/src/app/pathrequester.{h,cpp} (LGPL-3.0, same author).
//
// KUrlRequester cannot be used for the two configured directory entries: its USER property is
// a QUrl, so KConfigDialogManager writes "file:///home/…" back into a QString entry, and the
// leading tilde of the ScreenAiResourcesDir default is rewritten against the working
// directory. This exposes `path` as its USER property instead, which round-trips exactly what
// the user typed, and paths::expandPath() resolves the tilde where the value is read.
class PathRequester : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged USER true)

public:
    enum class Kind
    {
        Directory,
        File,
        // The browse button opens a menu with a file choice and a folder choice. Each
        // QFileDialog mode selects either files or one directory.
        FileOrDirectory,
    };

    explicit PathRequester(Kind kind = Kind::Directory, QWidget *parent = nullptr);

    [[nodiscard]] QString path() const;
    void setPath(const QString &path);
    void setPlaceholderText(const QString &text);

    void setKind(Kind kind);
    // Name filters of the file dialog in QFileDialog::setNameFilters() format, for example
    // "Text file (*.txt)". An empty list shows all files.
    void setNameFilters(const QStringList &filters);

Q_SIGNALS:
    void pathChanged(const QString &path);

private:
    void browse();
    void browseFile();
    void browseDirectory();
    void updateBrowseButton();

    QLineEdit *m_edit = nullptr;
    QToolButton *m_browse = nullptr;
    Kind m_kind = Kind::Directory;
    QStringList m_nameFilters;
};

} // namespace maru
