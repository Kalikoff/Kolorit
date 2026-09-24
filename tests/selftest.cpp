// Standalone check of the Kolorit core without a GUI.
//
// Usage: kolorit_selftest <image or -> <output folder>
// ("-" generates a test image)
//
// Runs repainting in every color model and dithering mode, all effects, VHS and
// palette building; results are saved as PNG. Every operation runs on one thread
// and on all cores, which shows the speed-up from parallel processing.

#include "core/BuiltinPalettes.h"
#include "core/ColorSpace.h"
#include "core/ImageTask.h"
#include "core/Palette.h"
#include "processing/ConsoleEffect.h"
#include "processing/CrtEffect.h"
#include "processing/Effects.h"
#include "processing/PaletteBuilder.h"
#include "processing/PaletteMapper.h"
#include "processing/VhsEffect.h"
#include "TestImage.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QList>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QThread>
#include <QThreadPool>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace kolorit;

namespace {

int g_failures = 0;

/// Run an operation synchronously (on the calling thread; rows still go parallel over the pool)
QImage runSync(const ImageOp& op, const QImage& source)
{
    QPromise<QImage> promise;
    promise.start();
    promise.setProgressRange(0, 100);
    QImage result = op(source, promise);
    promise.finish();
    return result;
}

/// Run an operation on one thread and on all of them, compare the results and save a PNG
void check(const QString& name, const ImageOp& op, const QImage& source, const QDir& outDir, bool mustMatch = true,
           const QSize& expectedSize = QSize())
{
    QThreadPool* pool = QThreadPool::globalInstance();
    const int ideal = QThread::idealThreadCount();

    pool->setMaxThreadCount(1);
    QElapsedTimer timer;
    timer.start();
    const QImage single = runSync(op, source);
    const qint64 singleMs = timer.elapsed();

    pool->setMaxThreadCount(ideal);
    timer.restart();
    const QImage multi = runSync(op, source);
    const qint64 multiMs = timer.elapsed();

    bool ok = !multi.isNull() && multi.size() == (expectedSize.isValid() ? expectedSize : source.size());
    if (ok && mustMatch && single != multi) {
        ok = false;
        std::printf("  !! the result depends on the number of threads\n");
    }
    if (ok && !multi.save(outDir.filePath(name + ".png"))) {
        ok = false;
        std::printf("  !! could not save %s\n", qPrintable(outDir.filePath(name + ".png")));
    }
    if (!ok)
        ++g_failures;

    std::printf("%-28s %s  1 thread: %5lld ms   %2d threads: %5lld ms   x%.1f\n",
                qPrintable(name), ok ? "OK  " : "FAIL", static_cast<long long>(singleMs), ideal,
                static_cast<long long>(multiMs), multiMs > 0 ? double(singleMs) / double(multiMs) : 0.0);
}

} // namespace

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // print immediately, even on a crash
    // No windows: fonts and QImage need a QGuiApplication, but not a display
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    if (argc < 3) {
        std::printf("Usage: kolorit_selftest <image or -> <output folder>\n");
        return 2;
    }

    const QImage source = testing::loadOrGenerate(QString::fromLocal8Bit(argv[1]));
    QDir outDir(QString::fromLocal8Bit(argv[2]));
    outDir.mkpath(".");
    source.save(outDir.filePath("source.png"));

    std::printf("Image %dx%d, cores: %d\n\n", source.width(), source.height(), QThread::idealThreadCount());

    vhs::registerFonts();
    std::printf("VHS font: %s\n\n", qPrintable(vhs::fontFamily()));

    // --- Palettes ---
    const Palette bundled = builtin::byName(QStringLiteral("Gartic Phone"));
    if (bundled.isEmpty()) {
        std::printf("Builtin palette not found\n");
        return 1;
    }
    std::printf("Builtin palette: %d colors\n", bundled.size());
    for (const builtin::BuiltinPalette& entry : builtin::palettes())
        std::printf("  %-24s %3d colors\n", qPrintable(entry.name), entry.palette.size());
    const Palette gray = builtin::grayscale(4);
    std::printf("Grayscale (4): %d colors, ends %06X..%06X\n\n", gray.size(), gray.at(0) & 0xFFFFFF,
                gray.at(gray.size() - 1) & 0xFFFFFF);
    if (gray.size() != 4 || builtin::grayscale(256).size() != 256 || builtin::webSafe().size() != 216) {
        std::printf("Palette generators returned a wrong number of colors\n");
        return 1;
    }

    const Palette chunks = palette_builder::fromChunks(source, 8, 8);
    std::printf("Palette from 8x8 chunks: %d colors\n", chunks.size());
    chunks.save(outDir.filePath("palette_chunks.png"));

    const QImage kmeansImage = runSync(palette_builder::kMeansOp(16, 12, 42), source);
    const Palette kmeans = Palette::fromImage(kmeansImage);
    std::printf("k-means palette (16): %d colors\n\n", kmeans.size());
    kmeans.save(outDir.filePath("palette_kmeans.png"));

    // --- Recomputing a chain of operations (this is how the history panel goes back) ---
    {
        RepaintParams repaint;
        repaint.model = ColorModel::Lab;
        effects::LevelsParams lv;
        lv.gamma = 1.2;
        effects::HalftoneParams half;
        const QList<ImageOp> chain = {effects::grayscale(),
                                      effects::sepia(),
                                      effects::levels(lv),
                                      effects::posterize(6),
                                      effects::adjust(5, 10, 15),
                                      effects::pixelate(3),
                                      effects::denoise(0, 2, 25, 1),
                                      effects::halftone(half),
                                      effects::invert(),
                                      PaletteMapper::makeOp(bundled, repaint)};

        QElapsedTimer timer;
        timer.start();
        QImage current = source;
        QPromise<QImage> promise;
        promise.start();
        promise.setProgressRange(0, 100);
        for (const ImageOp& op : chain)
            current = op(current, promise);
        promise.finish();
        const qint64 chainMs = timer.elapsed();

        const bool ok = !current.isNull() && current.size() == source.size();
        std::printf("Chain of %lld operations recomputed in %lld ms (%s)\n\n",
                    static_cast<long long>(chain.size()), static_cast<long long>(chainMs), ok ? "OK" : "FAIL");
        if (!ok)
            ++g_failures;
    }

    // --- Operations on the palette ---
    {
        const Palette ral = builtin::byName(QStringLiteral("RAL Classic"));
        const Palette small = palette_builder::reduce(ral, 16);

        // The reduced palette must consist of colors of the original and be noticeably closer
        // to it than an evenly spaced selection of the same colors
        bool ok = small.size() == 16;
        for (QRgb c : small.colors())
            ok = ok && ral.colors().contains(c);

        const auto averageError = [&ral](const Palette& candidate) {
            double sum = 0.0;
            for (QRgb c : ral.colors()) {
                const Vec3 v = colorspace::toModel(c, ColorModel::Lab);
                float best = std::numeric_limits<float>::max();
                for (QRgb p : candidate.colors())
                    best = std::min(best, colorspace::distanceSq(v, colorspace::toModel(p, ColorModel::Lab)));
                sum += std::sqrt(double(best));
            }
            return sum / ral.size();
        };
        Palette everyNth;
        for (int i = 0; i < ral.size() && everyNth.size() < 16; i += ral.size() / 16)
            everyNth.addUnique(ral.at(i));
        const double reduced = averageError(small);
        const double naive = averageError(everyNth);
        ok = ok && reduced < naive;

        // Gradient order: the sum of steps between neighbors must be smaller
        // than when sorting by luminance
        const auto pathLength = [](const Palette& p) {
            double sum = 0.0;
            for (int i = 1; i < p.size(); ++i)
                sum += std::sqrt(double(colorspace::distanceSq(colorspace::toModel(p.at(i - 1), ColorModel::Lab),
                                                               colorspace::toModel(p.at(i), ColorModel::Lab))));
            return sum;
        };
        Palette byLuma = small;
        byLuma.sortByLuminance();
        Palette asGradient = small;
        asGradient.sortAsGradient();
        const double lumaPath = pathLength(byLuma);
        const double gradientPath = pathLength(asGradient);
        ok = ok && asGradient.size() == small.size() && gradientPath < lumaPath;

        // Merging does not create duplicates
        Palette merged = small;
        ok = ok && merged.merge(small) == 0 && merged.size() == small.size();

        std::printf("Palette: RAL 213 → 16 reduction (error %.1f vs %.1f for an even selection), "
                    "gradient order %.0f vs %.0f by luminance: %s\n\n",
                    reduced, naive, gradientPath, lumaPath, ok ? "OK" : "FAIL");
        if (!ok)
            ++g_failures;
    }

    // --- Color profile: conversion to sRGB ---
    {
        QImage tagged(4, 1, QImage::Format_ARGB32);
        tagged.setPixel(0, 0, qRgb(220, 40, 40));
        tagged.setPixel(1, 0, qRgb(40, 200, 90));
        tagged.setPixel(2, 0, qRgb(60, 90, 210));
        tagged.setPixel(3, 0, qRgb(128, 128, 128));

        QImage untouched = tagged;
        untouched = colorspace::toSrgb(untouched);
        bool ok = untouched.pixel(0, 0) == tagged.pixel(0, 0);

        QImage asSrgb = tagged;
        asSrgb.setColorSpace(QColorSpace(QColorSpace::SRgb));
        ok = ok && colorspace::toSrgb(asSrgb).pixel(0, 0) == tagged.pixel(0, 0);

        QImage asP3 = tagged;
        asP3.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        QString profile;
        const QImage converted = colorspace::toSrgb(asP3, &profile);
        // Display P3 is wider than sRGB, so saturated colors have to move,
        // while neutral gray must stay put (the white point is the same, D65)
        const bool movedColors = converted.pixel(0, 0) != tagged.pixel(0, 0) &&
                                 converted.pixel(1, 0) != tagged.pixel(1, 0) &&
                                 converted.pixel(2, 0) != tagged.pixel(2, 0);
        const QRgb grayAfter = converted.pixel(3, 0);
        const bool grayStable = std::abs(qRed(grayAfter) - 128) <= 1 && std::abs(qGreen(grayAfter) - 128) <= 1 &&
                                std::abs(qBlue(grayAfter) - 128) <= 1;
        ok = ok && movedColors && grayStable && !profile.isEmpty();

        std::printf("Profile → sRGB: %s (%s; red %06X → %06X, gray %06X)\n\n", ok ? "OK" : "FAIL",
                    qPrintable(profile), tagged.pixel(0, 0) & 0xFFFFFF, converted.pixel(0, 0) & 0xFFFFFF,
                    grayAfter & 0xFFFFFF);
        if (!ok)
            ++g_failures;
    }

    // --- Repainting ---
    for (ColorModel model : colorspace::allModels()) {
        RepaintParams params;
        params.model = model;
        check("repaint_" + colorspace::modelName(model).replace(' ', '_'), PaletteMapper::makeOp(bundled, params), source, outDir);
    }
    {
        RepaintParams params;
        params.model = ColorModel::Lab;
        params.dithering = Dithering::Bayer8;
        check("repaint_bayer8", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::FloydSteinberg;
        check("repaint_floyd", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::BlueNoise;
        check("repaint_bluenoise", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::Atkinson;
        check("repaint_atkinson", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::Stucki;
        check("repaint_stucki", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::JarvisJudiceNinke;
        check("repaint_jarvis", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::Sierra;
        check("repaint_sierra", PaletteMapper::makeOp(bundled, params), source, outDir);
        params.dithering = Dithering::None;
        check("repaint_kmeans16", PaletteMapper::makeOp(kmeans, params), source, outDir);
    }

    // --- Effects ---
    check("invert", effects::invert(), source, outDir);
    check("grayscale", effects::grayscale(), source, outDir);
    check("sepia", effects::sepia(), source, outDir);
    check("posterize", effects::posterize(4), source, outDir);
    check("pixelate", effects::pixelate(12), source, outDir);
    check("adjust", effects::adjust(10, 25, 30), source, outDir);
    check("denoise", effects::denoise(0, 3, 25, 1), source, outDir);
    check("denoise_median", effects::denoise(1, 3, 25, 2), source, outDir);
    {
        // Compression artifacts: re-encode the source as low-quality JPEG and clean it up
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "JPEG", 12);
        const QImage compressed = QImage::fromData(jpeg, "JPEG").convertToFormat(QImage::Format_ARGB32);
        compressed.save(outDir.filePath("jpeg_q12.png"));
        check("denoise_jpeg", effects::denoise(0, 3, 30, 2), compressed, outDir);
    }
    {
        effects::PixelArtParams pixel;
        pixel.blockSize = 8;
        check("pixelart_levels", effects::pixelArt(pixel), source, outDir);
        pixel.palette = bundled;
        pixel.model = ColorModel::Lab;
        pixel.outlineWidth = 0.5;
        check("pixelart_palette", effects::pixelArt(pixel), source, outDir);
    }

    // --- Levels, color splash, halftone ---
    {
        effects::LevelsParams lv;
        lv.blackPoint = 16;
        lv.whitePoint = 240;
        lv.gamma = 1.3;
        lv.blueBlack = 8;
        check("levels", effects::levels(lv), source, outDir);

        effects::ColorSplashParams splash;
        splash.hue = 20;
        splash.tolerance = 30;
        splash.softness = 20;
        check("color_splash", effects::colorSplash(splash), source, outDir);

        effects::HalftoneParams half;
        check("halftone_cmyk", effects::halftone(half), source, outDir);
        half.colorSeparation = false;
        half.cellSize = 6;
        check("halftone_mono", effects::halftone(half), source, outDir);
    }

    // --- .cube color table: a round trip ---
    {
        // Build a table from sepia, save it, read it back and compare with the effect itself:
        // this checks writing, parsing and trilinear interpolation
        const effects::CubeLut lut = effects::CubeLut::fromOp(effects::sepia(), 33);
        const QString cubePath = outDir.filePath(QStringLiteral("sepia.cube"));
        QString error;
        bool ok = lut.isValid() && lut.save(cubePath, &error);
        const effects::CubeLut reloaded = effects::CubeLut::load(cubePath, &error);
        ok = ok && reloaded.isValid() && reloaded.size() == 33;

        if (ok) {
            const QImage direct = runSync(effects::sepia(), source);
            const QImage viaLut = runSync(reloaded.makeOp(1.0), source);
            // Table nodes are sparse, so no exact match is expected, only closeness
            int worst = 0;
            double sum = 0.0;
            qint64 count = 0;
            for (int y = 0; y < direct.height(); y += 3) {
                const QRgb* a = reinterpret_cast<const QRgb*>(direct.constScanLine(y));
                const QRgb* b = reinterpret_cast<const QRgb*>(viaLut.constScanLine(y));
                for (int x = 0; x < direct.width(); x += 3) {
                    const int d = std::max({std::abs(qRed(a[x]) - qRed(b[x])), std::abs(qGreen(a[x]) - qGreen(b[x])),
                                            std::abs(qBlue(a[x]) - qBlue(b[x]))});
                    worst = std::max(worst, d);
                    sum += d;
                    ++count;
                }
            }
            const double average = count ? sum / double(count) : 0.0;
            ok = worst <= 4;
            std::printf(".cube table 33^3: %s (sepia directly vs through the table: "
                        "mean %.2f, worst %d of 255)\n\n",
                        ok ? "OK" : "FAIL", average, worst);
        } else {
            std::printf(".cube table: FAIL (%s)\n\n", qPrintable(error));
        }
        if (!ok)
            ++g_failures;
    }

    // --- Gradient map ---
    {
        effects::GradientMapParams gm;
        gm.palette = Palette({qRgb(20, 16, 60), qRgb(220, 60, 40), qRgb(255, 240, 200)});
        check("gradient_map", effects::gradientMap(gm), source, outDir);
        gm.smooth = false;
        gm.palette = bundled;
        check("gradient_map_steps", effects::gradientMap(gm), source, outDir);

        // The builtin ramps are made exactly for this effect
        gm.smooth = true;
        gm.palette = builtin::byName(QStringLiteral("viridis"));
        check("gradient_map_viridis", effects::gradientMap(gm), source, outDir);
        gm.palette = builtin::byName(QStringLiteral("turbo (heat map)"));
        check("gradient_map_turbo", effects::gradientMap(gm), source, outDir);

        effects::DuotoneParams duo;
        check("duotone", effects::duotone(duo), source, outDir);
        duo.useMidtones = true;
        duo.balance = 0.2;
        check("tritone", effects::duotone(duo), source, outDir);
    }

    // --- Retro consoles ---
    for (Console console : allConsoles()) {
        ConsoleParams cp;
        cp.console = console;
        check("console_" + QString::number(int(console)), consoleOp(cp), source, outDir);
    }
    {
        ConsoleParams cp;
        cp.console = Console::ZxSpectrum;
        cp.attributeLimits = false;
        cp.integerScale = true;
        check("console_spectrum_free", consoleOp(cp), source, outDir);
    }

    // --- CRT television ---
    {
        CrtParams crt;
        check("crt", crtOp(crt), source, outDir);
        crt.mask = CrtMask::ShadowMask;
        crt.curvature = 0.6;
        crt.bloomRadius = 0;
        check("crt_shadowmask", crtOp(crt), source, outDir);
        // Console + CRT is the typical combination
        ConsoleParams nes;
        nes.console = Console::Nes;
        const QImage nesFrame = runSync(consoleOp(nes), source);
        crt = CrtParams();
        crt.mask = CrtMask::SlotMask;
        check("crt_nes", crtOp(crt), nesFrame, outDir);
    }

    // --- Disc ---
    {
        effects::DiscParams disc;
        const int side = std::min(source.width(), source.height());
        check("disc", effects::disc(disc), source, outDir, true, QSize(side, side));
        disc.transparentBackground = false;
        disc.hub = effects::DiscHub::DarkMechanism;
        disc.zoom = 1.3;
        disc.offsetY = -0.1;
        disc.sheen = 0.6;
        check("disc_dark_hub", effects::disc(disc), source, outDir, true, QSize(side, side));
        disc.hub = effects::DiscHub::PrintToCenter;
        disc.margin = 0.0;
        disc.transparentBackground = true;
        check("disc_print", effects::disc(disc), source, outDir, true, QSize(side, side));
    }

    // --- VHS ---
    {
        VhsParams params;
        params.useCurrentTime = false;
        params.dateTime = QDateTime(QDate(2023, 2, 21), QTime(13, 2));
        params.customText = QStringLiteral("KOLORIT");
        const QImage osd = vhs::renderOsd(params, source.size());
        check("vhs_default", vhs::makeOp(params, osd), source, outDir);

        // Only the layer aberration and the overlays
        VhsParams pure = params;
        pure.chromaBleed = 0;
        pure.brightness = pure.contrast = pure.saturation = 0.0;
        pure.lineJitter = 0;
        pure.tracking = false;
        pure.scanlines = false;
        pure.noise = 0.0;
        pure.vignette = 0.0;
        pure.label = OsdLabel::None;
        pure.customText.clear();
        check("vhs_pure_aberration", vhs::makeOp(pure, vhs::renderOsd(pure, source.size())), source, outDir);

        VhsParams retro = params;
        retro.imageLeft = Channel::Cyan;
        retro.imageRight = Channel::Red;
        retro.imageOffset = 4;
        retro.label = OsdLabel::Rec;
        retro.corner = OsdCorner::TopRight;
        retro.seed = 7;
        retro.noise = 0.2;
        check("vhs_rec", vhs::makeOp(retro, vhs::renderOsd(retro, source.size())), source, outDir);
    }

    std::printf("\n%s: %d failures. Results: %s\n", g_failures ? "FAILED" : "PASSED", g_failures,
                qPrintable(outDir.absolutePath()));
    return g_failures ? 1 : 0;
}
