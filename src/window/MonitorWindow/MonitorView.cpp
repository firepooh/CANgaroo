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
    : QWidget(parent)
{
    _buffer = new MonitorBuffer(backend, this);
    _table = new MonitorTable(_buffer, this);

    _scrollButton = makeButton(tr("Scroll"), true, this);
    _scrollButton->setToolTip(tr("On: every frame in arrival order. Off: one row per ID"));
    _expandBox = new QCheckBox(tr("Expand"), this);
    _expandBox->setChecked(true);
    _expandBox->setToolTip(tr("Show the data lines and signals of every row"));
    _timeAbsButton = makeButton(tr("Time Abs"), true, this);
    _timeAbsButton->setToolTip(tr("On: time since measurement start. Off: time since the previous frame"));
    _pauseButton = makeButton(tr("Pause"), true, this);
    _pauseButton->setToolTip(tr("Freeze this view; frames are still recorded"));
    QToolButton *eraseButton = makeButton(tr("Erase"), false, this);
    eraseButton->setToolTip(tr("Clear this view only"));

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
    toolbar->addWidget(_scrollButton);
    toolbar->addWidget(_expandBox);
    toolbar->addWidget(_timeAbsButton);
    toolbar->addWidget(_pauseButton);
    toolbar->addWidget(eraseButton);
    toolbar->addSpacing(16);
    toolbar->addWidget(new QLabel(tr("Refresh:"), this));
    toolbar->addWidget(_refreshBox);
    toolbar->addSpacing(16);
    toolbar->addWidget(new QLabel(tr("Find:"), this));
    toolbar->addWidget(_findColumn);
    toolbar->addWidget(_findText);
    toolbar->addWidget(prevButton);
    toolbar->addWidget(nextButton);
    toolbar->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(_table);

    QHeaderView *header = _table->header();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, &MonitorView::showHeaderMenu);

    connect(_scrollButton, &QToolButton::toggled, this, [this](bool on)
    {
        _buffer->setMode(on ? MonitorBuffer::Mode::Scroll : MonitorBuffer::Mode::Fixed);
        emit settingsChanged();
    });
    connect(_expandBox, &QCheckBox::toggled, this, [this](bool on)
    {
        _buffer->setAllExpanded(on);
        emit settingsChanged();
    });
    connect(_timeAbsButton, &QToolButton::toggled, this, [this](bool on)
    {
        _buffer->setAbsoluteTime(on);
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
    el.setAttribute("mode", _scrollButton->isChecked() ? "scroll" : "fixed");
    el.setAttribute("expand", _expandBox->isChecked() ? "1" : "0");
    el.setAttribute("timeAbs", _timeAbsButton->isChecked() ? "1" : "0");
    el.setAttribute("refreshMs", _refreshBox->currentData().toInt());
    el.setAttribute("findColumn", _findColumn->currentIndex());
    el.setAttribute("header", QString::fromLatin1(_table->header()->saveState().toBase64()));

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
    _scrollButton->setChecked(el.attribute("mode", "fixed") == "scroll");
    _expandBox->setChecked(el.attribute("expand", "1") == "1");
    _timeAbsButton->setChecked(el.attribute("timeAbs", "0") == "1");
    if (el.hasAttribute("refreshMs")) {
        const int index = _refreshBox->findData(el.attribute("refreshMs").toInt());
        if (index >= 0) { _refreshBox->setCurrentIndex(index); }
    }
    _findColumn->setCurrentIndex(el.attribute("findColumn", "0").toInt());

    const QByteArray header = QByteArray::fromBase64(el.attribute("header").toLatin1());
    if (!header.isEmpty()) { _table->header()->restoreState(header); }

    QHash<QString, QString> memos;
    for (QDomElement memoEl = el.firstChildElement("Memo"); !memoEl.isNull(); memoEl = memoEl.nextSiblingElement("Memo")) {
        memos.insert(memoEl.attribute("id"), memoEl.attribute("text"));
    }
    _buffer->setMemos(memos);
}
