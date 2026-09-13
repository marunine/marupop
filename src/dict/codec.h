// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The payload encoder the record table stores blobs from. One CBOR array per record, written
// positionally rather than as a map, so a field costs its value and one type byte and no key
// string. The first two elements are the codec version and the RecordKind, which is what lets a
// store hold more than one record kind and lets decodeRecord() reject a payload written by an
// older codec.
//
// The encoder is deliberately the only component that knows the byte layout: the storage backend
// behind it can be replaced without touching an importer or the lookup engine.
#pragma once

#include "dict/records.h"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace maru::dict
{

// Bumped whenever a record's field order or field set changes. StoreWriter writes it into the
// meta table as payload_codec and Store::open() rejects a store carrying a different value, which
// is what turns a codec change into a re-import rather than a mis-decode.
inline constexpr int codecVersion = 1;

// The CBOR payload for record. Fails only for a record holding a valueless variant, which cannot
// be constructed, so the result is always non-empty.
[[nodiscard]] QByteArray encodeRecord(const Record &record);

// The record payload holds, with id left at 0 and type set to type. Returns nothing when the
// payload is truncated, carries a different codec version, or names a RecordKind this build does
// not define.
[[nodiscard]] std::optional<Record> decodeRecord(DictType type, QByteArrayView payload);

// The RecordKind a payload declares, without decoding the rest of it. Returns nothing for a
// payload this build cannot read.
[[nodiscard]] std::optional<RecordKind> payloadKind(QByteArrayView payload);

} // namespace maru::dict
