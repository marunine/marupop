// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The interface every dictionary format importer implements, and the source reader the XML
// importers share.
//
// An importer runs on the caller's thread. DictionaryImportJob calls it on a worker QThread and
// forwards the progress callback to the GUI thread; a test calls it directly.
#pragma once

#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

class QFile;
class QIODevice;
class QXmlStreamReader;

namespace maru::dict
{

class StoreWriter;

// What one import produced. recordCount and keyCount are the values the store was left with, and
// maxKeyLength the longest key in UTF-16 code units.
struct ImportResult
{
    bool ok = false;
    bool cancelled = false;
    QString errorString;
    qint64 recordCount = 0;
    qint64 keyCount = 0;
    int maxKeyLength = 0;
};

// Called with a percentage from 0 to 100 and a message naming the phase. An importer calls it at
// most once per 4096 parsed entries, so a caller can post the value across a thread boundary
// without throttling it again.
using ProgressFn = std::function<void(int percent, const QString &message)>;

class Importer
{
public:
    Importer() = default;
    virtual ~Importer();

    Importer(const Importer &) = delete;
    Importer &operator=(const Importer &) = delete;
    Importer(Importer &&) = delete;
    Importer &operator=(Importer &&) = delete;

    // The format name, for a log line and for an error message.
    [[nodiscard]] virtual QString name() const = 0;

    // Reads source into writer. source is a file for the XML and TSV formats and a directory or a
    // .zip for the Yomitan formats. The importer calls writer.finish() itself on success and
    // writer.abort() on failure, so the caller only inspects the result.
    //
    // cancel is polled every entry; a cancelled import leaves ok false and cancelled true.
    [[nodiscard]] virtual ImportResult
    import(const QString &source, StoreWriter &writer, const ProgressFn &progress, std::atomic_bool &cancel) = 0;
};

// A file opened for reading, gunzipped when its first two bytes are 0x1f 0x8b. The compressed
// length drives the progress percentage, because the uncompressed length of a gzip member is not
// known before the last byte is read.
class SourceReader
{
public:
    SourceReader();
    ~SourceReader();

    SourceReader(const SourceReader &) = delete;
    SourceReader &operator=(const SourceReader &) = delete;

    [[nodiscard]] bool open(const QString &path);

    [[nodiscard]] QIODevice *device() const
    {
        return m_device;
    }

    // 0 to 100, from the position in the underlying file.
    [[nodiscard]] int percent() const;

    [[nodiscard]] QString errorString() const
    {
        return m_errorString;
    }

private:
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QIODevice> m_decompressor;
    QIODevice *m_device = nullptr;
    qint64 m_size = 0;
    QString m_errorString;
};

// Read DTD entity declarations and recover their short names from expanded descriptions.
// QXmlStreamReader expands declared entities before returning text, unlike the .NET
// XmlTextReader mode used by JL. Store both directions to retain the original tag names;
// identical duplicate declarations are harmless. See entity parsing tests for aliases.
class DtdEntityMap
{
public:
    // Reads the declarations from a reader positioned on its DTD token.
    void readDeclarations(const QXmlStreamReader &reader);

    // The short name whose declaration expands to text, or text itself when no declaration
    // matches.
    [[nodiscard]] QString shortName(const QString &text) const;

    [[nodiscard]] const QMap<QString, QString> &entities() const
    {
        return m_entities;
    }

    // The entities as a JSON object of name to description, for the store's meta table.
    [[nodiscard]] QString toJson() const;

private:
    QMap<QString, QString> m_entities;
    QHash<QString, QString> m_shortNames;
};

} // namespace maru::dict

// DictionaryImportJob carries the result back from its worker thread through a queued signal.
Q_DECLARE_METATYPE(maru::dict::ImportResult)
