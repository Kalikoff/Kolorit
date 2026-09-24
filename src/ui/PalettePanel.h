#pragma once

#include "core/Palette.h"

#include <QWidget>

class QAction;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QStackedWidget;
class QToolButton;

namespace kolorit {

class PaletteStrip;

/// Palette panel: a preview strip, a swatch grid with editing, sorting and
/// drag-and-drop reordering, plus loading, saving and creating palettes.
class PalettePanel : public QWidget {
    Q_OBJECT
public:
    explicit PalettePanel(QWidget* parent = nullptr);

    const Palette& colors() const { return m_palette; }
    void setColors(const Palette& palette);

    /// Main window actions placed on the panel: loading/saving a file and the menus
    /// for creating a palette from the image and from the builtin palettes
    void setExternalActions(QAction* load, QAction* save, QMenu* fromImageMenu, QMenu* bundledMenu);

    /// Redraw the icons after a theme change
    void refreshIcons();

public slots:
    void clearAll();

signals:
    void paletteChanged();

private slots:
    void addColor();
    void editSelected();
    void removeSelected();
    void sortByLuminance();
    void sortByHue();
    void sortAsGradient();
    void reduceColors();
    void mergeFromFile();
    void onItemDoubleClicked(QListWidgetItem* item);
    void onRowsMoved();
    void showContextMenu(const QPoint& pos);
    void updateState();

private:
    void rebuildList();
    void emitChanged();
    QListWidgetItem* makeItem(QRgb color) const;

    Palette m_palette;
    PaletteStrip* m_strip;
    QLabel* m_countLabel;
    QStackedWidget* m_stack;
    QLabel* m_emptyLabel;
    QListWidget* m_list;

    QAction* m_actAdd;
    QAction* m_actEdit;
    QAction* m_actRemove;
    QAction* m_actClear;
    QAction* m_actSortLuma;
    QAction* m_actSortHue;
    QAction* m_actSortGradient;
    QAction* m_actReduce;
    QAction* m_actMerge;
    QToolButton* m_moreButton;
    QToolButton* m_fromImageButton;
    QToolButton* m_bundledButton;
    QToolButton* m_loadButton;
    QToolButton* m_saveButton;
    bool m_rebuilding = false;
};

} // namespace kolorit
