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

#include <QCheckBox>
#include <QDomDocument>
#include <QHeaderView>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>

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


TraceWindow::TraceWindow(QWidget *parent, Backend &backend) :
    ConfigurableWidget(parent),
    ui(new Ui::TraceWindow),
    _backend(&backend),
    _timestampMode(timestamp_mode_absolute)
{
    ui->setupUi(this);

    applyIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](ThemeManager::Theme) { applyIcons(); });

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
        UnifiedTraceViewModel::Cat_XCP
    };

    QTreeView* trees[] = { ui->treeAgg, ui->treeUds, ui->treeJ1939, ui->treeXcp };

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
    ui->treeUds->setColumnWidth(BaseTraceViewModel::column_canid, 16 * ch);
    ui->treeJ1939->setColumnWidth(BaseTraceViewModel::column_canid, 22 * ch);
    // XCP packet names such as "DAQ-DTO (DAQ #12|ODT #3)" need more room than message names
    ui->treeXcp->setColumnWidth(BaseTraceViewModel::column_name, 34 * ch);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [trees](ThemeManager::Theme) {
        for (QTreeView *tree : trees) { MonitorStyleDelegate::applyPalette(tree); }
    });

    connect(_aggMonitorFilterModel, &QAbstractItemModel::rowsInserted, this, &TraceWindow::onRowsInserted);

    // Per-tab default modes
    _tabModes[Cat_Aggregated] = mode_aggregated;
    _tabModes[Cat_UDS]        = mode_unified;
    _tabModes[Cat_J1939]      = mode_aggregated;
    _tabModes[Cat_XCP]        = mode_aggregated;

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

    connect(ui->xcpSessionsButton, &QPushButton::clicked, this, &TraceWindow::openXcpSessions);
    connect(&XcpSessions::instance(), &XcpSessions::changed, this, &TraceWindow::updateXcpStatus);
    updateXcpStatus();

    connect(ui->tabs, &QTabWidget::currentChanged, this, &TraceWindow::on_tabs_currentChanged);
    ui->xcpSessionsButton->setVisible(ui->tabs->currentIndex() == Cat_XCP);
    ui->xcpStatusLabel->setVisible(ui->tabs->currentIndex() == Cat_XCP);

    ui->cbViewMode->addItem(tr("Aggregated"), mode_aggregated);
    ui->cbViewMode->addItem(tr("Rolling Log"), mode_unified);

    ui->cbTimestampMode->addItem(tr("Absolute"), timestamp_mode_absolute);
    ui->cbTimestampMode->addItem(tr("Absolute (UTC)"), timestamp_mode_absolute_utc);
    ui->cbTimestampMode->addItem(tr("Relative"), timestamp_mode_relative);
    ui->cbTimestampMode->addItem(tr("Delta"), timestamp_mode_delta);

    QSettings appSettings;
    const auto defaultTsMode = static_cast<timestamp_mode_t>(
        appSettings.value("tracewindow/defaultTimestampMode", timestamp_mode_delta).toInt());
    setTimestampMode(defaultTsMode);

    const auto defaultViewMode = static_cast<mode_t>(
        appSettings.value("tracewindow/defaultViewMode", mode_aggregated).toInt());
    _tabModes[Cat_Aggregated] = defaultViewMode;

    // Debounce typing so the filter models are not re-evaluated on every keystroke
    _filterDebounceTimer.setInterval(250);
    _filterDebounceTimer.setSingleShot(true);
    connect(&_filterDebounceTimer, &QTimer::timeout, this, &TraceWindow::on_cbFilterChanged);
    connect(ui->filterLineEdit, &QLineEdit::textChanged, &_filterDebounceTimer, qOverload<>(&QTimer::start));
    connect(ui->TraceClearpushButton, &QPushButton::released, this, &TraceWindow::on_cbTraceClearpushButton);
    connect(ui->cbViewMode, &QComboBox::currentIndexChanged, this, &TraceWindow::on_cbViewMode_currentIndexChanged);
    connect(ui->filterButton, &QPushButton::clicked, this, &TraceWindow::openFilterDialog);

    _scrollTimer.setInterval(100);
    _scrollTimer.setSingleShot(true);
    connect(&_scrollTimer, &QTimer::timeout, this, &TraceWindow::doScrollToBottom);

    setMode(defaultViewMode);
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

void TraceWindow::setMode(TraceWindow::mode_t mode)
{
    int tabIdx = ui->tabs->currentIndex();
    _tabModes[tabIdx] = mode;

    QTreeView *trees[] = { ui->treeAgg, ui->treeUds, ui->treeJ1939, ui->treeXcp };
    QTreeView *tree = trees[tabIdx];

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
                        : (tabIdx == Cat_J1939 || tabIdx == Cat_XCP) ? (mode == mode_aggregated)
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
            // The J1939 / XCP alternative is the rolling log: keep the arrival order
            tree->setSortingEnabled(false);
            tree->header()->setSortIndicator(-1, Qt::AscendingOrder);
            _altFilterModels[tabIdx]->sort(-1);
            tree->setModel(_altFilterModels[tabIdx]);
        }
    }

    for (int i = 0; i < ui->cbViewMode->count(); i++) {
        if (ui->cbViewMode->itemData(i).toInt() == mode) {
            ui->cbViewMode->setCurrentIndex(i);
            break;
        }
    }
    tree->scrollToBottom();
}

void TraceWindow::setMonitorPage(QWidget *page)
{
    _monitorPage = page;
    ui->treeAgg->hide();
    ui->verticalLayout_2->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout_2->addWidget(page);
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    // The protocol tabs get the same frame as the Monitor tab: tree flush with the tab
    for (QVBoxLayout *page : { ui->verticalLayout_3, ui->verticalLayout_4, ui->verticalLayout_xcp }) {
        page->setContentsMargins(0, 0, 0, 0);
        page->setSpacing(0);
    }
    applyMonitorToolbarLayout();
    placeToolbar(ui->tabs->currentIndex());
}

void TraceWindow::applyMonitorToolbarLayout()
{
    // Same look and order as the Monitor tab's toolbar: compact controls packed to
    // the left, view switches first, then the display options, then search and XCP
    QHBoxLayout *bar = ui->horizontalLayout;
    while (QLayoutItem *item = bar->takeAt(0)) {
        delete item;    // only the layout item; the widgets stay
    }
    ui->line->hide();
    bar->setContentsMargins(4, 4, 4, 2);
    bar->setSpacing(6);

    for (QPushButton *button : { ui->TraceClearpushButton, ui->filterButton, ui->xcpSessionsButton }) {
        button->setMinimumHeight(0);
        button->setIconSize(QSize(14, 14));
    }
    ui->filterLineEdit->setMinimumHeight(0);
    ui->filterLineEdit->setMaximumWidth(220);
    ui->filterLineEdit->setClearButtonEnabled(true);
    ui->label_2->setText(tr("Find:"));

    bar->addWidget(ui->cbAutoscroll);
    bar->addWidget(ui->TraceClearpushButton);
    bar->addWidget(ui->filterButton);
    bar->addSpacing(16);
    bar->addWidget(ui->label_3);
    bar->addWidget(ui->cbViewMode);
    bar->addWidget(ui->label);
    bar->addWidget(ui->cbTimestampMode);
    bar->addSpacing(16);
    bar->addWidget(ui->label_2);
    bar->addWidget(ui->filterLineEdit);
    bar->addSpacing(16);
    bar->addWidget(ui->xcpSessionsButton);
    bar->addWidget(ui->xcpStatusLabel);
    bar->addStretch(1);
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

void TraceWindow::on_tabs_currentChanged(int index)
{
    placeToolbar(index);
    // XCP IDs and the session status sit in the toolbar line, on the XCP tab only
    ui->xcpSessionsButton->setVisible(index == Cat_XCP);
    ui->xcpStatusLabel->setVisible(index == Cat_XCP);
    int tabIdx = ui->tabs->currentIndex();
    mode_t currentMode = _tabModes[tabIdx];
    for (int i = 0; i < ui->cbViewMode->count(); i++) {
        if (ui->cbViewMode->itemData(i).toInt() == currentMode) {
            ui->cbViewMode->setCurrentIndex(i);
            break;
        }
    }
}


void TraceWindow::setTimestampMode(int mode)
{
    timestamp_mode_t new_mode;
    if ( (mode>=0) && (mode<timestamp_modes_count) )
    {
        new_mode = (timestamp_mode_t) mode;
    }
    else
    {
        new_mode = timestamp_mode_absolute;
    }

    _aggregatedTraceViewModel->setTimestampMode(new_mode);
    for (int i = 0; i < Cat_Count; ++i)
    {
        _viewModels[i]->setTimestampMode(new_mode);
        if (_altViewModels[i])
            _altViewModels[i]->setTimestampMode(new_mode);
    }

    if (new_mode != _timestampMode)
    {
        _timestampMode = new_mode;
        for (int i=0; i<ui->cbTimestampMode->count(); i++)
        {
            if (ui->cbTimestampMode->itemData(i).toInt() == new_mode)
            {
                ui->cbTimestampMode->setCurrentIndex(i);
            }
        }
        emit(settingsChanged(this));
    }
}

bool TraceWindow::saveXML(Backend &backend, QDomDocument &xml, QDomElement &root)
{
    if (!ConfigurableWidget::saveXML(backend, xml, root))
    {
        return false;
    }

    root.setAttribute("type", "TraceWindow");
    root.setAttribute("mode", _tabModes[Cat_Aggregated] == mode_unified ? "unified" : "aggregated");
    root.setAttribute("modeUds", _tabModes[Cat_UDS] == mode_aggregated ? "aggregated" : "unified");
    root.setAttribute("modeJ1939", _tabModes[Cat_J1939] == mode_unified ? "unified" : "aggregated");
    root.setAttribute("modeXcp", _tabModes[Cat_XCP] == mode_unified ? "unified" : "aggregated");
    root.setAttribute("TimestampMode", _timestampMode);
    root.setAttribute("ActiveTab", ui->tabs->currentIndex());

    QDomElement elAggregated = xml.createElement("AggregatedTraceView");
    elAggregated.setAttribute("SortColumn", _aggregatedProxyModel->sortColumn());
    root.appendChild(elAggregated);

    QDomElement filterEl = xml.createElement("TraceFilter");
    filterEl.setAttribute("showTx", _filterShowTx ? "1" : "0");
    filterEl.setAttribute("showRx", _filterShowRx ? "1" : "0");

    QStringList hiddenMsgs;
    for (uint32_t id : _filterHiddenMessageIds)
    {
        hiddenMsgs.append(QString::number(id));
    }
    if (!hiddenMsgs.isEmpty())
    {
        filterEl.setAttribute("hiddenMessages", hiddenMsgs.join(","));
    }

    QStringList hiddenLinFrames;
    for (uint8_t id : _filterHiddenLinFrameIds)
    {
        hiddenLinFrames.append(QString::number(id));
    }
    if (!hiddenLinFrames.isEmpty())
    {
        filterEl.setAttribute("hiddenLinFrames", hiddenLinFrames.join(","));
    }

    QStringList hiddenIfs;
    for (BusInterfaceId id : _filterHiddenInterfaces)
    {
        hiddenIfs.append(QString::number(id));
    }
    if (!hiddenIfs.isEmpty())
    {
        filterEl.setAttribute("hiddenInterfaces", hiddenIfs.join(","));
    }

    root.appendChild(filterEl);

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
    _tabModes[Cat_UDS]  = (el.attribute("modeUds",   "unified")    == "aggregated") ? mode_aggregated : mode_unified;
    _tabModes[Cat_J1939]= (el.attribute("modeJ1939", "aggregated") == "unified")    ? mode_unified    : mode_aggregated;
    _tabModes[Cat_XCP]  = (el.attribute("modeXcp",   "aggregated") == "unified")    ? mode_unified    : mode_aggregated;
    // Apply mode for Monitor tab (the tab that is currently visible after loading)
    setMode(_tabModes[ui->tabs->currentIndex()]);
    setTimestampMode(el.attribute("TimestampMode", "0").toInt());
    ui->tabs->setCurrentIndex(el.attribute("ActiveTab", "0").toInt());

    QDomElement elAggregated = el.firstChildElement("AggregatedTraceView");
    int sortColumn = elAggregated.attribute("SortColumn", "-1").toInt();
    ui->treeAgg->sortByColumn(sortColumn, Qt::AscendingOrder);

    QDomElement filterEl = el.firstChildElement("TraceFilter");
    if (!filterEl.isNull())
    {
        _filterShowTx = filterEl.attribute("showTx", "1") == "1";
        _filterShowRx = filterEl.attribute("showRx", "1") == "1";

        QString hiddenMsgsStr = filterEl.attribute("hiddenMessages");
        if (!hiddenMsgsStr.isEmpty())
        {
            for (const QString &s : hiddenMsgsStr.split(","))
            {
                _filterHiddenMessageIds.insert(s.toUInt());
            }
        }

        QString hiddenLinStr = filterEl.attribute("hiddenLinFrames");
        if (!hiddenLinStr.isEmpty())
        {
            for (const QString &s : hiddenLinStr.split(","))
            {
                _filterHiddenLinFrameIds.insert(static_cast<uint8_t>(s.toUInt()));
            }
        }

        QString hiddenIfsStr = filterEl.attribute("hiddenInterfaces");
        if (!hiddenIfsStr.isEmpty())
        {
            for (const QString &s : hiddenIfsStr.split(","))
            {
                _filterHiddenInterfaces.insert(static_cast<BusInterfaceId>(s.toUInt()));
            }
        }

        applyDialogFilters();
    }

    loadXcpSessions(el);

    return true;
}

void TraceWindow::addMessage(const BusMessage &msg)
{
    _backend->getTrace()->enqueueMessage(msg);
}

void TraceWindow::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    (void) parent;
    (void) first;
    (void) last;

    if (!ui->cbAutoscroll->isChecked()) { return; }

    TraceFilterModel *filterModel = qobject_cast<TraceFilterModel*>(sender());

    for (int i = 0; i < Cat_Count; ++i) {
        if (_filterModels[i] == filterModel
            || (i == 0 && _aggMonitorFilterModel == filterModel)
            || (_altFilterModels[i] && _altFilterModels[i] == filterModel))
        {
            _scrollPending[i] = true;
            break;
        }
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
    QTreeView* trees[] = { ui->treeAgg, ui->treeUds, ui->treeJ1939, ui->treeXcp };
    for (int i = 0; i < Cat_Count; ++i) {
        if (_scrollPending[i]) {
            trees[i]->scrollToBottom();
            _scrollPending[i] = false;
        }
    }
}

void TraceWindow::on_cbTimestampMode_currentIndexChanged(int index)
{
    setTimestampMode((timestamp_mode_t)ui->cbTimestampMode->itemData(index).toInt());
}

void TraceWindow::on_cbFilterChanged()
{
    QString filterText = ui->filterLineEdit->text();
    _aggMonitorFilterModel->setFilterText(filterText);
    _aggMonitorFilterModel->invalidate();

    for (int i = 0; i < Cat_Count; ++i) {
        _filterModels[i]->setFilterText(filterText);
        _filterModels[i]->invalidate();
    }
}

void TraceWindow::on_cbTraceClearpushButton()
{
    _backend->clearTrace();
}

void TraceWindow::on_cbViewMode_currentIndexChanged(int index)
{
    setMode((mode_t)ui->cbViewMode->itemData(index).toInt());
}

void TraceWindow::applyIcons()
{
    // The bundled SVGs draw with stroke="currentColor", which QSvgRenderer does
    // not resolve — it paints them black, so they vanish against a dark theme.
    // Recolor them to the palette's text color instead of using the SVG as-is.
    ui->filterButton->setIcon(AppHelpers::themedIcon("view-filter", ":/assets/filter-symbolic.svg"));
    ui->TraceClearpushButton->setIcon(AppHelpers::themedIcon("user-trash-full",
                                                            ":/assets/icons/edit-clear-symbolic.svg"));
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
    TraceFilterDialog dlg(*_backend, this);
    dlg.setShowTx(_filterShowTx);
    dlg.setShowRx(_filterShowRx);
    dlg.setHiddenMessageIds(_filterHiddenMessageIds);
    dlg.setHiddenLinFrameIds(_filterHiddenLinFrameIds);
    dlg.setHiddenInterfaces(_filterHiddenInterfaces);

    if (dlg.exec() == QDialog::Accepted)
    {
        _filterShowTx = dlg.showTx();
        _filterShowRx = dlg.showRx();
        _filterHiddenMessageIds = dlg.hiddenMessageIds();
        _filterHiddenLinFrameIds = dlg.hiddenLinFrameIds();
        _filterHiddenInterfaces = dlg.hiddenInterfaces();

        applyDialogFilters();
        emit settingsChanged(this);
    }
}

void TraceWindow::applyDialogFilters()
{
    auto applyToModel = [&](TraceFilterModel *model)
    {
        model->setShowTx(_filterShowTx);
        model->setShowRx(_filterShowRx);
        model->setHiddenMessageIds(_filterHiddenMessageIds);
        model->setHiddenLinFrameIds(_filterHiddenLinFrameIds);
        model->setHiddenInterfaces(_filterHiddenInterfaces);
        model->invalidate();
    };

    for (int i = 0; i < Cat_Count; ++i)
    {
        applyToModel(_filterModels[i]);
        if (_altFilterModels[i])
            applyToModel(_altFilterModels[i]);
    }
    applyToModel(_aggMonitorFilterModel);
}

