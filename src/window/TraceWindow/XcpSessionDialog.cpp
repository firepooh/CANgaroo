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

#include "XcpSessionDialog.h"

#include <optional>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include "core/Backend.h"
#include "decoders/XcpSessions.h"

namespace
{

// "601", "0x601" -> 0x601; nothing for an empty or invalid field
std::optional<uint32_t> parseId(const QString &text)
{
    QString t = text.trimmed();
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) { t = t.mid(2); }
    if (t.isEmpty()) { return std::nullopt; }
    bool ok = false;
    const uint32_t v = t.toUInt(&ok, 16);
    return ok ? std::optional<uint32_t>(v) : std::nullopt;
}

QString hexId(uint32_t id)
{
    return QString::number(id, 16).toUpper();
}

} // namespace

XcpSessionDialog::XcpSessionDialog(Backend &backend, QWidget *parent)
    : QDialog(parent), _backend(backend)
{
    setWindowTitle(tr("XCP on CAN"));

    _interface = new QComboBox(this);
    for (BusInterfaceId id : backend.getInterfaceList()) {
        _interface->addItem(backend.getInterfaceName(id), id);
    }

    auto *hex = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("(0[xX])?[0-9a-fA-F]{0,8}")), this);
    _request = new QLineEdit(this);
    _response = new QLineEdit(this);
    _broadcast = new QLineEdit(this);
    for (QLineEdit *edit : {_request, _response, _broadcast}) { edit->setValidator(hex); }
    _broadcast->setPlaceholderText(tr("optional"));
    _extended = new QCheckBox(tr("29-bit identifiers"), this);

    _message = new QLabel(this);
    _message->setWordWrap(true);

    auto *form = new QFormLayout();
    form->addRow(tr("Interface"), _interface);
    form->addRow(tr("Request ID (master → slave)"), _request);
    form->addRow(tr("Response ID (slave → master)"), _response);
    form->addRow(tr("Broadcast ID"), _broadcast);
    form->addRow(QString(), _extended);

    auto *setButton = new QPushButton(tr("Set"), this);
    setButton->setDefault(true);
    auto *removeButton = new QPushButton(tr("Remove selected"), this);

    _sessions = new QListWidget(this);
    _sessions->setMinimumHeight(80);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto *setRow = new QHBoxLayout();
    setRow->addStretch(1);
    setRow->addWidget(setButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(setRow);
    layout->addWidget(_message);
    layout->addWidget(new QLabel(tr("Configured sessions (one per interface):"), this));
    layout->addWidget(_sessions);
    layout->addWidget(removeButton, 0, Qt::AlignLeft);
    layout->addWidget(new QLabel(tr("Changing the sessions decodes the trace again from the start. "
                                    "Frames older than the trace buffer are not decoded."), this));
    layout->addWidget(buttons);

    connect(_interface, &QComboBox::currentIndexChanged, this, &XcpSessionDialog::loadInterface);
    connect(setButton, &QPushButton::clicked, this, &XcpSessionDialog::apply);
    connect(removeButton, &QPushButton::clicked, this, &XcpSessionDialog::removeSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshList();
    loadInterface();
}

void XcpSessionDialog::loadInterface()
{
    const auto id = static_cast<BusInterfaceId>(_interface->currentData().toUInt());
    for (const XcpConfig &s : XcpSessions::instance().sessions()) {
        if (s.interfaceId == id) {
            _request->setText(hexId(s.requestId));
            _response->setText(hexId(s.responseId));
            _broadcast->setText(s.broadcastId ? hexId(*s.broadcastId) : QString());
            _extended->setChecked(s.extended);
            return;
        }
    }
}

void XcpSessionDialog::apply()
{
    if (_interface->count() == 0) {
        _message->setText(tr("No interface. Set up the measurement first."));
        return;
    }
    const auto request = parseId(_request->text());
    const auto response = parseId(_response->text());
    if (!request || !response) {
        _message->setText(tr("Enter the request and response IDs in hex, e.g. 601 and 701."));
        return;
    }
    if (*request == *response) {
        _message->setText(tr("Request and response need different IDs."));
        return;
    }

    XcpConfig config;
    config.interfaceId = static_cast<BusInterfaceId>(_interface->currentData().toUInt());
    config.requestId = *request;
    config.responseId = *response;
    config.broadcastId = parseId(_broadcast->text());
    config.extended = _extended->isChecked();
    XcpSessions::instance().set(config);

    _message->setText(tr("XCP on %1: %2").arg(_interface->currentText(), config.toString()));
    refreshList();
}

void XcpSessionDialog::removeSelected()
{
    QListWidgetItem *item = _sessions->currentItem();
    if (!item) { return; }
    XcpSessions::instance().remove(static_cast<BusInterfaceId>(item->data(Qt::UserRole).toUInt()));
    _message->setText(tr("Removed the XCP session on %1.").arg(item->text().section(QLatin1Char(':'), 0, 0)));
    refreshList();
}

void XcpSessionDialog::refreshList()
{
    _sessions->clear();
    for (const XcpConfig &s : XcpSessions::instance().sessions()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1: %2").arg(_backend.getInterfaceName(s.interfaceId), s.toString()));
        item->setData(Qt::UserRole, s.interfaceId);
        _sessions->addItem(item);
    }
}
