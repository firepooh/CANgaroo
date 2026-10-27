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

#include <QCheckBox>
#include <QComboBox>
#include <QDomDocument>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include "helpers/apphelpers.h"
#include "window/TraceWindow/TraceFilterDialog.h"
#include "MonitorBuffer.h"
#include "MonitorTable.h"

namespace
{

QToolButton *makeButton(const QString &text, bool checkable, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setCheckable(checkable);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace

MonitorView::MonitorView(Backend &backend, QWidget *parent)
    : QWidget(parent),
      _backend(backend)
{
    _buffer = new MonitorBuffer(backend, this);
    _table = new MonitorTable(_buffer, this);

    // Same order and names as the UDS / J1939 / XCP tabs: View, Time | Filter | Find | Clear | own items
    _viewBox = new QComboBox(this);
    _viewBox->addItem(tr("Fixed"), false);
    _viewBox->addItem(tr("Scroll"), true);
    _viewBox->setToolTip(tr("Fixed: one row per ID. Scroll: every frame in arrival order"));
    _expandBox = new QCheckBox(tr("Expand"), this);
    _expandBox->setChecked(true);
    _expandBox->setToolTip(tr("Show the data lines and signals of every row"));
    _timeBox = new QComboBox(this);
    _timeBox->addItem(tr("Delta"), false);
    _timeBox->addItem(tr("Relative"), true);
    _timeBox->setToolTip(tr("Delta: time since the previous frame. Relative: time since measurement start"));
    _pauseButton = makeButton(tr("Pause"), true, this);
    _pauseButton->setToolTip(tr("Freeze this view; frames are still recorded"));
    QToolButton *eraseButton = makeButton(tr("Clear"), false, this);
    eraseButton->setIcon(AppHelpers::themedIcon("user-trash-full", ":/assets/icons/edit-clear-symbolic.svg"));
    eraseButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    eraseButton->setToolTip(tr("Clear this Monitor view only; the trace and the other tabs keep their frames"));
    _filterButton = makeButton(tr("Filter"), false, this);
    _filterButton->setIcon(AppHelpers::themedIcon("view-filter", ":/assets/filter-symbolic.svg"));
    _filterButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    _refreshBox = new QComboBox(this);
    _refreshBox->setToolTip(tr("How often new frames are shown. Shorter needs more CPU"));
    for (int ms = MonitorBuffer::refresh_min_ms; ms <= MonitorBuffer::refresh_max_ms; ms += 10) {
        _refreshBox->addItem(tr("%1 ms").arg(ms), ms);
    }
    // The last choice is the default for new workspaces; a workspace may override it
    const int refreshMs = QSettings().value("monitor/refreshMs", MonitorBuffer::refresh_default_ms).toInt();
    _refreshBox->setCurrentIndex(std::max(0, _refreshBox->findData(refreshMs)));
    _buffer->setRefreshInterval(_refreshBox->currentData().toInt());

    _findColumn = new QComboBox(this);
    for (int col : {MonitorBuffer::col_arbid, MonitorBuffer::col_description, MonitorBuffer::col_data,
                    MonitorBuffer::col_network, MonitorBuffer::col_node, MonitorBuffer::col_memo}) {
        _findColumn->addItem(_buffer->headerText(col), col);
    }
    _findText = new QLineEdit(this);
    _findText->setClearButtonEnabled(true);
    _findText->setMaximumWidth(220);
    QToolButton *prevButton = makeButton(tr("Prev"), false, this);
    QToolButton *nextButton = makeButton(tr("Next"), false, this);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(4, 4, 4, 2);
    toolbar->setSpacing(6);
    // Same widths as the View / Time combos of the other tabs (TraceWindow::applyMonitorToolbarLayout)
    _viewBox->setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("Rolling Log")) + 48);
    _timeBox->setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("Absolute (UTC)")) + 48);
    toolbar->addWidget(new QLabel(tr("View:"), this));
    toolbar->addWidget(_viewBox);
    toolbar->addWidget(new QLabel(tr("Time:"), this));
    toolbar->addWidget(_timeBox);
    toolbar->addSpacing(16);
    toolbar->addWidget(_filterButton);
    toolbar->addSpacing(16);
    toolbar->addWidget(new QLabel(tr("Find:"), this));
    toolbar->addWidget(_findColumn);
    toolbar->addWidget(_findText);
    toolbar->addWidget(prevButton);
    toolbar->addWidget(nextButton);
    toolbar->addSpacing(16);
    toolbar->addWidget(eraseButton);
    toolbar->addSpacing(16);
    toolbar->addWidget(_expandBox);
    toolbar->addWidget(_pauseButton);
    toolbar->addWidget(new QLabel(tr("Refresh:"), this));
    toolbar->addWidget(_refreshBox);
    toolbar->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(_table);

    QHeaderView *header = _table->header();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, &MonitorView::showHeaderMenu);

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

    connect(_filterButton, &QToolButton::clicked, this, &MonitorView::openFilterDialog);
    updateFilterButton();

    connect(_viewBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        const bool scroll = _viewBox->currentData().toBool();
        _buffer->setMode(scroll ? MonitorBuffer::Mode::Scroll : MonitorBuffer::Mode::Fixed);
        updateSortIndicator();
        emit settingsChanged();
    });
    connect(_expandBox, &QCheckBox::toggled, this, [this](bool on)
    {
        _buffer->setAllExpanded(on);
        emit settingsChanged();
    });
    connect(_timeBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        _buffer->setAbsoluteTime(_timeBox->currentData().toBool());
        emit settingsChanged();
    });
    connect(_refreshBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        const int ms = _refreshBox->currentData().toInt();
        _buffer->setRefreshInterval(ms);
        QSettings().setValue("monitor/refreshMs", ms);
        emit settingsChanged();
    });
    connect(_pauseButton, &QToolButton::toggled, _buffer, &MonitorBuffer::setPaused);
    connect(eraseButton, &QToolButton::clicked, _buffer, &MonitorBuffer::erase);

    auto find = [this](bool forward)
    {
        const QString text = _findText->text().trimmed();
        if (!text.isEmpty()) { _table->find(_findColumn->currentData().toInt(), text, forward); }
    };
    connect(prevButton, &QToolButton::clicked, this, [find]() { find(false); });
    connect(nextButton, &QToolButton::clicked, this, [find]() { find(true); });
    connect(_findText, &QLineEdit::returnPressed, this, [find]() { find(true); });
    connect(_buffer, &MonitorBuffer::memosChanged, this, &MonitorView::settingsChanged);
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
    const bool active = _buffer->filter().isActive();
    _filterButton->setText(active ? tr("Filter (on)") : tr("Filter"));
    _filterButton->setToolTip(active ? tr("Some frames are hidden in this view. Click to change the filter")
                                     : tr("Hide frames by direction, ID or interface in this view"));
}

void MonitorView::showHeaderMenu(const QPoint &pos)
{
    QHeaderView *header = _table->header();
    QMenu menu(this);
    for (int col = 0; col < MonitorBuffer::column_count; ++col) {
        QAction *action = menu.addAction(_buffer->headerText(col));
        action->setCheckable(true);
        action->setChecked(!header->isSectionHidden(col));
        connect(action, &QAction::toggled, this, [this, header, col](bool on)
        {
            header->setSectionHidden(col, !on);
            emit settingsChanged();
        });
    }
    menu.addSeparator();
    QAction *reset = menu.addAction(tr("Default columns"));
    connect(reset, &QAction::triggered, this, [this, header]()
    {
        for (int col = 0; col < MonitorBuffer::column_count; ++col) {
            header->setSectionHidden(col, false);
            header->moveSection(header->visualIndex(col), col);
        }
        _table->applyDefaultColumnWidths();
        emit settingsChanged();
    });
    menu.exec(header->mapToGlobal(pos));
}

void MonitorView::saveXML(QDomDocument &xml, QDomElement &el) const
{
    el.setAttribute("mode", _viewBox->currentData().toBool() ? "scroll" : "fixed");
    el.setAttribute("expand", _expandBox->isChecked() ? "1" : "0");
    el.setAttribute("timeAbs", _timeBox->currentData().toBool() ? "1" : "0");
    el.setAttribute("refreshMs", _refreshBox->currentData().toInt());
    el.setAttribute("findColumn", _findColumn->currentIndex());
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
    _viewBox->setCurrentIndex(el.attribute("mode", "fixed") == "scroll" ? 1 : 0);
    _expandBox->setChecked(el.attribute("expand", "1") == "1");
    _timeBox->setCurrentIndex(el.attribute("timeAbs", "0") == "1" ? 1 : 0);
    if (el.hasAttribute("refreshMs")) {
        const int index = _refreshBox->findData(el.attribute("refreshMs").toInt());
        if (index >= 0) { _refreshBox->setCurrentIndex(index); }
    }
    _findColumn->setCurrentIndex(el.attribute("findColumn", "0").toInt());

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
}
