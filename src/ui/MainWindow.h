#pragma once

#include "core/ImageTask.h"
#include "core/ImageTaskRunner.h"
#include "core/Palette.h"
#include "processing/PaletteMapper.h"
#include "processing/VhsEffect.h"
#include "ui/HistoryPanel.h"
#include "ui/Theme.h"

#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QVariantMap>

#include <functional>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QProgressBar;
class QSpinBox;
class QToolButton;

namespace kolorit {

class ImageView;
class PalettePanel;
class PreviewDialog;
class RepaintPanel;

/// Main window: loading and saving images, the palette, repainting and effects.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Open an image from a file (also used for the command-line argument)
    bool openImageFile(const QString& path);

    const Palette& colorPalette() const;
    void setColorPalette(const Palette& palette);
    const QImage& sourceImage() const { return m_source; }

    /// History states and the index of the current one, for interface tests
    const QList<HistoryState>& history() const { return m_history; }
    int historyIndex() const { return m_historyIndex; }
    bool isProcessing() const { return m_runner.isRunning(); }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public slots:
    // File
    void openImage();
    void pasteImage();
    void saveResult();
    // Palette
    void loadPaletteFromFile();
    void savePalette();
    void loadBuiltinPalette(const QString& name);
    void paletteGenerate();
    void paletteFromChunks();
    void paletteFromKMeans();
    void paletteFromUniqueColors();
    void paletteFromReferenceImage();
    // Processing
    void repaintImage();
    void cancelProcessing();
    // Effects
    void effectInvert();
    void effectGrayscale();
    void effectSepia();
    void effectPosterize();
    void effectPixelate();
    void effectAdjust();
    void effectGradientMap();
    void effectDuotone();
    void effectDenoise();
    void effectLevels();
    void effectColorSplash();
    void effectHalftone();
    void effectLut();
    void exportLut();
    void effectPixelArt();
    void effectConsole();
    void effectCrt();
    void effectDisc();
    void effectVhs();
    // History
    void revertToOriginal();
    void undo();
    void redo();
    /// Return the image to the state with the given index
    void gotoHistoryState(int index);
    // View
    void setTheme(theme::Mode mode);
    /// Remember the interface language (empty: follow the system) and offer a restart
    void setLanguage(const QString& code);
    // Help
    void showAbout();
    /// Look for a newer release on GitHub and report the result either way
    void checkForUpdates();
    /// The quiet check at startup, if enabled: speaks up only when there is a new version
    void checkForUpdatesOnStartup();

private slots:
    void onTaskStarted();
    void onTaskProgress(int percent);
    void onTaskFinished(const QImage& result, qint64 elapsedMs);
    void onTaskCanceled();

private:
    void buildActions();
    /// Menus opened from the toolbar buttons and the palette panel (the window has no menu bar)
    void buildMenus();
    void buildToolbar();
    void buildStatusBar();
    void buildDocks();
    void refreshIcons();
    void loadSettings();
    void saveSettings();

    QAction* makeAction(const QString& text, const QString& iconName, const QKeySequence& shortcut,
                        const QString& tip, const std::function<void()>& slot);
    bool ensureImageLoaded();
    bool ensurePaletteNotEmpty(const QString& message);
    bool ensureIdle();
    void setSourceImage(const QImage& image, const QString& name);
    /// Add a state to the history and make it current.
    /// Steps after the current one are dropped, as in Photoshop without non-linear history.
    void pushHistory(const QString& title, const QString& icon, ImageOp op, const QImage& image);
    /// Build an operation that runs steps (from, to] as a chain
    ImageOp historyChain(int from, int to) const;
    void runOp(const QString& title, ImageOp op, const QString& icon = QStringLiteral("wand"));
    /// Show a modeless dialog with a preview; the dialog is deleted when closed
    void runPreviewDialog(PreviewDialog* dialog, const QString& title,
                          const QString& icon = QStringLiteral("wand"));
    void updateView();
    void updateActions();
    void updateStatus();
    QString imageFileFilter() const;
    /// Update check; a quiet one shows nothing on errors, without news or for a skipped version
    void runUpdateCheck(bool quiet);

    // Images: the original (as loaded) and the working image of the current history step
    QImage m_original;
    QImage m_source;
    QString m_sourceName;

    /// Operation history. What is stored is not pictures but the operations themselves:
    /// the image of any step is obtained by running the chain from the original in m_original.
    /// That is why history depth costs almost no memory (twenty snapshots of a 12 MP photo
    /// would take up to a gigabyte), and the history can be much deeper.
    ///
    /// The price is time: going back recomputes the chain, so it runs in the background
    /// with progress and cancellation like an ordinary operation. A step forward starts
    /// from the current image, so it costs exactly one operation.
    static constexpr int kMaxHistory = 64;
    QList<HistoryState> m_history;
    int m_historyIndex = 0;
    /// The step the running recomputation leads to (-1 for an ordinary operation)
    int m_pendingHistoryIndex = -1;

    ImageTaskRunner m_runner;
    QString m_taskTitle;
    QString m_taskIcon;   ///< history step icon of the current operation
    QPointer<PreviewDialog> m_previewDialog;   ///< the open effect settings dialog
    QImage m_previewImage;                     ///< the last frame of its preview
    QSize m_previewLogicalSize;                ///< the size this frame represents at full resolution
    VhsParams m_vhsParams;
    int m_chunkColumns = 8;
    int m_chunkRows = 8;
    /// Last state of the "Create palette" dialog, remembered between runs
    QVariantMap m_generatorState;

    QString m_lastImageDir;
    QString m_lastSaveDir;
    QString m_lastPaletteDir;
    QString m_lastLutDir;

    /// The last operation performed; a .cube is built from it on export
    ImageOp m_lastOp;

    ImageView* m_view = nullptr;
    RepaintPanel* m_repaintPanel = nullptr;
    PalettePanel* m_palettePanel = nullptr;
    HistoryPanel* m_historyPanel = nullptr;
    QDockWidget* m_repaintDock = nullptr;
    QDockWidget* m_paletteDock = nullptr;
    QDockWidget* m_historyDock = nullptr;
    QSpinBox* m_threads = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusImage = nullptr;
    QToolButton* m_zoomButton = nullptr;
    QToolButton* m_effectsButton = nullptr;
    QToolButton* m_moreButton = nullptr;
    QMenu* m_effectsMenu = nullptr;
    QMenu* m_moreMenu = nullptr;
    QMenu* m_paletteFromImageMenu = nullptr;
    QMenu* m_bundledMenu = nullptr;
    QActionGroup* m_themeGroup = nullptr;

    QAction* m_actOpen = nullptr;
    QAction* m_actPaste = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actExit = nullptr;
    QAction* m_actCheckUpdates = nullptr;
    QAction* m_actRepaint = nullptr;
    QAction* m_actCancel = nullptr;
    QAction* m_actShowOriginal = nullptr;
    QAction* m_actRevert = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actFit = nullptr;
    QAction* m_actActual = nullptr;
    QAction* m_actZoomIn = nullptr;
    QAction* m_actZoomOut = nullptr;
    QAction* m_actLoadPalette = nullptr;
    QAction* m_actSavePalette = nullptr;
    QList<QAction*> m_imageActions;       ///< need a loaded image
    QList<QAction*> m_processingActions;  ///< unavailable during processing
    QHash<QAction*, QString> m_iconNames; ///< icon name for every action (recolored on a theme change)
};

} // namespace kolorit
