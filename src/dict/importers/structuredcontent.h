// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Yomitan structured-content rendering to Qt rich text and a plain-text sibling.
// Retaining supported tables, lists and ruby lets the popup preserve dictionary structure.
// The supported HTML subset is documented at:
// https://doc.qt.io/qt-6/richtext-html-subset.html
// This renderer is original to MaruPop; see NOTICE for component attribution.
#pragma once

#include "dict/records.h"

#include <QList>
#include <QString>

class QJsonValue;

namespace maru::dict
{

// One rendered glossary element.
struct StructuredContentResult
{
    // The Qt rich-text subset. Empty when the element rendered to nothing.
    QString richText;
    // The same element with every tag removed, for the clipboard and for Anki export.
    QString plainText;
    // Every image the element referenced, in document order.
    QList<ImageInfo> images;
};

// Renders one element of a Yomitan term-bank glossary array.
//
// A bare string is escaped and returned as is. An object is walked as structured content. An array
// is Yomitan's deinflection information rather than a definition and renders to nothing, which is
// what JL's GetDefinitions does (JL.Core/Dicts/EPWING/Yomichan/EpwingYomichanUtils.cs).
//
// imageBasePath is the dictionary's extracted source directory; an image path is stored relative
// to it, so the dictionary directory can move without rewriting the records.
[[nodiscard]] StructuredContentResult renderStructuredContent(const QJsonValue &glossaryElement,
                                                              const QString &imageBasePath);

} // namespace maru::dict
