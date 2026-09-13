// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/lookupwindow.h"

#include "core/settings.h"
#include "jp/japanese.h"
#include "lookup/popupadapter.h"
#include "popup/popupview.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"
#include "scan/hitcontext.h"
#include "scan/wordcopier.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <algorithm>

namespace maru
{

namespace
{

// The size of a first show, in logical pixels: the 600 pixel default PopupMaxWidth, and enough
// height for the sentence and four entries at the default font sizes.
constexpr QSize kDefaultSize{600, 640};
constexpr int kSentenceLines = 3;

// The window size is state rather than a setting, so it lives in marupopstaterc beside the
// other state KDE applications keep, and SettingsDialog has no widget for it.
KConfigGroup stateGroup()
{
    return KSharedConfig::openStateConfig()->group(QStringLiteral("LookupWindow"));
}

QString sentenceHtml(const SentenceSpan &span)
{
    const QString &text = span.text;
    return QString{text.left(span.termStart).toHtmlEscaped() + QStringLiteral("<b><u>") +
                   text.mid(span.termStart, span.termLength).toHtmlEscaped() + QStringLiteral("</u></b>") +
                   text.mid(span.termStart + span.termLength).toHtmlEscaped()};
}

} // namespace

// The card behind the result view. PopupView paints no background of its own, and PopupPreview
// and PopupWindow draw the same card with popup::paintCard().
class LookupCard : public QWidget
{
public:
    explicit LookupCard(QWidget *parent)
        : QWidget{parent}
    {}

    void setTheme(const popup::Theme &theme)
    {
        m_theme = theme;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter{this};
        popup::paintCard(painter, QRectF{rect()}, m_theme);
    }

private:
    popup::Theme m_theme;
};

SentenceSpan sentenceAround(const QString &paragraph, qsizetype start, qsizetype length)
{
    SentenceSpan span;
    span.text = jp::findSentence(paragraph, start);
    if (span.text.isEmpty()) {
        return span;
    }
    for (qsizetype from = paragraph.indexOf(span.text); from >= 0; from = paragraph.indexOf(span.text, from + 1)) {
        if (from <= start && start < from + span.text.size()) {
            span.termStart = start - from;
            span.termLength = std::clamp<qsizetype>(length, 0, span.text.size() - span.termStart);
            return span;
        }
    }
    return span;
}

LookupWindow::LookupWindow(QWidget *parent)
    : QWidget{parent, Qt::Window}
{
    setWindowTitle(i18nc("@title:window", "Lookup"));

    auto *layout = new QVBoxLayout{this};

    auto *header = new QHBoxLayout;
    m_termLabel = new QLabel{this};
    m_termLabel->setTextFormat(Qt::PlainText);
    m_termLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(m_termLabel, 1);
    m_copyButton =
        new QPushButton{QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:button", "Copy Sentence"), this};
    m_copyButton->setObjectName(QStringLiteral("copySentenceButton"));
    connect(m_copyButton, &QPushButton::clicked, this, &LookupWindow::copySentence);
    header->addWidget(m_copyButton);
    layout->addLayout(header);

    auto *splitter = new QSplitter{Qt::Vertical, this};
    splitter->setChildrenCollapsible(false);

    m_sentenceView = new QTextBrowser{splitter};
    m_sentenceView->setObjectName(QStringLiteral("sentenceView"));
    m_sentenceView->setOpenLinks(false);
    m_sentenceView->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_sentenceView->setPlaceholderText(i18nc("@info:placeholder", "Enable scanning and point to Japanese text."));
    splitter->addWidget(m_sentenceView);

    m_card = new LookupCard{splitter};
    auto *cardLayout = new QVBoxLayout{m_card};
    m_view = new popup::PopupView{m_card};
    m_view->setScrollable(true);
    cardLayout->addWidget(m_view);
    m_placeholder = new QLabel{i18nc("@info", "No results"), m_card};
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    cardLayout->addWidget(m_placeholder);
    splitter->addWidget(m_card);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    applySettings();
    resize(stateGroup().readEntry("Size", kDefaultSize));
    // Three lines of the sentence font for the sentence, the rest for the card.
    const int sentenceHeight = (QFontMetrics{m_sentenceView->font()}.lineSpacing() * kSentenceLines) +
                               (2 * m_sentenceView->frameWidth()) +
                               (2 * qRound(m_sentenceView->document()->documentMargin()));
    splitter->setSizes({sentenceHeight, std::max(1, splitter->height() - sentenceHeight)});
}

void LookupWindow::setLookup(const lookup::Response &response, const scan::HitContext &context)
{
    if (isFrozen()) {
        return;
    }
    m_response = response;
    m_term = context.paragraphText.mid(response.highlightStart, response.highlightLength);
    m_sentence = sentenceAround(context.paragraphText, response.highlightStart, response.highlightLength);
    render();
}

bool LookupWindow::isFrozen() const
{
    return m_pointerInside;
}

SentenceSpan LookupWindow::sentence() const
{
    return m_sentence;
}

QString LookupWindow::term() const
{
    return m_term;
}

popup::PopupView *LookupWindow::resultView() const
{
    return m_view;
}

void LookupWindow::applySettings()
{
    popup::Theme theme = popup::themeFromSettings();
    // A top-level window is opaque on every platform MaruPop runs on, so the card is too;
    // BackgroundOpacity is the popup card's alone.
    theme.backgroundOpacity = 255;
    m_card->setTheme(theme);
    m_card->layout()->setContentsMargins(theme.padding, theme.padding, theme.padding, theme.padding);
    m_view->setTheme(theme);
    m_view->setRenderOptions(popup::renderOptionsFromSettings());
    QPalette placeholder = m_placeholder->palette();
    placeholder.setColor(QPalette::WindowText, popup::blend(theme.foreground, theme.background, 0.6));
    m_placeholder->setPalette(placeholder);

    // The sentence reads at the headword size, so the characters under the term are as legible
    // as the headword they matched.
    QFont sentenceFont = m_sentenceView->font();
    if (!theme.fontFamily.isEmpty()) {
        sentenceFont.setFamily(theme.fontFamily);
    }
    sentenceFont.setPointSize(theme.headerPt);
    m_sentenceView->setFont(sentenceFont);

    render();
}

void LookupWindow::copySentence()
{
    if (m_sentence.text.isEmpty()) {
        return;
    }
    scan::copyToClipboard(m_sentence.text);
    Q_EMIT sentenceCopied(m_sentence.text);
}

void LookupWindow::render()
{
    const popup::PopupModel model =
        lookup::toPopupModel(lookup::firstResults(m_response, PopSettings::lookupWindowMaxResults()));
    m_view->setModel(model);
    m_view->verticalScrollBar()->setValue(0);
    m_view->setHidden(model.isEmpty());
    m_placeholder->setHidden(!model.isEmpty());

    m_termLabel->setText(m_term.isEmpty() ? i18nc("@label", "No term")
                                          : i18nc("@label the text a lookup matched", "Term: %1", m_term));
    if (m_sentence.text.isEmpty()) {
        m_sentenceView->clear();
    } else {
        m_sentenceView->setHtml(sentenceHtml(m_sentence));
    }
    m_copyButton->setEnabled(!m_sentence.text.isEmpty());
}

void LookupWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!event->spontaneous()) {
        Q_EMIT visibilityChanged(true);
    }
}

void LookupWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // A hidden window receives no leave event, and a window shown again under a pointer that is
    // elsewhere would otherwise stay frozen.
    m_pointerInside = false;
    if (event->spontaneous()) {
        return;
    }
    KConfigGroup group = stateGroup();
    group.writeEntry("Size", size());
    group.sync();
    Q_EMIT visibilityChanged(false);
}

void LookupWindow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_pointerInside = true;
}

void LookupWindow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_pointerInside = false;
}

} // namespace maru
