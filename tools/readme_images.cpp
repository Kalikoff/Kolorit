// Renders the demonstration images of the README from the photos in docs/images/photos.
//
// Usage: kolorit_readme_images <photos folder> <output folder>
//
// Everything is produced by the same code the application runs, so the pictures
// can be regenerated after any change to the processing. Build it with
// -DKOLORIT_BUILD_TOOLS=ON.

#include "core/BuiltinPalettes.h"
#include "core/Palette.h"
#include "processing/ConsoleEffect.h"
#include "processing/CrtEffect.h"
#include "processing/Effects.h"
#include "processing/PaletteBuilder.h"
#include "processing/PaletteMapper.h"
#include "processing/VhsEffect.h"
#include "ui/MainWindow.h"
#include "ui/RepaintPanel.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>

#include <cstdio>
#include <functional>

using namespace kolorit;

namespace {

constexpr int kGap = 6;
const QColor kBackground(0x16, 0x18, 0x1d);

struct Tile {
    QImage image;
    QString label;
    Palette swatches;   ///< drawn as a thin strip along the bottom edge, if not empty
};

QImage run(const ImageOp& op, const QImage& source)
{
    QPromise<QImage> promise;
    promise.start();
    promise.setProgressRange(0, 100);
    QImage result = op(source, promise);
    promise.finish();
    return result.convertToFormat(QImage::Format_ARGB32);
}

/// Scale to cover the size and crop the middle; focusY moves the crop window (0 top, 1 bottom)
QImage cover(const QImage& image, QSize size, double focusX = 0.5, double focusY = 0.5)
{
    const QImage scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = int((scaled.width() - size.width()) * focusX);
    const int y = int((scaled.height() - size.height()) * focusY);
    return scaled.copy(x, y, size.width(), size.height()).convertToFormat(QImage::Format_ARGB32);
}

QFont labelFont()
{
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    return font;
}

void drawLabel(QPainter& p, const QRect& area, const QString& text, bool bottom = true)
{
    if (text.isEmpty())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QFont font = labelFont();
    const QFontMetrics fm(font);
    const int h = fm.height() + 8;
    const int w = fm.horizontalAdvance(text) + 18;
    const QRect r(area.left() + 8, bottom ? area.bottom() - h - 8 : area.top() + 8, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 12, 16, 190));
    p.drawRoundedRect(r, h / 2.0, h / 2.0);
    p.setFont(font);
    p.setPen(QColor(240, 242, 245));
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
}

void drawSwatches(QPainter& p, const QRect& area, const Palette& palette)
{
    if (palette.isEmpty())
        return;
    const int n = std::min(palette.size(), 64);
    const int h = 6;
    for (int i = 0; i < n; ++i) {
        const int x0 = area.left() + area.width() * i / n;
        const int x1 = area.left() + area.width() * (i + 1) / n;
        p.fillRect(QRect(x0, area.bottom() - h + 1, x1 - x0, h), QColor(palette.at(i)));
    }
}

/// Lay the tiles out in a grid with small gaps on a dark background
QImage grid(const QList<Tile>& tiles, int columns)
{
    const QSize cell = tiles.first().image.size();
    const int rows = int((tiles.size() + columns - 1) / columns);
    QImage out(columns * cell.width() + (columns - 1) * kGap, rows * cell.height() + (rows - 1) * kGap,
               QImage::Format_ARGB32);
    out.fill(kBackground);
    QPainter p(&out);
    p.setRenderHint(QPainter::TextAntialiasing);
    for (int i = 0; i < tiles.size(); ++i) {
        const QRect r(QPoint((i % columns) * (cell.width() + kGap), (i / columns) * (cell.height() + kGap)), cell);
        p.drawImage(r.topLeft(), tiles[i].image);
        drawSwatches(p, r, tiles[i].swatches);
        drawLabel(p, tiles[i].swatches.isEmpty() ? r : r.adjusted(0, 0, 0, -6), tiles[i].label);
    }
    return out;
}

bool save(const QImage& image, const QDir& dir, const QString& name, int quality = -1)
{
    const QString path = dir.filePath(name);
    const bool ok = image.save(path, nullptr, quality);
    std::printf("%-22s %s  %dx%d\n", qPrintable(name), ok ? "OK  " : "FAIL", image.width(), image.height());
    return ok;
}

Palette named(const char* name)
{
    return builtin::byName(QString::fromLatin1(name));
}

ImageOp repaint(const Palette& palette, Dithering dithering, ColorModel model = ColorModel::Lab, int strength = 32)
{
    return PaletteMapper::makeOp(palette, RepaintParams{model, dithering, strength});
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return condition();
}

} // namespace

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Rendered without a display; on Windows the offscreen platform needs to be told
    // where the system fonts are, otherwise the interface falls back to the VHS font
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
#endif

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kalikoff"));
    QCoreApplication::setApplicationName(QStringLiteral("KoloritReadme"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KOLORIT_VERSION));

    if (argc < 3) {
        std::printf("Usage: kolorit_readme_images <photos folder> <output folder>\n");
        return 2;
    }
    const QDir photos(QString::fromLocal8Bit(argv[1]));
    QDir out(QString::fromLocal8Bit(argv[2]));
    out.mkpath(QStringLiteral("."));

    const auto photo = [&](const char* name) {
        const QImage image(photos.filePath(QString::fromLatin1(name) + QStringLiteral(".jpg")));
        if (image.isNull())
            std::printf("missing photo: %s\n", name);
        return image.convertToFormat(QImage::Format_ARGB32);
    };
    const QImage parrot = photo("parrot");
    const QImage tokyo = photo("tokyo");
    const QImage lake = photo("lake");
    const QImage flowers = photo("flowers");
    const QImage car = photo("car");
    const QImage portrait = photo("portrait");
    if (parrot.isNull() || tokyo.isNull() || lake.isNull() || flowers.isNull() || car.isNull() || portrait.isNull())
        return 1;

    vhs::registerFonts();
    int failures = 0;

    // --- Banner: the same bird four times, each in its own treatment ---
    {
        const QImage base = cover(parrot, QSize(239, 360), 0.5, 0.5);
        const QList<Tile> tiles = {
            {base, QStringLiteral("Original"), {}},
            {run(repaint(named("Sweetie 16"), Dithering::BlueNoise), base), QStringLiteral("Sweetie 16"),
             named("Sweetie 16")},
            {run(repaint(builtin::gameBoy(), Dithering::Bayer4, ColorModel::RGB, 48), base),
             QStringLiteral("Game Boy"), builtin::gameBoy()},
            {run(repaint(named("PICO-8"), Dithering::FloydSteinberg), base), QStringLiteral("PICO-8"),
             named("PICO-8")},
        };
        failures += !save(grid(tiles, 4), out, QStringLiteral("banner.png"));
    }

    // --- One photo, many palettes ---
    {
        const QSize size(320, 213);
        const QImage base = cover(parrot, size);
        const Palette kmeans = Palette::fromImage(run(palette_builder::kMeansOp(8, 30, 1), parrot));
        struct Case {
            QString label;
            Palette palette;
            Dithering dithering;
            ColorModel model;
        };
        const QList<Case> cases = {
            {QStringLiteral("PICO-8"), named("PICO-8"), Dithering::FloydSteinberg, ColorModel::Lab},
            {QStringLiteral("Endesga 32"), named("Endesga 32"), Dithering::None, ColorModel::Lab},
            {QStringLiteral("Commodore 64"), named("Commodore 64"), Dithering::BlueNoise, ColorModel::Lab},
            {QStringLiteral("ZX Spectrum"), builtin::zxSpectrum(), Dithering::Bayer4, ColorModel::RGB},
            {QStringLiteral("CGA: cyan / magenta"), named("CGA: cyan / magenta"), Dithering::FloydSteinberg, ColorModel::RGB},
            {QStringLiteral("Game Boy"), builtin::gameBoy(), Dithering::Bayer8, ColorModel::RGB},
            {QStringLiteral("k-means, 8 colors from the photo"), kmeans, Dithering::None, ColorModel::Lab},
            {QStringLiteral("Nord"), named("Nord"), Dithering::BlueNoise, ColorModel::Lab},
        };
        QList<Tile> tiles{{base, QStringLiteral("Original"), {}}};
        for (const Case& c : cases)
            tiles.append({run(repaint(c.palette, c.dithering, c.model), base), c.label, c.palette});
        failures += !save(grid(tiles, 3), out, QStringLiteral("palettes.png"));
    }

    // --- Dithering: a close-up, pixels enlarged twice ---
    {
        const QImage crop = cover(parrot, QSize(480, 320)).copy(200, 40, 160, 106);
        const Palette palette = named("DawnBringer 16");
        const QList<QPair<QString, Dithering>> cases = {
            {QStringLiteral("No dithering"), Dithering::None},
            {QStringLiteral("Bayer 8×8"), Dithering::Bayer8},
            {QStringLiteral("Blue noise"), Dithering::BlueNoise},
            {QStringLiteral("Floyd–Steinberg"), Dithering::FloydSteinberg},
            {QStringLiteral("Atkinson"), Dithering::Atkinson},
            {QStringLiteral("Jarvis–Judice–Ninke"), Dithering::JarvisJudiceNinke},
        };
        QList<Tile> tiles;
        for (const auto& c : cases) {
            const QImage result = run(repaint(palette, c.second), crop);
            tiles.append({result.scaled(crop.size() * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation), c.first, {}});
        }
        failures += !save(grid(tiles, 3), out, QStringLiteral("dithering.png"));
    }

    // --- Effects gallery ---
    {
        const QSize size(320, 213);
        QList<Tile> tiles;

        {
            VhsParams p = VhsParams().scaled(size.width() / 960.0);
            p.useCurrentTime = false;
            p.dateTime = QDateTime(QDate(1994, 7, 16), QTime(21, 37));
            p.fontSizePercent = 7.0;
            p.seed = 7;
            p.corner = OsdCorner::TopRight;
            const QImage base = cover(tokyo, size);
            tiles.append({run(vhs::makeOp(p, vhs::renderOsd(p, size)), base), QStringLiteral("VHS camcorder"), {}});
        }
        {
            CrtParams p;
            p.bloomRadius = 3;
            const QImage base = cover(tokyo, size, 0.7);
            tiles.append({run(crtOp(p), base), QStringLiteral("CRT television"), {}});
        }
        {
            ConsoleParams p;
            p.console = Console::GameBoy;
            tiles.append({run(consoleOp(p), cover(car, size)), QStringLiteral("Retro console: Game Boy"), {}});
        }
        {
            effects::HalftoneParams p;
            p.cellSize = 4.0;
            tiles.append({run(effects::halftone(p), cover(flowers, size)), QStringLiteral("Newspaper halftone"), {}});
        }
        {
            effects::PixelArtParams p;
            p.blockSize = 4;
            p.palette = named("Endesga 32");
            p.model = ColorModel::Lab;
            p.outlineThreshold = 0.25;
            tiles.append({run(effects::pixelArt(p), cover(parrot, size)), QStringLiteral("Pixel art"), {}});
        }
        {
            effects::DuotoneParams p;
            p.shadows = QColor(0x1d, 0x1b, 0x4d);
            p.highlights = QColor(0xff, 0xc9, 0x8b);
            p.useMidtones = true;
            p.midtones = QColor(0xd6, 0x3f, 0x6c);
            tiles.append({run(effects::duotone(p), cover(portrait, size, 0.5, 0.3)), QStringLiteral("Tritone"), {}});
        }
        {
            effects::ColorSplashParams p;
            p.hue = 185.0;
            p.tolerance = 25.0;
            tiles.append({run(effects::colorSplash(p), cover(car, size)), QStringLiteral("Color splash"), {}});
        }
        {
            effects::GradientMapParams p;
            p.palette = named("magma");
            tiles.append({run(effects::gradientMap(p), cover(lake, size)), QStringLiteral("Gradient map: magma"), {}});
        }
        {
            effects::DiscParams p;
            p.transparentBackground = true;
            const QImage disc = run(effects::disc(p), cover(flowers, QSize(size.height(), size.height()), 0.35, 0.5));
            QImage tile(size, QImage::Format_ARGB32);
            tile.fill(QColor(0x22, 0x25, 0x2c));
            QPainter painter(&tile);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QPoint((size.width() - disc.width()) / 2, (size.height() - disc.height()) / 2), disc);
            painter.end();
            tiles.append({tile, QStringLiteral("Disc"), {}});
        }
        failures += !save(grid(tiles, 3), out, QStringLiteral("effects.jpg"), 90);
    }

    // --- Color models: one palette, five ways to measure "nearest" ---
    {
        const QSize size(320, 213);
        const QImage base = cover(lake, size);
        const Palette palette = named("DawnBringer 16");
        QList<Tile> tiles{{base, QStringLiteral("Original"), {}}};
        for (ColorModel m : colorspace::allModels())
            tiles.append({run(repaint(palette, Dithering::None, m), base), colorspace::modelName(m), palette});
        failures += !save(grid(tiles, 3), out, QStringLiteral("models.png"));
    }

    // --- The application window ---
    {
        QDir settings(out.filePath(QStringLiteral(".settings")));
        settings.removeRecursively();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        theme::captureSystemDefaults();
        theme::apply(theme::Mode::Dark);

        MainWindow window;
        window.resize(1280, 800);
        window.show();
        window.openImageFile(photos.filePath(QStringLiteral("parrot.jpg")));
        window.setColorPalette(named("Sweetie 16"));
        if (auto* panel = window.findChild<RepaintPanel*>())
            panel->setParams(RepaintParams{ColorModel::Lab, Dithering::BlueNoise, 32});
        window.repaintImage();
        waitFor([&] { return !window.isProcessing() && window.history().size() == 2; });
        QApplication::processEvents();
        failures += !save(window.grab().toImage(), out, QStringLiteral("screenshot.png"));
        window.close();
        settings.removeRecursively();
    }

    std::printf("\n%s\n", failures ? "FAILED" : "DONE");
    return failures ? 1 : 0;
}
