#include "ui/HistoryPanel.h"

#include "ui/Theme.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace kolorit {

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
    , m_status(new QLabel(this))
{
    m_list->setIconSize(QSize(18, 18));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setToolTip(tr("Click to return the image to the chosen step"));

    m_status->setObjectName(QStringLiteral("hintLabel"));
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignHCenter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_status);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_rebuilding || row < 0 || row == m_current)
            return;
        emit stateSelected(row);
    });

    setStates({}, 0);
}

void HistoryPanel::setStates(const QList<HistoryState>& states, int currentIndex)
{
    m_states = states;
    m_current = currentIndex;
    rebuild();
}

void HistoryPanel::refreshIcons()
{
    rebuild();
}

void HistoryPanel::rebuild()
{
    m_rebuilding = true;
    m_list->clear();

    for (int i = 0; i < m_states.size(); ++i) {
        const HistoryState& state = m_states.at(i);
        auto* item = new QListWidgetItem(theme::icon(state.icon), state.title, m_list);
        // Steps after the current one can still be returned to, but they disappear as soon
        // as a new operation is applied on top, so they are dimmed
        if (i > m_current) {
            QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
            item->setForeground(dim);
        }
        item->setToolTip(tr("%1 · %2×%3").arg(state.title).arg(state.size.width()).arg(state.size.height()));
    }

    if (m_current >= 0 && m_current < m_list->count())
        m_list->setCurrentRow(m_current);

    if (m_states.isEmpty())
        m_status->setText(tr("Open an image and its steps will appear here"));
    else if (m_states.size() == 1)
        m_status->setText(tr("Only the original so far"));
    else
        m_status->setText(tr("Step %1 of %2 · Ctrl+Z and Ctrl+Y").arg(m_current + 1).arg(m_states.size()));

    m_rebuilding = false;
}

} // namespace kolorit
