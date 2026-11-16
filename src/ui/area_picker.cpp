// SPDX-License-Identifier: GPL-2.0-only
#include "ui/area_picker.hpp"
#include "app/i18n.hpp"
#include "app/qt_compat.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace kaibo::ui {
namespace {
constexpr int kAreaRole = Qt::UserRole + 1;
}

AreaPicker::AreaPicker(QWidget *parent) : QFrame(parent)
{
	// Qt::Popup maps to a WS_POPUP window on Windows, and the system never routes
	// IME composition to those, so the search field silently refused CJK input.
	// A tool window is an ordinary top-level window and does get IME; the popup
	// behaviours (close on outside click, on Esc, on app deactivate) are restored
	// in open() and eventFilter().
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
	setObjectId(this, QStringLiteral("KbPopup"));
	setAttribute(Qt::WA_StyledBackground);
	setFixedWidth(296);

	auto *col = new QVBoxLayout(this);
	col->setContentsMargins(8, 8, 8, 8);
	col->setSpacing(8);

	// QLineEdit::addAction places its icon against the line edit's own contents
	// rect, which ignores the QSS min-height, so the glyph sat well below the
	// text baseline. Lay the icon out ourselves so it centres on the field.
	auto *searchBox = new QFrame(this);
	setObjectId(searchBox, QStringLiteral("KbSearch"));
	// Fixed vertical policy: when the result list hides itself (no matches) the
	// layout redistributes the freed space, and a Preferred frame would be
	// stretched into a tall box, dragging the recent row out of place.
	searchBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	searchBox->setFixedHeight(38);
	auto *searchRow = new QHBoxLayout(searchBox);
	searchRow->setContentsMargins(10, 0, 8, 0);
	searchRow->setSpacing(6);
	m_searchIcon = new QLabel(searchBox);
	m_searchIcon->setFixedSize(16, 16);
	m_searchIcon->setPixmap(Theme::instance().pixmap(QStringLiteral("search"),
							 Theme::instance().color(QStringLiteral("ink-muted")), 16,
							 devicePixelRatioF()));
	m_searchIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
	searchRow->addWidget(m_searchIcon, 0, Qt::AlignVCenter);

	m_search = new QLineEdit(searchBox);
	m_search->setPlaceholderText(T("Area.Search"));
	m_search->setClearButtonEnabled(true);
	m_search->setFrame(false);
	m_search->setAccessibleName(T("Area.Search"));
	m_search->installEventFilter(this);
	searchRow->addWidget(m_search, 1);
	col->addWidget(searchBox);

	m_recentLabel = new QLabel(T("Area.Recent"), this);
	m_recentLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	col->addWidget(m_recentLabel);
	m_recentBox = new QWidget(this);
	m_recentBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
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

	m_retry = new QPushButton(T("Loading.Retry"), this);
	m_retry->setProperty("kbSize", QStringLiteral("sm"));
	m_retry->setCursor(Qt::PointingHandCursor);
	connect(m_retry, &QPushButton::clicked, this, &AreaPicker::retryRequested);
	col->addWidget(m_retry, 0, Qt::AlignHCenter);

	connect(m_search, &QLineEdit::textChanged, this, &AreaPicker::rebuildList);
	connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
		if (!(item->flags() & Qt::ItemIsSelectable))
			return; // "needs approval" areas are shown but cannot be chosen
		const int id = item->data(kAreaRole).toInt();
		if (id > 0)
			choose(id);
	});
	connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
		if (!(item->flags() & Qt::ItemIsSelectable))
			return;
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
	m_anchor = anchor;
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
		pos.setX((std::min)(pos.x(), area.right() - width()));
	}
	move(pos);
	if (QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
		app->installEventFilter(this);
	show();
	raise();
	activateWindow();
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

bool AreaPicker::needsUpdate(const std::vector<AreaGroup> &groups, const std::vector<int> &recent, int current,
			     bool loading) const
{
	return loading != m_loading || current != m_current || recent != m_recent || groups.size() != m_groups.size() ||
	       groups.empty() != m_groups.empty();
}

void AreaPicker::refreshTheme()
{
	setStyleSheet(Theme::instance().styleSheet());
	m_searchIcon->setPixmap(Theme::instance().pixmap(QStringLiteral("search"),
							 Theme::instance().color(QStringLiteral("ink-muted")), 16,
							 devicePixelRatioF()));
	if (!isVisible())
		return;
	rebuildRecent();
	rebuildList();
	update();
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
		if (!area || area->locked)
			continue;
		auto *chip = new QPushButton(qs(area->name), m_recentBox);
		chip->setProperty("kbVariant", QStringLiteral("chip"));
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
	const QColor faint = Theme::instance().color(QStringLiteral("ink-faint"));
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
			if (area.locked) {
				// Bilibili would reject this area at go-live, so it must not look
				// (or behave) like something the person can pick. The tooltip stays.
				item->setFlags(Qt::NoItemFlags);
				item->setForeground(faint);
			} else if (area.id == m_current) {
				selected = item;
			}
		}
	}
	if (selected) {
		m_list->setCurrentItem(selected);
		m_list->scrollToItem(selected, QAbstractItemView::PositionAtCenter);
	}

	const bool empty = m_list->count() == 0;
	m_list->setVisible(!empty);
	m_empty->setVisible(empty);
	// A failed load keeps a visible retry action instead of asking the person to
	// close and reopen the picker.
	m_retry->setVisible(empty && !m_loading && m_groups.empty());
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
	// Stands in for the dismissal behaviour a Qt::Popup window gets for free.
	if (isVisible()) {
		if (event->type() == QEvent::MouseButtonPress) {
			auto *mouse = static_cast<QMouseEvent *>(event);
			const QPoint global = mouse ? mouse->globalPosition().toPoint() : QPoint();
			// A press on the anchor button must not dismiss the picker here: the
			// button's own clicked handler toggles it on release, and hiding on
			// press made the picker blink shut and reopen.
			const bool onAnchor = m_anchor && m_anchor->rect().contains(m_anchor->mapFromGlobal(global));
			if (mouse && !onAnchor && !frameGeometry().contains(global))
				hide();
		} else if (event->type() == QEvent::ApplicationDeactivate) {
			hide();
		}
	}
	if (watched == m_search && event->type() == QEvent::KeyPress) {
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Escape) {
			hide();
			return true;
		}
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

void AreaPicker::hideEvent(QHideEvent *event)
{
	if (QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
		app->removeEventFilter(this);
	QFrame::hideEvent(event);
}

} // namespace kaibo::ui
