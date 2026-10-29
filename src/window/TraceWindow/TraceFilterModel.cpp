/*
  Copyright (c) 2026 Schildkroet

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

#include "TraceFilterModel.h"
#include "BaseTraceViewModel.h"

#include <QSortFilterProxyModel>

#include "core/BusMessage.h"
#include "window/MonitorWindow/TraceFind.h"

TraceFilterModel::TraceFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent},
    _filterText("")
{
   setRecursiveFilteringEnabled(false);
   setDynamicSortFilter(false);
}


void TraceFilterModel::setFilterText(QString filtertext)
{
    _filterText = filtertext;
}

bool TraceFilterModel::lineMatches(const QModelIndex &index, const QString &text)
{
    if (text.isEmpty() || !index.isValid()) { return false; }
    auto cell = [&index](int column) { return index.sibling(index.row(), column).data().toString(); };
    return TraceFind::matchesText(text, cell(BaseTraceViewModel::column_canid))
        || TraceFind::matchesText(text, cell(BaseTraceViewModel::column_name))
        || TraceFind::matchesText(text, cell(BaseTraceViewModel::column_comment))
        || TraceFind::matchesData(text, cell(BaseTraceViewModel::column_data));
}

void TraceFilterModel::setShowTx(bool show)
{
    _showTx = show;
}

void TraceFilterModel::setShowRx(bool show)
{
    _showRx = show;
}

void TraceFilterModel::setHiddenMessageIds(const QSet<uint32_t> &ids)
{
    _hiddenMessageIds = ids;
}

void TraceFilterModel::setHiddenLinFrameIds(const QSet<uint8_t> &ids)
{
    _hiddenLinFrameIds = ids;
}

void TraceFilterModel::setHiddenInterfaces(const QSet<BusInterfaceId> &ids)
{
    _hiddenInterfaces = ids;
}

bool TraceFilterModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    // Fast path: nothing is filtered, so skip the message fetch entirely
    if (_filterText.isEmpty() && _showTx && _showRx
        && _hiddenMessageIds.isEmpty() && _hiddenLinFrameIds.isEmpty()
        && _hiddenInterfaces.isEmpty())
    {
        return true;
    }

    // Get the underlying BaseTraceViewModel to access the BusMessage directly
    auto *baseModel = qobject_cast<BaseTraceViewModel *>(sourceModel());
    QSortFilterProxyModel *proxySource = nullptr;
    if (!baseModel)
    {
        proxySource = qobject_cast<QSortFilterProxyModel *>(sourceModel());
        if (proxySource)
        {
            baseModel = qobject_cast<BaseTraceViewModel *>(proxySource->sourceModel());
        }
    }

    if (baseModel)
    {
        QModelIndex baseIdx;
        if (proxySource)
        {
            QModelIndex proxyIdx = proxySource->index(source_row, 0, source_parent);
            baseIdx = proxySource->mapToSource(proxyIdx);
        }
        else
        {
            baseIdx = sourceModel()->index(source_row, 0, source_parent);
        }
        BusMessage msg = baseModel->getMessage(baseIdx);

        if (!_showTx && !msg.isRX())
        {
            return false;
        }
        if (!_showRx && msg.isRX())
        {
            return false;
        }
        if (msg.busType() == BusType::LIN)
        {
            if (_hiddenLinFrameIds.contains(static_cast<uint8_t>(msg.getId())))
            {
                return false;
            }
        }
        else if (_hiddenMessageIds.contains(msg.getId()))
        {
            return false;
        }
        if (_hiddenInterfaces.contains(msg.getInterfaceId()))
        {
            return false;
        }
    }

    // Only matches (T-11): Find's rules. A row stays when it or one of its decoded
    // lines matches; the lines under a row that stays are all shown.
    if (_filterText.isEmpty() || source_parent.isValid())
    {
        return true;
    }
    const QModelIndex row = sourceModel()->index(source_row, 0, source_parent);
    if (lineMatches(row, _filterText))
    {
        return true;
    }
    for (int child = 0; child < sourceModel()->rowCount(row); ++child)
    {
        if (lineMatches(sourceModel()->index(child, 0, row), _filterText))
        {
            return true;
        }
    }
    return false;
}
