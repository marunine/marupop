// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "structuredcontent.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <array>

namespace maru::dict
{

namespace
{

// The Yomitan style properties Qt applies to any element, and the CSS property each becomes.
// Qt's rich-text engine reads these from a style attribute on the element itself.
constexpr std::array<std::pair<QLatin1StringView, QLatin1StringView>, 12> characterProperties{{
    {QLatin1StringView("fontWeight"), QLatin1StringView("font-weight")},
    {QLatin1StringView("fontStyle"), QLatin1StringView("font-style")},
    {QLatin1StringView("fontSize"), QLatin1StringView("font-size")},
    {QLatin1StringView("color"), QLatin1StringView("color")},
    {QLatin1StringView("backgroundColor"), QLatin1StringView("background-color")},
    {QLatin1StringView("textDecorationLine"), QLatin1StringView("text-decoration")},
    {QLatin1StringView("fontFamily"), QLatin1StringView("font-family")},
    {QLatin1StringView("textTransform"), QLatin1StringView("text-transform")},
    {QLatin1StringView("fontVariant"), QLatin1StringView("font-variant")},
    {QLatin1StringView("whiteSpace"), QLatin1StringView("white-space")},
    {QLatin1StringView("lineHeight"), QLatin1StringView("line-height")},
    {QLatin1StringView("wordSpacing"), QLatin1StringView("word-spacing")},
}};

// The properties Qt applies to a block element alone. Emitting them on an inline element has no
// effect and inflates the payload, so they are dropped there.
constexpr std::array<std::pair<QLatin1StringView, QLatin1StringView>, 11> blockProperties{{
    {QLatin1StringView("marginTop"), QLatin1StringView("margin-top")},
    {QLatin1StringView("marginBottom"), QLatin1StringView("margin-bottom")},
    {QLatin1StringView("marginLeft"), QLatin1StringView("margin-left")},
    {QLatin1StringView("marginRight"), QLatin1StringView("margin-right")},
    {QLatin1StringView("paddingTop"), QLatin1StringView("padding-top")},
    {QLatin1StringView("paddingBottom"), QLatin1StringView("padding-bottom")},
    {QLatin1StringView("paddingLeft"), QLatin1StringView("padding-left")},
    {QLatin1StringView("paddingRight"), QLatin1StringView("padding-right")},
    {QLatin1StringView("textAlign"), QLatin1StringView("text-align")},
    {QLatin1StringView("textIndent"), QLatin1StringView("text-indent")},
    {QLatin1StringView("verticalAlign"), QLatin1StringView("vertical-align")},
}};

// Qt draws a border on a table, a td and a th and nowhere else, so a bordered box elsewhere is
// marked with square brackets instead.
constexpr std::array<QLatin1StringView, 3> borderProperties{
    QLatin1StringView("borderStyle"),
    QLatin1StringView("borderWidth"),
    QLatin1StringView("borderColor"),
};

bool isBlockTag(QStringView tag)
{
    static constexpr std::array<QLatin1StringView, 18> blockTags{
        QLatin1StringView("div"),
        QLatin1StringView("p"),
        QLatin1StringView("ul"),
        QLatin1StringView("ol"),
        QLatin1StringView("li"),
        QLatin1StringView("table"),
        QLatin1StringView("thead"),
        QLatin1StringView("tbody"),
        QLatin1StringView("tfoot"),
        QLatin1StringView("tr"),
        QLatin1StringView("td"),
        QLatin1StringView("th"),
        QLatin1StringView("h1"),
        QLatin1StringView("h2"),
        QLatin1StringView("h3"),
        QLatin1StringView("h4"),
        QLatin1StringView("h5"),
        QLatin1StringView("h6"),
    };
    return std::ranges::any_of(blockTags, [tag](QLatin1StringView candidate) {
        return tag == candidate;
    });
}

// The tags Qt's rich-text subset renders, after details and summary are mapped to div.
QString mappedTag(QStringView tag)
{
    if (tag == QLatin1String("details") || tag == QLatin1String("summary"))
        return QStringLiteral("div");
    static constexpr std::array<QLatin1StringView, 26> supported{
        QLatin1StringView("div"),    QLatin1StringView("span"),  QLatin1StringView("p"),     QLatin1StringView("ul"),
        QLatin1StringView("ol"),     QLatin1StringView("li"),    QLatin1StringView("table"), QLatin1StringView("thead"),
        QLatin1StringView("tbody"),  QLatin1StringView("tfoot"), QLatin1StringView("tr"),    QLatin1StringView("td"),
        QLatin1StringView("th"),     QLatin1StringView("b"),     QLatin1StringView("i"),     QLatin1StringView("em"),
        QLatin1StringView("strong"), QLatin1StringView("sub"),   QLatin1StringView("sup"),   QLatin1StringView("code"),
        QLatin1StringView("u"),      QLatin1StringView("s"),     QLatin1StringView("h1"),    QLatin1StringView("h2"),
        QLatin1StringView("h3"),     QLatin1StringView("h4"),
    };
    for (const QLatin1StringView candidate : supported) {
        if (tag == candidate)
            return candidate;
    }
    return {};
}

// A Yomitan style value is a string ("1.2em", "bold") or a number. A bare number on a size
// property is Yomitan's em unit.
QString styleValue(const QJsonValue &value, bool emUnit)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const QString number = QString::number(value.toDouble(), 'g', 4);
        return emUnit ? number + QLatin1String("em") : number;
    }
    if (value.isArray()) {
        QStringList parts;
        const QJsonArray array = value.toArray();
        for (const auto &element : array)
            parts.append(element.toString());
        return parts.join(QLatin1Char(' '));
    }
    return {};
}

// The CSS a style object contributes to tag, and whether a border was requested that Qt cannot
// draw there.
struct StyleResult
{
    QString css;
    bool unsupportedBorder = false;
};

StyleResult renderStyle(const QJsonObject &style, QStringView tag)
{
    StyleResult result;
    if (style.isEmpty())
        return result;

    QStringList declarations;
    for (const auto &[yomitanName, cssName] : characterProperties) {
        const QJsonValue value = style.value(yomitanName);
        if (value.isUndefined() || value.isNull())
            continue;
        const QString rendered = styleValue(value, yomitanName == QLatin1String("fontSize"));
        if (!rendered.isEmpty())
            declarations.append(cssName + QLatin1String(":") + rendered);
    }

    if (isBlockTag(tag)) {
        for (const auto &[yomitanName, cssName] : blockProperties) {
            const QJsonValue value = style.value(yomitanName);
            if (value.isUndefined() || value.isNull())
                continue;
            const QString rendered = styleValue(
                value, yomitanName != QLatin1String("textAlign") && yomitanName != QLatin1String("verticalAlign"));
            if (!rendered.isEmpty())
                declarations.append(cssName + QLatin1String(":") + rendered);
        }
    }

    bool hasBorder = false;
    for (const QLatin1StringView name : borderProperties) {
        if (style.contains(name))
            hasBorder = true;
    }
    if (hasBorder) {
        const bool borderable =
            tag == QLatin1String("table") || tag == QLatin1String("td") || tag == QLatin1String("th");
        if (borderable) {
            const QString width = styleValue(style.value(QLatin1String("borderWidth")), true);
            const QString borderStyle = styleValue(style.value(QLatin1String("borderStyle")), false);
            const QString color = styleValue(style.value(QLatin1String("borderColor")), false);
            QStringList parts;
            if (!width.isEmpty())
                parts.append(width);
            parts.append(borderStyle.isEmpty() ? QStringLiteral("solid") : borderStyle);
            if (!color.isEmpty())
                parts.append(color);
            declarations.append(QLatin1String("border:") + parts.join(QLatin1Char(' ')));
        } else {
            result.unsupportedBorder = true;
        }
    }

    result.css = declarations.join(QLatin1Char(';'));
    return result;
}

// The attribute a td or th writes for one of Yomitan's rowSpan and colSpan properties, or nothing
// for a span of 1, which is the default.
QString spanAttribute(const QJsonObject &object, QLatin1StringView yomitanName, QLatin1StringView htmlName)
{
    const int span = object.value(yomitanName).toInt();
    if (span <= 1)
        return {};
    return QLatin1String(" ") + htmlName + QLatin1String("=\"") + QString::number(span) + QLatin1String("\"");
}

// Structured content is a tree, so the renderer is a recursive descent over it. The depth is the
// nesting depth a dictionary publishes, which is single digits in every dictionary on the test
// mount.
// NOLINTBEGIN(misc-no-recursion)
class Renderer
{
public:
    explicit Renderer(QString imageBasePath)
        : m_imageBasePath(std::move(imageBasePath))
    {}

    void render(const QJsonValue &node)
    {
        if (node.isString()) {
            appendText(node.toString());
            return;
        }
        if (node.isArray()) {
            const QJsonArray array = node.toArray();
            for (const auto &element : array)
                render(element);
            return;
        }
        if (node.isObject())
            renderObject(node.toObject());
    }

    [[nodiscard]] StructuredContentResult take()
    {
        StructuredContentResult result;
        result.richText = m_rich.trimmed();
        result.plainText = m_plain.trimmed();
        result.images = m_images;
        return result;
    }

private:
    void appendText(const QString &text)
    {
        if (text.isEmpty())
            return;
        m_rich += text.toHtmlEscaped();
        m_plain += text;
    }

    void appendRaw(const QString &rich, const QString &plain)
    {
        m_rich += rich;
        m_plain += plain;
    }

    void renderObject(const QJsonObject &object)
    {
        const QString type = object.value(QLatin1String("type")).toString();
        if (type == QLatin1String("text")) {
            appendText(object.value(QLatin1String("text")).toString());
            return;
        }
        if (type == QLatin1String("image")) {
            renderImage(object);
            return;
        }
        if (type == QLatin1String("structured-content")) {
            render(object.value(QLatin1String("content")));
            return;
        }

        const QString tag = object.value(QLatin1String("tag")).toString();
        if (tag.isEmpty()) {
            // A node with content and no tag is a bare container.
            render(object.value(QLatin1String("content")));
            return;
        }
        renderTag(tag, object);
    }

    void renderImage(const QJsonObject &object)
    {
        const QString path = object.value(QLatin1String("path")).toString();
        const int width = object.value(QLatin1String("pixelWidth")).toInt();
        const int height = object.value(QLatin1String("pixelHeight")).toInt();

        // Yomitan uses images 5 pixels or smaller as spacers, which carry no meaning in a popup.
        if ((width > 0 && width <= 5) || (height > 0 && height <= 5))
            return;

        if (!path.isEmpty()) {
            ImageInfo info;
            // A dictionary that names an absolute path is stored relative to its own directory,
            // so moving the directory does not orphan the image.
            info.path = QDir::isAbsolutePath(path) && !m_imageBasePath.isEmpty()
                            ? QDir(m_imageBasePath).relativeFilePath(path)
                            : path;
            info.pixelWidth = width;
            info.pixelHeight = height;
            info.width = object.value(QLatin1String("width")).toDouble();
            info.height = object.value(QLatin1String("height")).toDouble();
            m_images.append(info);
        }

        // The image itself is drawn by the popup from the ImageInfo, which is what lets the
        // per-dictionary ShowImages option suppress it. The alt text stands in its place in the
        // flow so a definition that is only an image is not empty.
        const QString alt = object.value(QLatin1String("alt")).toString();
        const QString description = alt.isEmpty() ? object.value(QLatin1String("title")).toString() : alt;
        if (!description.isEmpty())
            appendRaw(QLatin1String("<i>") + description.toHtmlEscaped() + QLatin1String("</i>"), description);
    }

    void renderTag(const QString &tag, const QJsonObject &object)
    {
        if (tag == QLatin1String("br")) {
            appendRaw(QStringLiteral("<br>"), QStringLiteral("\n"));
            return;
        }
        if (tag == QLatin1String("rp"))
            return;
        if (tag == QLatin1String("img")) {
            renderImage(object);
            return;
        }
        if (tag == QLatin1String("rt")) {
            // A reading is rendered beside its base rather than above it: Qt's rich text has no
            // ruby layout.
            appendRaw(QStringLiteral("（"), QStringLiteral("（"));
            render(object.value(QLatin1String("content")));
            appendRaw(QStringLiteral("）"), QStringLiteral("）"));
            return;
        }
        if (tag == QLatin1String("a")) {
            // A link never survives: the popup is input-transparent unless pinned, and a href
            // into a dictionary's own query scheme has no target here. Yomitan's data.alt carries
            // the label the dictionary intended, for example ［例］.
            const QString alt = object.value(QLatin1String("data")).toObject().value(QLatin1String("alt")).toString();
            if (!alt.isEmpty())
                appendText(alt);
            else
                render(object.value(QLatin1String("content")));
            return;
        }

        const QJsonObject style = object.value(QLatin1String("style")).toObject();
        const QJsonObject data = object.value(QLatin1String("data")).toObject();
        const StyleResult styleResult = renderStyle(style, tag);
        const QString element = mappedTag(tag);

        if (tag == QLatin1String("li")) {
            renderListItem(object, styleResult);
            return;
        }

        // An unstyled span or div that carries no data attribute adds nothing Qt can render, so
        // it is unwrapped rather than emitted.
        const bool collapsible = (tag == QLatin1String("span") || tag == QLatin1String("div")) &&
                                 styleResult.css.isEmpty() && !styleResult.unsupportedBorder && data.isEmpty();
        if (element.isEmpty() || collapsible) {
            if (styleResult.unsupportedBorder)
                appendRaw(QStringLiteral("["), QStringLiteral("["));
            render(object.value(QLatin1String("content")));
            if (styleResult.unsupportedBorder)
                appendRaw(QStringLiteral("]"), QStringLiteral("]"));
            return;
        }

        QString open = QLatin1String("<") + element;
        if (!styleResult.css.isEmpty())
            open += QLatin1String(" style=\"") + styleResult.css.toHtmlEscaped() + QLatin1String("\"");
        const bool cell = element == QLatin1String("td") || element == QLatin1String("th");
        if (cell) {
            // Qt lays out rowspan and colspan. 四字熟語辞典オンライン and 故事・ことわざ・慣用句オンライン
            // carry 4,616 spanning cells, such as a 類義語 header over four rows, and a table
            // rendered with the span dropped shifts every later cell of those rows one column left.
            open += spanAttribute(object, QLatin1String("rowSpan"), QLatin1String("rowspan"));
            open += spanAttribute(object, QLatin1String("colSpan"), QLatin1String("colspan"));
        }
        open += QLatin1String(">");

        const bool block = isBlockTag(element);
        QString openPlain;
        QString closePlain;
        if (element == QLatin1String("td") || element == QLatin1String("th"))
            openPlain = QStringLiteral(" | ");
        else if (block)
            openPlain = QStringLiteral("\n");
        if (block)
            closePlain = QStringLiteral("\n");

        if (styleResult.unsupportedBorder) {
            open += QLatin1String("[");
            openPlain += QLatin1String("[");
        }

        appendRaw(open, openPlain);
        // A ul pushes the unordered sentinel so its own items render a bullet and the counter of
        // an enclosing ol keeps its value: without the push, a ul nested in an ol numbers its
        // bullets from the outer list and the item after the ul continues from the wrong number.
        const bool ordered = element == QLatin1String("ol");
        const bool unordered = element == QLatin1String("ul");
        if (ordered)
            m_orderedItemCounts.append(0);
        else if (unordered)
            m_orderedItemCounts.append(unorderedList);
        render(object.value(QLatin1String("content")));
        if ((ordered || unordered) && !m_orderedItemCounts.isEmpty())
            m_orderedItemCounts.removeLast();

        QString close;
        QString closeText = closePlain;
        if (styleResult.unsupportedBorder) {
            close = QLatin1String("]");
            closeText = QLatin1String("]") + closePlain;
        }
        close += QLatin1String("</") + element + QLatin1String(">");
        appendRaw(close, closeText);
    }

    void renderListItem(const QJsonObject &object, const StyleResult &styleResult)
    {
        QString marker;
        if (!m_orderedItemCounts.isEmpty() && m_orderedItemCounts.last() != unorderedList) {
            m_orderedItemCounts.last() += 1;
            marker = QString::number(m_orderedItemCounts.last()) + QLatin1String(". ");
        } else {
            const QString listStyle =
                object.value(QLatin1String("style")).toObject().value(QLatin1String("listStyleType")).toString();
            marker = listMarker(listStyle);
        }

        QString open = QStringLiteral("<li");
        if (!styleResult.css.isEmpty())
            open += QLatin1String(" style=\"") + styleResult.css.toHtmlEscaped() + QLatin1String("\"");
        open += QLatin1String(">");

        appendRaw(open, QLatin1String("\n") + marker);
        render(object.value(QLatin1String("content")));
        appendRaw(QStringLiteral("</li>"), QString());
    }

    // The CSS list-style-type names Yomitan dictionaries use, plus a quoted custom marker.
    static QString listMarker(const QString &listStyleType)
    {
        if (listStyleType == QLatin1String("circle"))
            return QStringLiteral("◦ ");
        if (listStyleType == QLatin1String("square"))
            return QStringLiteral("▪ ");
        if (listStyleType.size() >= 2 && listStyleType.startsWith(QLatin1Char('"')) &&
            listStyleType.endsWith(QLatin1Char('"'))) {
            return listStyleType.mid(1, listStyleType.size() - 2) + QLatin1Char(' ');
        }
        if (!listStyleType.isEmpty() && listStyleType.at(0).isLetter())
            return {};
        return QStringLiteral("• ");
    }

    // The value m_orderedItemCounts carries for a ul, which no ol counter can reach: a counter
    // starts at 0 and only grows.
    static constexpr int unorderedList = -1;

    QString m_imageBasePath;
    QString m_rich;
    QString m_plain;
    QList<ImageInfo> m_images;
    // One entry per open ol or ul, innermost last: the number of li elements an ol has rendered,
    // or unorderedList for a ul.
    QList<int> m_orderedItemCounts;
};

// NOLINTEND(misc-no-recursion)

} // namespace

StructuredContentResult renderStructuredContent(const QJsonValue &glossaryElement, const QString &imageBasePath)
{
    StructuredContentResult result;

    if (glossaryElement.isString()) {
        const QString text = glossaryElement.toString();
        result.plainText = text;
        result.richText = text.toHtmlEscaped();
        return result;
    }

    // A glossary element that is a bare array is Yomitan's deinflection information.
    if (!glossaryElement.isObject())
        return result;

    Renderer renderer(imageBasePath);
    renderer.render(glossaryElement);
    return renderer.take();
}

} // namespace maru::dict
