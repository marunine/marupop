// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <QString>
#include <QWidget>

class QLineEdit;

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
    };

    explicit PathRequester(Kind kind = Kind::Directory, QWidget *parent = nullptr);

    [[nodiscard]] QString path() const;
    void setPath(const QString &path);
    void setPlaceholderText(const QString &text);

Q_SIGNALS:
    void pathChanged(const QString &path);

private:
    void browse();

    QLineEdit *m_edit = nullptr;
    Kind m_kind = Kind::Directory;
};

} // namespace maru
