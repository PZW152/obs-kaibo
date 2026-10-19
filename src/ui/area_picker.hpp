// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/areas.hpp"

#include <QFrame>

#include <vector>

class QLineEdit;
class QListWidget;
class QLabel;
class QWidget;

namespace kaibo::ui {

class FlowLayout;

// Search-first popover for picking a live area, with recent areas as chips.
class AreaPicker : public QFrame {
	Q_OBJECT
public:
	explicit AreaPicker(QWidget *parent);
	void open(QWidget *anchor, const std::vector<AreaGroup> &groups, const std::vector<int> &recent, int current,
		  bool loading);
	// Refreshes the lists while open (e.g. the area list finished loading).
	void updateData(const std::vector<AreaGroup> &groups, const std::vector<int> &recent, int current,
			bool loading);
	bool isLoading() const { return m_loading; }

signals:
	void picked(int areaId);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void rebuildList();
	void rebuildRecent();
	void choose(int areaId);

	QLineEdit *m_search;
	QLabel *m_recentLabel;
	QWidget *m_recentBox;
	FlowLayout *m_recentFlow;
	QListWidget *m_list;
	QLabel *m_empty;
	std::vector<AreaGroup> m_groups;
	std::vector<int> m_recent;
	int m_current = 0;
	bool m_loading = false;
};

} // namespace kaibo::ui
