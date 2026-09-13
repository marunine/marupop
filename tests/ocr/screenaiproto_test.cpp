// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/screenaiproto.h"

#include <QByteArray>

#include <cstring>
#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

QByteArray varint(quint64 value)
{
    QByteArray encoded;
    do {
        quint8 byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        encoded.append(static_cast<char>(byte));
    } while (value != 0);
    return encoded;
}

QByteArray tag(int field, int wireType)
{
    return varint((static_cast<quint64>(field) << 3) | static_cast<quint64>(wireType));
}

QByteArray varintField(int field, qint64 value)
{
    return tag(field, 0) + varint(static_cast<quint64>(value));
}

QByteArray bytesField(int field, const QByteArray &payload)
{
    return tag(field, 2) + varint(static_cast<quint64>(payload.size())) + payload;
}

QByteArray stringField(int field, const QString &value)
{
    return bytesField(field, value.toUtf8());
}

QByteArray floatField(int field, float value)
{
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    QByteArray encoded = tag(field, 5);
    for (int shift = 0; shift < 32; shift += 8) {
        encoded.append(static_cast<char>((bits >> shift) & 0xFF));
    }
    return encoded;
}

// chrome_screen_ai.Rect: x = 1, y = 2, width = 3, height = 4.
QByteArray rect(int field, int x, int y, int width, int height)
{
    return bytesField(field, varintField(1, x) + varintField(2, y) + varintField(3, width) + varintField(4, height));
}

QByteArray symbolBox(const QString &text, int x, int y, int width, int height, float confidence)
{
    return bytesField(1, rect(1, x, y, width, height) + stringField(2, text) + floatField(3, confidence));
}

} // namespace

TEST(ScreenAiProto, parsesSymbolBoxesIntoCharacterBoxes)
{
    const QByteArray word =
        bytesField(1,
                   symbolBox(QStringLiteral("日"), 10, 20, 16, 16, 0.95F) +
                       symbolBox(QStringLiteral("本"), 10, 40, 16, 16, 0.90F) + rect(2, 10, 20, 16, 36) +
                       stringField(3, QStringLiteral("日本")) + stringField(5, QStringLiteral("ja")) +
                       varintField(12, kDirectionTopToBottom) + floatField(15, 0.93F));
    const QByteArray line = bytesField(2,
                                       word + rect(2, 10, 20, 16, 36) + stringField(3, QStringLiteral("日本")) +
                                           stringField(4, QStringLiteral("ja")) +
                                           varintField(7, kDirectionTopToBottom) + floatField(10, 0.92F));

    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(line);
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 1);

    const TextLine &parsed = lines->constFirst();
    EXPECT_EQ(parsed.text, QStringLiteral("日本"));
    EXPECT_TRUE(parsed.vertical);
    EXPECT_EQ(parsed.box, QRect(10, 20, 16, 36));
    EXPECT_FLOAT_EQ(parsed.confidence, 0.92F);
    ASSERT_EQ(parsed.chars.size(), 2);
    EXPECT_EQ(parsed.chars.at(0).codePoint, U'日');
    EXPECT_EQ(parsed.chars.at(0).box, QRect(10, 20, 16, 16));
    EXPECT_FLOAT_EQ(parsed.chars.at(0).confidence, 0.95F);
    EXPECT_EQ(parsed.chars.at(1).codePoint, U'本');
    EXPECT_EQ(parsed.chars.at(1).box, QRect(10, 40, 16, 16));
}

TEST(ScreenAiProto, horizontalDirectionLeavesTheLineHorizontal)
{
    const QByteArray word =
        bytesField(1, symbolBox(QStringLiteral("あ"), 0, 0, 10, 10, 0.9F) + varintField(12, kDirectionLeftToRight));
    const QByteArray line = bytesField(2, word + varintField(7, kDirectionLeftToRight));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(line);
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 1);
    EXPECT_FALSE(lines->constFirst().vertical);
    // No bounding_box was sent, so the line box is the union of the character boxes.
    EXPECT_EQ(lines->constFirst().box, QRect(0, 0, 10, 10));
}

TEST(ScreenAiProto, aLineWithoutADirectionTakesTheDirectionOfItsWord)
{
    const QByteArray word =
        bytesField(1, symbolBox(QStringLiteral("あ"), 0, 0, 10, 10, 0.9F) + varintField(12, kDirectionTopToBottom));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(bytesField(2, word));
    ASSERT_TRUE(lines.has_value());
    EXPECT_TRUE(lines->constFirst().vertical);
}

TEST(ScreenAiProto, splitsASymbolCarryingSeveralCodePoints)
{
    const QByteArray word = bytesField(1, symbolBox(QStringLiteral("あい"), 0, 0, 20, 10, 0.9F));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(bytesField(2, word));
    ASSERT_TRUE(lines.has_value());
    const TextLine &parsed = lines->constFirst();
    ASSERT_EQ(parsed.chars.size(), 2);
    EXPECT_EQ(parsed.chars.at(0).box, QRect(0, 0, 10, 10));
    EXPECT_EQ(parsed.chars.at(1).box, QRect(10, 0, 10, 10));
    EXPECT_EQ(parsed.text, QStringLiteral("あい"));
}

TEST(ScreenAiProto, dividesAWordThatReportsNoSymbols)
{
    const QByteArray word = bytesField(1, rect(2, 0, 0, 30, 10) + stringField(3, QStringLiteral("あいう")));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(bytesField(2, word));
    ASSERT_TRUE(lines.has_value());
    const TextLine &parsed = lines->constFirst();
    ASSERT_EQ(parsed.chars.size(), 3);
    EXPECT_EQ(parsed.chars.at(1).box, QRect(10, 0, 10, 10));
}

TEST(ScreenAiProto, dividesALineThatReportsNoWords)
{
    const QByteArray line = bytesField(2, rect(2, 0, 0, 40, 10) + stringField(3, QStringLiteral("あいうえ")));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(line);
    ASSERT_TRUE(lines.has_value());
    const TextLine &parsed = lines->constFirst();
    EXPECT_EQ(parsed.text, QStringLiteral("あいうえ"));
    ASSERT_EQ(parsed.chars.size(), 4);
    EXPECT_EQ(parsed.chars.at(3).box, QRect(30, 0, 10, 10));
}

TEST(ScreenAiProto, unknownFieldsAreSkipped)
{
    // Field 99 of every wire type this reader handles, inside the LineBox and beside it.
    const QByteArray line =
        bytesField(2,
                   varintField(99, 1234) + floatField(98, 1.0F) + bytesField(97, QByteArrayLiteral("ignored")) +
                       rect(2, 1, 2, 3, 4) + stringField(3, QStringLiteral("あ")));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(line + varintField(96, 7));
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 1);
    EXPECT_EQ(lines->constFirst().text, QStringLiteral("あ"));
}

TEST(ScreenAiProto, everyLineOfTheAnnotationIsReported)
{
    const QByteArray first = bytesField(2, rect(2, 0, 0, 10, 10) + stringField(3, QStringLiteral("あ")));
    const QByteArray second = bytesField(2, rect(2, 0, 20, 10, 10) + stringField(3, QStringLiteral("い")));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(first + second);
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 2);
    EXPECT_EQ(lines->at(1).text, QStringLiteral("い"));
}

TEST(ScreenAiProto, rejectsTruncatedInput)
{
    const QByteArray line = bytesField(2, rect(2, 0, 0, 10, 10) + stringField(3, QStringLiteral("あ")));
    EXPECT_FALSE(parseVisualAnnotation(line.left(line.size() - 2)).has_value());
}

TEST(ScreenAiProto, emptyInputGivesNoLines)
{
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(QByteArray{});
    ASSERT_TRUE(lines.has_value());
    EXPECT_TRUE(lines->isEmpty());
}

TEST(ScreenAiProto, textAndCharacterCountsAgree)
{
    const QByteArray word = bytesField(1,
                                       symbolBox(QStringLiteral("日"), 0, 0, 10, 10, 0.9F) +
                                           symbolBox(QStringLiteral("本"), 10, 0, 10, 10, 0.9F) +
                                           symbolBox(QStringLiteral("語"), 20, 0, 10, 10, 0.9F));
    const std::optional<QList<TextLine>> lines = parseVisualAnnotation(bytesField(2, word));
    ASSERT_TRUE(lines.has_value());
    EXPECT_EQ(lines->constFirst().text.size(), lines->constFirst().chars.size());
}
