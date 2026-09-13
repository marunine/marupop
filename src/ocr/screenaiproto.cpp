// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/screenaiproto.h"

#include "core/logging.h"

#include <QLoggingCategory>

#include <cstring>
#include <utility>

namespace maru::ocr
{

namespace
{

// Cursor over one length-delimited scope; readers return false on truncated input.
struct WireReader
{
    const quint8 *data = nullptr;
    qsizetype size = 0;
    qsizetype pos = 0;

    [[nodiscard]] bool atEnd() const
    {
        return pos >= size;
    }

    bool readVarint(quint64 &value)
    {
        value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (pos >= size) {
                return false;
            }
            const quint8 byte = data[pos++];
            value |= static_cast<quint64>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false; // more than 10 bytes: malformed
    }

    bool readTag(int &fieldNumber, int &wireType)
    {
        quint64 tag = 0;
        if (!readVarint(tag)) {
            return false;
        }
        fieldNumber = static_cast<int>(tag >> 3);
        wireType = static_cast<int>(tag & 0x7);
        return fieldNumber != 0;
    }

    bool readLengthDelimited(WireReader &scope)
    {
        quint64 length = 0;
        if (!readVarint(length) || std::cmp_greater(length, size - pos)) {
            return false;
        }
        scope = WireReader{.data = data + pos, .size = static_cast<qsizetype>(length), .pos = 0};
        pos += static_cast<qsizetype>(length);
        return true;
    }

    bool readFixed32(quint32 &value)
    {
        if (size - pos < 4) {
            return false;
        }
        std::memcpy(&value, data + pos, 4); // protobuf fixed32 is little-endian, as is x86
        pos += 4;
        return true;
    }

    bool skip(int wireType)
    {
        switch (wireType) {
        case 0: { // varint
            quint64 ignored = 0;
            return readVarint(ignored);
        }
        case 1: // fixed64
            if (size - pos < 8) {
                return false;
            }
            pos += 8;
            return true;
        case 2: { // length-delimited
            WireReader ignored;
            return readLengthDelimited(ignored);
        }
        case 5: { // fixed32
            quint32 ignored = 0;
            return readFixed32(ignored);
        }
        default: // groups and unknown wire types are unsupported
            return false;
        }
    }

    [[nodiscard]] QString utf8() const
    {
        return QString::fromUtf8(reinterpret_cast<const char *>(data), size);
    }
};

// Varints for int32 fields carry the two's-complement value sign-extended to 64 bits.
int toInt32(quint64 value)
{
    return static_cast<int>(static_cast<qint64>(value));
}

float toFloat(quint32 bits)
{
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

struct SymbolBox
{
    QRect box;
    QString text;
    float confidence = 0.0F;
};

struct WordBox
{
    QList<SymbolBox> symbols;
    QRect box;
    QString text;
    QString language;
    int direction = 0;
    float confidence = 0.0F;
};

bool parseRect(WireReader reader, QRect &rect)
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    while (!reader.atEnd()) {
        int field = 0;
        int wireType = 0;
        if (!reader.readTag(field, wireType)) {
            return false;
        }
        quint64 value = 0;
        if (wireType == 0 && field >= 1 && field <= 4) {
            if (!reader.readVarint(value)) {
                return false;
            }
            switch (field) {
            case 1:
                x = toInt32(value);
                break;
            case 2:
                y = toInt32(value);
                break;
            case 3:
                width = toInt32(value);
                break;
            case 4:
                height = toInt32(value);
                break;
            default:
                break;
            }
        } else if (!reader.skip(wireType)) {
            return false;
        }
    }
    rect = QRect(x, y, width, height);
    return true;
}

bool parseSymbolBox(WireReader reader, SymbolBox &symbol)
{
    while (!reader.atEnd()) {
        int field = 0;
        int wireType = 0;
        if (!reader.readTag(field, wireType)) {
            return false;
        }
        if (field == 1 && wireType == 2) { // bounding_box
            WireReader scope;
            if (!reader.readLengthDelimited(scope) || !parseRect(scope, symbol.box)) {
                return false;
            }
        } else if (field == 2 && wireType == 2) { // utf8_string
            WireReader scope;
            if (!reader.readLengthDelimited(scope)) {
                return false;
            }
            symbol.text = scope.utf8();
        } else if (field == 3 && wireType == 5) { // confidence
            quint32 bits = 0;
            if (!reader.readFixed32(bits)) {
                return false;
            }
            symbol.confidence = toFloat(bits);
        } else if (!reader.skip(wireType)) {
            return false;
        }
    }
    return true;
}

bool parseWordBox(WireReader reader, WordBox &word)
{
    while (!reader.atEnd()) {
        int field = 0;
        int wireType = 0;
        if (!reader.readTag(field, wireType)) {
            return false;
        }
        if (field == 1 && wireType == 2) { // repeated SymbolBox symbols
            WireReader scope;
            SymbolBox symbol;
            if (!reader.readLengthDelimited(scope) || !parseSymbolBox(scope, symbol)) {
                return false;
            }
            word.symbols.append(symbol);
        } else if (field == 2 && wireType == 2) { // bounding_box
            WireReader scope;
            if (!reader.readLengthDelimited(scope) || !parseRect(scope, word.box)) {
                return false;
            }
        } else if (field == 3 && wireType == 2) { // utf8_string
            WireReader scope;
            if (!reader.readLengthDelimited(scope)) {
                return false;
            }
            word.text = scope.utf8();
        } else if (field == 5 && wireType == 2) { // language
            WireReader scope;
            if (!reader.readLengthDelimited(scope)) {
                return false;
            }
            word.language = scope.utf8();
        } else if (field == 12 && wireType == 0) { // direction
            quint64 value = 0;
            if (!reader.readVarint(value)) {
                return false;
            }
            word.direction = toInt32(value);
        } else if (field == 15 && wireType == 5) { // confidence
            quint32 bits = 0;
            if (!reader.readFixed32(bits)) {
                return false;
            }
            word.confidence = toFloat(bits);
        } else if (!reader.skip(wireType)) {
            return false;
        }
    }
    return true;
}

// One box divided along the reading axis into one part per code point of text. Screen AI
// reports one SymbolBox per glyph for Japanese, so the division runs for a symbol carrying a
// grapheme cluster of several code points, and for a word that reports no symbols at all.
void appendSplitCharBoxes(QList<CharBox> &chars, const QString &text, QRect box, bool vertical, float confidence)
{
    const QList<uint> codePoints = text.toUcs4();
    if (codePoints.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(codePoints.size());
    for (int index = 0; index < count; ++index) {
        if (codePoints.at(index) > 0xFFFF) {
            // ocr::TextLine indexes chars by UTF-16 code unit; a supplementary-plane character
            // would break the text.size() == chars.size() invariant.
            qCWarning(logScreenAiProto) << "dropping a non-BMP code point" << Qt::hex << codePoints.at(index);
            continue;
        }
        QRect part = box;
        if (count > 1) {
            if (vertical) {
                const int top = box.y() + ((box.height() * index) / count);
                const int bottom = box.y() + ((box.height() * (index + 1)) / count);
                part = QRect{box.x(), top, box.width(), bottom - top};
            } else {
                const int left = box.x() + ((box.width() * index) / count);
                const int right = box.x() + ((box.width() * (index + 1)) / count);
                part = QRect{left, box.y(), right - left, box.height()};
            }
        }
        chars.append(
            CharBox{.codePoint = static_cast<char32_t>(codePoints.at(index)), .box = part, .confidence = confidence});
    }
}

bool parseLineBox(WireReader reader, TextLine &line)
{
    QList<WordBox> words;
    QRect boundingBox;
    QString lineText;
    int direction = 0;
    while (!reader.atEnd()) {
        int field = 0;
        int wireType = 0;
        if (!reader.readTag(field, wireType)) {
            return false;
        }
        if (field == 1 && wireType == 2) { // repeated WordBox words
            WireReader scope;
            WordBox word;
            if (!reader.readLengthDelimited(scope) || !parseWordBox(scope, word)) {
                return false;
            }
            words.append(word);
        } else if (field == 2 && wireType == 2) { // bounding_box
            WireReader scope;
            if (!reader.readLengthDelimited(scope) || !parseRect(scope, boundingBox)) {
                return false;
            }
        } else if (field == 3 && wireType == 2) { // utf8_string
            WireReader scope;
            if (!reader.readLengthDelimited(scope)) {
                return false;
            }
            lineText = scope.utf8();
        } else if (field == 4 && wireType == 2) { // language
            WireReader scope;
            if (!reader.readLengthDelimited(scope)) {
                return false;
            }
        } else if (field == 7 && wireType == 0) { // direction
            quint64 value = 0;
            if (!reader.readVarint(value)) {
                return false;
            }
            direction = toInt32(value);
        } else if (field == 10 && wireType == 5) { // confidence
            quint32 bits = 0;
            if (!reader.readFixed32(bits)) {
                return false;
            }
            line.confidence = toFloat(bits);
        } else if (!reader.skip(wireType)) {
            return false;
        }
    }

    // The line direction wins where it is set; a line that reports DIRECTION_UNSPECIFIED takes
    // the orientation of its first word, which is the granularity Screen AI fills for
    // vertical Japanese.
    line.vertical = direction == kDirectionTopToBottom;
    if (direction == 0) {
        for (const WordBox &word : std::as_const(words)) {
            if (word.direction != 0) {
                line.vertical = word.direction == kDirectionTopToBottom;
                break;
            }
        }
    }

    for (const WordBox &word : std::as_const(words)) {
        const bool wordVertical = word.direction != 0 ? word.direction == kDirectionTopToBottom : line.vertical;
        if (word.symbols.isEmpty()) {
            appendSplitCharBoxes(line.chars, word.text, word.box, wordVertical, word.confidence);
            continue;
        }
        for (const SymbolBox &symbol : word.symbols) {
            const float confidence = symbol.confidence > 0.0F ? symbol.confidence : word.confidence;
            appendSplitCharBoxes(line.chars, symbol.text, symbol.box, wordVertical, confidence);
        }
    }
    if (line.chars.isEmpty()) {
        // A line reporting neither words nor symbols still carries its own text; its box is
        // divided so the text.size() == chars.size() invariant holds for every line.
        appendSplitCharBoxes(line.chars, lineText, boundingBox, line.vertical, line.confidence);
    }

    QRect charBounds;
    for (const CharBox &character : std::as_const(line.chars)) {
        line.text.append(QChar(static_cast<char16_t>(character.codePoint)));
        charBounds = charBounds.united(character.box);
    }
    line.box = boundingBox.isValid() ? boundingBox : charBounds;
    return true;
}

} // namespace

std::optional<QList<TextLine>> parseVisualAnnotation(const QByteArray &data)
{
    WireReader reader{.data = reinterpret_cast<const quint8 *>(data.constData()), .size = data.size(), .pos = 0};
    QList<TextLine> lines;
    while (!reader.atEnd()) {
        int field = 0;
        int wireType = 0;
        if (!reader.readTag(field, wireType)) {
            qCWarning(logScreenAiProto) << "malformed tag at" << reader.pos;
            return std::nullopt;
        }
        if (field == 2 && wireType == 2) { // repeated LineBox lines
            WireReader scope;
            TextLine line;
            if (!reader.readLengthDelimited(scope) || !parseLineBox(scope, line)) {
                qCWarning(logScreenAiProto) << "malformed LineBox at" << reader.pos;
                return std::nullopt;
            }
            lines.append(line);
        } else if (!reader.skip(wireType)) {
            qCWarning(logScreenAiProto) << "malformed field" << field << "at" << reader.pos;
            return std::nullopt;
        }
    }
    return lines;
}

} // namespace maru::ocr
