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

namespace Ui {
class TraceWindow;
}
class Backend;
class QDomDocument;
class QDomElement;
class QSortFilterProxyModel;
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
    void setTimestampMode(int mode);

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

    void on_cbTimestampMode_currentIndexChanged(int index);
    void on_cbFilterChanged(void);

    void on_cbTraceClearpushButton(void);
    void on_cbViewMode_currentIndexChanged(int index);
    void on_tabs_currentChanged(int index);
    void openFilterDialog();

private slots:
    void doScrollToBottom();

private:
    void placeToolbar(int tab);
    void applyMonitorToolbarLayout();

    Ui::TraceWindow *ui;
    Backend *_backend;
    QWidget *_monitorPage = nullptr;
    timestamp_mode_t _timestampMode;
    QTimer _scrollTimer;
    QTimer _filterDebounceTimer;
    enum Category {
        Cat_Aggregated = 0,
        Cat_UDS = 1,
        Cat_J1939 = 2,
        Cat_XCP = 3,
        Cat_Count = 4
    };

    bool _scrollPending[Cat_Count] = {};

    TraceFilterModel * _filterModels[Cat_Count];
    UnifiedTraceViewModel *_viewModels[Cat_Count];
    UnifiedTraceViewModel *_altViewModels[Cat_Count];   // alternative mode per tab (null for monitor)
    TraceFilterModel *_altFilterModels[Cat_Count];
    mode_t _tabModes[Cat_Count];
    AggregatedTraceViewModel *_aggregatedTraceViewModel;
    QSortFilterProxyModel *_aggregatedProxyModel;
    TraceFilterModel * _aggMonitorFilterModel;

    bool _filterShowTx = true;
    bool _filterShowRx = true;
    QSet<uint32_t> _filterHiddenMessageIds;
    QSet<uint8_t>  _filterHiddenLinFrameIds;
    QSet<BusInterfaceId> _filterHiddenInterfaces;

    void applyDialogFilters();
    void applyIcons();
    void openXcpSessions();
    void updateXcpStatus();
    // XCP IDs in the workspace; the interface is kept by USB device path and name
    [[nodiscard]] QString interfaceDevice(BusInterfaceId id) const;
    void saveXcpSessions(QDomDocument &xml, QDomElement &root) const;
    void loadXcpSessions(const QDomElement &el);
};
