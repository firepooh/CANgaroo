/*
  Copyright (c) 2026 firepooh

  This file is part of cangaroo.

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

#include "MonitorView.h"

#include <algorithm>

#include <QActionGroup>
#include <QDomDocument>
#include <QHeaderView>
#include <QMenu>
#include <QSettings>
#include <QVBoxLayout>

#include "window/TraceWindow/TraceFilterDialog.h"
#include "MonitorBuffer.h"
#include "MonitorTable.h"
#include "TraceFind.h"
#include "TraceToolbar.h"

MonitorView::MonitorView(Backend &backend, QWidget *parent)
    : QWidget(parent),
      _backend(backend)
{
    _buffer = new MonitorBuffer(backend, this);
    _table = new MonitorTable(_buffer, this);
    _toolbar = new TraceToolbar(this);

    // The last Refresh choice is the default for new workspaces; a workspace may override it
    _buffer->setRefreshInterval(QSettings().value("monitor/refreshMs", MonitorBuffer::refresh_default_ms).toInt());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(_toolbar);
    layout->addWidget(_table);

    QHeaderView *header = _table->header();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, [this, header](const QPoint &pos)
    {
        QMenu menu(this);
        fillMoreMenu(&menu);
        menu.exec(header->mapToGlobal(pos));
    });

    // Click a column title to sort the Fixed view by it, again to reverse
    connect(header, &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder order)
    {
        if (MonitorBuffer::isSortable(column)) {
            _buffer->setSort(column, order == Qt::DescendingOrder);
            emit settingsChanged();
        }
        updateSortIndicator();
    });
    updateSortIndicator();

    connect(_toolbar, &TraceToolbar::scrollToggled, this, [this](bool on)
    {
        _buffer->setMode(on ? MonitorBuffer::Mode::Scroll : MonitorBuffer::Mode::Fixed);
        updateSortIndicator();
        emit settingsChanged();
    });
    connect(_toolbar, &TraceToolbar::expandToggled, this, [this](bool on)
    {
        _buffer->setAllExpanded(on);
        emit settingsChanged();
    });
    connect(_toolbar, &TraceToolbar::absTimeToggled, this, [this](bool on)
    {
        _buffer->setAbsoluteTime(on);
        emit settingsChanged();
    });
    connect(_toolbar, &TraceToolbar::pauseToggled, this, [this](bool on)
    {
        _buffer->setPaused(on);
        if (on) {
            _pauseInfoTimer.start();
            _toolbar->setPauseInfo(0, 0);
        } else {
            _pauseInfoTimer.stop();
        }
    });
    _pauseInfoTimer.setInterval(500);
    connect(&_pauseInfoTimer, &QTimer::timeout, this, [this]() { _toolbar->setPauseInfo(_buffer->pendingFrames(), 0); });
    connect(_toolbar, &TraceToolbar::eraseClicked, _buffer, &MonitorBuffer::erase);

    connect(_toolbar, &TraceToolbar::findTextChanged, this, [this]()
    {
        applyMatchFilter();
        updateFindPosition();
        emit settingsChanged();
    });
    connect(_toolbar, &TraceToolbar::findNext, this, [this]() { find(true); });
    connect(_toolbar, &TraceToolbar::findPrevious, this, [this]() { find(false); });
    connect(_toolbar, &TraceToolbar::onlyMatchesToggled, this, [this]()
    {
        applyMatchFilter();
        emit settingsChanged();
    });
    connect(_toolbar, &TraceToolbar::filterClicked, this, &MonitorView::openFilterDialog);
    updateFilterButton();

    connect(_toolbar, &TraceToolbar::followToggled, _table, &MonitorTable::setFollow);
    connect(_table, &MonitorTable::followChanged, _toolbar, &TraceToolbar::setFollow);
    connect(_toolbar, &TraceToolbar::moreMenuAboutToShow, this, &MonitorView::fillMoreMenu);
    connect(_buffer, &MonitorBuffer::memosChanged, this, &MonitorView::settingsChanged);

    // Same defaults as a new workspace: rows closed (Expand off)
    _buffer->setAllExpanded(false);
}

void MonitorView::find(bool forward)
{
    const QString text = _toolbar->findText();
    _findMatches = _buffer->findAll(text);
    const int current = static_cast<int>(_findMatches.indexOf(_table->selectedEntry()));
    const int next = TraceFind::step(current, static_cast<int>(_findMatches.size()), forward);
    if (next >= 0) {
        const int entry = _findMatches[next];
        // T-10: a match in a signal line opens the row
        _table->showFindResult(entry, _buffer->matchesOnlyInSignals(_buffer->entry(entry), text));
        _toolbar->setFollow(false);
    }
    _toolbar->setFindPosition(next, static_cast<int>(_findMatches.size()));
}

void MonitorView::updateFindPosition()
{
    const QString text = _toolbar->findText();
    if (text.isEmpty()) {
        _findMatches.clear();
        _toolbar->setFindPosition(-1, -1);
        return;
    }
    _findMatches = _buffer->findAll(text);
    _toolbar->setFindPosition(static_cast<int>(_findMatches.indexOf(_table->selectedEntry())),
                              static_cast<int>(_findMatches.size()));
}

void MonitorView::applyMatchFilter()
{
    _buffer->setMatchText(_toolbar->state().onlyMatches ? _toolbar->findText() : QString());
}

void MonitorView::fillMoreMenu(QMenu *menu)
{
    // Refresh interval of this tab
    QMenu *refresh = menu->addMenu(tr("Refresh"));
    refresh->setToolTipsVisible(true);
    auto *group = new QActionGroup(refresh);
    for (int ms = MonitorBuffer::refresh_min_ms; ms <= MonitorBuffer::refresh_max_ms; ms += 10) {
        QAction *action = refresh->addAction(tr("%1 ms").arg(ms));
        action->setCheckable(true);
        action->setChecked(_buffer->refreshInterval() == ms);
        action->setToolTip(tr("How often new frames are shown. Shorter needs more CPU"));
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, ms]()
        {
            _buffer->setRefreshInterval(ms);
            QSettings().setValue("monitor/refreshMs", ms);
            emit settingsChanged();
        });
    }

    // Columns
    QHeaderView *header = _table->header();
    QMenu *columns = menu->addMenu(tr("Columns"));
    for (int col = 0; col < MonitorBuffer::column_count; ++col) {
        QAction *action = columns->addAction(_buffer->headerText(col));
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(col));
        connect(action, &QAction::toggled, this, [this, header, col](bool on)
        {
            header->setSectionHidden(col, !on);
            emit settingsChanged();
        });
    }
    columns->addSeparator();
    QAction *reset = columns->addAction(tr("Default columns"));
    connect(reset, &QAction::triggered, this, [this, header]()
    {
        for (int col = 0; col < MonitorBuffer::column_count; ++col) {
            header->setSectionHidden(col, false);
            header->moveSection(header->visualIndex(col), col);
        }
        _table->applyDefaultColumnWidths();
        emit settingsChanged();
    });
}

void MonitorView::updateSortIndicator()
{
    // Scroll mode keeps the arrival order: no indicator, clicks do nothing
    QHeaderView *header = _table->header();
    const QSignalBlocker block(header);
    const bool fixed = _buffer->mode() == MonitorBuffer::Mode::Fixed;
    header->setSortIndicatorShown(fixed);
    header->setSortIndicator(fixed ? _buffer->sortColumn() : -1,
                             _buffer->sortDescending() ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void MonitorView::openFilterDialog()
{
    // The Trace window's dialog; the filter applies to this view only
    const MonitorBuffer::Filter &current = _buffer->filter();
    TraceFilterDialog dlg(_backend, this);
    dlg.setShowTx(current.showTx);
    dlg.setShowRx(current.showRx);
    dlg.setHiddenMessageIds(current.hiddenMessageIds);
    dlg.setHiddenLinFrameIds(current.hiddenLinFrameIds);
    dlg.setHiddenInterfaces(current.hiddenInterfaces);
    if (dlg.exec() != QDialog::Accepted) { return; }

    MonitorBuffer::Filter filter;
    filter.showTx = dlg.showTx();
    filter.showRx = dlg.showRx();
    filter.hiddenMessageIds = dlg.hiddenMessageIds();
    filter.hiddenLinFrameIds = dlg.hiddenLinFrameIds();
    filter.hiddenInterfaces = dlg.hiddenInterfaces();
    _buffer->setFilter(filter);
    updateFilterButton();
    emit settingsChanged();
}

void MonitorView::updateFilterButton()
{
    const MonitorBuffer::Filter &f = _buffer->filter();
    _toolbar->setFilterCount(TraceFind::filterCount(f.showTx, f.showRx, static_cast<int>(f.hiddenMessageIds.size()),
                                                    static_cast<int>(f.hiddenLinFrameIds.size()),
                                                    static_cast<int>(f.hiddenInterfaces.size())));
}

void MonitorView::saveXML(QDomDocument &xml, QDomElement &el) const
{
    _toolbar->state().save(el);
    el.setAttribute("refreshMs", _buffer->refreshInterval());
    el.setAttribute("header", QString::fromLatin1(_table->header()->saveState().toBase64()));
    el.setAttribute("sortColumn", _buffer->sortColumn());
    el.setAttribute("sortDescending", _buffer->sortDescending() ? "1" : "0");

    // Same attributes as the Trace window's <TraceFilter>
    const MonitorBuffer::Filter &filter = _buffer->filter();
    QDomElement filterEl = xml.createElement("MonitorFilter");
    filterEl.setAttribute("showTx", filter.showTx ? "1" : "0");
    filterEl.setAttribute("showRx", filter.showRx ? "1" : "0");
    auto join = [](const auto &set)
    {
        QStringList list;
        for (auto v : set) { list.append(QString::number(v)); }
        list.sort();
        return list.join(',');
    };
    if (!filter.hiddenMessageIds.isEmpty()) { filterEl.setAttribute("hiddenMessages", join(filter.hiddenMessageIds)); }
    if (!filter.hiddenLinFrameIds.isEmpty()) { filterEl.setAttribute("hiddenLinFrames", join(filter.hiddenLinFrameIds)); }
    if (!filter.hiddenInterfaces.isEmpty()) { filterEl.setAttribute("hiddenInterfaces", join(filter.hiddenInterfaces)); }
    el.appendChild(filterEl);

    const QHash<QString, QString> &memos = _buffer->memos();
    for (auto it = memos.constBegin(); it != memos.constEnd(); ++it) {
        QDomElement memoEl = xml.createElement("Memo");
        memoEl.setAttribute("id", it.key());
        memoEl.setAttribute("text", it.value());
        el.appendChild(memoEl);
    }
}

void MonitorView::loadXML(const QDomElement &el)
{
    // Workspaces before the shared toolbar wrote mode = fixed / scroll and timeAbs
    TraceToolbar::State state;
    state.scroll = TraceFind::scrollFromLegacyMode(el.attribute("mode", "fixed"));
    state.absTime = el.attribute("timeAbs", "0") == "1";
    state.load(el);
    _toolbar->setState(state);
    _buffer->setMode(state.scroll ? MonitorBuffer::Mode::Scroll : MonitorBuffer::Mode::Fixed);
    _buffer->setAllExpanded(state.expand);
    _buffer->setAbsoluteTime(state.absTime);
    _table->setFollow(state.follow);
    if (el.hasAttribute("refreshMs")) {
        const int ms = el.attribute("refreshMs").toInt();
        if (ms >= MonitorBuffer::refresh_min_ms && ms <= MonitorBuffer::refresh_max_ms) { _buffer->setRefreshInterval(ms); }
    }

    const QByteArray header = QByteArray::fromBase64(el.attribute("header").toLatin1());
    if (!header.isEmpty()) {
        const QSignalBlocker block(_table->header());
        _table->header()->restoreState(header);
    }
    _buffer->setSort(el.attribute("sortColumn", QString::number(MonitorBuffer::col_arbid)).toInt(),
                     el.attribute("sortDescending", "0") == "1");
    updateSortIndicator();

    MonitorBuffer::Filter filter;
    const QDomElement filterEl = el.firstChildElement("MonitorFilter");
    if (!filterEl.isNull()) {
        filter.showTx = filterEl.attribute("showTx", "1") == "1";
        filter.showRx = filterEl.attribute("showRx", "1") == "1";
        for (const QString &s : filterEl.attribute("hiddenMessages").split(',', Qt::SkipEmptyParts)) {
            filter.hiddenMessageIds.insert(s.toUInt());
        }
        for (const QString &s : filterEl.attribute("hiddenLinFrames").split(',', Qt::SkipEmptyParts)) {
            filter.hiddenLinFrameIds.insert(static_cast<uint8_t>(s.toUInt()));
        }
        for (const QString &s : filterEl.attribute("hiddenInterfaces").split(',', Qt::SkipEmptyParts)) {
            filter.hiddenInterfaces.insert(static_cast<BusInterfaceId>(s.toUInt()));
        }
    }
    _buffer->setFilter(filter);
    updateFilterButton();

    QHash<QString, QString> memos;
    for (QDomElement memoEl = el.firstChildElement("Memo"); !memoEl.isNull(); memoEl = memoEl.nextSiblingElement("Memo")) {
        memos.insert(memoEl.attribute("id"), memoEl.attribute("text"));
    }
    _buffer->setMemos(memos);
    applyMatchFilter();
    updateFindPosition();
}
