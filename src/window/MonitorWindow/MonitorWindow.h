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

#pragma once

#include "core/ConfigurableWidget.h"

class MonitorView;
class QTabWidget;
class TraceWindow;

// Central widget of a trace tab: the message monitor plus the original
// TraceWindow (with its UDS and J1939 views) as a second page.
//
// The workspace keeps TraceWindow's format: TraceWindow writes its attributes
// (type "TraceWindow") and the monitor adds a <MonitorView> child element.
class MonitorWindow : public ConfigurableWidget
{
    Q_OBJECT
public:
    MonitorWindow(QWidget *parent, Backend &backend);

    [[nodiscard]] TraceWindow *traceWindow() const { return _trace; }

    bool saveXML(Backend &backend, QDomDocument &xml, QDomElement &root) override;
    bool loadXML(Backend &backend, QDomElement &el) override;

protected:
    void retranslateUi() override;

private:
    QTabWidget *_tabs;
    MonitorView *_monitor;
    TraceWindow *_trace;
};
