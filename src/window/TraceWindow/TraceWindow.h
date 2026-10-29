/*

  Copyright (c) 2015, 2016 Hubert Denkmair <hubert@denkmair.de>
  Copyright (c) 2026 Schildkroet

  This file is part of CANgaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#pragma once

#include <QSet>
#include <QTimer>

#include "core/BusMessage.h"
#include "core/ConfigurableWidget.h"
#include "driver/CanDriver.h"

#include "TraceFilterModel.h"
#include "TraceViewTypes.h"
#include "window/MonitorWindow/TraceToolbar.h"

namespace Ui {
class TraceWindow;
}
class Backend;
class QDomDocument;
class QDomElement;
class QMenu;
class QSortFilterProxyModel;
class QToolButton;
class QTreeView;
class AggregatedTraceViewModel;
class UnifiedTraceViewModel;


class TraceWindow : public ConfigurableWidget
{
    Q_OBJECT

public:
    enum mode_t {
        mode_aggregated,
        mode_unified
    };

    explicit TraceWindow(QWidget *parent, Backend &backend);
    ~TraceWindow();

    void setMode(mode_t mode);

    virtual bool saveXML(Backend &backend, QDomDocument &xml, QDomElement &root);
    virtual bool loadXML(Backend &backend, QDomElement &el);

    // Shows `page` (the Monitor window) on the first tab instead of the
    // aggregated tree. The view / timestamp / filter toolbar then moves into
    // the UDS / J1939 / XCP tabs, so all tabs sit in one row at the top.
    void setMonitorPage(QWidget *page);

protected:
    void retranslateUi() override;

public slots:
    void addMessage(const BusMessage &msg);

private slots:
    void onRowsInserted(const QModelIndex & parent, int first, int last);

    void on_tabs_currentChanged(int index);
    void openFilterDialog();

private slots:
    void doScrollToBottom();

private:
    void placeToolbar(int tab);
    void buildToolbar();

    Ui::TraceWindow *ui;
    Backend *_backend;
    QWidget *_monitorPage = nullptr;
    timestamp_mode_t _timestampMode;
    QTimer _scrollTimer;
    enum Category {
        Cat_Aggregated = 0,
        Cat_UDS = 1,
        Cat_J1939 = 2,
        Cat_XCP = 3,
        Cat_OBD = 4,
        Cat_Count = 5
    };

    bool _scrollPending[Cat_Count] = {};

    // Pause per tab (UDS / J1939 / XCP / OBD2), like the Monitor tab's Pause
    bool _paused[Cat_Count] = {};
    QTimer _pauseLabelTimer;
    void setTabPaused(int tab, bool paused);
    void updatePauseLabel();

    // The protocol tabs share one TraceToolbar (GUI_DESIGN.md 3.1); each tab keeps
    // its own state and filter (T-12), shown when the tab comes to the front
    struct TabFilter
    {
        bool showTx = true;
        bool showRx = true;
        QSet<uint32_t> hiddenMessageIds;
        QSet<uint8_t> hiddenLinFrameIds;
        QSet<BusInterfaceId> hiddenInterfaces;
    };
    TraceToolbar *_toolbar = nullptr;
    QToolButton *_xcpButton = nullptr;
    TraceToolbar::State _tabState[Cat_Count];
    TabFilter _tabFilter[Cat_Count];
    // Time column until the Timestamp column exists: 0 = Δt / Abs time, 1 = time of day, 2 = UTC
    int _timeOfDay[Cat_Count] = {};

    [[nodiscard]] QTreeView *tree(int tab) const;
    [[nodiscard]] int currentProtocolTab() const;     // -1 on the Monitor tab
    void setTabMode(int tab, mode_t mode);
    void setLegacyTimestampMode(int tab, int mode);
    [[nodiscard]] int legacyTimestampMode(int tab) const;
    [[nodiscard]] QString absTimeToolTip(int tab) const;
    void applyTabTime(int tab);
    void applyTabFilter(int tab);
    void applyFindFilter(int tab);
    void applyExpand(int tab);
    [[nodiscard]] int filterCount(int tab) const;
    [[nodiscard]] QList<QModelIndex> findMatches(int tab) const;
    void findInTab(bool forward);
    void updateFindPosition();
    void fillMoreMenu(QMenu *menu);

    TraceFilterModel * _filterModels[Cat_Count];
    UnifiedTraceViewModel *_viewModels[Cat_Count];
    UnifiedTraceViewModel *_altViewModels[Cat_Count];   // alternative mode per tab (null for monitor)
    TraceFilterModel *_altFilterModels[Cat_Count];
    mode_t _tabModes[Cat_Count];
    AggregatedTraceViewModel *_aggregatedTraceViewModel;
    QSortFilterProxyModel *_aggregatedProxyModel;
    TraceFilterModel * _aggMonitorFilterModel;

    void openXcpSessions();
    void updateXcpStatus();
    // XCP IDs in the workspace; the interface is kept by USB device path and name
    [[nodiscard]] QString interfaceDevice(BusInterfaceId id) const;
    void saveXcpSessions(QDomDocument &xml, QDomElement &root) const;
    void loadXcpSessions(const QDomElement &el);
};
