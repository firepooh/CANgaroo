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

#include "MonitorWindow.h"

#include <QDomDocument>
#include <QVBoxLayout>

#include "window/TraceWindow/TraceWindow.h"
#include "MonitorView.h"

MonitorWindow::MonitorWindow(QWidget *parent, Backend &backend)
    : ConfigurableWidget(parent)
{
    _trace = new TraceWindow(this, backend);
    _monitor = new MonitorView(backend, _trace);
    _trace->setMonitorPage(_monitor);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_trace);

    connect(_monitor, &MonitorView::settingsChanged, this, [this]() { emit settingsChanged(this); });
    connect(_trace, &ConfigurableWidget::settingsChanged, this, [this]() { emit settingsChanged(this); });
}

bool MonitorWindow::saveXML(Backend &backend, QDomDocument &xml, QDomElement &root)
{
    if (!_trace->saveXML(backend, xml, root)) { return false; }

    // The active tab is TraceWindow's ActiveTab (0 = Monitor)
    QDomElement el = xml.createElement("MonitorView");
    _monitor->saveXML(xml, el);
    root.appendChild(el);
    return true;
}

bool MonitorWindow::loadXML(Backend &backend, QDomElement &el)
{
    if (!_trace->loadXML(backend, el)) { return false; }

    const QDomElement monitorEl = el.firstChildElement("MonitorView");
    if (!monitorEl.isNull()) {
        // Older workspaces also have ActivePage (the former outer page tabs); it is ignored
        _monitor->loadXML(monitorEl);
    }
    return true;
}

void MonitorWindow::retranslateUi()
{
}
