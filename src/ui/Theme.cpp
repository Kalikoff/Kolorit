#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>
#include <QSvgRenderer>

#include <memory>

namespace kolorit::theme {

namespace {

struct State {
    bool captured = false;
    QString systemStyle;
    QPalette systemPalette;
    Mode mode = Mode::System;
};

State& state()
{
    static State s;
    return s;
}

const QColor kAccent(0x4c, 0x8d, 0xff);

QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x2b, 0x2d, 0x31);
    const QColor base(0x1f, 0x20, 0x24);
    const QColor alternate(0x26, 0x28, 0x2c);
    const QColor button(0x36, 0x38, 0x3e);
    const QColor text(0xe4, 0xe6, 0xea);
    const QColor disabled(0x7c, 0x7f, 0x87);
    const QColor border(0x45, 0x48, 0x50);

    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3c, 0x42));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0x8a, 0xb4, 0xff));
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x50, 0x53, 0x5b));
    p.setColor(QPalette::Midlight, QColor(0x42, 0x45, 0x4c));
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1b, 0x1e));
    p.setColor(QPalette::Shadow, QColor(0x0e, 0x0f, 0x11));

    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3a, 0x3d, 0x45));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Button, QColor(0x30, 0x32, 0x37));
    return p;
}

QPalette lightPalette()
{
    QPalette p;
    const QColor window(0xf3, 0xf4, 0xf6);
    const QColor base(0xff, 0xff, 0xff);
    const QColor alternate(0xf7, 0xf8, 0xfa);
    const QColor button(0xe9, 0xeb, 0xef);
    const QColor text(0x1f, 0x23, 0x28);
    const QColor disabled(0x9a, 0x9e, 0xa6);
    const QColor border(0xd0, 0xd3, 0xd9);

    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xf0));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xd0, 0x30, 0x30));
    p.setColor(QPalette::Link, QColor(0x2f, 0x6f, 0xe8));
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe2, 0xe4, 0xe8));
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, QColor(0xb8, 0xbb, 0xc2));
    p.setColor(QPalette::Shadow, QColor(0x8a, 0x8d, 0x94));

    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0xc8, 0xd4, 0xea));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

/// Style rules on top of Fusion: spacing, rounding, accent button, progress and sliders
QString fusionStyleSheet(const QPalette& pal)
{
    const QString accent = kAccent.name();
    const QString accentHover = kAccent.lighter(112).name();
    const QString accentPressed = kAccent.darker(115).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString hover = pal.color(QPalette::Midlight).name();
    const QString panel = pal.color(QPalette::AlternateBase).name();
    const QString disabledText = pal.color(QPalette::Disabled, QPalette::ButtonText).name();
    const QString disabledButton = pal.color(QPalette::Disabled, QPalette::Button).name();
    const QString button = pal.color(QPalette::Button).name();
    QColor checked = kAccent;
    checked.setAlpha(70);
    const QString checkedBg = QStringLiteral("rgba(%1,%2,%3,%4)")
                                  .arg(checked.red()).arg(checked.green()).arg(checked.blue()).arg(checked.alpha());

    return QStringLiteral(R"(
QToolBar { spacing: 3px; padding: 4px 6px; border: none; border-bottom: 1px solid %BORDER%; }
QToolBar::separator { width: 1px; background: %BORDER%; margin: 6px 6px; }
QToolButton { padding: 4px 8px; border-radius: 6px; border: 1px solid transparent; }
QToolButton:hover { background: %HOVER%; }
QToolButton:pressed { background: %BORDER%; }
QToolButton:checked { background: %CHECKED%; border-color: %ACCENT%; }
QToolButton::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding; left: -2px; }
QDockWidget { titlebar-close-icon: none; }
QDockWidget::title { padding: 7px 10px; background: %PANEL%; border-bottom: 1px solid %BORDER%; font-weight: 600; }
QGroupBox { font-weight: 600; margin-top: 14px; border: 1px solid %BORDER%; border-radius: 8px; padding: 10px 6px 6px 6px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 5px; }
QPushButton { background: %BUTTON%; border: 1px solid %BORDER%; border-radius: 6px; padding: 5px 12px; min-width: 60px; }
QPushButton:hover { background: %HOVER%; }
QPushButton:pressed { background: %BORDER%; }
QPushButton:default { border-color: %ACCENT%; }
QPushButton:disabled { background: %DISABLED_BUTTON%; color: %DISABLED_TEXT%; }
QPushButton#primaryButton { background: %ACCENT%; color: white; font-weight: 600; padding: 8px 14px; border: none; }
QPushButton#primaryButton:hover { background: %ACCENT_HOVER%; }
QPushButton#primaryButton:pressed { background: %ACCENT_PRESSED%; }
QPushButton#primaryButton:disabled { background: %DISABLED_BUTTON%; color: %DISABLED_TEXT%; }
QLabel#hintLabel { color: %DISABLED_TEXT%; }
QLabel#placeholderLabel { color: %DISABLED_TEXT%; font-size: 10pt; }
QStatusBar { border-top: 1px solid %BORDER%; }
QStatusBar::item { border: none; }
QProgressBar { border: 1px solid %BORDER%; border-radius: 5px; text-align: center; min-height: 14px; max-height: 16px; }
QProgressBar::chunk { background: %ACCENT%; border-radius: 4px; }
QSlider::groove:horizontal { height: 4px; background: %BORDER%; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: %ACCENT%; border: 2px solid %PANEL%; }
QSlider::handle:horizontal:hover { background: %ACCENT_HOVER%; }
QListWidget { border: 1px solid %BORDER%; border-radius: 6px; padding: 4px; }
QMenu { padding: 4px; border: 1px solid %BORDER%; }
QMenu::item { padding: 5px 24px 5px 30px; border-radius: 4px; }
QMenu::item:selected { background: %ACCENT%; color: white; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }
QToolTip { border: 1px solid %BORDER%; padding: 4px 6px; }
)")
        .replace(QStringLiteral("%ACCENT_HOVER%"), accentHover)
        .replace(QStringLiteral("%ACCENT_PRESSED%"), accentPressed)
        .replace(QStringLiteral("%ACCENT%"), accent)
        .replace(QStringLiteral("%BORDER%"), border)
        .replace(QStringLiteral("%HOVER%"), hover)
        .replace(QStringLiteral("%BUTTON%"), button)
        .replace(QStringLiteral("%PANEL%"), panel)
        .replace(QStringLiteral("%CHECKED%"), checkedBg)
        .replace(QStringLiteral("%DISABLED_TEXT%"), disabledText)
        .replace(QStringLiteral("%DISABLED_BUTTON%"), disabledButton);
}

/// Minimal rules for the system style: only the accent button and hint labels
QString systemStyleSheet(const QPalette& pal)
{
    return QStringLiteral(R"(
QPushButton#primaryButton { background: %ACCENT%; color: white; font-weight: 600; padding: 8px 14px; border: none; border-radius: 6px; }
QPushButton#primaryButton:hover { background: %ACCENT_HOVER%; }
QPushButton#primaryButton:disabled { background: %DISABLED_BUTTON%; color: %DISABLED_TEXT%; }
QLabel#hintLabel { color: %DISABLED_TEXT%; }
QLabel#placeholderLabel { color: %DISABLED_TEXT%; }
QStatusBar::item { border: none; }
)")
        .replace(QStringLiteral("%ACCENT_HOVER%"), kAccent.lighter(112).name())
        .replace(QStringLiteral("%ACCENT%"), kAccent.name())
        .replace(QStringLiteral("%DISABLED_TEXT%"), pal.color(QPalette::Disabled, QPalette::ButtonText).name())
        .replace(QStringLiteral("%DISABLED_BUTTON%"), pal.color(QPalette::Disabled, QPalette::Button).name());
}

/// Icon engine: an SVG using currentColor is recolored to the needed color
/// and drawn as vectors at any size and DPI
class SvgIconEngine : public QIconEngine {
public:
    SvgIconEngine(QByteArray svg, QColor color)
        : m_svg(std::move(svg))
        , m_color(color)
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (mode == QIcon::Disabled)
            painter->setOpacity(painter->opacity() * 0.35);
        renderer().render(painter, QRectF(rect));
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine* clone() const override { return new SvgIconEngine(m_svg, m_color); }

private:
    QSvgRenderer& renderer()
    {
        if (!m_renderer) {
            QByteArray doc = m_svg;
            doc.replace("currentColor", m_color.name().toLatin1());
            m_renderer = std::make_unique<QSvgRenderer>(doc);
        }
        return *m_renderer;
    }

    QByteArray m_svg;
    QColor m_color;
    std::unique_ptr<QSvgRenderer> m_renderer;
};

} // namespace

void captureSystemDefaults()
{
    State& s = state();
    if (s.captured || !qApp)
        return;
    s.captured = true;
    s.systemStyle = QApplication::style()->objectName();
    s.systemPalette = QApplication::palette();
}

void apply(Mode mode)
{
    captureSystemDefaults();
    State& s = state();
    s.mode = mode;

    if (mode == Mode::System) {
        if (QStyle* style = QStyleFactory::create(s.systemStyle))
            QApplication::setStyle(style);
        QApplication::setPalette(s.systemPalette);
        qApp->setStyleSheet(systemStyleSheet(s.systemPalette));
        return;
    }

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QPalette pal = mode == Mode::Dark ? darkPalette() : lightPalette();
    QApplication::setPalette(pal);
    qApp->setStyleSheet(fusionStyleSheet(pal));
}

Mode current()
{
    return state().mode;
}

QString modeName(Mode mode)
{
    switch (mode) {
    case Mode::System: return QObject::tr("System");
    case Mode::Dark:   return QObject::tr("Dark");
    case Mode::Light:  return QObject::tr("Light");
    }
    return {};
}

QColor accent()
{
    return kAccent;
}

QIcon icon(const QString& name, const QColor& color)
{
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QIcon(new SvgIconEngine(file.readAll(), color));
}

QIcon icon(const QString& name)
{
    return icon(name, QApplication::palette().color(QPalette::Active, QPalette::WindowText));
}

} // namespace kolorit::theme
