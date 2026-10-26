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
#include <QTabWidget>
#include <QVBoxLayout>

#include "window/TraceWindow/TraceWindow.h"
#include "MonitorView.h"

MonitorWindow::MonitorWindow(QWidget *parent, Backend &backend)
    : ConfigurableWidget(parent)
{
    _tabs = new QTabWidget(this);
    _tabs->setDocumentMode(true);
    _monitor = new MonitorView(backend, _tabs);
    _trace = new TraceWindow(_tabs, backend);
    _tabs->addTab(_monitor, tr("Monitor"));
    _tabs->addTab(_trace, tr("Trace / UDS / J1939"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_tabs);

    connect(_monitor, &MonitorView::settingsChanged, this, [this]() { emit settingsChanged(this); });
    connect(_trace, &ConfigurableWidget::settingsChanged, this, [this]() { emit settingsChanged(this); });
}

bool MonitorWindow::saveXML(Backend &backend, QDomDocument &xml, QDomElement &root)
{
    if (!_trace->saveXML(backend, xml, root)) { return false; }

    QDomElement el = xml.createElement("MonitorView");
    el.setAttribute("ActivePage", _tabs->currentIndex());
    _monitor->saveXML(xml, el);
    root.appendChild(el);
    return true;
}

bool MonitorWindow::loadXML(Backend &backend, QDomElement &el)
{
    if (!_trace->loadXML(backend, el)) { return false; }

    const QDomElement monitorEl = el.firstChildElement("MonitorView");
    if (!monitorEl.isNull()) {
        _monitor->loadXML(monitorEl);
        _tabs->setCurrentIndex(monitorEl.attribute("ActivePage", "0").toInt());
    }
    return true;
}

void MonitorWindow::retranslateUi()
{
    _tabs->setTabText(0, tr("Monitor"));
    _tabs->setTabText(1, tr("Trace / UDS / J1939"));
}
