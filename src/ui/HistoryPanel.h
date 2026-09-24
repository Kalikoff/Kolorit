#pragma once

#include "core/ImageTask.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;

namespace kolorit {

/// One history step.
///
/// What is stored is not a picture but the operation that leads to this state from the
/// previous one (step zero has none: it is the opened file itself). The image of any step
/// is obtained by running the chain of operations from the original, so the history depth
/// costs almost no memory: twenty snapshots of a 12 MP photo would take up to a gigabyte.
struct HistoryState {
    QString title;   ///< label in the list: "Open", "Repaint (CIE Lab, Floyd–Steinberg)" and so on
    QString icon;    ///< icon name from the resources
    QSize size;      ///< image size after the step, for the tooltip
    ImageOp op;      ///< the operation producing this state from the previous one
};

/// History panel.
///
/// Works like in Photoshop: a list of states from top to bottom, the first being the
/// original image. Clicking any row returns the image to that state; steps below the
/// current one are dimmed: they can still be returned to, but they disappear as soon
/// as a new operation is applied on top.
class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    /// Redraw the list for a new history state
    void setStates(const QList<HistoryState>& states, int currentIndex);

    /// Redraw the icons after a theme change
    void refreshIcons();

signals:
    /// The user chose the step with this number
    void stateSelected(int index);

private:
    void rebuild();

    QListWidget* m_list;
    QLabel* m_status;
    QList<HistoryState> m_states;
    int m_current = 0;
    bool m_rebuilding = false;
};

} // namespace kolorit
