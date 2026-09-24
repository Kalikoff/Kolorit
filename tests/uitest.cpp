// Offscreen check of the Kolorit interface.
//
// Usage: kolorit_uitest <image or -> <snapshot folder>
// ("-" generates a test image)
//
// Without a display (the offscreen platform) creates the main window and every dialog,
// loads an image and a palette, runs repainting and VHS through the normal window
// pipeline, waits for the background preview and saves widget snapshots as PNG.
// Settings are written to a temporary folder so the user's settings are not touched.

#include "core/BuiltinPalettes.h"
#include "core/Palette.h"
#include "processing/Effects.h"
#include "processing/VhsEffect.h"
#include "ui/ChunkPaletteDialog.h"
#include "ui/EffectParamsDialog.h"
#include "ui/ImageView.h"
#include "ui/HistoryPanel.h"
#include "ui/KMeansPaletteDialog.h"
#include "ui/PaletteGeneratorDialog.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/VhsDialog.h"
#include "TestImage.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QSettings>
#include <QTimer>
#include <QTranslator>

#include <cmath>
#include <cstdio>
#include <functional>

using namespace kolorit;

namespace {

int g_failures = 0;

/// Spin the event loop until the condition holds or the timeout expires
bool waitFor(const std::function<bool()>& condition, int timeoutMs = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        // processEvents does not run deferred deletions (deleteLater), so do it explicitly
        // as the normal application event loop would
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return true;
}

/// The visible preview dialog of the main window (closed ones may still await deletion)
PreviewDialog* activePreviewDialog(QWidget& window)
{
    for (PreviewDialog* d : window.findChildren<PreviewDialog*>())
        if (d->isVisible())
            return d;
    return nullptr;
}

void snapshot(QWidget& widget, const QDir& dir, const QString& name)
{
    QApplication::processEvents();
    const QPixmap pixmap = widget.grab();
    const QString path = dir.filePath(name + ".png");
    const bool ok = !pixmap.isNull() && pixmap.save(path);
    if (!ok)
        ++g_failures;
    std::printf("%-26s %s  %dx%d\n", qPrintable(name), ok ? "OK  " : "FAIL", pixmap.width(), pixmap.height());
}

void expect(bool condition, const char* what)
{
    if (!condition)
        ++g_failures;
    std::printf("%-26s %s\n", what, condition ? "OK  " : "FAIL");
}

} // namespace

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // print immediately, even on a crash
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kalikoff"));
    QCoreApplication::setApplicationName(QStringLiteral("KoloritUiTest"));

    if (argc < 3) {
        std::printf("Usage: kolorit_uitest <image> <snapshot folder>\n");
        return 2;
    }
    QDir outDir(QString::fromLocal8Bit(argv[2]));
    outDir.mkpath(".");
    const QString imagePath = outDir.filePath("source.png");
    testing::loadOrGenerate(QString::fromLocal8Bit(argv[1])).save(imagePath);

    // Isolated, clean settings: snapshots must not depend on previous runs
    QDir(outDir.filePath("settings")).removeRecursively();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outDir.filePath("settings"));

    vhs::registerFonts();
    theme::captureSystemDefaults();
    theme::apply(theme::Mode::Dark);

    MainWindow window;
    window.setColorPalette(Palette());
    window.resize(1180, 760);
    window.show();
    QApplication::processEvents();
    snapshot(window, outDir, "01_main_empty");

    expect(window.openImageFile(imagePath), "open image");
    window.setColorPalette(builtin::byName(QStringLiteral("Gartic Phone")));
    expect(window.colorPalette().size() == 18, "bundled palette loaded");
    QApplication::processEvents();
    snapshot(window, outDir, "02_main_loaded");

    // --- Repaint through the normal window pipeline ---
    const QImage beforeRepaint = window.sourceImage();
    window.repaintImage();
    expect(waitFor([&] { return !window.isProcessing(); }), "repaint finished");
    expect(window.sourceImage().size() == beforeRepaint.size() && window.sourceImage() != beforeRepaint,
           "repaint result valid");
    expect(window.historyIndex() == 1 && window.history().size() == 2, "repaint added a history step");
    snapshot(window, outDir, "03_main_repainted");

    // --- Invert on top: operations add themselves to the history ---
    window.effectInvert();
    expect(waitFor([&] { return !window.isProcessing(); }), "invert finished");
    snapshot(window, outDir, "04_main_inverted");
    window.revertToOriginal();

    // --- Modeless dialog from the main window: zoom while tuning, full-size frame, apply ---
    {
        auto* view = window.findChild<ImageView*>();
        expect(view != nullptr, "image view found");
        window.effectPosterize();
        auto* dialog = activePreviewDialog(window);
        expect(dialog != nullptr && !dialog->isModal(), "non-modal dialog shown");

        bool fullArrived = false;
        int frames = 0;
        QObject::connect(dialog, &PreviewDialog::previewReady, [&](const QImage&, bool full) {
            ++frames;
            fullArrived = fullArrived || full;
        });

        // While the preview runs, the main window is alive: zoom it
        view->zoomActualSize();
        view->zoomIn();
        view->zoomIn();
        const double zoomBefore = view->zoom();
        expect(zoomBefore > 1.0, "zoomed while previewing");

        expect(waitFor([&] { return fullArrived; }), "full-resolution preview arrived");
        expect(std::fabs(view->zoom() - zoomBefore) < 1e-9, "zoom kept across preview frames");
        snapshot(window, outDir, "10_main_nonmodal_preview");

        // Apply: the full-size frame is already computed, so the result appears without recomputation
        // The window stands at step zero, so applying cuts off the tail of the history
        // and adds its own step: wait for exactly that transition
        dialog->accept();
        expect(waitFor([&] { return !window.isProcessing() && window.historyIndex() > 0; }),
               "result applied from full preview");
        expect(window.sourceImage().size() == window.history().first().size,
               "applied result has source size");
        expect(activePreviewDialog(window) == nullptr, "dialog closed after accept");
        view->fitToWindow();
        window.revertToOriginal();
    }

    // --- Pixel art from the window: palette and model are picked up on the fly, "levels" are disabled ---
    {
        window.setColorPalette(builtin::byName(QStringLiteral("Gartic Phone")));
        window.effectPixelArt();
        auto* dialog = qobject_cast<EffectParamsDialog*>(activePreviewDialog(window));
        expect(dialog != nullptr, "pixel art dialog opened");
        if (dialog) {
            int frames = 0;
            QObject::connect(dialog, &PreviewDialog::previewReady, [&](const QImage&, bool) { ++frames; });
            expect(waitFor([&] { return frames > 0; }), "pixel art preview arrived");
            expect(dialog->values().value("usePalette").toBool(), "palette checkbox on by default");
            auto* levelsSpin = dialog->findChildren<QDoubleSpinBox*>().value(1);
            expect(levelsSpin != nullptr && !levelsSpin->isEnabled(), "levels disabled while using palette");

            // Changing the palette on the panel must restart the preview
            const int before = frames;
            window.setColorPalette(builtin::grayscale(4));
            expect(waitFor([&] { return frames > before; }), "palette change refreshed preview");
            auto* checkbox = dialog->findChild<QCheckBox*>();
            expect(checkbox && checkbox->text().contains(QStringLiteral("4 colors")), "checkbox label follows palette");

            // Empty palette: the checkbox is disabled, the levels are active again
            window.setColorPalette(Palette());
            QApplication::processEvents();
            expect(levelsSpin->isEnabled(), "levels enabled when palette empty");
            snapshot(*dialog, outDir, "12_pixelart_synced");
            dialog->reject();
            QApplication::processEvents();
        }
        window.setColorPalette(builtin::byName(QStringLiteral("Gartic Phone")));
    }

    // --- VHS dialog with a live preview ---
    {
        VhsParams params;
        params.customText = QStringLiteral("KOLORIT");
        VhsDialog dialog(window.sourceImage(), params, &window);
        bool previewArrived = false;
        QImage preview;
        QObject::connect(&dialog, &PreviewDialog::previewReady, [&](const QImage& image, bool) {
            previewArrived = true;
            preview = image;
        });
        dialog.show();
        expect(waitFor([&] { return previewArrived; }), "vhs preview arrived");
        expect(!preview.isNull(), "vhs preview valid");
        preview.save(outDir.filePath("vhs_preview.png"));
        snapshot(dialog, outDir, "05_vhs_dialog");
        dialog.close();
    }

    // --- Generic parameter dialog ---
    {
        EffectParamsDialog dialog(
            QStringLiteral("Color correction"), window.sourceImage(),
            {ParamSpec{"brightness", QStringLiteral("Brightness"), -100, 100, 10, 1},
             ParamSpec{"contrast", QStringLiteral("Contrast"), -100, 100, 20, 1},
             ParamSpec{"saturation", QStringLiteral("Saturation"), -100, 100, 30, 1}},
            [](const QVariantMap& v) {
                return effects::adjust(v["brightness"].toDouble(), v["contrast"].toDouble(),
                                       v["saturation"].toDouble());
            },
            &window);
        bool previewArrived = false;
        QObject::connect(&dialog, &PreviewDialog::previewReady, [&](const QImage&, bool) { previewArrived = true; });
        dialog.show();
        expect(waitFor([&] { return previewArrived; }), "adjust preview arrived");
        snapshot(dialog, outDir, "06_params_dialog");
        dialog.close();
    }

    // --- Pixel art dialog: numeric parameters + the palette checkbox ---
    {
        const Palette palette = window.colorPalette();
        EffectParamsDialog dialog(
            QStringLiteral("Pixel art"), window.sourceImage(),
            {ParamSpec{"block", QStringLiteral("Pixel size"), 2, 64, 8, 1, 0, true, QStringLiteral(" px")},
             ParamSpec{"threshold", QStringLiteral("Outline contrast threshold"), 0, 100, 20, 1, 0, false, QStringLiteral(" %")},
             ParamSpec::checkbox("usePalette", QStringLiteral("Quantize to the current palette"), true)},
            [palette](const QVariantMap& v) {
                effects::PixelArtParams p;
                p.blockSize = v["block"].toInt();
                p.outlineThreshold = v["threshold"].toDouble() / 100.0;
                if (v["usePalette"].toBool())
                    p.palette = palette;
                return effects::pixelArt(p);
            },
            &window);
        dialog.setDescription(QStringLiteral("Checking the checkbox and the description in the generic dialog."));
        bool previewArrived = false;
        QImage preview;
        QObject::connect(&dialog, &PreviewDialog::previewReady, [&](const QImage& image, bool) {
            previewArrived = true;
            preview = image;
        });
        dialog.show();
        expect(waitFor([&] { return previewArrived; }), "pixel art preview arrived");
        preview.save(outDir.filePath("pixelart_preview.png"));
        snapshot(dialog, outDir, "09_pixelart_dialog");
        dialog.close();
    }

    // --- Palette dialogs ---
    {
        ChunkPaletteDialog dialog(window.sourceImage(), &window);
        dialog.show();
        QApplication::processEvents();
        expect(!dialog.palette().isEmpty(), "chunk palette computed");
        snapshot(dialog, outDir, "07_chunk_dialog");
        dialog.close();
    }
    {
        KMeansPaletteDialog dialog(window.sourceImage(), &window);
        dialog.show();
        QApplication::processEvents();
        snapshot(dialog, outDir, "08_kmeans_dialog");
        dialog.close();
    }
    {
        // Palette generator: check all five types and snapshot the busiest one
        PaletteGeneratorDialog dialog(&window);
        for (int kind = PaletteGeneratorDialog::Grayscale; kind <= PaletteGeneratorDialog::HueWheel; ++kind) {
            dialog.setState(kind, 8, true, QColor(0x8B, 0xAC, 0x0F), 8, QColor(0x1B, 0x1F, 0x3B),
                            QColor(0xC0, 0x39, 0x2B), QColor(0xF5, 0xE6, 0xC8), true, 12, 4, 12, 60.0, 45.0);
            QApplication::processEvents();
            expect(!dialog.palette().isEmpty(), "generator produces colors");
        }
        dialog.setState(PaletteGeneratorDialog::Ramp, 8, true, QColor(0x8B, 0xAC, 0x0F), 8,
                        QColor(0x1B, 0x1F, 0x3B), QColor(0xC0, 0x39, 0x2B), QColor(0xF5, 0xE6, 0xC8), true, 12,
                        4, 12, 60.0, 45.0);
        dialog.show();
        QApplication::processEvents();
        snapshot(dialog, outDir, "13_generator_dialog");
        dialog.close();
    }

    // --- Operation history ---
    {
        // Back to step zero and two operations on top: the tail of the old history
        // is dropped, leaving the original and two new steps
        window.revertToOriginal();
        const QImage original = window.sourceImage();

        window.repaintImage();
        expect(waitFor([&] { return !window.isProcessing(); }), "repaint for history finished");
        window.effectInvert();
        expect(waitFor([&] { return !window.isProcessing(); }), "invert for history finished");

        expect(window.history().size() == 3, "two operations after a jump leave three steps");
        expect(window.historyIndex() == window.history().size() - 1, "current step is the last one");
        const QImage afterTwo = window.sourceImage();

        // A step back recomputes the chain from the original, so it runs in the background
        window.undo();
        expect(waitFor([&] { return !window.isProcessing(); }), "undo finished");
        expect(window.historyIndex() == 1 && window.sourceImage() != afterTwo, "undo moved back a step");

        window.redo();
        expect(waitFor([&] { return !window.isProcessing(); }), "redo finished");
        expect(window.historyIndex() == 2 && window.sourceImage() == afterTwo,
               "redo rebuilt exactly the same image");

        // Jumping to step zero returns the original without recomputation
        window.gotoHistoryState(0);
        expect(waitFor([&] { return !window.isProcessing(); }), "jump to step 0 finished");
        expect(window.sourceImage() == original, "jump to step 0 restores the original");
        expect(window.historyIndex() == 0, "index follows the jump");

        // A history click during a computation must neither open windows nor change the step
        window.effectInvert();
        window.gotoHistoryState(0);
        expect(window.historyIndex() == 0, "history click is ignored while busy");
        expect(waitFor([&] { return !window.isProcessing(); }), "invert during busy check finished");

        // A new operation on top dropped the steps after the current one
        expect(window.history().size() == 2, "steps after the current one are dropped");

        const auto* panel = window.findChild<HistoryPanel*>();
        expect(panel != nullptr, "history panel exists");
        snapshot(window, outDir, "14_main_history");
    }

    // --- Light theme ---
    window.setTheme(theme::Mode::Light);
    QApplication::processEvents();
    snapshot(window, outDir, "11_main_light");
    window.setTheme(theme::Mode::Dark);

    // --- Russian translation: the compiled catalog is embedded and switches the interface ---
    {
        QTranslator russian;
        expect(russian.load(QStringLiteral(":/i18n/kolorit_ru.qm")), "Russian translation embedded");
        QCoreApplication::installTranslator(&russian);
        expect(QObject::tr("Error") == QStringLiteral("Ошибка"), "Russian translation applies");
        expect(builtin::groupName(builtin::PaletteGroup::Retro) == QStringLiteral("Ретро-платформы"),
               "core strings translated");
        MainWindow russianWindow;
        russianWindow.resize(1180, 760);
        russianWindow.show();
        russianWindow.openImageFile(imagePath);
        QApplication::processEvents();
        snapshot(russianWindow, outDir, "15_main_russian");
        russianWindow.close();
        QCoreApplication::removeTranslator(&russian);
    }

    window.close();
    std::printf("\n%s: %d failures. Snapshots: %s\n", g_failures ? "FAILED" : "PASSED", g_failures,
                qPrintable(outDir.absolutePath()));
    return g_failures ? 1 : 0;
}
