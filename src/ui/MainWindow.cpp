#include "ui/MainWindow.h"

#include "app/AppPaths.h"
#include "app/Language.h"
#include "app/Updates.h"
#include "core/BuiltinPalettes.h"
#include "core/ColorSpace.h"
#include "processing/ConsoleEffect.h"
#include "processing/CrtEffect.h"
#include "processing/Effects.h"
#include "processing/PaletteBuilder.h"
#include "ui/ChunkPaletteDialog.h"
#include "ui/EffectParamsDialog.h"
#include "ui/PaletteGeneratorDialog.h"
#include "ui/ImageView.h"
#include "ui/KMeansPaletteDialog.h"
#include "ui/PalettePanel.h"
#include "ui/PreviewDialog.h"
#include "ui/RepaintPanel.h"
#include "ui/VhsDialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include <cmath>

namespace kolorit {

namespace {

QString captionError()
{
    return QObject::tr("Error");
}

constexpr int kLayoutVersion = 2;

/// Toolbar button with a drop-down menu
QToolButton* menuToolButton(const QString& text, QMenu* menu, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_view(new ImageView(this))
{
    setWindowTitle(QStringLiteral("Kolorit"));
    setAcceptDrops(true);
    setCentralWidget(m_view);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    resize(1280, 820);

    buildActions();
    buildDocks();
    buildMenus();
    buildToolbar();
    buildStatusBar();
    refreshIcons();

    connect(&m_runner, &ImageTaskRunner::started, this, &MainWindow::onTaskStarted);
    connect(&m_runner, &ImageTaskRunner::progressChanged, this, &MainWindow::onTaskProgress);
    connect(&m_runner, &ImageTaskRunner::finished, this, &MainWindow::onTaskFinished);
    connect(&m_runner, &ImageTaskRunner::canceled, this, &MainWindow::onTaskCanceled);
    connect(m_view, &ImageView::zoomChanged, this, [this](double factor) {
        m_zoomButton->setText(tr("%1 %").arg(int(factor * 100.0 + 0.5)));
    });
    connect(m_palettePanel, &PalettePanel::paletteChanged, this, [this] {
        m_repaintPanel->setPaletteSize(m_palettePanel->colors().size());
        updateStatus();
    });
    connect(m_repaintPanel, &RepaintPanel::repaintRequested, this, &MainWindow::repaintImage);

    loadSettings();
    updateActions();
    updateStatus();
}

MainWindow::~MainWindow() = default;

const Palette& MainWindow::colorPalette() const
{
    return m_palettePanel->colors();
}

void MainWindow::setColorPalette(const Palette& palette)
{
    m_palettePanel->setColors(palette);
}

// ---------------------------------------------------------------------------
// Building the interface
// ---------------------------------------------------------------------------

QAction* MainWindow::makeAction(const QString& text, const QString& iconName, const QKeySequence& shortcut,
                                const QString& tip, const std::function<void()>& slot)
{
    auto* action = new QAction(text, this);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    // Tooltip with the shortcut: "Open image (Ctrl+O)"
    const QString keys = shortcut.toString(QKeySequence::NativeText);
    action->setToolTip(keys.isEmpty() ? tip : tr("%1 (%2)").arg(tip, keys));
    action->setStatusTip(tip);
    if (!iconName.isEmpty())
        m_iconNames.insert(action, iconName);
    if (slot)
        connect(action, &QAction::triggered, this, slot);
    return action;
}

void MainWindow::buildActions()
{
    m_actOpen = makeAction(tr("Open…"), "open", QKeySequence::Open, tr("Open an image"),
                           [this] { openImage(); });
    m_actPaste = makeAction(tr("Paste"), "paste", QKeySequence::Paste, tr("Paste an image from the clipboard"),
                            [this] { pasteImage(); });
    m_actSave = makeAction(tr("Save"), "save", QKeySequence::Save, tr("Save the result to a file"),
                           [this] { saveResult(); });
    m_actExit = makeAction(tr("Exit"), "exit", QKeySequence::Quit, tr("Close the application"), [this] { close(); });

    m_actRepaint = makeAction(tr("Repaint into palette"), "repaint", QKeySequence(Qt::CTRL | Qt::Key_R),
                              tr("Replace every pixel with the nearest palette color"), [this] { repaintImage(); });
    m_actCancel = makeAction(tr("Cancel"), "cancel", QKeySequence(Qt::Key_Escape),
                             tr("Stop the current operation"), [this] { cancelProcessing(); });
    m_actCancel->setEnabled(false);

    m_actShowOriginal = makeAction(tr("Original"), "compare", QKeySequence(Qt::CTRL | Qt::Key_B),
                                   tr("Show the original instead of the result, for comparison"), {});
    m_actShowOriginal->setCheckable(true);
    connect(m_actShowOriginal, &QAction::toggled, this, &MainWindow::updateView);
    connect(m_actShowOriginal, &QAction::toggled, this, &MainWindow::updateStatus);

    // Undo and redo are shortcuts only: the steps are visible and selectable in the History panel
    m_actUndo = makeAction(tr("Step back"), "undo", QKeySequence::Undo,
                           tr("Go one step back in the history (Ctrl+Z)"), [this] { undo(); });
    m_actRedo = makeAction(tr("Step forward"), "redo", QKeySequence::Redo,
                           tr("Go one step forward in the history (Ctrl+Y or Ctrl+Shift+Z)"), [this] { redo(); });
    // Ctrl+Shift+Z is "step forward" in Photoshop, and that is what people expect of it
    m_actRedo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    addAction(m_actUndo);
    addAction(m_actRedo);
    m_actRevert = makeAction(tr("Revert to original"), "revert", QKeySequence(Qt::Key_F12),
                             tr("Go back to history step zero, the loaded image (F12)"),
                             [this] { revertToOriginal(); });

    m_actFit = makeAction(tr("Fit to window"), "fit", QKeySequence(Qt::CTRL | Qt::Key_0),
                          tr("Show the whole image"), [this] { m_view->fitToWindow(); });
    m_actActual = makeAction(tr("Actual size"), "actual", QKeySequence(Qt::CTRL | Qt::Key_1),
                             tr("100 % zoom, pixel for pixel"), [this] { m_view->zoomActualSize(); });
    m_actZoomIn = makeAction(tr("Zoom in"), "zoom-in", QKeySequence::ZoomIn, tr("Increase the zoom"),
                             [this] { m_view->zoomIn(); });
    m_actZoomOut = makeAction(tr("Zoom out"), "zoom-out", QKeySequence::ZoomOut, tr("Decrease the zoom"),
                              [this] { m_view->zoomOut(); });

    m_actLoadPalette = makeAction(tr("Load from file…"), "folder", QKeySequence(Qt::CTRL | Qt::Key_L),
                                  tr("Load a palette from a PNG (a row of colors) or any image"),
                                  [this] { loadPaletteFromFile(); });
    m_actSavePalette = makeAction(tr("Save to file…"), "download", {}, tr("Save the palette as PNG"),
                                  [this] { savePalette(); });

    // View actions stay available while a modeless effect settings dialog is active
    for (QAction* a : {m_actFit, m_actActual, m_actZoomIn, m_actZoomOut, m_actShowOriginal})
        a->setShortcutContext(Qt::ApplicationShortcut);

    m_imageActions = {m_actSave, m_actRepaint, m_actShowOriginal, m_actRevert, m_actUndo, m_actRedo,
                      m_actFit, m_actActual, m_actZoomIn, m_actZoomOut};
    m_processingActions = {m_actOpen,   m_actPaste, m_actRepaint,     m_actRevert,
                           m_actUndo,   m_actRedo,  m_actLoadPalette};
}

void MainWindow::buildDocks()
{
    m_repaintPanel = new RepaintPanel(this);
    m_repaintDock = new QDockWidget(tr("Repaint"), this);
    m_repaintDock->setObjectName(QStringLiteral("repaintDock"));
    m_repaintDock->setWidget(m_repaintPanel);
    m_repaintDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_palettePanel = new PalettePanel(this);
    m_paletteDock = new QDockWidget(tr("Palette"), this);
    m_paletteDock->setObjectName(QStringLiteral("paletteDock"));
    m_paletteDock->setWidget(m_palettePanel);
    m_paletteDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_historyPanel = new HistoryPanel(this);
    m_historyDock = new QDockWidget(tr("History"), this);
    m_historyDock->setObjectName(QStringLiteral("historyDock"));
    m_historyDock->setWidget(m_historyPanel);
    m_historyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    connect(m_historyPanel, &HistoryPanel::stateSelected, this, &MainWindow::gotoHistoryState);

    for (QDockWidget* dock : {m_repaintDock, m_paletteDock, m_historyDock}) {
        dock->setMinimumWidth(290);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, dock);
    }
    // Repaint is compact at the top, the palette takes most of the space, history goes at the bottom
    resizeDocks({m_repaintDock, m_paletteDock, m_historyDock}, {1, 4, 3}, Qt::Vertical);
}

void MainWindow::buildMenus()
{
    // --- Palette sources: used by the buttons of the Palette panel ---
    // Builtin palettes are compiled into the code (BuiltinPalettes); grayscale is generated from the number of levels
    m_bundledMenu = new QMenu(tr("Builtin palettes"), this);
    auto* actGen = m_bundledMenu->addAction(tr("Create palette…"), this, &MainWindow::paletteGenerate);
    actGen->setStatusTip(tr("Grayscale, tint of one color, ramp, levels per channel or a hue wheel"));
    m_bundledMenu->addSeparator();
    // There are many palettes, so they are split into sections: the panel keeps a single button
    // and the list does not stretch over the whole screen
    for (builtin::PaletteGroup group : builtin::allGroups()) {
        const QList<builtin::BuiltinPalette> entries = builtin::palettes(group);
        if (entries.isEmpty())
            continue;
        QMenu* sub = m_bundledMenu->addMenu(builtin::groupName(group));
        sub->setStatusTip(builtin::groupHint(group));
        for (const builtin::BuiltinPalette& entry : entries) {
            const QString name = entry.name;
            auto* a = sub->addAction(tr("%1 (%2 colors)").arg(name).arg(entry.palette.size()), this,
                                     [this, name] { loadBuiltinPalette(name); });
            a->setStatusTip(tr("Builtin palette \"%1\"").arg(name));
        }
    }

    m_paletteFromImageMenu = new QMenu(tr("From image"), this);
    auto* actChunks = m_paletteFromImageMenu->addAction(tr("Average colors on a grid (chunks)…"), this,
                                                         &MainWindow::paletteFromChunks);
    actChunks->setStatusTip(tr("Split the image into a grid and take the average color of every cell"));
    auto* actKMeans = m_paletteFromImageMenu->addAction(tr("Characteristic colors (k-means)…"), this,
                                                         &MainWindow::paletteFromKMeans);
    actKMeans->setStatusTip(tr("Find the given number of colors that best describe the image"));
    auto* actUnique = m_paletteFromImageMenu->addAction(tr("All unique colors"), this,
                                                         &MainWindow::paletteFromUniqueColors);
    actUnique->setStatusTip(tr("Collect all distinct colors of the image (up to 256)"));
    m_paletteFromImageMenu->addSeparator();
    auto* actReference = m_paletteFromImageMenu->addAction(tr("From another image…"), this,
                                                           &MainWindow::paletteFromReferenceImage);
    actReference->setStatusTip(tr("Take the characteristic colors of a reference file to repaint into its look"));
    m_imageActions << actChunks << actKMeans << actUnique;
    m_processingActions << actChunks << actKMeans << actUnique << actReference;

    m_palettePanel->setExternalActions(m_actLoadPalette, m_actSavePalette, m_paletteFromImageMenu, m_bundledMenu);

    // --- Effects: toolbar button ---
    m_effectsMenu = new QMenu(tr("Effects"), this);
    struct EffectEntry {
        QString text;
        QString icon;
        QString tip;
        void (MainWindow::*slot)();
        bool separatorBefore = false;
        QKeySequence shortcut;
    };
    const EffectEntry entries[] = {
        {tr("Invert"), "invert", tr("Invert the colors"), &MainWindow::effectInvert},
        {tr("Grayscale"), "sun", tr("Remove color, keep luminance"), &MainWindow::effectGrayscale},
        {tr("Sepia"), "sun", tr("The warm brown tone of an old photograph"), &MainWindow::effectSepia},
        {tr("Posterize…"), "sliders", tr("Reduce the number of levels in every channel"), &MainWindow::effectPosterize, true},
        {tr("Pixelate…"), "pixels", tr("Enlarge the pixels"), &MainWindow::effectPixelate},
        {tr("Color correction…"), "sliders", tr("Brightness, contrast, saturation"), &MainWindow::effectAdjust},
        {tr("Levels…"), "sliders", tr("Black and white points, gamma and per-channel shadow tint"), &MainWindow::effectLevels},
        {tr("Color splash…"), "hue", tr("Keep one hue range in color, turn the rest gray"), &MainWindow::effectColorSplash},
        {tr("Gradient map…"), "hue", tr("Tone by luminance with the colors of the current palette"), &MainWindow::effectGradientMap},
        {tr("Duotone / tritone…"), "theme", tr("Two or three colors by luminance: shadows, midtones, highlights"), &MainWindow::effectDuotone},
        {tr("Remove artifacts…"), "wand", tr("Remove noise and JPEG compression artifacts while keeping edges"), &MainWindow::effectDenoise},
        {tr("Halftone…"), "grid", tr("Break the image into CMYK print dots"), &MainWindow::effectHalftone},
        {tr("Color table (.cube)…"), "palette", tr("Apply a 3D LUT from DaVinci, Premiere or Photoshop"), &MainWindow::effectLut},
        {tr("Save .cube from result…"), "download", tr("Turn the repaint you got into a 3D LUT for a video editor"), &MainWindow::exportLut},
        {tr("Pixel art…"), "pixels", tr("Big pixels, limited colors and outlined shapes"), &MainWindow::effectPixelArt, true},
        {tr("Retro console…"), "grid", tr("Game Boy, NES or ZX Spectrum: resolution, palette and console limits"), &MainWindow::effectConsole},
        {tr("CRT television…"), "actual", tr("Curved screen, phosphor mask, scanlines, glow"), &MainWindow::effectCrt},
        {tr("Disc…"), "disc", tr("Put the image onto a CD/DVD with a hole in the middle"), &MainWindow::effectDisc},
        {tr("VHS camcorder…"), "vhs", tr("Interference, aberration and captions of an old videotape"), &MainWindow::effectVhs, false,
         QKeySequence(Qt::CTRL | Qt::Key_H)},
    };
    for (const EffectEntry& e : entries) {
        if (e.separatorBefore)
            m_effectsMenu->addSeparator();
        QAction* a = makeAction(e.text, e.icon, e.shortcut, e.tip, [this, slot = e.slot] { (this->*slot)(); });
        m_effectsMenu->addAction(a);
        m_imageActions << a;
        m_processingActions << a;
    }

    // --- "More": theme, language, panels, help, exit ---
    m_moreMenu = new QMenu(tr("More"), this);
    QMenu* themeMenu = m_moreMenu->addMenu(tr("Theme"));
    m_iconNames.insert(themeMenu->menuAction(), QStringLiteral("theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    for (theme::Mode mode : {theme::Mode::Dark, theme::Mode::Light, theme::Mode::System}) {
        QAction* a = themeMenu->addAction(theme::modeName(mode));
        a->setCheckable(true);
        a->setData(int(mode));
        a->setChecked(mode == theme::current());
        m_themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode] { setTheme(mode); });
    }
    // Language names are written in the language itself, so anyone can find their own
    QMenu* languageMenu = m_moreMenu->addMenu(tr("Language"));
    m_iconNames.insert(languageMenu->menuAction(), QStringLiteral("language"));
    auto* languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);
    QStringList codes{QString()};
    codes << language::available();
    for (const QString& code : codes) {
        QAction* a = languageMenu->addAction(code.isEmpty() ? tr("System default") : language::nativeName(code));
        a->setCheckable(true);
        a->setChecked(code == language::preferred());
        languageGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, code] { setLanguage(code); });
        if (code.isEmpty())
            languageMenu->addSeparator();
    }
    QAction* repaintDockAction = m_repaintDock->toggleViewAction();
    repaintDockAction->setText(tr("Repaint panel"));
    m_moreMenu->addAction(repaintDockAction);
    QAction* historyDockAction = m_historyDock->toggleViewAction();
    historyDockAction->setText(tr("History panel"));
    m_moreMenu->addAction(historyDockAction);
    QAction* paletteDockAction = m_paletteDock->toggleViewAction();
    paletteDockAction->setText(tr("Palette panel"));
    m_moreMenu->addAction(paletteDockAction);
    m_moreMenu->addSeparator();
    m_actCheckUpdates = m_moreMenu->addAction(tr("Check for updates…"), this, &MainWindow::checkForUpdates);
    m_iconNames.insert(m_actCheckUpdates, QStringLiteral("download"));
    QAction* checkOnStartup = m_moreMenu->addAction(tr("Check for updates at startup"));
    checkOnStartup->setCheckable(true);
    checkOnStartup->setChecked(updates::checkOnStartup());
    connect(checkOnStartup, &QAction::toggled, this, [](bool on) { updates::setCheckOnStartup(on); });
    QAction* about = m_moreMenu->addAction(tr("About Kolorit"), this, &MainWindow::showAbout);
    m_iconNames.insert(about, QStringLiteral("info"));
    m_moreMenu->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
    m_moreMenu->addSeparator();
    m_moreMenu->addAction(m_actExit);

    // There is no menu bar, so the shortcuts of all actions are registered on the window
    for (QAction* a : {m_actOpen, m_actPaste, m_actSave, m_actExit, m_actRepaint, m_actCancel, m_actShowOriginal,
                       m_actRevert, m_actFit, m_actActual, m_actZoomIn, m_actZoomOut, m_actLoadPalette,
                       m_actSavePalette})
        addAction(a);
    addActions(m_effectsMenu->actions());
}

void MainWindow::buildToolbar()
{
    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // The only top bar: file | comparison and operation chain | cancel … effects, more
    toolbar->addAction(m_actOpen);
    toolbar->addAction(m_actPaste);
    toolbar->addAction(m_actSave);
    toolbar->addSeparator();
    toolbar->addAction(m_actShowOriginal);
    toolbar->addAction(m_actRevert);
    toolbar->addSeparator();
    toolbar->addAction(m_actCancel);

    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    m_effectsButton = menuToolButton(tr("Effects"), m_effectsMenu, toolbar);
    m_effectsButton->setToolTip(tr("Apply an effect to the image"));
    toolbar->addWidget(m_effectsButton);

    m_moreButton = menuToolButton(QString(), m_moreMenu, toolbar);
    m_moreButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_moreButton->setToolTip(tr("Theme, language, panels, about"));
    toolbar->addWidget(m_moreButton);
}

void MainWindow::buildStatusBar()
{
    m_statusImage = new QLabel(this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setFixedWidth(200);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);

    // Number of worker threads in the pool, adjustable right from the status bar
    auto* threadsLabel = new QLabel(tr("Threads:"), this);
    m_threads = new QSpinBox(this);
    m_threads->setRange(1, 128);
    m_threads->setValue(QThread::idealThreadCount());
    m_threads->setToolTip(tr("Number of worker threads for processing (by default the number of CPU cores: %1)")
                              .arg(QThread::idealThreadCount()));
    connect(m_threads, &QSpinBox::valueChanged, this, [](int value) {
        QThreadPool::globalInstance()->setMaxThreadCount(value);
    });

    // Zoom: a button with a menu of quick values
    m_zoomButton = new QToolButton(this);
    m_zoomButton->setText(tr("100 %"));
    m_zoomButton->setAutoRaise(true);
    m_zoomButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_zoomButton->setPopupMode(QToolButton::InstantPopup);
    m_zoomButton->setToolTip(tr("View zoom"));
    auto* zoomMenu = new QMenu(m_zoomButton);
    zoomMenu->addAction(m_actFit);
    zoomMenu->addAction(m_actActual);
    zoomMenu->addSeparator();
    for (int percent : {50, 200, 400}) {
        zoomMenu->addAction(tr("%1 %").arg(percent), this, [this, percent] { m_view->setZoom(percent / 100.0); });
    }
    zoomMenu->addSeparator();
    zoomMenu->addAction(m_actZoomIn);
    zoomMenu->addAction(m_actZoomOut);
    m_zoomButton->setMenu(zoomMenu);

    // Hints and progress messages on the left, permanent information on the right
    m_statusImage->setObjectName(QStringLiteral("hintLabel"));
    statusBar()->addPermanentWidget(m_statusImage);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->addPermanentWidget(threadsLabel);
    statusBar()->addPermanentWidget(m_threads);
    statusBar()->addPermanentWidget(m_zoomButton);
    statusBar()->showMessage(tr("Open an image to start"));
}

void MainWindow::refreshIcons()
{
    for (auto it = m_iconNames.cbegin(); it != m_iconNames.cend(); ++it)
        it.key()->setIcon(theme::icon(it.value()));
    if (m_effectsButton)
        m_effectsButton->setIcon(theme::icon(QStringLiteral("effects")));
    if (m_moreButton)
        m_moreButton->setIcon(theme::icon(QStringLiteral("more")));
    if (m_zoomButton)
        m_zoomButton->setIcon(theme::icon(QStringLiteral("zoom-in")));
    m_palettePanel->refreshIcons();
    m_repaintPanel->refreshIcons();
}

void MainWindow::setTheme(theme::Mode mode)
{
    theme::apply(mode);
    refreshIcons();
    for (QAction* a : m_themeGroup->actions())
        a->setChecked(theme::Mode(a->data().toInt()) == mode);
    QSettings().setValue("ui/theme", int(mode));
}

void MainWindow::setLanguage(const QString& code)
{
    const QString before = language::resolved();
    language::setPreferred(code);
    if (language::resolved() == before)
        return;

    // Translations are loaded before any window is built, so switching needs a restart
    const auto answer = QMessageBox::question(
        this, tr("Language"),
        tr("The new language takes effect after a restart. Restart Kolorit now?\n"
           "The current image and its history will be closed."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), {}))
        close();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    // Layout version: when the set of panels changes, the old state is discarded
    restoreState(s.value("window/state").toByteArray(), kLayoutVersion);

    m_lastImageDir = s.value("dirs/images", AppPaths::imagesDir()).toString();
    m_lastSaveDir = s.value("dirs/results", AppPaths::resultsDir()).toString();
    m_lastPaletteDir = s.value("dirs/palettes", AppPaths::palettesDir()).toString();

    RepaintParams params;
    params.model = ColorModel(s.value("repaint/model", int(ColorModel::RGB)).toInt());
    params.dithering = Dithering(s.value("repaint/dithering", int(Dithering::None)).toInt());
    params.ditherStrength = s.value("repaint/ditherStrength", 32).toInt();
    m_repaintPanel->setParams(params);

    const int threads = s.value("processing/threads", QThread::idealThreadCount()).toInt();
    m_threads->setValue(std::clamp(threads, 1, 128));
    QThreadPool::globalInstance()->setMaxThreadCount(m_threads->value());

    m_vhsParams = VhsParams::fromVariantMap(s.value("vhs/params").toMap());
    m_chunkColumns = s.value("palette/chunkColumns", 8).toInt();
    m_chunkRows = s.value("palette/chunkRows", 8).toInt();
    m_generatorState = s.value("palette/generator").toMap();

    const QString lastPalette = s.value("palette/last").toString();
    if (!lastPalette.isEmpty()) {
        const Palette palette = Palette::load(lastPalette);
        if (!palette.isEmpty())
            m_palettePanel->setColors(palette);
    }
    m_repaintPanel->setPaletteSize(m_palettePanel->colors().size());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState(kLayoutVersion));
    s.setValue("dirs/images", m_lastImageDir);
    s.setValue("dirs/results", m_lastSaveDir);
    s.setValue("dirs/palettes", m_lastPaletteDir);
    const RepaintParams params = m_repaintPanel->params();
    s.setValue("repaint/model", int(params.model));
    s.setValue("repaint/dithering", int(params.dithering));
    s.setValue("repaint/ditherStrength", params.ditherStrength);
    s.setValue("processing/threads", m_threads->value());
    s.setValue("vhs/params", m_vhsParams.toVariantMap());
    s.setValue("palette/chunkColumns", m_chunkColumns);
    s.setValue("palette/chunkRows", m_chunkRows);
    s.setValue("palette/generator", m_generatorState);
    s.setValue("ui/theme", int(theme::current()));

    // The current palette is kept in the user profile and restored at startup
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString autosave = QDir(dataDir).filePath(QStringLiteral("last_palette.png"));
    if (m_palettePanel->colors().isEmpty()) {
        QFile::remove(autosave);
        s.remove("palette/last");
    } else if (m_palettePanel->colors().save(autosave)) {
        s.setValue("palette/last", autosave);
    }
}

// ---------------------------------------------------------------------------
// Checks and state
// ---------------------------------------------------------------------------

bool MainWindow::ensureImageLoaded()
{
    if (!m_source.isNull())
        return true;
    QMessageBox::warning(this, captionError(), tr("Open an image first."));
    return false;
}

bool MainWindow::ensurePaletteNotEmpty(const QString& message)
{
    if (!m_palettePanel->colors().isEmpty())
        return true;
    QMessageBox::warning(this, captionError(), message);
    return false;
}

bool MainWindow::ensureIdle()
{
    if (m_previewDialog) {
        m_previewDialog->raise();
        m_previewDialog->activateWindow();
        QMessageBox::information(this, tr("An effect is being set up"),
                                 tr("Apply or cancel the \"%1\" effect first.").arg(m_previewDialog->windowTitle()));
        return false;
    }
    if (!m_runner.isRunning())
        return true;
    QMessageBox::information(this, tr("Processing"),
                             tr("Wait for the current operation to finish or cancel it (Esc)."));
    return false;
}

void MainWindow::setSourceImage(const QImage& image, const QString& name)
{
    // The file's profile is taken into account here, at the single entry point of images:
    // from here on all processing treats colors as sRGB values
    QString profile;
    const QImage converted = colorspace::toSrgb(image, &profile);
    m_original = converted.convertToFormat(QImage::Format_ARGB32);
    m_source = m_original;
    m_sourceName = name;
    // A new image starts the history over, with the file itself as step zero
    m_history.clear();
    m_historyIndex = 0;
    m_history.append(HistoryState{tr("Open: %1").arg(name), QStringLiteral("open"), m_original.size(), {}});
    m_historyPanel->setStates(m_history, m_historyIndex);
    m_actShowOriginal->setChecked(false);
    m_view->setImage(m_source);
    m_view->fitToWindow();
    setWindowTitle(tr("%1 — Kolorit").arg(name));
    updateActions();
    updateStatus();
    if (profile.isEmpty()) {
        statusBar()->showMessage(tr("Loaded: %1 (%2×%3). Now pick a palette and press \"Repaint\" "
                                    "or apply an effect")
                                     .arg(name).arg(image.width()).arg(image.height()), 8000);
    } else {
        statusBar()->showMessage(tr("Loaded: %1 (%2×%3). Colors converted from \"%4\" to sRGB")
                                     .arg(name).arg(image.width()).arg(image.height()).arg(profile), 8000);
    }
}

void MainWindow::updateView()
{
    if (m_source.isNull()) {
        m_view->clear();
        return;
    }
    if (m_actShowOriginal->isChecked()) {
        // The comparison is against history step zero, the original file
        m_view->setImage(m_original);
        return;
    }
    // While an effect is being set up, its preview is shown in the full image's coordinates
    if (m_previewDialog && !m_previewImage.isNull()) {
        m_view->setImage(m_previewImage, m_previewLogicalSize);
        return;
    }
    m_view->setImage(m_source);
}

void MainWindow::updateActions()
{
    const bool hasImage = !m_source.isNull();
    const bool running = m_runner.isRunning();
    const bool previewing = !m_previewDialog.isNull();
    const bool busy = running || previewing;
    // First restore the base state, then block everything unneeded while processing
    // or setting up an effect; viewing (zoom, comparison with the original) stays available
    for (QAction* a : m_processingActions)
        a->setEnabled(true);
    for (QAction* a : m_imageActions)
        a->setEnabled(hasImage);
    if (busy) {
        for (QAction* a : m_processingActions)
            a->setEnabled(false);
    }
    m_actSave->setEnabled(hasImage && !previewing);
    m_actRevert->setEnabled(hasImage && !busy && m_historyIndex > 0);
    m_actShowOriginal->setEnabled(hasImage && (m_historyIndex > 0 || previewing));
    m_actUndo->setEnabled(hasImage && !busy && m_historyIndex > 0);
    m_actRedo->setEnabled(hasImage && !busy && m_historyIndex + 1 < m_history.size());
    m_actCancel->setEnabled(running);
    m_actCancel->setVisible(running);
    m_repaintPanel->setImageLoaded(hasImage);
    m_repaintPanel->setBusy(busy);
}

void MainWindow::updateStatus()
{
    if (m_source.isNull()) {
        m_statusImage->setText(tr("No image loaded"));
        return;
    }
    QString state;
    if (m_previewDialog)
        state = tr("setting up an effect");
    else if (m_historyIndex > 0)
        state = m_actShowOriginal->isChecked()
                    ? tr("showing the original")
                    : tr("step %1 of %2").arg(m_historyIndex + 1).arg(m_history.size());
    else
        state = tr("original");
    m_statusImage->setText(tr("%1 · %2×%3 · %4   ").arg(m_sourceName).arg(m_source.width()).arg(m_source.height()).arg(state));
}

QString MainWindow::imageFileFilter() const
{
    QStringList extensions;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        extensions << QStringLiteral("*.") + QString::fromLatin1(format);
    return tr("Images (%1);;All files (*.*)").arg(extensions.join(QLatin1Char(' ')));
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

bool MainWindow::openImageFile(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, captionError(), tr("Could not open \"%1\":\n%2").arg(path, reader.errorString()));
        return false;
    }
    m_lastImageDir = QFileInfo(path).absolutePath();
    setSourceImage(image, QFileInfo(path).fileName());
    return true;
}

void MainWindow::openImage()
{
    if (!ensureIdle())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open image"), m_lastImageDir, imageFileFilter());
    if (!path.isEmpty())
        openImageFile(path);
}

void MainWindow::pasteImage()
{
    if (!ensureIdle())
        return;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (mime && mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            setSourceImage(image, tr("Clipboard"));
            return;
        }
    }
    if (mime && mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile() && openImageFile(url.toLocalFile()))
                return;
        }
    }
    QMessageBox::warning(this, captionError(), tr("There is no image on the clipboard."));
}

void MainWindow::saveResult()
{
    if (m_source.isNull()) {
        QMessageBox::warning(this, captionError(), tr("Open an image first."));
        return;
    }
    const QString baseName = QFileInfo(m_sourceName).completeBaseName();
    const QString suggested = QDir(m_lastSaveDir).filePath((baseName.isEmpty() ? QStringLiteral("image") : baseName)
                                                           + QStringLiteral("_kolorit.png"));
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("Save result"), suggested,
                                                tr("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;WebP (*.webp)"),
                                                &selectedFilter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty()) {
        if (selectedFilter.startsWith("JPEG"))      path += ".jpg";
        else if (selectedFilter.startsWith("BMP"))  path += ".bmp";
        else if (selectedFilter.startsWith("WebP")) path += ".webp";
        else                                        path += ".png";
    }

    QImage toSave = m_source;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "jpg" || suffix == "jpeg" || suffix == "bmp")
        toSave = toSave.convertToFormat(QImage::Format_RGB32);   // formats without transparency

    QImageWriter writer(path);
    if (suffix == "jpg" || suffix == "jpeg")
        writer.setQuality(95);
    if (!writer.write(toSave)) {
        QMessageBox::warning(this, captionError(), tr("Could not save the file:\n%1").arg(writer.errorString()));
        return;
    }
    m_lastSaveDir = QFileInfo(path).absolutePath();
    statusBar()->showMessage(tr("Saved: %1").arg(path), 5000);
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

void MainWindow::loadPaletteFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load palette"), m_lastPaletteDir, imageFileFilter());
    if (path.isEmpty())
        return;
    QString error;
    const Palette palette = Palette::load(path, &error);
    if (palette.isEmpty()) {
        QMessageBox::warning(this, captionError(), tr("Could not load the palette:\n%1").arg(error));
        return;
    }
    m_lastPaletteDir = QFileInfo(path).absolutePath();
    m_palettePanel->setColors(palette);
    statusBar()->showMessage(tr("Palette loaded: %1 colors").arg(palette.size()), 5000);
}

void MainWindow::savePalette()
{
    if (!ensurePaletteNotEmpty(tr("The palette is empty, there is nothing to save.")))
        return;
    QString path = QFileDialog::getSaveFileName(this, tr("Save palette"),
                                                QDir(m_lastPaletteDir).filePath(QStringLiteral("palette.png")),
                                                tr("PNG (*.png)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".png";
    QString error;
    if (!m_palettePanel->colors().save(path, &error)) {
        QMessageBox::warning(this, captionError(), error);
        return;
    }
    m_lastPaletteDir = QFileInfo(path).absolutePath();
    statusBar()->showMessage(tr("Palette saved: %1").arg(path), 5000);
}

void MainWindow::loadBuiltinPalette(const QString& name)
{
    const Palette palette = builtin::byName(name);
    if (palette.isEmpty())
        return;
    m_palettePanel->setColors(palette);
    statusBar()->showMessage(tr("Palette \"%1\": %2 colors").arg(name).arg(palette.size()), 5000);
}

void MainWindow::paletteGenerate()
{
    const QVariantMap& g = m_generatorState;
    PaletteGeneratorDialog dialog(this);
    dialog.setState(g.value("kind", 0).toInt(), g.value("grayCount", 8).toInt(),
                    g.value("grayPerceptual", false).toBool(), g.value("tintColor").value<QColor>(),
                    g.value("tintCount", 8).toInt(), g.value("rampFrom").value<QColor>(),
                    g.value("rampMid").value<QColor>(), g.value("rampTo").value<QColor>(),
                    g.value("rampUseMid", false).toBool(), g.value("rampCount", 12).toInt(),
                    g.value("levels", 4).toInt(), g.value("wheelCount", 12).toInt(),
                    g.value("wheelLightness", 60.0).toDouble(), g.value("wheelChroma", 45.0).toDouble());
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_generatorState = QVariantMap{
        {"kind", dialog.kind()},
        {"grayCount", dialog.grayCount()},
        {"grayPerceptual", dialog.grayPerceptual()},
        {"tintColor", dialog.tintColor()},
        {"tintCount", dialog.tintCount()},
        {"rampFrom", dialog.rampFrom()},
        {"rampMid", dialog.rampMid()},
        {"rampTo", dialog.rampTo()},
        {"rampUseMid", dialog.rampUseMid()},
        {"rampCount", dialog.rampCount()},
        {"levels", dialog.levels()},
        {"wheelCount", dialog.wheelCount()},
        {"wheelLightness", dialog.wheelLightness()},
        {"wheelChroma", dialog.wheelChroma()},
    };

    const Palette palette = dialog.palette();
    m_palettePanel->setColors(palette);
    statusBar()->showMessage(tr("Palette created: %1 colors").arg(palette.size()), 5000);
}

void MainWindow::paletteFromChunks()
{
    if (!ensureImageLoaded())
        return;
    ChunkPaletteDialog dialog(m_source, this);
    dialog.setGrid(m_chunkColumns, m_chunkRows);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_chunkColumns = dialog.columns();
    m_chunkRows = dialog.rows();
    m_palettePanel->setColors(dialog.palette());
    statusBar()->showMessage(tr("Palette from a %1×%2 grid: %3 colors").arg(m_chunkColumns).arg(m_chunkRows).arg(dialog.palette().size()), 5000);
}

void MainWindow::paletteFromKMeans()
{
    if (!ensureImageLoaded())
        return;
    KMeansPaletteDialog dialog(m_source, this);
    if (dialog.exec() != QDialog::Accepted || dialog.palette().isEmpty())
        return;
    m_palettePanel->setColors(dialog.palette());
    statusBar()->showMessage(tr("k-means palette: %1 colors").arg(dialog.palette().size()), 5000);
}

void MainWindow::paletteFromUniqueColors()
{
    if (!ensureImageLoaded())
        return;
    const Palette palette = palette_builder::uniqueColors(m_source, 256);
    if (palette.size() >= 256) {
        const auto answer = QMessageBox::question(
            this, tr("Too many colors"),
            tr("The image has more than 256 unique colors. Take the first 256?\n"
               "For photos, a k-means palette works better."));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_palettePanel->setColors(palette);
    statusBar()->showMessage(tr("Unique colors: %1").arg(palette.size()), 5000);
}

// ---------------------------------------------------------------------------
// Processing
// ---------------------------------------------------------------------------

void MainWindow::runOp(const QString& title, ImageOp op, const QString& icon)
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    m_taskTitle = title;
    m_taskIcon = icon;
    // The whole operation is remembered: the .cube export is built from it
    m_lastOp = op;
    m_runner.start(m_source, std::move(op));
}

void MainWindow::runPreviewDialog(PreviewDialog* dialog, const QString& title, const QString& icon)
{
    m_previewDialog = dialog;
    m_previewImage = QImage();
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    connect(dialog, &PreviewDialog::previewReady, this, [this, dialog](const QImage& image, bool fullResolution) {
        // The downscaled frame is shown at the size it would take at full resolution;
        // an effect may change the size (the Disc makes a square), so it is computed from the frame itself
        const double scale = fullResolution ? 1.0 : dialog->previewScale();
        m_previewImage = image;
        m_previewLogicalSize = QSize(int(std::lround(image.width() / scale)), int(std::lround(image.height() / scale)));
        if (!m_actShowOriginal->isChecked())
            m_view->setImage(image, m_previewLogicalSize);
        const QString name = m_previewDialog ? m_previewDialog->windowTitle() : QString();
        statusBar()->showMessage(fullResolution
                                     ? tr("%1: full-resolution preview. Wheel to zoom, "
                                          "drag to pan, Ctrl+B to compare with the original").arg(name)
                                     : tr("%1: fast preview, computing the full size…").arg(name));
    });

    connect(dialog, &QDialog::finished, this, [this, dialog, title, icon](int code) {
        m_previewDialog = nullptr;
        m_previewImage = QImage();
        m_previewLogicalSize = QSize();
        if (code == QDialog::Accepted) {
            // If the full-size frame is already computed for the current parameters, it is used as is
            const QImage ready = dialog->takeFullResult();
            if (!ready.isNull()) {
                m_taskTitle = title;
                m_taskIcon = icon;
                m_lastOp = dialog->finalOp();
                onTaskFinished(ready, dialog->fullResultElapsedMs());
            } else {
                runOp(title, dialog->finalOp(), icon);
            }
        } else {
            updateView();
            statusBar()->showMessage(tr("%1: cancelled").arg(title), 3000);
        }
        updateActions();
        updateStatus();
    });

    dialog->show();
    updateActions();
    updateStatus();
}

void MainWindow::paletteFromReferenceImage()
{
    if (!ensureIdle())
        return;

    // A palette can be taken from any file, not only from the open photo: this is the
    // "make this shot in the look of that one" technique: the palette comes from the
    // reference and is applied to the current image
    const QString path = QFileDialog::getOpenFileName(this, tr("Reference image"), m_lastImageDir,
                                                      imageFileFilter());
    if (path.isEmpty())
        return;
    m_lastImageDir = QFileInfo(path).absolutePath();

    QImage reference(path);
    if (reference.isNull()) {
        QMessageBox::warning(this, tr("Could not open the file"),
                             tr("\"%1\" could not be read as an image.").arg(QFileInfo(path).fileName()));
        return;
    }
    reference = colorspace::toSrgb(reference).convertToFormat(QImage::Format_ARGB32);

    KMeansPaletteDialog dialog(reference, this);
    dialog.setWindowTitle(tr("Palette from reference: %1").arg(QFileInfo(path).fileName()));
    if (dialog.exec() != QDialog::Accepted || dialog.palette().isEmpty())
        return;
    m_palettePanel->setColors(dialog.palette());
    statusBar()->showMessage(tr("Palette taken from \"%1\": %2 colors")
                                 .arg(QFileInfo(path).fileName())
                                 .arg(dialog.palette().size()),
                             6000);
}

void MainWindow::pushHistory(const QString& title, const QString& icon, ImageOp op, const QImage& image)
{
    if (image.isNull())
        return;

    // Steps after the current one are dropped: a new operation starts a new branch,
    // and there is no non-linear history here, as in Photoshop by default
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();

    m_history.append(HistoryState{title, icon, image.size(), std::move(op)});
    // Step zero is never dropped: the whole chain is computed from it
    while (m_history.size() > kMaxHistory)
        m_history.removeAt(1);
    m_historyIndex = m_history.size() - 1;

    m_source = image;
    m_historyPanel->setStates(m_history, m_historyIndex);
    updateView();
    updateActions();
    updateStatus();
}

ImageOp MainWindow::historyChain(int from, int to) const
{
    QList<ImageOp> ops;
    for (int i = from + 1; i <= to && i < m_history.size(); ++i)
        if (m_history.at(i).op)
            ops.append(m_history.at(i).op);

    return [ops](const QImage& source, QPromise<QImage>& promise) -> QImage {
        QImage current = source;
        // The operations get the same promise rather than a nested one: only then does Esc
        // stop the recomputation inside a long step instead of between steps. The price is
        // that the progress bar runs its 0..100 separately on every step.
        for (const ImageOp& op : ops) {
            if (promise.isCanceled())
                return {};
            current = op(current, promise);
            if (current.isNull())
                return {};
        }
        return current;
    };
}

void MainWindow::gotoHistoryState(int index)
{
    if (index < 0 || index >= m_history.size())
        return;
    if (m_previewDialog || m_runner.isRunning()) {
        // Nowhere to jump while a computation is running. A modal box here would punish
        // an impatient click, so the selection is simply put back
        m_historyPanel->setStates(m_history, m_historyIndex);
        statusBar()->showMessage(tr("Wait for the current operation to finish or cancel it (Esc)"), 4000);
        return;
    }
    if (index == m_historyIndex)
        return;

    m_actShowOriginal->setChecked(false);

    // Step zero is available ready-made, there is nothing to recompute
    if (index == 0) {
        m_historyIndex = 0;
        m_source = m_original;
        m_historyPanel->setStates(m_history, m_historyIndex);
        updateView();
        updateActions();
        updateStatus();
        statusBar()->showMessage(tr("Step 1 of %1: %2").arg(m_history.size()).arg(m_history.first().title), 4000);
        return;
    }

    // Going forward starts from the current image: exactly the missing operations;
    // going back has to compute the chain from the original
    const bool forward = index > m_historyIndex;
    const QImage base = forward ? m_source : m_original;
    const int from = forward ? m_historyIndex : 0;

    m_pendingHistoryIndex = index;
    m_taskTitle = tr("Step %1: %2").arg(index + 1).arg(m_history.at(index).title);
    m_taskIcon = m_history.at(index).icon;
    m_runner.start(base, historyChain(from, index));
}

void MainWindow::undo()
{
    gotoHistoryState(m_historyIndex - 1);
}

void MainWindow::redo()
{
    gotoHistoryState(m_historyIndex + 1);
}

void MainWindow::revertToOriginal()
{
    gotoHistoryState(0);
}

void MainWindow::repaintImage()
{
    if (!ensureImageLoaded() ||
        !ensurePaletteNotEmpty(tr("The palette is empty. Load one from a file, pick a builtin one or create one from the image.")))
        return;
    const RepaintParams params = m_repaintPanel->params();
    runOp(tr("Repaint (%1, %2)").arg(colorspace::modelName(params.model), ditheringName(params.dithering)),
          PaletteMapper::makeOp(m_palettePanel->colors(), params), QStringLiteral("repaint"));
}

void MainWindow::cancelProcessing()
{
    m_runner.cancel();
}

void MainWindow::onTaskStarted()
{
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_view->setOverlayText(tr("%1…").arg(m_taskTitle));
    statusBar()->showMessage(tr("%1… (Esc to cancel)").arg(m_taskTitle));
    updateActions();
}

void MainWindow::onTaskProgress(int percent)
{
    m_progress->setValue(percent);
    m_view->setOverlayText(tr("%1… %2 %").arg(m_taskTitle).arg(percent));
}

void MainWindow::onTaskFinished(const QImage& result, qint64 elapsedMs)
{
    m_progress->setVisible(false);
    m_view->setOverlayText(QString());
    m_actShowOriginal->setChecked(false);
    if (m_pendingHistoryIndex >= 0) {
        // A chain recomputation arrived: just move to the step, do not add a new one
        m_historyIndex = m_pendingHistoryIndex;
        m_pendingHistoryIndex = -1;
        m_source = result;
        m_historyPanel->setStates(m_history, m_historyIndex);
        updateView();
        updateActions();
        updateStatus();
    } else {
        // A finished operation immediately becomes a history step and the current image
        pushHistory(m_taskTitle, m_taskIcon, m_lastOp, result);
    }
    statusBar()->showMessage(tr("%1: done in %2 s (%3 threads). Ctrl+B to compare with the original, Ctrl+S to save")
                                 .arg(m_taskTitle)
                                 .arg(elapsedMs / 1000.0, 0, 'f', 2)
                                 .arg(QThreadPool::globalInstance()->maxThreadCount()));
}

void MainWindow::onTaskCanceled()
{
    m_pendingHistoryIndex = -1;
    m_historyPanel->setStates(m_history, m_historyIndex);
    m_progress->setVisible(false);
    m_view->setOverlayText(QString());
    updateView();
    updateActions();
    updateStatus();
    statusBar()->showMessage(tr("%1: cancelled").arg(m_taskTitle), 5000);
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

void MainWindow::effectInvert()
{
    runOp(tr("Invert"), effects::invert());
}

void MainWindow::effectGrayscale()
{
    runOp(tr("Grayscale"), effects::grayscale());
}

void MainWindow::effectSepia()
{
    runOp(tr("Sepia"), effects::sepia());
}

void MainWindow::effectPosterize()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    auto* dialog = new EffectParamsDialog(tr("Posterize"), m_source,
                                          {ParamSpec{"levels", tr("Levels per channel"), 2, 64, 4, 1}},
                                          [](const QVariantMap& v) { return effects::posterize(v["levels"].toInt()); }, this);
    dialog->setDescription(tr("The number of shades in every channel is reduced to the given one, and the image turns poster-like."));
    dialog->enablePresets(QStringLiteral("posterize"));
    runPreviewDialog(dialog, tr("Posterize"));
}

void MainWindow::effectPixelate()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    auto* dialog = new EffectParamsDialog(tr("Pixelate"), m_source,
                                          {ParamSpec{"block", tr("Block size"), 1, 256, 8, 1, 0, true, tr(" px")}},
                                          [](const QVariantMap& v) { return effects::pixelate(v["block"].toInt()); }, this);
    dialog->setDescription(tr("The image is split into squares, each filled with its average color."));
    dialog->enablePresets(QStringLiteral("pixelate"));
    runPreviewDialog(dialog, tr("Pixelate"));
}

void MainWindow::effectAdjust()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    auto* dialog = new EffectParamsDialog(tr("Color correction"), m_source,
                                          {ParamSpec{"brightness", tr("Brightness"), -100, 100, 0, 1},
                                           ParamSpec{"contrast", tr("Contrast"), -100, 100, 0, 1},
                                           ParamSpec{"saturation", tr("Saturation"), -100, 100, 0, 1}},
                                          [](const QVariantMap& v) {
                                              return effects::adjust(v["brightness"].toDouble(), v["contrast"].toDouble(),
                                                                     v["saturation"].toDouble());
                                          },
                                          this);
    dialog->enablePresets(QStringLiteral("adjust"));
    runPreviewDialog(dialog, tr("Color correction"));
}

void MainWindow::effectGradientMap()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;

    const QList<ParamSpec> specs = {
        ParamSpec::checkbox("smooth", tr("Smooth transitions between colors"), true),
        ParamSpec::checkbox("reverse", tr("Palette in reverse order"), false),
        ParamSpec{"balance", tr("Midtone balance"), -100, 100, 0, 1},
        ParamSpec{"opacity", tr("Strength"), 0, 100, 100, 1, 0, false, tr(" %")},
    };

    // The palette is read from the panel at computation time, so it can change without closing the dialog
    auto* dialog = new EffectParamsDialog(tr("Gradient map"), m_source, specs,
                                          [this](const QVariantMap& v) {
                                              effects::GradientMapParams p;
                                              p.palette = m_palettePanel->colors();
                                              p.smooth = v["smooth"].toBool();
                                              p.reverse = v["reverse"].toBool();
                                              p.balance = v["balance"].toDouble() / 100.0;
                                              p.opacity = v["opacity"].toDouble() / 100.0;
                                              return effects::gradientMap(p);
                                          },
                                          this);
    auto describe = [this, dialog] {
        const int count = m_palettePanel->colors().size();
        dialog->setDescription(count > 0
                                   ? tr("The luminance of every pixel becomes a position on a gradient made of the colors "
                                        "of the current palette (%1 colors): the first color is the shadows, the last the highlights. "
                                        "The order of colors can be changed on the Palette panel without closing the dialog. "
                                        "Two or three colors give a duotone, a colormap gives a heat map.")
                                         .arg(count)
                                   : tr("The palette is empty, so a black-to-white gradient is used. Add colors "
                                        "on the Palette panel: the first becomes the shadows, the last the highlights."));
        dialog->refreshPreview();
    };
    connect(m_palettePanel, &PalettePanel::paletteChanged, dialog, describe);
    describe();
    dialog->enablePresets(QStringLiteral("gradientMap"));
    runPreviewDialog(dialog, tr("Gradient map"));
}

void MainWindow::effectDuotone()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    const effects::DuotoneParams defaults;
    const QList<ParamSpec> specs = {
        ParamSpec::colorPick("shadows", tr("Shadows"), defaults.shadows),
        ParamSpec::colorPick("highlights", tr("Highlights"), defaults.highlights),
        ParamSpec::checkbox("tritone", tr("Tritone: a separate midtone color"), false),
        ParamSpec::colorPick("midtones", tr("Midtones"), defaults.midtones),
        ParamSpec{"balance", tr("Midtone balance"), -100, 100, 0, 1},
        ParamSpec{"opacity", tr("Strength"), 0, 100, 100, 1, 0, false, tr(" %")},
    };
    auto* dialog = new EffectParamsDialog(tr("Duotone / tritone"), m_source, specs,
                                          [](const QVariantMap& v) {
                                              effects::DuotoneParams p;
                                              p.shadows = v["shadows"].value<QColor>();
                                              p.highlights = v["highlights"].value<QColor>();
                                              p.useMidtones = v["tritone"].toBool();
                                              p.midtones = v["midtones"].value<QColor>();
                                              p.balance = v["balance"].toDouble() / 100.0;
                                              p.opacity = v["opacity"].toDouble() / 100.0;
                                              return effects::duotone(p);
                                          },
                                          this);
    dialog->setDescription(tr("The image is converted to shades of two colors: dark areas get the shadow color, "
                              "light ones the highlight color, with a smooth transition between them. A tritone adds a third "
                              "color for the midtones. A classic of print and posters."));
    auto sync = [dialog] { dialog->setControlEnabled("midtones", dialog->values().value("tritone").toBool()); };
    connect(dialog, &EffectParamsDialog::valuesChanged, dialog, sync);
    sync();
    dialog->enablePresets(QStringLiteral("duotone"));
    runPreviewDialog(dialog, tr("Duotone / tritone"));
}

void MainWindow::effectDenoise()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    auto* dialog = new EffectParamsDialog(tr("Remove artifacts"), m_source,
                                          {ParamSpec{"radius", tr("Smoothing radius"), 1, 8, 3, 1, 0, true, tr(" px")},
                                           ParamSpec{"tolerance", tr("Color difference threshold"), 1, 100, 25, 1},
                                           ParamSpec{"passes", tr("Passes"), 1, 4, 1, 1},
                                           ParamSpec{"median", tr("Median filter (0 = off)"), 0, 3, 0, 1, 0, true, tr(" px")}},
                                          [](const QVariantMap& v) {
                                              return effects::denoise(v["median"].toInt(), v["radius"].toInt(),
                                                                      v["tolerance"].toDouble(), v["passes"].toInt());
                                          },
                                          this);
    dialog->setDescription(tr("Smooths noise, grain and JPEG compression artifacts (blocking, \"mosquito\" noise around edges) "
                              "while keeping edges: only neighboring pixels of similar color are averaged. "
                              "The threshold sets how different colors may be and still count as \"the same\". "
                              "The median filter additionally removes speckle noise."));
    dialog->enablePresets(QStringLiteral("denoise"));
    runPreviewDialog(dialog, tr("Remove artifacts"));
}

void MainWindow::effectLevels()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    const QList<ParamSpec> specs = {
        ParamSpec{"black", tr("Black point"), 0, 254, 0, 1},
        ParamSpec{"white", tr("White point"), 1, 255, 255, 1},
        ParamSpec{"gamma", tr("Midtone gamma"), 0.1, 4.0, 1.0, 0.01, 2},
        ParamSpec{"outBlack", tr("Output black"), 0, 255, 0, 1},
        ParamSpec{"outWhite", tr("Output white"), 0, 255, 255, 1},
        ParamSpec{"redBlack", tr("Black shift: red"), -64, 64, 0, 1},
        ParamSpec{"greenBlack", tr("Black shift: green"), -64, 64, 0, 1},
        ParamSpec{"blueBlack", tr("Black shift: blue"), -64, 64, 0, 1},
    };
    auto* dialog = new EffectParamsDialog(tr("Levels"), m_source, specs,
                                          [](const QVariantMap& v) {
                                              effects::LevelsParams p;
                                              p.blackPoint = v["black"].toDouble();
                                              p.whitePoint = v["white"].toDouble();
                                              p.gamma = v["gamma"].toDouble();
                                              p.outputBlack = v["outBlack"].toDouble();
                                              p.outputWhite = v["outWhite"].toDouble();
                                              p.redBlack = v["redBlack"].toDouble();
                                              p.greenBlack = v["greenBlack"].toDouble();
                                              p.blueBlack = v["blueBlack"].toDouble();
                                              return effects::levels(p);
                                          },
                                          this);
    dialog->setDescription(tr("The black and white points stretch the range, gamma moves the midtones, "
                              "and the per-channel shifts remove a color cast in the shadows."));
    dialog->enablePresets(QStringLiteral("levels"));
    runPreviewDialog(dialog, tr("Levels"));
}

void MainWindow::effectColorSplash()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    const QList<ParamSpec> specs = {
        ParamSpec{"hue", tr("Hue"), 0, 359, 0, 1, 0, false, tr("°")},
        ParamSpec{"tolerance", tr("Range width"), 0, 180, 30, 1, 0, false, tr("°")},
        ParamSpec{"softness", tr("Edge softness"), 0, 90, 15, 1, 0, false, tr("°")},
        ParamSpec{"minSat", tr("Saturation threshold"), 0, 100, 15, 1, 0, false, tr(" %")},
        ParamSpec::checkbox("invert", tr("Inverted: desaturate the chosen range"), false),
    };
    auto* dialog = new EffectParamsDialog(tr("Color splash"), m_source, specs,
                                          [](const QVariantMap& v) {
                                              effects::ColorSplashParams p;
                                              p.hue = v["hue"].toDouble();
                                              p.tolerance = v["tolerance"].toDouble();
                                              p.softness = v["softness"].toDouble();
                                              p.minSaturation = v["minSat"].toDouble() / 100.0;
                                              p.invert = v["invert"].toBool();
                                              return effects::colorSplash(p);
                                          },
                                          this);
    dialog->setDescription(tr("The hue is picked on the circle: 0° is red, 60° yellow, 120° green, "
                              "180° cyan, 240° blue, 300° magenta. The saturation threshold keeps "
                              "nearly gray pixels from being caught."));
    dialog->enablePresets(QStringLiteral("colorSplash"));
    runPreviewDialog(dialog, tr("Color splash"));
}

void MainWindow::effectHalftone()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    const QList<ParamSpec> specs = {
        ParamSpec{"cell", tr("Screen pitch"), 2, 64, 4, 1, 0, true, tr(" px")},
        ParamSpec{"angle", tr("Screen angle"), 0, 90, 15, 1, 0, false, tr("°")},
        ParamSpec{"sharpness", tr("Dot sharpness"), 0, 100, 45, 1, 0, false, tr(" %")},
        ParamSpec::checkbox("color", tr("CMYK separation (otherwise a single black ink)"), true),
        ParamSpec::checkbox("white", tr("Print on white"), true),
    };
    auto* dialog = new EffectParamsDialog(tr("Halftone"), m_source, specs,
                                          [](const QVariantMap& v) {
                                              effects::HalftoneParams p;
                                              p.cellSize = v["cell"].toDouble();
                                              p.angle = v["angle"].toDouble();
                                              p.sharpness = v["sharpness"].toDouble() / 100.0;
                                              p.colorSeparation = v["color"].toBool();
                                              p.onWhite = v["white"].toBool();
                                              return effects::halftone(p);
                                          },
                                          this);
    dialog->setDescription(tr("The image is broken into dots of varying size. With color separation every "
                              "ink gets its own screen angle, as in offset printing: the dots form rosettes "
                              "and produce no moiré."));
    dialog->enablePresets(QStringLiteral("halftone"));
    runPreviewDialog(dialog, tr("Halftone"));
}

void MainWindow::effectLut()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;

    const QString path = QFileDialog::getOpenFileName(this, tr("Color table"), m_lastLutDir,
                                                      tr("Color tables (*.cube);;All files (*)"));
    if (path.isEmpty())
        return;
    m_lastLutDir = QFileInfo(path).absolutePath();

    QString error;
    const effects::CubeLut lut = effects::CubeLut::load(path, &error);
    if (!lut.isValid()) {
        QMessageBox::warning(this, captionError(),
                             tr("Could not read \"%1\":\n%2").arg(QFileInfo(path).fileName(), error));
        return;
    }

    auto* dialog = new EffectParamsDialog(tr("Color table"), m_source,
                                          {ParamSpec{"strength", tr("Strength"), 0, 100, 100, 1, 0, false, tr(" %")}},
                                          [lut](const QVariantMap& v) {
                                              return lut.makeOp(v["strength"].toDouble() / 100.0);
                                          },
                                          this);
    dialog->setDescription(tr("\"%1\" is a %2³ table. Values between nodes are computed by trilinear interpolation.")
                               .arg(lut.title().isEmpty() ? QFileInfo(path).fileName() : lut.title())
                               .arg(lut.size()));
    runPreviewDialog(dialog, tr("Color table"));
}

void MainWindow::exportLut()
{
    if (!ensureIdle())
        return;
    if (m_historyIndex == 0 || m_source.isNull()) {
        QMessageBox::information(this, tr("Nothing to save"),
                                 tr("Apply a repaint or an effect first: the table is built from "
                                    "how the operation changed the colors."));
        return;
    }

    // The table is taken from the same operation that produced the current result
    if (!m_lastOp) {
        QMessageBox::information(this, tr("Nothing to save"),
                                 tr("The last operation could not be repeated to build the table."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Save color table"),
                                                      QDir(m_lastLutDir).filePath(QStringLiteral("kolorit.cube")),
                                                      tr("Color tables (*.cube)"));
    if (path.isEmpty())
        return;
    m_lastLutDir = QFileInfo(path).absolutePath();

    effects::CubeLut lut = effects::CubeLut::fromOp(m_lastOp, 33);
    if (!lut.isValid()) {
        QMessageBox::warning(this, captionError(), tr("Could not build a table from the last operation."));
        return;
    }
    QString error;
    if (!lut.save(path, &error)) {
        QMessageBox::warning(this, captionError(), tr("Could not save \"%1\":\n%2").arg(path, error));
        return;
    }
    statusBar()->showMessage(tr("33³ table saved: %1").arg(QFileInfo(path).fileName()), 6000);
}

void MainWindow::effectPixelArt()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;

    const QList<ParamSpec> specs = {
        ParamSpec{"block", tr("Pixel size"), 2, 64, 8, 1, 0, true, tr(" px")},
        ParamSpec{"levels", tr("Color levels per channel (0 = no quantization)"), 0, 32, 8, 1},
        ParamSpec{"threshold", tr("Outline contrast threshold"), 0, 100, 20, 1, 0, false, tr(" %")},
        ParamSpec{"darkness", tr("Outline darkness"), 0, 100, 70, 1, 0, false, tr(" %")},
        ParamSpec{"width", tr("Outline width (fraction of a pixel)"), 10, 100, 100, 5, 0, false, tr(" %")},
        ParamSpec::checkbox("usePalette", QString(), !m_palettePanel->colors().isEmpty()),
    };

    // The palette and color model are read from the panels at computation time: the dialog is
    // modeless, so they can be changed while the parameters are being tuned
    auto* dialog = new EffectParamsDialog(tr("Pixel art"), m_source, specs,
                              [this](const QVariantMap& v) {
                                  effects::PixelArtParams p;
                                  p.blockSize = v["block"].toInt();
                                  p.levels = v["levels"].toInt();
                                  p.outlineThreshold = v["threshold"].toDouble() / 100.0;
                                  p.outlineDarkness = v["darkness"].toDouble() / 100.0;
                                  p.outlineWidth = v["width"].toDouble() / 100.0;
                                  if (v.value("usePalette").toBool() && !m_palettePanel->colors().isEmpty()) {
                                      p.palette = m_palettePanel->colors();
                                      p.model = m_repaintPanel->params().model;
                                  }
                                  return effects::pixelArt(p);
                              },
                              this);
    dialog->setDescription(tr("The image is enlarged into \"pixels\", colors are reduced to a limited set "
                              "(levels per channel or the current palette in the chosen color model), and edges between "
                              "contrasting areas are outlined with a dark line along the darker block, as in "
                              "pixel art. A 100 % threshold turns the outline off. The palette and model can be changed "
                              "in the panels on the right without closing the dialog."));

    // The checkbox label and whether "levels" is enabled follow the panels and the checkbox itself
    auto sync = [this, dialog](bool recompute) {
        const int count = m_palettePanel->colors().size();
        const bool hasPalette = count > 0;
        const bool usePalette = hasPalette && dialog->values().value("usePalette").toBool();
        dialog->setLabel("usePalette",
                         hasPalette ? tr("Quantize to the current palette (%1 colors, %2 model)")
                                          .arg(count)
                                          .arg(colorspace::modelName(m_repaintPanel->params().model))
                                    : tr("Quantize to the current palette (the palette is empty)"));
        dialog->setControlEnabled("usePalette", hasPalette);
        // Levels per channel do not apply when quantizing to a palette
        dialog->setControlEnabled("levels", !usePalette);
        if (recompute)
            dialog->refreshPreview();
    };
    connect(dialog, &EffectParamsDialog::valuesChanged, dialog, [sync] { sync(false); });
    connect(m_palettePanel, &PalettePanel::paletteChanged, dialog, [sync] { sync(true); });
    connect(m_repaintPanel, &RepaintPanel::paramsChanged, dialog, [sync] { sync(true); });
    sync(false);

    dialog->enablePresets(QStringLiteral("pixelArt"));
    runPreviewDialog(dialog, tr("Pixel art"));
}

void MainWindow::effectConsole()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    QStringList consoles;
    for (Console c : allConsoles())
        consoles << tr("%1: %2×%3, %4 colors")
                        .arg(consoleName(c))
                        .arg(consoleResolution(c).width())
                        .arg(consoleResolution(c).height())
                        .arg(consolePalette(c).size());
    const QList<ParamSpec> specs = {
        ParamSpec::choice("console", tr("Console"), consoles, 0),
        ParamSpec::choice("dither", tr("Dithering"),
                          {ditheringName(Dithering::None), ditheringName(Dithering::Bayer2),
                           ditheringName(Dithering::Bayer4), ditheringName(Dithering::Bayer8),
                           ditheringName(Dithering::BlueNoise)},
                          2),
        ParamSpec{"strength", tr("Dithering strength"), 0, 128, 48, 1},
        ParamSpec{"contrast", tr("Contrast before quantization"), -100, 100, 15, 1, 0, false, tr(" %")},
        ParamSpec::checkbox("limits", tr("Color limits per block (NES: 4 per 16×16, Spectrum: 2 per 8×8)"), true),
        ParamSpec::checkbox("integer", tr("Integer upscaling with borders (crisp pixels without distortion)"), false),
    };
    auto* dialog = new EffectParamsDialog(tr("Retro console"), m_source, specs,
                                          [](const QVariantMap& v) {
                                              ConsoleParams p;
                                              p.console = allConsoles().value(v["console"].toInt(), Console::GameBoy);
                                              static const Dithering dithers[] = {Dithering::None, Dithering::Bayer2,
                                                                                  Dithering::Bayer4, Dithering::Bayer8,
                                                                                  Dithering::BlueNoise};
                                              p.dithering = dithers[std::clamp(v["dither"].toInt(), 0, 4)];
                                              p.ditherStrength = v["strength"].toInt();
                                              p.contrast = v["contrast"].toDouble() / 100.0;
                                              p.attributeLimits = v["limits"].toBool();
                                              p.integerScale = v["integer"].toBool();
                                              return consoleOp(p);
                                          },
                                          this);
    dialog->setDescription(tr("The image is downscaled to the console resolution, quantized to its hardware palette "
                              "with dithering and scaled back up with crisp pixels. The Game Boy sees only "
                              "luminance (4 levels); the NES is limited to 4 colors per 16×16 block; the ZX Spectrum to two "
                              "colors of the same brightness per 8×8 cell (the famous \"attribute clash\")."));
    dialog->enablePresets(QStringLiteral("console"));
    runPreviewDialog(dialog, tr("Retro console"));
}

void MainWindow::effectCrt()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    QStringList masks;
    for (CrtMask m : allCrtMasks())
        masks << crtMaskName(m);
    const CrtParams defaults;
    const QList<ParamSpec> specs = {
        ParamSpec{"curvature", tr("Screen curvature"), 0, 100, defaults.curvature * 100, 1, 0, false, tr(" %")},
        ParamSpec::choice("mask", tr("Phosphor mask"), masks, int(defaults.mask)),
        ParamSpec{"maskPeriod", tr("RGB triad width"), 2, 12, double(defaults.maskPeriod), 1, 0, true, tr(" px")},
        ParamSpec{"maskStrength", tr("Mask strength"), 0, 100, defaults.maskStrength * 100, 1, 0, false, tr(" %")},
        ParamSpec{"scanPeriod", tr("Scanline pitch"), 2, 16, double(defaults.scanlinePeriod), 1, 0, true, tr(" px")},
        ParamSpec{"scanIntensity", tr("Scanline depth"), 0, 100, defaults.scanlineIntensity * 100, 1, 0, false, tr(" %")},
        ParamSpec{"bloomRadius", tr("Glow radius"), 0, 32, double(defaults.bloomRadius), 1, 0, true, tr(" px")},
        ParamSpec{"bloomStrength", tr("Glow strength"), 0, 100, defaults.bloomStrength * 100, 1, 0, false, tr(" %")},
        ParamSpec{"aberration", tr("Color misconvergence"), 0, 8, double(defaults.aberration), 1, 0, true, tr(" px")},
        ParamSpec{"vignette", tr("Vignette"), 0, 100, defaults.vignette * 100, 1, 0, false, tr(" %")},
        ParamSpec{"glass", tr("Glass reflection"), 0, 100, defaults.glass * 100, 1, 0, false, tr(" %")},
        ParamSpec{"gain", tr("Brightness gain"), 50, 200, defaults.gain * 100, 5, 0, false, tr(" %")},
    };
    auto* dialog = new EffectParamsDialog(tr("CRT television"), m_source, specs,
                                          [](const QVariantMap& v) {
                                              CrtParams p;
                                              p.curvature = v["curvature"].toDouble() / 100.0;
                                              p.mask = allCrtMasks().value(v["mask"].toInt(), CrtMask::ApertureGrille);
                                              p.maskPeriod = v["maskPeriod"].toInt();
                                              p.maskStrength = v["maskStrength"].toDouble() / 100.0;
                                              p.scanlinePeriod = v["scanPeriod"].toInt();
                                              p.scanlineIntensity = v["scanIntensity"].toDouble() / 100.0;
                                              p.bloomRadius = v["bloomRadius"].toInt();
                                              p.bloomStrength = v["bloomStrength"].toDouble() / 100.0;
                                              p.aberration = v["aberration"].toInt();
                                              p.vignette = v["vignette"].toDouble() / 100.0;
                                              p.glass = v["glass"].toDouble() / 100.0;
                                              p.gain = v["gain"].toDouble() / 100.0;
                                              return crtOp(p);
                                          },
                                          this);
    dialog->setDescription(tr("The image is shown on the curved screen of a picture tube with rounded corners and a dark "
                              "bezel: color misconvergence at the edges, glow of bright areas, a phosphor mask "
                              "(RGB subpixels), scanlines, a vignette and a glass reflection. The mask and scanlines darken "
                              "the picture, so compensate with the brightness gain. Goes well with Retro console: "
                              "apply it first and the CRT as the next step."));
    dialog->enablePresets(QStringLiteral("crt"));
    runPreviewDialog(dialog, tr("CRT television"));
}

void MainWindow::effectDisc()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    auto* dialog = new EffectParamsDialog(
        tr("Disc"), m_source,
        {ParamSpec{"hole", tr("Hole"), 4, 40, 12, 1, 0, false, tr(" %")},
         ParamSpec{"hub", tr("Hub (diameter)"), 0, 60, 28, 1, 0, false, tr(" %")},
         ParamSpec::choice("hubStyle", tr("Hub style"),
                           {tr("Clear plastic (CD)"), tr("Dark mechanism"), tr("Print to center")}, 0),
         ParamSpec{"zoom", tr("Image scale"), 50, 300, 100, 5, 0, false, tr(" %")},
         ParamSpec{"dx", tr("Horizontal shift"), -50, 50, 0, 1, 0, false, tr(" %")},
         ParamSpec{"dy", tr("Vertical shift"), -50, 50, 0, 1, 0, false, tr(" %")},
         ParamSpec{"sheen", tr("Rainbow diffraction"), 0, 100, 40, 1, 0, false, tr(" %")},
         ParamSpec{"angle", tr("Light angle"), 0, 180, 30, 5, 0, false, tr("°")},
         ParamSpec{"gloss", tr("Glossy highlight"), 0, 100, 30, 1, 0, false, tr(" %")},
         ParamSpec{"tracks", tr("Tracks"), 0, 100, 30, 1, 0, false, tr(" %")},
         ParamSpec{"margin", tr("Margin around the disc"), 0, 20, 5, 1, 0, false, tr(" %")},
         ParamSpec{"shadow", tr("Shadow under the disc"), 0, 100, 60, 1, 0, false, tr(" %")},
         ParamSpec::checkbox("transparent", tr("Transparent background around the disc"), true)},
        [](const QVariantMap& v) {
            effects::DiscParams p;
            p.holeSize = v["hole"].toDouble() / 100.0;
            p.hubSize = v["hub"].toDouble() / 100.0;
            p.hub = effects::DiscHub(std::clamp(v["hubStyle"].toInt(), 0, 2));
            p.zoom = v["zoom"].toDouble() / 100.0;
            p.offsetX = v["dx"].toDouble() / 100.0;
            p.offsetY = v["dy"].toDouble() / 100.0;
            p.sheen = v["sheen"].toDouble() / 100.0;
            p.sheenAngle = v["angle"].toDouble();
            p.gloss = v["gloss"].toDouble() / 100.0;
            p.tracks = v["tracks"].toDouble() / 100.0;
            p.margin = v["margin"].toDouble() / 100.0;
            p.shadow = v["shadow"].toDouble() / 100.0;
            p.transparentBackground = v["transparent"].toBool();
            return effects::disc(p);
        },
        this);
    dialog->setDescription(tr("The image is cropped to a circle and laid onto a disc: a hole and hub in the middle "
                              "(clear plastic, a dark mechanism with a ring matching the picture, or print to the center), "
                              "a clear rim with a chamfered edge, a diffraction rainbow and a glossy highlight at the chosen "
                              "light angle, tracks, a soft shadow. The result is square with transparency around the disc "
                              "(save as PNG or WebP)."));
    dialog->enablePresets(QStringLiteral("disc"));
    runPreviewDialog(dialog, tr("Disc"));
}

void MainWindow::effectVhs()
{
    if (!ensureImageLoaded() || !ensureIdle())
        return;
    auto* dialog = new VhsDialog(m_source, m_vhsParams, this);
    // Parameters are remembered on accept (the signal comes before finished, which starts the computation)
    connect(dialog, &QDialog::accepted, this, [this, dialog] { m_vhsParams = dialog->params(); });
    runPreviewDialog(dialog, tr("VHS camcorder"));
}

// ---------------------------------------------------------------------------
// Help and window events
// ---------------------------------------------------------------------------

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About Kolorit"),
        tr("<h3>Kolorit %1</h3>"
           "<p>Repaints images into a chosen palette and adds retro effects.</p>"
           "<p>Every pixel of the image is replaced with the nearest palette color "
           "in the chosen color model (RGB, HSL, HSV, XYZ, CIE Lab), with or without dithering. "
           "Palettes can be built from images (chunks, k-means, unique colors), and there are effects "
           "including a configurable VHS camcorder.</p>"
           "<p>All processing runs in parallel over image rows on every CPU core "
           "(now: %2 threads) with cancellation and progress.</p>"
           "<p>Free software under the MIT License.</p>"
           "<p>Qt %3 · C++20</p>")
            .arg(QCoreApplication::applicationVersion())
            .arg(QThreadPool::globalInstance()->maxThreadCount())
            .arg(QString::fromLatin1(qVersion())));
}

void MainWindow::checkForUpdates()
{
    runUpdateCheck(false);
}

void MainWindow::checkForUpdatesOnStartup()
{
    if (updates::checkOnStartup())
        runUpdateCheck(true);
}

void MainWindow::runUpdateCheck(bool quiet)
{
    // One check at a time: the menu item waits for the answer
    if (!m_actCheckUpdates->isEnabled())
        return;
    m_actCheckUpdates->setEnabled(false);
    updates::fetchLatest(this, [this, quiet](const std::optional<updates::Release>& release, const QString& error) {
        m_actCheckUpdates->setEnabled(true);
        const QString current = QCoreApplication::applicationVersion();
        if (!release) {
            if (!quiet)
                QMessageBox::warning(this, tr("Updates"), tr("Could not check for updates: %1").arg(error));
            return;
        }
        const QString latest = release->version.toString();
        if (!updates::isNewer(*release, current)) {
            if (!quiet)
                QMessageBox::information(this, tr("Updates"), tr("You have the latest version, Kolorit %1.").arg(current));
            return;
        }
        if (quiet && updates::skippedVersion() == latest)
            return;

        QMessageBox box(this);
        box.setWindowTitle(tr("Update available"));
        box.setIcon(QMessageBox::Information);
        box.setText(tr("<b>Kolorit %1 is available</b>").arg(latest));
        box.setInformativeText(tr("You have version %1. The download opens in the browser.").arg(current));
        if (!release->notes.isEmpty())
            box.setDetailedText(release->notes);
        QPushButton* download = box.addButton(tr("Download"), QMessageBox::AcceptRole);
        QPushButton* skip = box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.setDefaultButton(download);
        box.exec();
        if (box.clickedButton() == download)
            QDesktopServices::openUrl(QUrl(release->downloadUrl));
        else if (box.clickedButton() == skip)
            updates::setSkippedVersion(latest);
    });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_runner.cancel();
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!ensureIdle())
        return;
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile() && openImageFile(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            setSourceImage(image, tr("Dropped image"));
            event->acceptProposedAction();
        }
    }
}

} // namespace kolorit
