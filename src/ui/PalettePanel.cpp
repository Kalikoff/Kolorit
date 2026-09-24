#include "ui/PalettePanel.h"

#include "processing/PaletteBuilder.h"
#include "ui/PaletteStrip.h"
#include "ui/Theme.h"

#include <QAbstractItemModel>
#include <QColorDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace kolorit {

namespace {

constexpr int kSwatchSize = 30;
constexpr int kColorRole = Qt::UserRole + 1;

QIcon swatchIcon(QRgb color)
{
    QPixmap pixmap(kSwatchSize, kSwatchSize);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QColor(0, 0, 0, 110));
    p.setBrush(QColor::fromRgb(color));
    p.drawRoundedRect(QRectF(0.5, 0.5, kSwatchSize - 1, kSwatchSize - 1), 4, 4);
    return QIcon(pixmap);
}

QToolButton* menuButton(const QString& text, QMenu* menu, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    return button;
}

} // namespace

PalettePanel::PalettePanel(QWidget* parent)
    : QWidget(parent)
    , m_strip(new PaletteStrip(this))
    , m_countLabel(new QLabel(this))
    , m_stack(new QStackedWidget(this))
    , m_emptyLabel(new QLabel(this))
    , m_list(new QListWidget(this))
{
    // --- Header: preview strip and number of colors ---
    m_strip->setFixedHeight(22);
    m_countLabel->setObjectName(QStringLiteral("hintLabel"));

    // --- Empty state with a hint ---
    m_emptyLabel->setObjectName(QStringLiteral("placeholderLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setText(tr("The palette is empty.\n\nAdd colors with the \"+\" button, load a file, "
                             "pick a builtin palette or create one from the image."));

    // --- Swatch grid: dragging changes the order of colors ---
    m_list->setViewMode(QListView::ListMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setIconSize(QSize(kSwatchSize, kSwatchSize));
    m_list->setSpacing(3);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setToolTip(tr("Double-click to change a color, Delete to remove, drag to reorder"));

    m_stack->addWidget(m_emptyLabel);
    m_stack->addWidget(m_list);

    // --- Editing actions ---
    m_actAdd = new QAction(tr("Add color…"), this);
    m_actEdit = new QAction(tr("Edit color…"), this);
    m_actRemove = new QAction(tr("Remove selected"), this);
    m_actRemove->setShortcut(QKeySequence::Delete);
    m_actRemove->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_actClear = new QAction(tr("Clear palette"), this);
    m_actSortLuma = new QAction(tr("Sort by luminance"), this);
    m_actSortHue = new QAction(tr("Sort by hue"), this);
    m_actSortGradient = new QAction(tr("Order as gradient"), this);
    m_actSortGradient->setToolTip(tr("Put the closest colors next to each other, for a gradient map or duotone"));
    m_actReduce = new QAction(tr("Reduce to N colors…"), this);
    m_actReduce->setToolTip(tr("Keep the given number of the most characteristic colors"));
    m_actMerge = new QAction(tr("Add palette from file…"), this);
    m_actMerge->setToolTip(tr("Append the colors of another palette to the current one"));
    for (QAction* a : {m_actAdd, m_actEdit, m_actRemove, m_actClear, m_actSortLuma, m_actSortHue})
        a->setToolTip(a->text());
    m_actRemove->setToolTip(tr("Remove selected (Delete)"));

    auto* editBar = new QToolBar(this);
    editBar->setIconSize(QSize(18, 18));
    editBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    editBar->addAction(m_actAdd);
    editBar->addAction(m_actEdit);
    editBar->addAction(m_actRemove);
    editBar->addAction(m_actClear);
    editBar->addSeparator();
    editBar->addAction(m_actSortLuma);
    editBar->addAction(m_actSortHue);
    // Rare operations hide behind one button: the panel is narrow and has six buttons already
    auto* moreMenu = new QMenu(this);
    moreMenu->addAction(m_actSortGradient);
    moreMenu->addSeparator();
    moreMenu->addAction(m_actReduce);
    moreMenu->addAction(m_actMerge);
    m_moreButton = new QToolButton(this);
    m_moreButton->setMenu(moreMenu);
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    m_moreButton->setAutoRaise(true);
    m_moreButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_moreButton->setToolTip(tr("More: gradient order, reduce, merge"));
    editBar->addWidget(m_moreButton);
    editBar->setStyleSheet(QStringLiteral("QToolBar { border: none; padding: 0; }"));

    // --- Palette sources (the buttons are filled in by the main window) ---
    m_loadButton = new QToolButton(this);
    m_saveButton = new QToolButton(this);
    m_fromImageButton = menuButton(tr("From image"), nullptr, this);
    m_bundledButton = menuButton(tr("Builtin"), nullptr, this);
    for (QToolButton* b : {m_loadButton, m_saveButton}) {
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }

    auto* sourcesRow = new QHBoxLayout;
    sourcesRow->setSpacing(2);
    sourcesRow->addWidget(m_fromImageButton);
    sourcesRow->addWidget(m_bundledButton);
    sourcesRow->addStretch(1);
    sourcesRow->addWidget(m_loadButton);
    sourcesRow->addWidget(m_saveButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);
    layout->addWidget(m_strip);
    layout->addWidget(m_countLabel);
    layout->addWidget(m_stack, 1);
    layout->addWidget(editBar);
    layout->addLayout(sourcesRow);

    addAction(m_actRemove);   // so that Delete works

    connect(m_actAdd, &QAction::triggered, this, &PalettePanel::addColor);
    connect(m_actEdit, &QAction::triggered, this, &PalettePanel::editSelected);
    connect(m_actRemove, &QAction::triggered, this, &PalettePanel::removeSelected);
    connect(m_actClear, &QAction::triggered, this, &PalettePanel::clearAll);
    connect(m_actSortLuma, &QAction::triggered, this, &PalettePanel::sortByLuminance);
    connect(m_actSortHue, &QAction::triggered, this, &PalettePanel::sortByHue);
    connect(m_actSortGradient, &QAction::triggered, this, &PalettePanel::sortAsGradient);
    connect(m_actReduce, &QAction::triggered, this, &PalettePanel::reduceColors);
    connect(m_actMerge, &QAction::triggered, this, &PalettePanel::mergeFromFile);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PalettePanel::onItemDoubleClicked);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &PalettePanel::updateState);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &PalettePanel::showContextMenu);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &PalettePanel::onRowsMoved);

    refreshIcons();
    rebuildList();
}

void PalettePanel::setExternalActions(QAction* load, QAction* save, QMenu* fromImageMenu, QMenu* bundledMenu)
{
    m_loadButton->setDefaultAction(load);
    m_saveButton->setDefaultAction(save);
    m_fromImageButton->setMenu(fromImageMenu);
    m_bundledButton->setMenu(bundledMenu);
    m_fromImageButton->setToolTip(tr("Create a palette from the open image"));
    m_bundledButton->setToolTip(tr("Palettes built into the application"));
}

void PalettePanel::refreshIcons()
{
    m_actAdd->setIcon(theme::icon(QStringLiteral("plus")));
    m_actEdit->setIcon(theme::icon(QStringLiteral("edit")));
    m_actRemove->setIcon(theme::icon(QStringLiteral("trash")));
    m_actClear->setIcon(theme::icon(QStringLiteral("clear")));
    m_actSortLuma->setIcon(theme::icon(QStringLiteral("sun")));
    m_actSortHue->setIcon(theme::icon(QStringLiteral("hue")));
    m_moreButton->setIcon(theme::icon(QStringLiteral("more")));
    m_fromImageButton->setIcon(theme::icon(QStringLiteral("image")));
    m_bundledButton->setIcon(theme::icon(QStringLiteral("grid")));
}

void PalettePanel::setColors(const Palette& palette)
{
    if (palette == m_palette)
        return;
    m_palette = palette;
    rebuildList();
    emit paletteChanged();
}

QListWidgetItem* PalettePanel::makeItem(QRgb color) const
{
    auto* item = new QListWidgetItem(swatchIcon(color), QString());
    item->setData(kColorRole, uint(color));
    item->setToolTip(QColor::fromRgb(color).name().toUpper());
    item->setSizeHint(QSize(kSwatchSize + 6, kSwatchSize + 6));
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
    return item;
}

void PalettePanel::rebuildList()
{
    m_rebuilding = true;
    m_list->clear();
    for (QRgb c : m_palette.colors())
        m_list->addItem(makeItem(c));
    m_rebuilding = false;
    updateState();
}

void PalettePanel::emitChanged()
{
    updateState();
    emit paletteChanged();
}

void PalettePanel::updateState()
{
    const int count = m_palette.size();
    const int selected = int(m_list->selectedItems().size());
    m_strip->setColors(m_palette);
    m_countLabel->setText(count == 0 ? tr("No colors") : tr("Colors: %1").arg(count));
    m_stack->setCurrentWidget(count == 0 ? static_cast<QWidget*>(m_emptyLabel) : static_cast<QWidget*>(m_list));
    m_actEdit->setEnabled(selected == 1);
    m_actRemove->setEnabled(selected > 0);
    m_actClear->setEnabled(count > 0);
    m_actSortLuma->setEnabled(count > 1);
    m_actSortHue->setEnabled(count > 1);
    m_actSortGradient->setEnabled(count > 2);
    m_actReduce->setEnabled(count > 2);
}

void PalettePanel::addColor()
{
    const QColor color = QColorDialog::getColor(Qt::white, this, tr("Add color to palette"));
    if (!color.isValid())
        return;
    if (!m_palette.addUnique(color.rgb()))
        return;   // this color is already there
    m_list->addItem(makeItem(color.rgb()));
    emitChanged();
}

void PalettePanel::editSelected()
{
    const QList<QListWidgetItem*> selected = m_list->selectedItems();
    if (selected.size() != 1)
        return;
    onItemDoubleClicked(selected.first());
}

void PalettePanel::sortAsGradient()
{
    m_palette.sortAsGradient();
    rebuildList();
    emit paletteChanged();
}

void PalettePanel::reduceColors()
{
    if (m_palette.size() < 3)
        return;
    bool ok = false;
    const int wanted = QInputDialog::getInt(this, tr("Reduce palette"),
                                            tr("How many colors to keep (now %1):").arg(m_palette.size()),
                                            std::min(16, m_palette.size() - 1), 2, m_palette.size() - 1, 1, &ok);
    if (!ok)
        return;
    const int before = m_palette.size();
    m_palette = palette_builder::reduce(m_palette, wanted);
    rebuildList();
    emit paletteChanged();
    updateState();
    QToolTip::showText(mapToGlobal(rect().center()), tr("Was %1 colors, now %2").arg(before).arg(m_palette.size()),
                       this, {}, 3000);
}

void PalettePanel::mergeFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Palette to add"), QString(),
                                                      tr("Images and palettes (*.png *.bmp *.jpg *.jpeg *.webp);;"
                                                         "All files (*)"));
    if (path.isEmpty())
        return;
    QString error;
    const Palette other = Palette::load(path, &error);
    if (other.isEmpty()) {
        QMessageBox::warning(this, tr("Could not read the file"),
                             error.isEmpty() ? tr("The file has no colors.") : error);
        return;
    }
    const int added = m_palette.merge(other);
    rebuildList();
    emit paletteChanged();
    updateState();
    QToolTip::showText(mapToGlobal(rect().center()),
                       added > 0 ? tr("Colors added: %1").arg(added) : tr("No new colors found"), this, {},
                       3000);
}

void PalettePanel::onItemDoubleClicked(QListWidgetItem* item)
{
    const int row = m_list->row(item);
    if (row < 0)
        return;
    const QColor current = QColor::fromRgb(m_palette.at(row));
    const QColor color = QColorDialog::getColor(current, this, tr("Edit palette color"));
    if (!color.isValid() || color.rgb() == current.rgb())
        return;
    m_palette.set(row, color.rgb());
    item->setIcon(swatchIcon(color.rgb()));
    item->setData(kColorRole, uint(color.rgb()));
    item->setToolTip(color.name().toUpper());
    emitChanged();
}

void PalettePanel::removeSelected()
{
    QList<int> rows;
    for (QListWidgetItem* item : m_list->selectedItems())
        rows << m_list->row(item);
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        m_palette.removeAt(row);
        delete m_list->takeItem(row);
    }
    emitChanged();
}

void PalettePanel::clearAll()
{
    if (m_palette.isEmpty())
        return;
    m_palette.clear();
    rebuildList();
    emit paletteChanged();
}

void PalettePanel::sortByLuminance()
{
    m_palette.sortByLuminance();
    rebuildList();
    emit paletteChanged();
}

void PalettePanel::sortByHue()
{
    m_palette.sortByHue();
    rebuildList();
    emit paletteChanged();
}

void PalettePanel::onRowsMoved()
{
    if (m_rebuilding)
        return;
    // The order in the list changed by dragging, so the palette is rebuilt from the items
    QVector<QRgb> colors;
    colors.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i)
        colors.append(QRgb(m_list->item(i)->data(kColorRole).toUInt()));
    m_palette = Palette(colors);
    emitChanged();
}

void PalettePanel::showContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    menu.addAction(m_actAdd);
    menu.addAction(m_actEdit);
    menu.addAction(m_actRemove);
    menu.addSeparator();
    menu.addAction(m_actSortLuma);
    menu.addAction(m_actSortHue);
    menu.addAction(m_actSortGradient);
    menu.addSeparator();
    menu.addAction(m_actReduce);
    menu.addAction(m_actMerge);
    menu.addSeparator();
    menu.addAction(m_actClear);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

} // namespace kolorit
