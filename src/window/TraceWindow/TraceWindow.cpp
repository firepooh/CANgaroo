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

#include "TraceWindow.h"
#include "ui_TraceWindow.h"

#include <algorithm>
#include <iterator>

#include <QActionGroup>
#include <QDomDocument>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/Backend.h"
#include "core/BusTrace.h"
#include "core/Log.h"
#include "driver/BusInterface.h"
#include "core/ThemeManager.h"
#include "helpers/apphelpers.h"
#include "window/ConditionalLoggingDialog/ConditionalLoggingDialog.h"

#include "AggregatedTraceViewModel.h"
#include "MonitorStyleDelegate.h"
#include "TraceFilterDialog.h"
#include "TraceFilterModel.h"
#include "UnifiedTraceViewModel.h"
#include "XcpSessionDialog.h"
#include "decoders/XcpSessions.h"
#include "window/MonitorWindow/TraceFind.h"


TraceWindow::TraceWindow(QWidget *parent, Backend &backend) :
    ConfigurableWidget(parent),
    ui(new Ui::TraceWindow),
    _backend(&backend),
    _timestampMode(timestamp_mode_absolute)
{
    ui->setupUi(this);

    _aggregatedTraceViewModel = new AggregatedTraceViewModel(backend);
    _aggregatedProxyModel = new QSortFilterProxyModel(this);
    _aggregatedProxyModel->setSourceModel(_aggregatedTraceViewModel);
    _aggregatedProxyModel->setDynamicSortFilter(false);

    _aggMonitorFilterModel = new TraceFilterModel(this);
    _aggMonitorFilterModel->setSourceModel(_aggregatedProxyModel);

    // Initialize the 4 rolling categories
    UnifiedTraceViewModel::Category cats[] = {
        UnifiedTraceViewModel::Cat_All,
        UnifiedTraceViewModel::Cat_UDS,
        UnifiedTraceViewModel::Cat_J1939,
        UnifiedTraceViewModel::Cat_XCP,
        UnifiedTraceViewModel::Cat_OBD
    };

    QTreeView* trees[] = { ui->treeAgg, ui->treeUds, ui->treeJ1939, ui->treeXcp, ui->treeObd };

    for (int i = 0; i < Cat_Count; ++i) {
        _viewModels[i] = new UnifiedTraceViewModel(backend, cats[i]);
        _filterModels[i] = new TraceFilterModel(this);
        _filterModels[i]->setSourceModel(_viewModels[i]);

        QTreeView* tree = trees[i];
        tree->setModel(_filterModels[i]);
        tree->setRootIsDecorated(true);
        MonitorStyleDelegate::applyTo(tree);

        connect(_filterModels[i], &QAbstractItemModel::rowsInserted, this, &TraceWindow::onRowsInserted);
    }

    // Protocol tabs: the type column only repeats "UDS" / "XCP", and the ID
    // carries the SID or PGN
    const int ch = ui->treeXcp->fontMetrics().averageCharWidth();
    ui->treeUds->setColumnHidden(BaseTraceViewModel::column_type, true);
    ui->treeXcp->setColumnHidden(BaseTraceViewModel::column_type, true);
    ui->treeObd->setColumnHidden(BaseTraceViewModel::column_type, true);
    ui->treeUds->setColumnWidth(BaseTraceViewModel::column_canid, 16 * ch);
    ui->treeJ1939->setColumnWidth(BaseTraceViewModel::column_canid, 22 * ch);
    // XCP packet names such as "DAQ-DTO (DAQ #12|ODT #3)" need more room than message names
    ui->treeXcp->setColumnWidth(BaseTraceViewModel::column_name, 34 * ch);
    // OBD2: the ID column also shows service and PID ("0x7E8 (41 0C)"), the name is the PID's
    ui->treeObd->setColumnWidth(BaseTraceViewModel::column_canid, 22 * ch);
    ui->treeObd->setColumnWidth(BaseTraceViewModel::column_name, 34 * ch);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [trees](ThemeManager::Theme) {
        for (QTreeView *tree : trees) { MonitorStyleDelegate::applyPalette(tree); }
    });

    connect(_aggMonitorFilterModel, &QAbstractItemModel::rowsInserted, this, &TraceWindow::onRowsInserted);

    // Per-tab default modes
    _tabModes[Cat_Aggregated] = mode_aggregated;
    _tabModes[Cat_UDS]        = mode_aggregated;   // V-1: Scroll off on every tab
    _tabModes[Cat_J1939]      = mode_aggregated;
    _tabModes[Cat_XCP]        = mode_aggregated;
    _tabModes[Cat_OBD]        = mode_aggregated;

    // Alt models: UDS aggregated, J1939 rolling (monitor tab has no alt model here)
    _altViewModels[Cat_Aggregated] = nullptr;
    _altFilterModels[Cat_Aggregated] = nullptr;

    auto makeAltModel = [&](int i, UnifiedTraceViewModel::Category cat, bool aggregating) {
        _altViewModels[i] = new UnifiedTraceViewModel(backend, cat);
        _altViewModels[i]->setAggregating(aggregating);
        _altViewModels[i]->setTimestampMode(_timestampMode);
        _altFilterModels[i] = new TraceFilterModel(this);
        _altFilterModels[i]->setSourceModel(_altViewModels[i]);

        connect(_altFilterModels[i], &QAbstractItemModel::rowsInserted, this, &TraceWindow::onRowsInserted);
    };

    makeAltModel(Cat_UDS,    UnifiedTraceViewModel::Cat_UDS,    true);   // UDS aggregated
    makeAltModel(Cat_J1939,  UnifiedTraceViewModel::Cat_J1939,  false);  // J1939 rolling
    makeAltModel(Cat_XCP,    UnifiedTraceViewModel::Cat_XCP,    false);  // XCP rolling (command sequence)
    makeAltModel(Cat_OBD,    UnifiedTraceViewModel::Cat_OBD,    false);  // OBD2 rolling (request / response order)

    connect(&XcpSessions::instance(), &XcpSessions::changed, this, &TraceWindow::updateXcpStatus);

    // Settings dialog defaults. The Monitor tab's hidden tree keeps its view mode
    // setting; the protocol tabs start with Scroll off (V-1) and the default time.
    QSettings appSettings;
    const auto defaultViewMode = static_cast<mode_t>(
        appSettings.value("tracewindow/defaultViewMode", mode_aggregated).toInt());
    _tabModes[Cat_Aggregated] = defaultViewMode;
    const int defaultTsMode = appSettings.value("tracewindow/defaultTimestampMode", timestamp_mode_delta).toInt();
    for (int tab = Cat_UDS; tab < Cat_Count; ++tab) {
        setLegacyTimestampMode(tab, defaultTsMode);
    }

    buildToolbar();
    updateXcpStatus();

    _scrollTimer.setInterval(100);
    _scrollTimer.setSingleShot(true);
    connect(&_scrollTimer, &QTimer::timeout, this, &TraceWindow::doScrollToBottom);

    for (int tab = Cat_Aggregated; tab < Cat_Count; ++tab) {
        setTabMode(tab, _tabModes[tab]);
        if (tab > Cat_Aggregated) { applyTabTime(tab); }
    }
}

TraceWindow::~TraceWindow()
{
    delete ui;
    delete _aggregatedTraceViewModel;
    for (int i = 0; i < Cat_Count; ++i)
    {
        delete _viewModels[i];
        delete _altViewModels[i];
    }
}

void TraceWindow::retranslateUi()
{
    ui->retranslateUi(this);
}

QTreeView *TraceWindow::tree(int tab) const
{
    QTreeView *trees[] = { ui->treeAgg, ui->treeUds, ui->treeJ1939, ui->treeXcp, ui->treeObd };
    return trees[tab];
}

int TraceWindow::currentProtocolTab() const
{
    const int tab = ui->tabs->currentIndex();
    return (tab > Cat_Aggregated && tab < Cat_Count) ? tab : -1;
}

void TraceWindow::buildToolbar()
{
    // The shared toolbar takes the place of the form's View / Time / Filter / Find /
    // Clear / Autoscroll widgets. They stay in TraceWindow.ui, hidden, so the form
    // keeps the upstream layout.
    QHBoxLayout *bar = ui->horizontalLayout;
    while (QLayoutItem *item = bar->takeAt(0)) {
        if (item->widget()) { item->widget()->hide(); }
        delete item;
    }
    for (QWidget *old : std::initializer_list<QWidget *>{ ui->label_3, ui->cbViewMode, ui->label, ui->cbTimestampMode,
                                                          ui->filterButton, ui->label_2, ui->filterLineEdit,
                                                          ui->TraceClearpushButton, ui->cbAutoscroll,
                                                          ui->xcpSessionsButton, ui->line }) {
        old->hide();
    }
    bar->setContentsMargins(0, 0, 0, 0);
    _toolbar = new TraceToolbar(ui->widget);
    bar->addWidget(_toolbar);

    // XCP tab only: the XCP IDs button and the session status, in the tab slot
    _xcpButton = new QToolButton(_toolbar);
    _xcpButton->setText(tr("XCP IDs…"));
    _xcpButton->setToolTip(tr("Request and response IDs of the XCP slave on each interface"));
    _xcpButton->setProperty("iconName", "preferences-other");
    _xcpButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(_xcpButton, &QToolButton::clicked, this, &TraceWindow::openXcpSessions);
    _toolbar->addToTabSlot(_xcpButton);
    _toolbar->addToTabSlot(ui->xcpStatusLabel);

    connect(_toolbar, &TraceToolbar::scrollToggled, this, [this](bool on)
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        _tabState[tab].scroll = on;
        setTabMode(tab, on ? mode_unified : mode_aggregated);
        emit settingsChanged(this);
    });
    connect(_toolbar, &TraceToolbar::expandToggled, this, [this](bool on)
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        _tabState[tab].expand = on;
        applyExpand(tab);
        emit settingsChanged(this);
    });
    connect(_toolbar, &TraceToolbar::absTimeToggled, this, [this](bool on)
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        _tabState[tab].absTime = on;
        applyTabTime(tab);
        emit settingsChanged(this);
    });
    connect(_toolbar, &TraceToolbar::pauseToggled, this, [this](bool on)
    {
        const int tab = currentProtocolTab();
        if (tab >= 0) { setTabPaused(tab, on); }
    });
    connect(_toolbar, &TraceToolbar::eraseClicked, this, [this]()
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        for (UnifiedTraceViewModel *model : { _viewModels[tab], _altViewModels[tab] }) {
            if (model) { model->erase(); }
        }
        applyExpand(tab);
        updateFindPosition();
    });
    connect(_toolbar, &TraceToolbar::findTextChanged, this, [this](const QString &text)
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        _tabState[tab].findText = text;
        applyFindFilter(tab);
        updateFindPosition();
        emit settingsChanged(this);
    });
    connect(_toolbar, &TraceToolbar::findNext, this, [this]() { findInTab(true); });
    connect(_toolbar, &TraceToolbar::findPrevious, this, [this]() { findInTab(false); });
    connect(_toolbar, &TraceToolbar::onlyMatchesToggled, this, [this](bool on)
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        _tabState[tab].onlyMatches = on;
        applyFindFilter(tab);
        emit settingsChanged(this);
    });
    connect(_toolbar, &TraceToolbar::filterClicked, this, &TraceWindow::openFilterDialog);
    connect(_toolbar, &TraceToolbar::followToggled, this, [this](bool on)
    {
        const int tab = currentProtocolTab();
        if (tab < 0) { return; }
        _tabState[tab].follow = on;
        if (on) { tree(tab)->scrollToBottom(); }
        emit settingsChanged(this);
    });
    connect(_toolbar, &TraceToolbar::moreMenuAboutToShow, this, &TraceWindow::fillMoreMenu);

    // T-14: the user's own scrolling and row picking switch Follow
    for (int tab = Cat_UDS; tab < Cat_Count; ++tab) {
        auto *watcher = new FollowWatcher(tree(tab));
        connect(watcher, &FollowWatcher::followChanged, this, [this, tab](bool on)
        {
            if (_tabState[tab].follow == on) { return; }
            _tabState[tab].follow = on;
            if (ui->tabs->currentIndex() == tab) { _toolbar->setFollow(on); }
        });
    }

    _pauseLabelTimer.setInterval(500);
    connect(&_pauseLabelTimer, &QTimer::timeout, this, &TraceWindow::updatePauseLabel);
}

void TraceWindow::setMode(TraceWindow::mode_t mode)
{
    setTabMode(ui->tabs->currentIndex(), mode);
}

void TraceWindow::setTabMode(int tabIdx, TraceWindow::mode_t mode)
{
    _tabModes[tabIdx] = mode;
    QTreeView *tree = this->tree(tabIdx);

    if (tabIdx == Cat_Aggregated) {
        if (mode == mode_aggregated) {
            tree->setModel(_aggMonitorFilterModel);
            tree->setSortingEnabled(true);
            tree->sortByColumn(BaseTraceViewModel::column_canid, Qt::AscendingOrder);
        } else {
            tree->setSortingEnabled(false);
            tree->header()->setSortIndicator(-1, Qt::AscendingOrder);
            _filterModels[Cat_Aggregated]->sort(-1);
            tree->setModel(_filterModels[Cat_Aggregated]);
            tree->setRootIsDecorated(true);
        }
    } else {
        // UDS and J1939: primary model = the original (rolling for UDS, aggregated for J1939)
        // alt model = the new alternative (aggregated for UDS, rolling for J1939)
        bool usePrimary = (tabIdx == Cat_UDS)   ? (mode == mode_unified)
                        : (tabIdx == Cat_J1939 || tabIdx == Cat_XCP || tabIdx == Cat_OBD) ? (mode == mode_aggregated)
                        : true;
        if (usePrimary) {
            tree->setSortingEnabled(false);
            tree->header()->setSortIndicator(-1, Qt::AscendingOrder);
            _filterModels[tabIdx]->sort(-1);
            tree->setModel(_filterModels[tabIdx]);
        } else if (tabIdx == Cat_UDS) {
            // The UDS alternative is aggregated: sort it by ID
            tree->setSortingEnabled(true);
            tree->sortByColumn(BaseTraceViewModel::column_canid, Qt::AscendingOrder);
            tree->setModel(_altFilterModels[tabIdx]);
        } else {
            // The J1939 / XCP / OBD2 alternative is the rolling log: keep the arrival order
            tree->setSortingEnabled(false);
            tree->header()->setSortIndicator(-1, Qt::AscendingOrder);
            _altFilterModels[tabIdx]->sort(-1);
            tree->setModel(_altFilterModels[tabIdx]);
        }
        applyExpand(tabIdx);
    }

    if (tabIdx == Cat_Aggregated || _tabState[tabIdx].follow) {
        tree->scrollToBottom();
    }
}

void TraceWindow::setMonitorPage(QWidget *page)
{
    _monitorPage = page;
    ui->treeAgg->hide();
    ui->verticalLayout_2->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout_2->addWidget(page);
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    // The protocol tabs get the same frame as the Monitor tab: tree flush with the tab
    for (QVBoxLayout *page : { ui->verticalLayout_3, ui->verticalLayout_4, ui->verticalLayout_xcp, ui->verticalLayout_obd }) {
        page->setContentsMargins(0, 0, 0, 0);
        page->setSpacing(0);
    }
    placeToolbar(ui->tabs->currentIndex());
}

void TraceWindow::placeToolbar(int tab)
{
    if (!_monitorPage) { return; }

    // The Monitor page has its own toolbar; the other tabs get this one above their tree
    if (tab == Cat_Aggregated) {
        ui->widget->hide();
        return;
    }
    auto *layout = qobject_cast<QVBoxLayout *>(ui->tabs->widget(tab)->layout());
    if (layout && layout->indexOf(ui->widget) != 0) {
        layout->insertWidget(0, ui->widget);
    }
    ui->widget->show();
}

void TraceWindow::setTabPaused(int tab, bool paused)
{
    if (tab <= Cat_Aggregated || tab >= Cat_Count) {
        return;     // the Monitor tab has its own Pause
    }
    _paused[tab] = paused;
    _tabState[tab].paused = paused;
    for (UnifiedTraceViewModel *model : { _viewModels[tab], _altViewModels[tab] }) {
        if (model) {
            model->setPaused(paused);
        }
    }
    if (paused) {
        _pauseLabelTimer.start();
    } else if (std::none_of(std::begin(_paused), std::end(_paused), [](bool p) { return p; })) {
        _pauseLabelTimer.stop();
    }
    updatePauseLabel();
}

void TraceWindow::updatePauseLabel()
{
    const int tab = currentProtocolTab();
    if (tab < 0 || !_paused[tab] || !_viewModels[tab]) {
        return;
    }
    // The primary and alternative models of a tab pause together, so either one counts
    _toolbar->setPauseInfo(_viewModels[tab]->pendingFrames(), _viewModels[tab]->framesSkippedWhilePaused());
}

void TraceWindow::on_tabs_currentChanged(int index)
{
    placeToolbar(index);
    if (index <= Cat_Aggregated || index >= Cat_Count) {
        return;
    }
    // One toolbar for the protocol tabs: show the state of the tab now in front
    _toolbar->setState(_tabState[index]);
    _toolbar->setFilterCount(filterCount(index));
    _toolbar->setAbsTimeEnabled(_timeOfDay[index] == 0, absTimeToolTip(index));
    updatePauseLabel();
    // XCP IDs and the session status are the XCP tab's own
    _toolbar->setTabSlotVisible(index == Cat_XCP);
    updateFindPosition();
}

void TraceWindow::setLegacyTimestampMode(int tab, int mode)
{
    // One Time box for all tabs before the shared toolbar: Delta, Relative, or a time of day
    _tabState[tab].absTime = mode == timestamp_mode_relative;
    _timeOfDay[tab] = mode == timestamp_mode_absolute ? 1 : mode == timestamp_mode_absolute_utc ? 2 : 0;
}

int TraceWindow::legacyTimestampMode(int tab) const
{
    if (_timeOfDay[tab] == 1) { return timestamp_mode_absolute; }
    if (_timeOfDay[tab] == 2) { return timestamp_mode_absolute_utc; }
    return _tabState[tab].absTime ? timestamp_mode_relative : timestamp_mode_delta;
}

QString TraceWindow::absTimeToolTip(int tab) const
{
    return _timeOfDay[tab] == 0 ? QString()
        : tr("The Time column shows the time of day. Change it in the ⋯ menu, Time column");
}

void TraceWindow::applyTabTime(int tab)
{
    const auto mode = static_cast<timestamp_mode_t>(legacyTimestampMode(tab));
    for (UnifiedTraceViewModel *model : { _viewModels[tab], _altViewModels[tab] }) {
        if (model) { model->setTimestampMode(mode); }
    }
    tree(tab)->viewport()->update();
    if (ui->tabs->currentIndex() == tab) {
        _toolbar->setAbsTimeEnabled(_timeOfDay[tab] == 0, absTimeToolTip(tab));
    }
}

void TraceWindow::applyExpand(int tab)
{
    if (_tabState[tab].expand) {
        tree(tab)->expandAll();
    } else {
        tree(tab)->collapseAll();
    }
}

void TraceWindow::applyFindFilter(int tab)
{
    // Only matches hides rows; Find alone only moves to them (T-11)
    const QString text = _tabState[tab].onlyMatches ? _tabState[tab].findText : QString();
    for (TraceFilterModel *model : { _filterModels[tab], _altFilterModels[tab] }) {
        if (model) {
            model->setFilterText(text);
            model->invalidate();
        }
    }
    applyExpand(tab);
}

QList<QModelIndex> TraceWindow::findMatches(int tab) const
{
    // Every matching line in view order: a row, then its decoded and frame lines (T-8)
    QList<QModelIndex> matches;
    const QString text = _tabState[tab].findText;
    QAbstractItemModel *model = tree(tab)->model();
    if (text.isEmpty() || !model) { return matches; }
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        if (TraceFilterModel::lineMatches(index, text)) { matches.append(index); }
        for (int child = 0; child < model->rowCount(index); ++child) {
            const QModelIndex childIndex = model->index(child, 0, index);
            if (TraceFilterModel::lineMatches(childIndex, text)) { matches.append(childIndex); }
        }
    }
    return matches;
}

void TraceWindow::findInTab(bool forward)
{
    const int tab = currentProtocolTab();
    if (tab < 0) { return; }
    QTreeView *view = tree(tab);
    const QList<QModelIndex> matches = findMatches(tab);
    const QModelIndex current = view->currentIndex();
    const int at = current.isValid() ? static_cast<int>(matches.indexOf(current.siblingAtColumn(0))) : -1;
    const int next = TraceFind::step(at, static_cast<int>(matches.size()), forward);
    if (next >= 0) {
        const QModelIndex target = matches[next];
        // T-10: a match in a closed row opens it and picks the matching line
        if (target.parent().isValid()) { view->expand(target.parent()); }
        _tabState[tab].follow = false;
        _toolbar->setFollow(false);
        view->setCurrentIndex(target);
        view->scrollTo(target, QAbstractItemView::PositionAtCenter);
    }
    _toolbar->setFindPosition(next, static_cast<int>(matches.size()));
}

void TraceWindow::updateFindPosition()
{
    const int tab = currentProtocolTab();
    if (tab < 0) { return; }
    if (_tabState[tab].findText.isEmpty()) {
        _toolbar->setFindPosition(-1, -1);
        return;
    }
    const QList<QModelIndex> matches = findMatches(tab);
    const QModelIndex current = tree(tab)->currentIndex();
    _toolbar->setFindPosition(current.isValid() ? static_cast<int>(matches.indexOf(current.siblingAtColumn(0))) : -1,
                              static_cast<int>(matches.size()));
}

void TraceWindow::fillMoreMenu(QMenu *menu)
{
    const int tab = currentProtocolTab();
    if (tab < 0) { return; }

    // Until the Timestamp column (GUI_DESIGN.md step 3) the time of day is shown in the
    // Time column, as the former Time box's Absolute and Absolute (UTC) did
    QMenu *time = menu->addMenu(tr("Time column"));
    auto *group = new QActionGroup(time);
    const QString names[] = { tr("Δt, or since start with Abs time"), tr("Time of day"), tr("Time of day (UTC)") };
    for (int choice = 0; choice < 3; ++choice) {
        QAction *action = time->addAction(names[choice]);
        action->setCheckable(true);
        action->setChecked(_timeOfDay[tab] == choice);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, tab, choice]()
        {
            _timeOfDay[tab] = choice;
            applyTabTime(tab);
            emit settingsChanged(this);
        });
    }

    QHeaderView *header = tree(tab)->header();
    QAbstractItemModel *model = tree(tab)->model();
    QMenu *columns = menu->addMenu(tr("Columns"));
    for (int col = 0; model && col < model->columnCount(); ++col) {
        QAction *action = columns->addAction(model->headerData(col, Qt::Horizontal).toString());
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(col));
        connect(action, &QAction::toggled, this, [header, col](bool on) { header->setSectionHidden(col, !on); });
    }
}

int TraceWindow::filterCount(int tab) const
{
    const TabFilter &f = _tabFilter[tab];
    return TraceFind::filterCount(f.showTx, f.showRx, static_cast<int>(f.hiddenMessageIds.size()),
                                  static_cast<int>(f.hiddenLinFrameIds.size()),
                                  static_cast<int>(f.hiddenInterfaces.size()));
}

namespace
{

// Tab names in the workspace (TabView and TraceFilter tab="...")
const char *const k_tabNames[] = { "monitor", "uds", "j1939", "xcp", "obd2" };

} // namespace

bool TraceWindow::saveXML(Backend &backend, QDomDocument &xml, QDomElement &root)
{
    if (!ConfigurableWidget::saveXML(backend, xml, root))
    {
        return false;
    }

    root.setAttribute("type", "TraceWindow");
    // mode* and TimestampMode as before the shared toolbar, so older builds still read the views
    root.setAttribute("mode", _tabModes[Cat_Aggregated] == mode_unified ? "unified" : "aggregated");
    root.setAttribute("modeUds", _tabState[Cat_UDS].scroll ? "unified" : "aggregated");
    root.setAttribute("modeJ1939", _tabState[Cat_J1939].scroll ? "unified" : "aggregated");
    root.setAttribute("modeXcp", _tabState[Cat_XCP].scroll ? "unified" : "aggregated");
    root.setAttribute("modeObd", _tabState[Cat_OBD].scroll ? "unified" : "aggregated");
    root.setAttribute("TimestampMode", legacyTimestampMode(Cat_UDS));
    root.setAttribute("ActiveTab", ui->tabs->currentIndex());

    QDomElement elAggregated = xml.createElement("AggregatedTraceView");
    elAggregated.setAttribute("SortColumn", _aggregatedProxyModel->sortColumn());
    root.appendChild(elAggregated);

    for (int tab = Cat_UDS; tab < Cat_Count; ++tab)
    {
        QDomElement tabEl = xml.createElement("TabView");
        tabEl.setAttribute("tab", k_tabNames[tab]);
        _tabState[tab].save(tabEl);
        tabEl.setAttribute("timeOfDay", _timeOfDay[tab]);
        root.appendChild(tabEl);
    }

    // T-12: one filter per tab
    for (int tab = Cat_UDS; tab < Cat_Count; ++tab)
    {
        const TabFilter &f = _tabFilter[tab];
        QDomElement filterEl = xml.createElement("TraceFilter");
        filterEl.setAttribute("tab", k_tabNames[tab]);
        filterEl.setAttribute("showTx", f.showTx ? "1" : "0");
        filterEl.setAttribute("showRx", f.showRx ? "1" : "0");
        auto join = [](const auto &set)
        {
            QStringList list;
            for (auto v : set) { list.append(QString::number(v)); }
            list.sort();
            return list.join(',');
        };
        if (!f.hiddenMessageIds.isEmpty()) { filterEl.setAttribute("hiddenMessages", join(f.hiddenMessageIds)); }
        if (!f.hiddenLinFrameIds.isEmpty()) { filterEl.setAttribute("hiddenLinFrames", join(f.hiddenLinFrameIds)); }
        if (!f.hiddenInterfaces.isEmpty()) { filterEl.setAttribute("hiddenInterfaces", join(f.hiddenInterfaces)); }
        root.appendChild(filterEl);
    }

    saveXcpSessions(xml, root);

    return true;
}

QString TraceWindow::interfaceDevice(BusInterfaceId id) const
{
    // The USB device path in the interface details ("usb#vid_1d50&pid_606f&mi_00#7&...")
    // stays the same for an adapter, while candleN numbering can change between runs
    BusInterface *intf = _backend->getInterfaceById(id);
    if (!intf) { return QString(); }
    static const QRegularExpression usbPath(QStringLiteral("usb#[^#\\s|]+#[^#\\s|]+"),
                                            QRegularExpression::CaseInsensitiveOption);
    return usbPath.match(intf->getDetailsStr()).captured(0).toLower();
}

void TraceWindow::saveXcpSessions(QDomDocument &xml, QDomElement &root) const
{
    QDomElement xcpEl = xml.createElement("XcpSessions");
    for (const XcpConfig &s : XcpSessions::instance().sessions()) {
        QDomElement e = xml.createElement("Session");
        e.setAttribute("interface", _backend->getInterfaceName(s.interfaceId));
        e.setAttribute("device", interfaceDevice(s.interfaceId));
        e.setAttribute("request", QString::number(s.requestId, 16).toUpper());
        e.setAttribute("response", QString::number(s.responseId, 16).toUpper());
        if (s.broadcastId) { e.setAttribute("broadcast", QString::number(*s.broadcastId, 16).toUpper()); }
        e.setAttribute("extended", s.extended ? "1" : "0");
        xcpEl.appendChild(e);
    }
    root.appendChild(xcpEl);
}

void TraceWindow::loadXcpSessions(const QDomElement &el)
{
    // A workspace without XCP sessions clears them, like any other trace setting
    QList<XcpConfig> sessions;
    const QDomElement xcpEl = el.firstChildElement("XcpSessions");
    for (QDomElement e = xcpEl.firstChildElement("Session"); !e.isNull(); e = e.nextSiblingElement("Session")) {
        const QString device = e.attribute("device");
        const QString name = e.attribute("interface");

        // The same adapter first (by USB device path), then the same name
        std::optional<BusInterfaceId> found;
        const BusInterfaceIdList ids = _backend->getInterfaceList();
        if (!device.isEmpty()) {
            for (BusInterfaceId id : ids) {
                if (interfaceDevice(id) == device) { found = id; break; }
            }
        }
        if (!found) {
            for (BusInterfaceId id : ids) {
                if (_backend->getInterfaceName(id) == name) { found = id; break; }
            }
        }
        if (!found) {
            log_warning(tr("XCP session on %1 not restored: no such interface").arg(name));
            continue;
        }

        bool okRequest = false;
        bool okResponse = false;
        XcpConfig c;
        c.interfaceId = *found;
        c.requestId = e.attribute("request").toUInt(&okRequest, 16);
        c.responseId = e.attribute("response").toUInt(&okResponse, 16);
        bool okBroadcast = false;
        const uint32_t broadcast = e.attribute("broadcast").toUInt(&okBroadcast, 16);
        if (okBroadcast) { c.broadcastId = broadcast; }
        c.extended = e.attribute("extended") == "1";
        if (!okRequest || !okResponse) { continue; }
        sessions.removeIf([&](const XcpConfig &s) { return s.interfaceId == c.interfaceId; });
        sessions.append(c);
    }
    XcpSessions::instance().replaceAll(sessions);
}

bool TraceWindow::loadXML(Backend &backend, QDomElement &el)
{
    if (!ConfigurableWidget::loadXML(backend, el))
    {
        return false;
    }

    QString modeStr = el.attribute("mode", "aggregated");
    _tabModes[Cat_Aggregated] = (modeStr == "unified") ? mode_unified : mode_aggregated;

    // Workspaces before the shared toolbar: mode* per tab, one TimestampMode and one
    // filter for all protocol tabs. TabView and TraceFilter tab="..." override them.
    const char *const modeAttr[] = { nullptr, "modeUds", "modeJ1939", "modeXcp", "modeObd" };
    const int legacyTs = el.attribute("TimestampMode", QString::number(timestamp_mode_delta)).toInt();
    for (int tab = Cat_UDS; tab < Cat_Count; ++tab)
    {
        _tabState[tab] = TraceToolbar::State();
        _tabState[tab].scroll = TraceFind::scrollFromLegacyMode(el.attribute(modeAttr[tab], "aggregated"));
        setLegacyTimestampMode(tab, legacyTs);
        _tabFilter[tab] = TabFilter();
    }
    auto tabOf = [](const QString &name)
    {
        for (int tab = Cat_UDS; tab < Cat_Count; ++tab) { if (name == k_tabNames[tab]) { return tab; } }
        return -1;
    };
    for (QDomElement e = el.firstChildElement("TabView"); !e.isNull(); e = e.nextSiblingElement("TabView"))
    {
        const int tab = tabOf(e.attribute("tab"));
        if (tab < 0) { continue; }
        _tabState[tab].load(e);
        _timeOfDay[tab] = std::clamp(e.attribute("timeOfDay", "0").toInt(), 0, 2);
    }
    for (QDomElement e = el.firstChildElement("TraceFilter"); !e.isNull(); e = e.nextSiblingElement("TraceFilter"))
    {
        TabFilter f;
        f.showTx = e.attribute("showTx", "1") == "1";
        f.showRx = e.attribute("showRx", "1") == "1";
        for (const QString &s : e.attribute("hiddenMessages").split(',', Qt::SkipEmptyParts)) {
            f.hiddenMessageIds.insert(s.toUInt());
        }
        for (const QString &s : e.attribute("hiddenLinFrames").split(',', Qt::SkipEmptyParts)) {
            f.hiddenLinFrameIds.insert(static_cast<uint8_t>(s.toUInt()));
        }
        for (const QString &s : e.attribute("hiddenInterfaces").split(',', Qt::SkipEmptyParts)) {
            f.hiddenInterfaces.insert(static_cast<BusInterfaceId>(s.toUInt()));
        }
        const int tab = tabOf(e.attribute("tab"));
        if (tab >= 0) {
            _tabFilter[tab] = f;
        } else {
            for (int t = Cat_UDS; t < Cat_Count; ++t) { _tabFilter[t] = f; }   // older workspace: all tabs
        }
    }

    for (int tab = Cat_Aggregated; tab < Cat_Count; ++tab)
    {
        if (tab > Cat_Aggregated)
        {
            _tabModes[tab] = _tabState[tab].scroll ? mode_unified : mode_aggregated;
            setTabPaused(tab, false);
            applyTabFilter(tab);
            applyFindFilter(tab);
            applyTabTime(tab);
        }
        setTabMode(tab, _tabModes[tab]);
    }
    ui->tabs->setCurrentIndex(el.attribute("ActiveTab", "0").toInt());
    on_tabs_currentChanged(ui->tabs->currentIndex());

    QDomElement elAggregated = el.firstChildElement("AggregatedTraceView");
    int sortColumn = elAggregated.attribute("SortColumn", "-1").toInt();
    ui->treeAgg->sortByColumn(sortColumn, Qt::AscendingOrder);

    loadXcpSessions(el);

    return true;
}

void TraceWindow::addMessage(const BusMessage &msg)
{
    _backend->getTrace()->enqueueMessage(msg);
}

void TraceWindow::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    TraceFilterModel *filterModel = qobject_cast<TraceFilterModel*>(sender());

    // Only the model a tab shows scrolls it. Each tab keeps its other view mode
    // (rolling or aggregated) up to date too, and rows arriving there must not pull
    // the shown view to the bottom: a rolling log behind an aggregated view gets a
    // row per frame, which made the aggregated view impossible to scroll.
    for (int i = 0; i < Cat_Count; ++i) {
        if (!filterModel || tree(i)->model() != filterModel) {
            continue;
        }
        // Expand: new rows open like the others
        if (i > Cat_Aggregated && _tabState[i].expand && !parent.isValid()) {
            for (int row = first; row <= last; ++row) { tree(i)->expand(filterModel->index(row, 0)); }
        }
        // Follow (T-13)
        if (i == Cat_Aggregated || _tabState[i].follow) {
            _scrollPending[i] = true;
        }
        break;
    }

    if (!_scrollTimer.isActive()) {
        _scrollTimer.start();
    }

    if(_backend->getTrace()->size() > 1000000)
    {
        _backend->clearTrace();
    }
}

void TraceWindow::doScrollToBottom()
{
    QTreeView* trees[] = { ui->treeAgg, ui->treeUds, ui->treeJ1939, ui->treeXcp, ui->treeObd };
    for (int i = 0; i < Cat_Count; ++i) {
        if (_scrollPending[i]) {
            trees[i]->scrollToBottom();
            _scrollPending[i] = false;
        }
    }
}

void TraceWindow::openXcpSessions()
{
    XcpSessionDialog dialog(*_backend, this);
    dialog.exec();
}

void TraceWindow::updateXcpStatus()
{
    QStringList parts;
    for (const XcpConfig &s : XcpSessions::instance().sessions()) {
        parts << QStringLiteral("%1: %2").arg(_backend->getInterfaceName(s.interfaceId), s.toString());
    }
    ui->xcpStatusLabel->setText(parts.isEmpty() ? tr("No XCP session. Set the request and response IDs first.")
                                                : parts.join(QStringLiteral("    ")));
}

void TraceWindow::openFilterDialog()
{
    // T-12: the filter of the tab in front only
    const int tab = currentProtocolTab();
    if (tab < 0) { return; }
    TabFilter &f = _tabFilter[tab];
    TraceFilterDialog dlg(*_backend, this);
    dlg.setShowTx(f.showTx);
    dlg.setShowRx(f.showRx);
    dlg.setHiddenMessageIds(f.hiddenMessageIds);
    dlg.setHiddenLinFrameIds(f.hiddenLinFrameIds);
    dlg.setHiddenInterfaces(f.hiddenInterfaces);

    if (dlg.exec() == QDialog::Accepted)
    {
        f.showTx = dlg.showTx();
        f.showRx = dlg.showRx();
        f.hiddenMessageIds = dlg.hiddenMessageIds();
        f.hiddenLinFrameIds = dlg.hiddenLinFrameIds();
        f.hiddenInterfaces = dlg.hiddenInterfaces();

        applyTabFilter(tab);
        _toolbar->setFilterCount(filterCount(tab));
        emit settingsChanged(this);
    }
}

void TraceWindow::applyTabFilter(int tab)
{
    const TabFilter &f = _tabFilter[tab];
    auto applyToModel = [&f](TraceFilterModel *model)
    {
        model->setShowTx(f.showTx);
        model->setShowRx(f.showRx);
        model->setHiddenMessageIds(f.hiddenMessageIds);
        model->setHiddenLinFrameIds(f.hiddenLinFrameIds);
        model->setHiddenInterfaces(f.hiddenInterfaces);
        model->invalidate();
    };

    applyToModel(_filterModels[tab]);
    if (_altFilterModels[tab]) { applyToModel(_altFilterModels[tab]); }
    if (tab == Cat_Aggregated) { applyToModel(_aggMonitorFilterModel); }
    if (tab > Cat_Aggregated) { applyExpand(tab); }
}
