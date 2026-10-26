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

#include <QDialog>

class Backend;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

// Sets the XCP request / response IDs per interface (XcpSessions)
class XcpSessionDialog : public QDialog
{
    Q_OBJECT
public:
    XcpSessionDialog(Backend &backend, QWidget *parent = nullptr);

private:
    Backend &_backend;
    QComboBox *_interface;
    QLineEdit *_request;
    QLineEdit *_response;
    QLineEdit *_broadcast;
    QCheckBox *_extended;
    QLabel *_message;
    QListWidget *_sessions;

    void loadInterface();
    void apply();
    void removeSelected();
    void refreshList();
};
