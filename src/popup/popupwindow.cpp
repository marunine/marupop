// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/popupwindow.h"

#include "core/layerscope.h"
#include "core/logging.h"
#include "core/settings.h"
#include "popup/entrymodel.h"
#include "popup/placement.h"
#include "popup/popupview.h"

#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWindow>

#include <LayerShellQt/Window>
#include <cmath>

namespace maru::popup
{

namespace
{

// Radius of the pin indicator, in logical pixels, and its inset from the card's top right
// corner.
constexpr int pinIndicatorRadius = 4;
constexpr int pinIndicatorInset = 8;

// The LayerShellQt handle of widget, or nullptr where widget has no platform window yet. Every
// caller reads it under PopupWindow::isLayerShellSession(), which is what says the handle drives
// a zwlr_layer_surface_v1. A file-local function rather than a member, so the header needs no
// declaration of the LayerShellQt namespace.
LayerShellQt::Window *layerWindowOf(const QWidget *widget)
{
    QWindow *handle = widget->windowHandle();
    if (handle == nullptr) {
        return nullptr;
    }
    return LayerShellQt::Window::get(handle);
}

} // namespace

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget{parent}
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    m_view = new PopupView{this};
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(m_theme.padding, m_theme.padding, m_theme.padding, m_theme.padding);
    layout->addWidget(m_view);

    // A graphics effect is what fades the content, since a top-level widget takes none and
    // Qt's Wayland plugin ignores the window opacity. The card background is faded by the
    // painter instead.
    auto *effect = new QGraphicsOpacityEffect{m_view};
    effect->setOpacity(1.0);
    m_view->setGraphicsEffect(effect);

    m_fade = new QPropertyAnimation{this, QByteArrayLiteral("fadeOpacity"), this};

    applyTheme();
    applyRenderOptions();
    applyWindowFlags();
}

PopupWindow::~PopupWindow() = default;

bool PopupWindow::isLayerShellSession()
{
    // Cached: the platform plugin is fixed once QGuiApplication exists, and a PopupWindow is built
    // after it. showNear() reads this three times per pointer sample, which is 125 Hz from
    // ACTIVE_MS = 8 in data/kwin-script/marupopcursor/contents/code/main.js, and
    // QGuiApplication::platformName() returns a QString by value.
    static const bool layerShell = QGuiApplication::platformName().startsWith(QLatin1StringView{"wayland"});
    return layerShell;
}

void PopupWindow::setModel(const PopupModel &model)
{
    if (isFrozen() || m_suppressed) {
        return;
    }
    m_view->setModel(model);
    m_contentChanged = true;
    updateSize();
}

bool PopupWindow::isFrozen() const
{
    // A pinned card that is on screen is one the user is reading and scrolling, so its content
    // and its position both hold until it is unpinned. Pinning a card that is hidden arms the
    // mode for the next result rather than freezing an empty card, which is why the visibility is
    // part of the test.
    return m_pinned && isVisible();
}

QString PopupWindow::plainText() const
{
    return m_view->plainText();
}

void PopupWindow::applyTheme()
{
    m_theme = themeFromSettings();
    m_positionMode = settings::popupPositionMode();
    m_sides = {};
    m_view->setTheme(m_theme);
    if (auto *box = qobject_cast<QVBoxLayout *>(layout())) {
        box->setContentsMargins(m_theme.padding, m_theme.padding, m_theme.padding, m_theme.padding);
    }
    updateSize();
    update();
}

void PopupWindow::applyRenderOptions()
{
    m_options = renderOptionsFromSettings();
    m_view->setRenderOptions(m_options);
    updateSize();
}

void PopupWindow::updateSize()
{
    const int padding = m_theme.padding;
    const int maxContentWidth = qMax(1, m_theme.maxWidth - (2 * padding));
    const int maxContentHeight = qMax(1, m_theme.maxHeight - (2 * padding));

    QSizeF content = m_view->layoutContent(maxContentWidth);
    int width = static_cast<int>(std::ceil(content.width()));
    int height = static_cast<int>(std::ceil(content.height()));

    // A scroll bar takes width away from the viewport, which would reflow the document; the
    // card grows by that width instead.
    if (m_pinned && height > maxContentHeight) {
        width += m_view->verticalScrollBar()->sizeHint().width();
    }

    width = qBound(1, width, maxContentWidth);
    height = qBound(1, height, maxContentHeight);

    const QSize size{width + (2 * padding), height + (2 * padding)};
    resize(size);
    m_rect.setSize(size);
}

void PopupWindow::applyWindowFlags()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
    if (!isLayerShellSession()) {
        // A layer surface needs no tool hint; every other platform gets one so the card stays
        // out of the task switcher.
        flags |= Qt::Tool;
    }
    if (!m_pinned) {
        flags |= Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus;
    }
    // The attribute is written before the flags: QWidgetPrivate::create() adds
    // Qt::WindowTransparentForInput back to the flags of a widget that carries
    // Qt::WA_TransparentForMouseEvents, so clearing the flag first and the attribute second
    // leaves the flag set.
    setAttribute(Qt::WA_ShowWithoutActivating, !m_pinned);
    setAttribute(Qt::WA_TransparentForMouseEvents, !m_pinned);
    setWindowFlags(flags);
    m_view->setScrollable(m_pinned);
    m_view->setAttribute(Qt::WA_TransparentForMouseEvents, !m_pinned);
    // A flag change destroys the platform window, so the layer surface is configured again on
    // the next show.
    m_surfaceConfigured = false;
}

void PopupWindow::configureSurface()
{
    if (!isLayerShellSession()) {
        return;
    }
    if (windowHandle() == nullptr) {
        qCWarning(logPopup) << "no QWindow to promote to a layer surface";
        return;
    }
    auto *layer = layerWindowOf(this);
    if (layer == nullptr) {
        qCWarning(logPopup) << "LayerShellQt refused the popup surface";
        return;
    }
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setScope(QString{popupLayerScope});
    layer->setKeyboardInteractivity(m_pinned ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                             : LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(m_pinned);
    layer->setExclusiveZone(-1); // never push panels around
    layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorLeft});
    m_surfaceConfigured = true;
}

void PopupWindow::applyGeometry()
{
    resize(m_rect.size());
    if (!isLayerShellSession()) {
        move(m_rect.topLeft());
        return;
    }
    auto *layer = layerWindowOf(this);
    if (layer == nullptr) {
        return;
    }
    // The margins are relative to m_surfaceScreen's origin, since the anchors are the top and
    // the left edge of the output the surface is bound to. mapSurface() is what keeps that
    // output and m_screen the same one.
    const QPoint origin = m_surfaceScreen != nullptr ? m_surfaceScreen->geometry().topLeft() : QPoint{0, 0};
    layer->setMargins(QMargins{m_rect.x() - origin.x(), m_rect.y() - origin.y(), 0, 0});
    layer->setDesiredSize(m_rect.size());
}

void PopupWindow::mapSurface()
{
    // Before winId(), so the platform window is created at the size the card keeps. applyGeometry()
    // below repeats the call, which QWidget::resize() drops as an unchanged size.
    resize(m_rect.size());
    winId(); // realizes the QWindow so the layer surface can be configured
    if (!m_surfaceConfigured) {
        configureSurface();
    }
    if (isLayerShellSession() && m_screen != nullptr) {
        if (auto *layer = layerWindowOf(this); layer != nullptr) {
            // Read by the QWaylandLayerSurface constructor that show() below triggers. The output
            // is an argument of get_layer_surface, so this call is what binds the card to a
            // screen, and the surface keeps that output for the rest of its life.
            layer->setScreen(m_screen);
        }
    }
    // Before applyGeometry(), which measures the margins from the origin of the output the
    // surface is about to be bound to.
    m_surfaceScreen = m_screen;
    applyGeometry();
    show();
    if (m_pinned) {
        activateWindow();
        m_view->setFocus(Qt::ShortcutFocusReason);
    }
}

void PopupWindow::setSuppressed(bool suppressed)
{
    m_suppressed = suppressed;
    // A pinned card is the user reading it, and hiding it would also end isFrozen(), so the next
    // result would replace the pinned content once the suppression is released.
    if (suppressed && !m_pinned) {
        hidePopup();
    }
}

bool PopupWindow::isSuppressed() const
{
    return m_suppressed;
}

void PopupWindow::showNear(QPoint cursorLogical, QScreen *screen)
{
    if (m_suppressed) {
        return;
    }
    if (m_view->model().isEmpty()) {
        hidePopup();
        return;
    }

    QScreen *target = screen != nullptr ? screen : QGuiApplication::screenAt(cursorLogical);
    if (target == nullptr) {
        target = QGuiApplication::primaryScreen();
    }
    if (target == nullptr) {
        qCWarning(logPopup) << "no screen to place the popup on";
        return;
    }

    if (isFrozen()) {
        return;
    }

    const bool wasVisible = isVisible();
    if (wasVisible && !m_followCursor && !m_contentChanged && m_screen == target) {
        return;
    }
    const bool contentChanged = m_contentChanged;
    const bool sameScreen = m_screen == target;
    const QRect previousRect = m_rect;
    m_contentChanged = false;
    m_screen = target;

    if (wasVisible && m_hiding) {
        // hidePopup() armed a fade to 0 that ends in hide(), and PopupFadeMs is 120 ms by default.
        // A response that arrives inside that window would otherwise be rendered onto a card that
        // finishes fading out and hides with the new content on it.
        startFade(1.0, false);
    }

    // The avoid rect is applied on a session whose pixel source composites the card into the next
    // grab; on every other session the placement is the mode's own, unchanged.
    // A card coming up afresh, or on another screen, has no side to keep: the previous one was
    // chosen against another pointer or another edge.
    if (!wasVisible || !sameScreen) {
        m_sides = {};
    }
    // An empty avoid rect answers the plain placement.
    m_rect = placePopupAvoiding(cursorLogical,
                                size(),
                                target->geometry(),
                                m_positionMode,
                                m_theme.cursorOffset,
                                m_avoidsText ? m_avoidRect : QRect{},
                                &m_sides);
    if (wasVisible && !contentChanged && sameScreen && m_rect == previousRect) {
        // The placement clamps the card into the screen, so every pointer sample inside the band
        // where the card rests against a screen edge lands it on the rectangle it already
        // occupies. applyGeometry() drives a zwlr_layer_surface_v1 reconfigure and a commit, and
        // the return keeps 125 of those per second off the compositor.
        return;
    }
    if (contentChanged || !wasVisible) {
        // The headword the card leads with, which is what a log read back against a hit line
        // confirms the popup actually shows. plainText() is the rendered document, so the first
        // line is the entry the renderer put at the top rather than the model's idea of it.
        qCInfo(logPopup).nospace() << "showing " << m_rect << " on " << target->name() << " for the pointer at "
                                   << cursorLogical << ": \"" << plainText().section(QLatin1Char('\n'), 0, 0) << '"';
    } else {
        // ScanController::hitMoved() reaches this branch once per pointer sample, which is 125 Hz
        // from ACTIVE_MS = 8 in data/kwin-script/marupopcursor/contents/code/main.js. QtDebugMsg
        // rather than QtInfoMsg for that reason: the category is disabled by default, so the
        // stream and the plainText() call above are both skipped.
        qCDebug(logPopup).nospace() << "moving the popup to " << m_rect << " for the pointer at " << cursorLogical;
    }

    if (!wasVisible) {
        setFadeOpacity(m_theme.fadeMs > 0 ? 0.0 : 1.0);
        mapSurface();
        startFade(1.0, false);
        return;
    }
    if (isLayerShellSession() && m_screen != m_surfaceScreen) {
        // The pointer crossed a monitor boundary while the card was up. A zwlr_layer_surface_v1
        // keeps the output it was created on for its life, so the margins applyGeometry() measures
        // would be applied against the corner of the screen the pointer left and put the card on
        // that display. Destroying the surface and building it again on the new output is the way
        // across; the fade in flight keeps running, and the card comes back at the opacity it had.
        // Every other platform reaches the new screen through the QWidget::move() below.
        qCDebug(logPopup).nospace() << "rebuilding the popup surface on " << target->name() << " for the pointer at "
                                    << cursorLogical;
        hide();
        mapSurface();
        return;
    }
    applyGeometry();
}

void PopupWindow::hidePopup()
{
    if (!isVisible()) {
        return;
    }
    qCInfo(logPopup) << "hiding the popup";
    startFade(0.0, true);
}

void PopupWindow::startFade(qreal to, bool hideWhenDone)
{
    m_fade->stop();
    // Read by showNear(), which restarts the fade rather than render onto a card that is on its
    // way out.
    m_hiding = hideWhenDone;
    if (m_theme.fadeMs <= 0) {
        setFadeOpacity(to);
        if (hideWhenDone) {
            m_hiding = false;
            hide();
        }
        return;
    }
    m_fade->setDuration(m_theme.fadeMs);
    m_fade->setStartValue(m_fadeOpacity);
    m_fade->setEndValue(to);
    m_fade->disconnect(this);
    if (hideWhenDone) {
        connect(m_fade, &QPropertyAnimation::finished, this, [this] {
            m_hiding = false;
            hide();
        });
    }
    m_fade->start();
}

qreal PopupWindow::fadeOpacity() const
{
    return m_fadeOpacity;
}

void PopupWindow::setFadeOpacity(qreal opacity)
{
    m_fadeOpacity = qBound(0.0, opacity, 1.0);
    // Not on Wayland: the platform plugin has no per-surface opacity and answers every call
    // with "This plugin does not support setting window opacity" on the default logging
    // category, which is eight lines per fade in the journal. The graphics effect below and
    // the painter's own opacity carry the fade there.
    if (!isLayerShellSession()) {
        setWindowOpacity(m_fadeOpacity);
    }
    if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(m_view->graphicsEffect())) {
        effect->setOpacity(m_fadeOpacity);
    }
    update();
}

void PopupWindow::setPinned(bool pinned)
{
    if (m_pinned == pinned) {
        return;
    }
    if (pinned && !isLayerShellSession()) {
        m_previousFocusWindow = QGuiApplication::focusWindow();
    }
    const bool restoreFocus = !pinned && isActiveWindow();
    m_pinned = pinned;

    const bool wasVisible = isVisible();
    if (wasVisible) {
        // A layer surface fixes its input region when it is mapped, so the surface is
        // destroyed and recreated rather than reconfigured. The stop cancels a hide fade as
        // well, and the card is re-shown at full opacity below.
        m_fade->stop();
        m_hiding = false;
        hide();
    }
    applyWindowFlags();
    updateSize();
    if (!wasVisible) {
        return;
    }
    setFadeOpacity(1.0);
    mapSurface();
    // Wayland restores the previously active surface when this layer releases its keyboard.
    // Other Qt platforms can retain the now-passive tool window as their active window.
    if (restoreFocus && m_previousFocusWindow != nullptr) {
        m_previousFocusWindow->requestActivate();
    }
    if (!pinned) {
        m_previousFocusWindow.clear();
    }
}

bool PopupWindow::isPinned() const
{
    return m_pinned;
}

void PopupWindow::setFollowCursor(bool follow)
{
    m_followCursor = follow;
}

bool PopupWindow::followsCursor() const
{
    return m_followCursor;
}

QRect PopupWindow::popupRect() const
{
    return m_rect;
}

QRect PopupWindow::occlusionRect() const
{
    // A card that is fading out still has pixels on the desktop, so isVisible() rather than a
    // test on m_hiding is what decides. hide() runs at the end of the fade and clears it.
    return isVisible() ? m_rect : QRect{};
}

void PopupWindow::setAvoidsText(bool avoid)
{
    m_avoidsText = avoid;
}

bool PopupWindow::avoidsText() const
{
    return m_avoidsText;
}

void PopupWindow::setAvoidRect(QRect logical)
{
    m_avoidRect = logical;
}

QRect PopupWindow::avoidRect() const
{
    return m_avoidRect;
}

void PopupWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_pinned && event->key() == Qt::Key_Escape) {
        setPinned(false);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PopupWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter{this};
    painter.setOpacity(m_fadeOpacity);
    paintCard(painter, QRectF{rect()}, m_theme);

    if (m_pinned) {
        // A filled dot in the top right corner, which is the only chrome distinguishing a
        // pinned card from an input-transparent one.
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_theme.highlightWord);
        const QPointF center{static_cast<qreal>(width() - pinIndicatorInset), static_cast<qreal>(pinIndicatorInset)};
        painter.drawEllipse(center, pinIndicatorRadius, pinIndicatorRadius);
    }
}

} // namespace maru::popup
