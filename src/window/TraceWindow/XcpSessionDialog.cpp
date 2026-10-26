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

#include <algorithm>
#include <optional>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QMap>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include "core/Backend.h"
#include "core/BusTrace.h"
#include "decoders/A2lXcpReader.h"
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
    auto *a2lButton = new QPushButton(tr("Load IDs from A2L..."), this);

    _sessions = new QListWidget(this);
    _sessions->setMinimumHeight(80);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto *setRow = new QHBoxLayout();
    setRow->addWidget(a2lButton);
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
    connect(a2lButton, &QPushButton::clicked, this, &XcpSessionDialog::loadA2l);
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

std::pair<int, int> XcpSessionDialog::interfaceCarrying(uint32_t master, uint32_t slave, bool extended) const
{
    // The A2L names the identifiers but not the bus. On a two-adapter setup the files are
    // told apart only by their identifiers, and those are already in the trace: reading the
    // answer off the capture beats guessing it from the order the files were picked.
    QMap<BusInterfaceId, int> counts;
    BusTrace *trace = _backend.getTrace();
    const int size = static_cast<int>(trace->size());
    for (int i = 0; i < size; ++i) {
        const BusMessage m = trace->getMessage(i);
        if (m.busType() != BusType::CAN || m.isErrorFrame() || m.isExtended() != extended) { continue; }
        if (m.getId() == master || m.getId() == slave) { ++counts[m.getInterfaceId()]; }
    }
    int best = -1;
    int bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    }
    return {best, bestCount};
}

void XcpSessionDialog::loadA2l()
{
    QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load XCP IDs from A2L"), QString(),
                                                      tr("A2L files (*.a2l);;All files (*)"));
    if (paths.isEmpty()) { return; }
    std::sort(paths.begin(), paths.end(), [](const QString &a, const QString &b)
    {
        return QFileInfo(a).fileName().compare(QFileInfo(b).fileName(), Qt::CaseInsensitive) < 0;
    });

    struct Found
    {
        QString path;
        A2lXcpIds ids;
    };
    QList<Found> found;
    QStringList problems;
    for (const QString &path : std::as_const(paths)) {
        QString error;
        const A2lXcpIds ids = A2lXcpReader::read(path, &error);
        if (!error.isEmpty()) {
            problems << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
        } else if (ids.hasPair()) {
            found.append({path, ids});
        } else {
            problems << tr("%1: no CAN_ID_MASTER / CAN_ID_SLAVE pair in an XCP_ON_CAN block").arg(QFileInfo(path).fileName());
        }
    }
    if (found.isEmpty()) {
        QMessageBox::warning(this, tr("Load A2L"),
            tr("No XCP on CAN identifiers found. The file may describe another transport layer "
               "(Ethernet, USB, FlexRay).\n\n%1").arg(problems.join(QLatin1Char('\n'))));
        return;
    }

    // Which interface each A2L describes: suggest the one its IDs were seen on
    QDialog assign(this);
    assign.setWindowTitle(tr("Load A2L"));
    auto *grid = new QGridLayout();
    grid->addWidget(new QLabel(tr("<b>A2L file</b>"), &assign), 0, 0);
    grid->addWidget(new QLabel(tr("<b>XCP IDs</b>"), &assign), 0, 1);
    grid->addWidget(new QLabel(tr("<b>Interface</b>"), &assign), 0, 2);
    grid->addWidget(new QLabel(tr("<b>Why</b>"), &assign), 0, 3);
    QList<QComboBox *> combos;
    for (int i = 0; i < found.size(); ++i) {
        const A2lXcpIds &ids = found[i].ids;
        auto *combo = new QComboBox(&assign);
        combo->addItem(tr("(do not use)"), -1);
        for (int k = 0; k < _interface->count(); ++k) {
            combo->addItem(_interface->itemText(k), _interface->itemData(k));
        }
        const auto [seenOn, frames] = interfaceCarrying(*ids.master, *ids.slave, ids.extended);
        QString why;
        if (seenOn >= 0 && combo->findData(seenOn) >= 0) {
            combo->setCurrentIndex(combo->findData(seenOn));
            why = tr("seen on %1 (%2 frames)").arg(_backend.getInterfaceName(static_cast<BusInterfaceId>(seenOn))).arg(frames);
        } else {
            // Not in the capture: fall back to the order the files were picked
            combo->setCurrentIndex(std::min(i + 1, combo->count() - 1));
            why = tr("not seen in the capture");
        }
        QString idText = tr("master 0x%1 / slave 0x%2").arg(QString::number(*ids.master, 16).toUpper(),
                                                           QString::number(*ids.slave, 16).toUpper());
        if (ids.broadcast) { idText += tr(" / bcast 0x%1").arg(QString::number(*ids.broadcast, 16).toUpper()); }
        if (ids.extended) { idText += tr(" (29-bit)"); }
        grid->addWidget(new QLabel(QFileInfo(found[i].path).fileName(), &assign), i + 1, 0);
        grid->addWidget(new QLabel(idText, &assign), i + 1, 1);
        grid->addWidget(combo, i + 1, 2);
        grid->addWidget(new QLabel(why, &assign), i + 1, 3);
        combos << combo;
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &assign);
    auto *layout = new QVBoxLayout(&assign);
    layout->addWidget(new QLabel(tr("The A2L gives the CAN ID pair but not the bus it runs on. "
                                    "The suggestion comes from where those IDs were seen in the trace."), &assign));
    layout->addLayout(grid);
    if (!problems.isEmpty()) { layout->addWidget(new QLabel(problems.join(QLatin1Char('\n')), &assign)); }
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &assign, [&]()
    {
        // One session per interface: two files on one interface would silently replace each other
        QList<int> chosen;
        for (QComboBox *c : std::as_const(combos)) {
            const int id = c->currentData().toInt();
            if (id < 0) { continue; }
            if (chosen.contains(id)) {
                QMessageBox::warning(&assign, tr("Load A2L"), tr("Two A2L files are assigned to %1. "
                                                                 "Each interface carries one XCP session.").arg(c->currentText()));
                return;
            }
            chosen << id;
        }
        assign.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &assign, &QDialog::reject);
    if (assign.exec() != QDialog::Accepted) { return; }

    QList<XcpConfig> sessions = XcpSessions::instance().sessions();
    QStringList lines;
    for (int i = 0; i < found.size(); ++i) {
        const int id = combos[i]->currentData().toInt();
        if (id < 0) { continue; }
        const A2lXcpIds &ids = found[i].ids;
        XcpConfig config;
        config.interfaceId = static_cast<BusInterfaceId>(id);
        config.requestId = *ids.master;
        config.responseId = *ids.slave;
        config.broadcastId = ids.broadcast;
        config.extended = ids.extended;
        // A pair moved to another interface must not stay on the old one
        sessions.removeIf([&](const XcpConfig &s)
        {
            return s.interfaceId == config.interfaceId
                || (s.requestId == config.requestId && s.responseId == config.responseId);
        });
        sessions.append(config);
        lines << QStringLiteral("%1 → %2: %3").arg(QFileInfo(found[i].path).fileName(), combos[i]->currentText(), config.toString());
    }
    if (lines.isEmpty()) { return; }
    XcpSessions::instance().replaceAll(sessions);
    _message->setText(tr("Loaded from A2L:\n%1").arg(lines.join(QLatin1Char('\n'))));
    refreshList();
    loadInterface();
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
