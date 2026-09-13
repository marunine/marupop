// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The popup card itself: a frameless translucent layer-shell surface carrying a PopupView.
#pragma once

#include "core/enums.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <QPointer>
#include <QRect>
#include <QWidget>

class QPropertyAnimation;
class QScreen;

namespace maru::popup
{

struct PopupModel;
class PopupView;

// The result popup. The surface is a layer-shell overlay on Wayland, anchored to the top left
// corner of the target screen and positioned through its margins, which is the only way a
// client can place a window at desktop coordinates on Wayland. On every other platform, the
// offscreen platform plugin the tests run under included, the window is a frameless tool
// window positioned with QWidget::move().
//
// The card is input-transparent while it is not pinned. Pinning drops the transparency and
// enables wheel scrolling; a layer surface fixes its input region when it is mapped, so
// setPinned() destroys and recreates the surface and re-shows it at the same rectangle.
//
// A layer surface is also bound to one output for its life, since the output is an argument of
// get_layer_surface and no request moves it. showNear() therefore destroys and recreates the
// surface for a pointer that crossed onto another screen, and updates the margins alone for
// every move inside one screen.
class PopupWindow : public QWidget
{
    Q_OBJECT
    // The animated fade. Qt's Wayland platform plugin ignores QWidget::setWindowOpacity(), so
    // the value is applied twice: to the window opacity, which is what the fallback platforms
    // honour, and to the alpha the card and its content are painted at, which is what Wayland
    // honours.
    Q_PROPERTY(qreal fadeOpacity READ fadeOpacity WRITE setFadeOpacity)

public:
    explicit PopupWindow(QWidget *parent = nullptr);
    ~PopupWindow() override;

    // Renders the model with the theme and the options in hand, and resizes the card to the
    // content, bounded by Theme::maxWidth and Theme::maxHeight. A call made while isFrozen()
    // holds is dropped.
    void setModel(const PopupModel &model);
    // The plain-text rendering of the model in hand, for the clipboard.
    [[nodiscard]] QString plainText() const;

    // Places the card next to cursor on screen and shows it. screen may be nullptr, in which
    // case the screen under cursor is used, and the primary screen when there is none. A call
    // that repeats the model already shown moves the card only while followCursor is set.
    void showNear(QPoint cursorLogical, QScreen *screen);
    void hidePopup();

    // Pinning makes the card accept the pointer: it stops being input-transparent, it scrolls
    // on the wheel, accepts keyboard selection/copy and scrolling, and draws a pin indicator.
    // Escape releases the pin and returns the card to its passive hover behavior.
    void setPinned(bool pinned);
    [[nodiscard]] bool isPinned() const;

    // True while setModel() and showNear() are dropped, which is a pinned card that is on screen.
    // hidePopup() is honoured throughout, so the scanning toggle and the quit path still take a
    // pinned card away.
    [[nodiscard]] bool isFrozen() const;

    // A suppressed card is hidden, and setModel() and showNear() are dropped until it is released.
    // A pinned card stays on screen with its content, which is what isFrozen() holds it to.
    // Application suppresses it while app/LookupWindow is open and HidePopupWhileLookupWindowOpen
    // is set.
    void setSuppressed(bool suppressed);
    [[nodiscard]] bool isSuppressed() const;

    // Whether showNear() repositions the card for a cursor move that produced the same model.
    // The default is true, which is what the Application handler for
    // scan::ScanController::hitMoved() places the card through; setting it to false pins the card
    // to the point the last new model was placed at.
    void setFollowCursor(bool follow);
    [[nodiscard]] bool followsCursor() const;

    // Re-read marupoprc. applyTheme() also re-sizes the card, since the bounds and the fonts
    // are what the size follows.
    void applyTheme();
    void applyRenderOptions();

    [[nodiscard]] qreal fadeOpacity() const;
    void setFadeOpacity(qreal opacity);

    // The rectangle the card occupies in logical desktop coordinates, which is what the tests
    // and the placement probe assert on.
    [[nodiscard]] QRect popupRect() const;

    // The rectangle the card covers on the desktop right now, or an empty rect while the card is
    // hidden. capture::FrameSource::setOcclusionProvider() is assigned this on a session whose
    // pixel source composites MaruPop's own windows into a grab, and the answer becomes
    // capture::Frame::occluded.
    [[nodiscard]] QRect occlusionRect() const;

    // Whether the card is placed off the paragraph it answers for. False by default, and set to
    // true on a session whose pixel source composites MaruPop's own windows into a grab: a card
    // over the paragraph removes that text from the next scan.
    void setAvoidsText(bool avoid);
    [[nodiscard]] bool avoidsText() const;

    // The logical bounding box of the paragraph the card is showing, from
    // scan::HitContext::paragraphRectLogical. Assign it before the showNear() that places the
    // card for a new model; the value is kept for the pointer samples that follow, which carry
    // no rectangle of their own. Ignored while avoidsText() is false.
    void setAvoidRect(QRect logical);
    [[nodiscard]] QRect avoidRect() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateSize();
    void applyWindowFlags();
    void configureSurface();
    void applyGeometry();
    // Realizes the surface on m_screen and shows it. A layer surface is bound to the output
    // named when it is created, so this is the only place the card's screen is decided.
    void mapSurface();
    void startFade(qreal to, bool hideWhenDone);
    // True where the platform plugin name starts with "wayland", which is where the card is a
    // layer surface rather than a frameless tool window.
    [[nodiscard]] static bool isLayerShellSession();

    PopupView *m_view = nullptr;
    QPointer<QWindow> m_previousFocusWindow;
    Theme m_theme;
    RenderOptions m_options;
    PopupPositionMode m_positionMode = PopupPositionMode::VisualNovel;
    QPropertyAnimation *m_fade = nullptr;
    QPointer<QScreen> m_screen;
    // The output the mapped layer surface is bound to, which is the origin its margins are
    // measured from. mapSurface() is the one writer, so the two screens are equal from every
    // map until a showNear() reassigns m_screen; the window between those two points is a
    // pointer that crossed a monitor boundary while the card was up, and it is what the rebuild
    // branch in showNear() reads. Off Wayland the card is moved with QWidget::move() and this
    // is never read.
    QPointer<QScreen> m_surfaceScreen;
    QRect m_rect;
    // The paragraph the card must stay off, in logical global desktop coordinates. Empty until
    // the first setAvoidRect().
    QRect m_avoidRect;
    bool m_avoidsText = false;
    qreal m_fadeOpacity = 1.0;
    bool m_pinned = false;
    bool m_suppressed = false;
    // A fade to 0 that ends in hide() is running. showNear() reverses it rather than render onto
    // a card that is on its way out.
    bool m_hiding = false;
    bool m_followCursor = true;
    bool m_contentChanged = true;
    bool m_surfaceConfigured = false;
};

} // namespace maru::popup
