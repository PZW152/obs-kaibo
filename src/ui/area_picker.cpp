// SPDX-License-Identifier: GPL-2.0-only
#include "ui/area_picker.hpp"
#include "app/i18n.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace kaibo::ui {
namespace {
constexpr int kAreaRole = Qt::UserRole + 1;
}

AreaPicker::AreaPicker(QWidget *parent) : QFrame(parent, Qt::Popup)
{
	setObjectName(QStringLiteral("KbPopup"));
	setAttribute(Qt::WA_StyledBackground);
	setFixedWidth(296);

	auto *col = new QVBoxLayout(this);
	col->setContentsMargins(8, 8, 8, 8);
	col->setSpacing(8);

	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(T("Area.Search"));
	m_search->setClearButtonEnabled(true);
	m_search->addAction(Theme::instance().icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
	m_search->setAccessibleName(T("Area.Search"));
	m_search->installEventFilter(this);
	col->addWidget(m_search);

	m_recentLabel = new QLabel(T("Area.Recent"), this);
	col->addWidget(m_recentLabel);
	m_recentBox = new QWidget(this);
	m_recentFlow = new FlowLayout(m_recentBox, 6);
	col->addWidget(m_recentBox);

	m_list = new QListWidget(this);
	m_list->setUniformItemSizes(false);
	m_list->setMinimumHeight(260);
	m_list->setAccessibleName(T("Room.Area"));
	m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	col->addWidget(m_list, 1);

	m_empty = new QLabel(this);
	m_empty->setAlignment(Qt::AlignCenter);
	m_empty->setWordWrap(true);
	m_empty->setMinimumHeight(120);
	col->addWidget(m_empty);

	connect(m_search, &QLineEdit::textChanged, this, &AreaPicker::rebuildList);
	connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
		const int id = item->data(kAreaRole).toInt();
		if (id > 0)
			choose(id);
	});
	connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
		const int id = item->data(kAreaRole).toInt();
		if (id > 0)
			choose(id);
	});
}

void AreaPicker::open(QWidget *anchor, const std::vector<AreaGroup> &groups, const std::vector<int> &recent,
		      int current, bool loading)
{
	m_groups = groups;
	m_recent = recent;
	m_current = current;
	m_loading = loading;
	setStyleSheet(Theme::instance().styleSheet());
	m_search->clear();
	rebuildRecent();
	rebuildList();

	adjustSize();
	QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4));
	if (const QScreen *screen = anchor->screen()) {
		const QRect area = screen->availableGeometry();
		if (pos.y() + height() > area.bottom())
			pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 4);
		pos.setX(std::min(pos.x(), area.right() - width()));
	}
	move(pos);
	show();
	m_search->setFocus();
}

void AreaPicker::updateData(const std::vector<AreaGroup> &groups, const std::vector<int> &recent, int current,
			    bool loading)
{
	m_groups = groups;
	m_recent = recent;
	m_current = current;
	m_loading = loading;
	rebuildRecent();
	rebuildList();
	adjustSize();
}

void AreaPicker::rebuildRecent()
{
	while (QLayoutItem *item = m_recentFlow->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	int shown = 0;
	for (int id : m_recent) {
		const Area *area = findArea(m_groups, id);
		if (!area)
			continue;
		auto *chip = new QPushButton(qs(area->name), m_recentBox);
		chip->setProperty("kbOn", id == m_current);
		chip->setCursor(Qt::PointingHandCursor);
		connect(chip, &QPushButton::clicked, this, [this, id] { choose(id); });
		m_recentFlow->addWidget(chip);
		++shown;
	}
	m_recentLabel->setVisible(shown > 0);
	m_recentBox->setVisible(shown > 0);
}

void AreaPicker::rebuildList()
{
	m_list->clear();
	const QString query = m_search->text().trimmed();
	const auto results = searchAreas(m_groups, ss(query));
	QListWidgetItem *selected = nullptr;
	const QColor muted = Theme::instance().color(QStringLiteral("ink-muted"));
	for (const auto &group : results) {
		auto *header = new QListWidgetItem(qs(group.name), m_list);
		header->setFlags(Qt::NoItemFlags);
		QFont f = header->font();
		f.setPixelSize(11);
		f.setWeight(QFont::DemiBold);
		header->setFont(f);
		header->setForeground(muted);
		for (const auto &area : group.areas) {
			QString text = qs(area.name);
			if (area.locked)
				text += QStringLiteral("  ·  ") + T("Area.Locked");
			auto *item = new QListWidgetItem(text, m_list);
			item->setData(kAreaRole, area.id);
			item->setToolTip(area.locked ? T("Area.LockedHint") : qs(group.name + " › " + area.name));
			if (area.id == m_current)
				selected = item;
		}
	}
	if (selected) {
		m_list->setCurrentItem(selected);
		m_list->scrollToItem(selected, QAbstractItemView::PositionAtCenter);
	}

	const bool empty = m_list->count() == 0;
	m_list->setVisible(!empty);
	m_empty->setVisible(empty);
	if (empty)
		m_empty->setText(m_loading          ? T("Area.Loading")
				 : m_groups.empty() ? T("Area.Unavailable")
						    : T("Area.NoMatch").arg(query));
}

void AreaPicker::choose(int areaId)
{
	if (!isVisible())
		return; // itemClicked and itemActivated can both fire for one click
	hide();
	emit picked(areaId);
}

bool AreaPicker::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == m_search && event->type() == QEvent::KeyPress) {
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
			// Move through selectable rows without leaving the search field.
			int row = m_list->currentRow();
			const int step = key->key() == Qt::Key_Down ? 1 : -1;
			for (int i = row + step; i >= 0 && i < m_list->count(); i += step) {
				if (m_list->item(i)->flags() & Qt::ItemIsSelectable) {
					m_list->setCurrentRow(i);
					break;
				}
			}
			return true;
		}
		if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
			QListWidgetItem *item = m_list->currentItem();
			if (!item || !(item->flags() & Qt::ItemIsSelectable)) {
				for (int i = 0; i < m_list->count(); ++i)
					if (m_list->item(i)->flags() & Qt::ItemIsSelectable) {
						item = m_list->item(i);
						break;
					}
			}
			if (item)
				choose(item->data(kAreaRole).toInt());
			return true;
		}
	}
	return QFrame::eventFilter(watched, event);
}

} // namespace kaibo::ui
