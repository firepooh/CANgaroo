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

#include "TraceToolbar.h"

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QDomElement>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>

#include "core/ThemeManager.h"
#include "helpers/apphelpers.h"
#include "TraceFind.h"

namespace
{

constexpr int button_height = 28;
constexpr int compact_width = 1100;     // narrower: icons and tool tips only

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(18);
    line->setEnabled(false);    // the disabled text colour is a quiet grey in both themes
    return line;
}

} // namespace

void TraceToolbar::State::save(QDomElement &el) const
{
    el.setAttribute("scroll", scroll ? "1" : "0");
    el.setAttribute("expand", expand ? "1" : "0");
    el.setAttribute("absTime", absTime ? "1" : "0");
    el.setAttribute("follow", follow ? "1" : "0");
    el.setAttribute("onlyMatches", onlyMatches ? "1" : "0");
    el.setAttribute("find", findText);
}

void TraceToolbar::State::load(const QDomElement &el)
{
    auto flag = [&el](const char *name, bool current) {
        return el.hasAttribute(name) ? el.attribute(name) == "1" : current;
    };
    scroll = flag("scroll", scroll);
    expand = flag("expand", expand);
    absTime = flag("absTime", absTime);
    follow = flag("follow", follow);
    onlyMatches = flag("onlyMatches", onlyMatches);
    if (el.hasAttribute("find")) { findText = el.attribute("find"); }
}

TraceToolbar::TraceToolbar(QWidget *parent)
    : QWidget(parent)
{
    _scroll = makeButton(tr("Scroll"), "view-list-details", true,
                         tr("Off: one row per message. On: every frame in arrival order"));
    _expand = makeButton(tr("Expand"), "view-expand-all", true, tr("Open every row of this tab"));
    _absTime = makeButton(tr("Abs time"), "chronometer", true,
                          tr("Off: time since the previous frame (Δt). On: time since measurement start"));
    _pause = makeButton(tr("Pause"), "media-playback-pause", true,
                        tr("Freeze this tab. Frames are still recorded and shown when you resume"));
    _pauseChip = new QLabel(this);
    _pauseChip->setObjectName("pauseChip");
    _pauseChip->hide();
    _erase = makeButton(tr("Erase"), "edit-clear", false,
                        tr("Empty this tab only. The trace and the other tabs keep their frames"));

    _find = new QLineEdit(this);
    _find->setPlaceholderText(tr("ID, description or data"));
    _find->setFixedWidth(220);
    _find->setFixedHeight(button_height - 2);
    _find->setToolTip(tr("Find in ID, Description, Data and Info, and in decoded lines. "
                         "Enter: next, Shift+Enter: previous, Esc: clear"));
    _find->installEventFilter(this);
    _findPosition = new QLabel(_find);
    _findPosition->hide();
    _findUp = makeButton(QStringLiteral("↑"), QString(), false, tr("Previous result"));
    _findDown = makeButton(QStringLiteral("↓"), QString(), false, tr("Next result"));
    _onlyMatches = new QCheckBox(tr("Only matches"), this);
    _onlyMatches->setToolTip(tr("Hide the rows that do not match the Find text"));

    _filter = makeButton(tr("Filter"), "view-filter", true, QString());
    _follow = makeButton(tr("Follow"), "go-bottom", true,
                         tr("Scroll to new rows. Scrolling up or picking a row turns it off, "
                            "scrolling to the bottom turns it on again"));

    _more = makeButton(QString(), "view-more", false, tr("More: refresh, columns"));
    _more->setToolButtonStyle(Qt::ToolButtonIconOnly);
    _moreMenu = new QMenu(_more);
    _more->setMenu(_moreMenu);
    _more->setPopupMode(QToolButton::InstantPopup);

    auto *bar = new QHBoxLayout(this);
    bar->setContentsMargins(4, 3, 4, 3);
    bar->setSpacing(6);
    auto group = [this, bar]() {
        bar->addSpacing(6);
        bar->addWidget(makeSeparator(this));
        bar->addSpacing(6);
    };
    bar->addWidget(_scroll);
    bar->addWidget(_expand);
    bar->addWidget(_absTime);
    bar->addWidget(_pause);
    bar->addWidget(_erase);
    group();
    bar->addWidget(_find);
    bar->addWidget(_findUp);
    bar->addWidget(_findDown);
    bar->addWidget(_onlyMatches);
    group();
    bar->addWidget(_filter);
    group();
    bar->addWidget(_follow);
    bar->addSpacing(6);
    _slotSeparator = makeSeparator(this);
    bar->addWidget(_slotSeparator);
    bar->addSpacing(6);
    _tabSlot = new QHBoxLayout();
    _tabSlot->setSpacing(6);
    bar->addLayout(_tabSlot);
    _slotSeparator->hide();     // until a tab shows its slot
    bar->addStretch(1);
    // The chip sits right of the stretch: its changing width moves no button
    bar->addWidget(_pauseChip);
    bar->addWidget(_more);

    connect(_scroll, &QToolButton::toggled, this, &TraceToolbar::scrollToggled);
    connect(_expand, &QToolButton::toggled, this, &TraceToolbar::expandToggled);
    connect(_absTime, &QToolButton::toggled, this, &TraceToolbar::absTimeToggled);
    connect(_pause, &QToolButton::toggled, this, [this](bool on) {
        if (!on) { _pauseChip->hide(); }
        emit pauseToggled(on);
    });
    connect(_erase, &QToolButton::clicked, this, &TraceToolbar::eraseClicked);
    connect(_findUp, &QToolButton::clicked, this, &TraceToolbar::findPrevious);
    connect(_findDown, &QToolButton::clicked, this, &TraceToolbar::findNext);
    connect(_onlyMatches, &QCheckBox::toggled, this, &TraceToolbar::onlyMatchesToggled);
    connect(_filter, &QToolButton::clicked, this, [this]() {
        // Lit while a filter is set, not by the click
        const QSignalBlocker block(_filter);
        _filter->setChecked(_filterCount > 0);
        emit filterClicked();
    });
    connect(_follow, &QToolButton::toggled, this, &TraceToolbar::followToggled);
    connect(_moreMenu, &QMenu::aboutToShow, this, [this]() {
        _moreMenu->clear();
        emit moreMenuAboutToShow(_moreMenu);
    });

    _findDebounce.setInterval(250);
    _findDebounce.setSingleShot(true);
    connect(_find, &QLineEdit::textChanged, &_findDebounce, qOverload<>(&QTimer::start));
    connect(&_findDebounce, &QTimer::timeout, this, [this]() { emit findTextChanged(findText()); });

    setFilterCount(0);
    setState(State());
    applyIcons();
    applyStyle();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](ThemeManager::Theme) {
        applyIcons();
        applyStyle();
    });
}

QToolButton *TraceToolbar::makeButton(const QString &text, const QString &icon, bool checkable,
                                      const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    button->setFixedHeight(button_height);
    button->setIconSize(QSize(16, 16));
    button->setToolButtonStyle(icon.isEmpty() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
    button->setToolTip(toolTip.isEmpty() ? text : toolTip);
    button->setProperty("iconName", icon);
    return button;
}

void TraceToolbar::applyIcons()
{
    // Theme icon first, then the bundled one drawn in the text colour (GUI_DESIGN.md 3.7).
    // Windows has no icon theme, but Qt answers some names (edit-clear as an X,
    // media-playback-pause) from the Segoe Fluent font, which mixes two sets in one
    // toolbar; there the bundled set is used throughout.
    const QColor color = palette().color(QPalette::WindowText);
    for (QToolButton *button : findChildren<QToolButton *>()) {
        const QString name = button->property("iconName").toString();
        if (name.isEmpty()) { continue; }
        const QString bundled = QStringLiteral(":/assets/icons/%1-symbolic.svg").arg(name);
#ifdef Q_OS_WIN
        button->setIcon(AppHelpers::recoloredSvgIcon(bundled, color));
#else
        button->setIcon(AppHelpers::themedIcon(name, bundled));
#endif
    }
}

void TraceToolbar::applyStyle()
{
    // One "on" colour for every toggle (GUI_DESIGN.md 3.5). The design has only the
    // light theme so far; the dark values are the same hue, dark enough for light text.
    const bool dark = ThemeManager::instance().isDarkMode();
    const QString on = dark ? "#26435E" : "#DCEBF8";
    const QString onBorder = dark ? "#3A6A94" : "#B9D4EE";
    const QString hover = dark ? "rgba(255,255,255,0.08)" : "rgba(0,0,0,0.06)";
    const QString chip = dark ? "#3A3A3A" : "#ECECEC";
    setStyleSheet(QStringLiteral(
        "TraceToolbar QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 0px 6px; }"
        "TraceToolbar QToolButton:hover { background: %3; }"
        "TraceToolbar QToolButton:checked { background: %1; border-color: %2; }"
        "TraceToolbar QToolButton::menu-indicator { image: none; width: 0px; }"
        "TraceToolbar QLabel#pauseChip { background: %4; border-radius: 9px; padding: 1px 8px; }")
        .arg(on, onBorder, hover, chip));
}

void TraceToolbar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) { applyIcons(); }
}

TraceToolbar::State TraceToolbar::state() const
{
    State s;
    s.scroll = _scroll->isChecked();
    s.expand = _expand->isChecked();
    s.absTime = _absTime->isChecked();
    s.paused = _pause->isChecked();
    s.follow = _follow->isChecked();
    s.onlyMatches = _onlyMatches->isChecked();
    s.findText = _find->text();
    return s;
}

void TraceToolbar::setState(const State &state)
{
    const QSignalBlocker b1(_scroll), b2(_expand), b3(_absTime), b4(_pause), b5(_follow), b6(_onlyMatches), b7(_find);
    _scroll->setChecked(state.scroll);
    _expand->setChecked(state.expand);
    _absTime->setChecked(state.absTime);
    _pause->setChecked(state.paused);
    _follow->setChecked(state.follow);
    _onlyMatches->setChecked(state.onlyMatches);
    _find->setText(state.findText);
    _findDebounce.stop();
    if (!state.paused) { _pauseChip->hide(); }
    setFindPosition(-1, -1);
}

void TraceToolbar::setFollow(bool on)
{
    const QSignalBlocker block(_follow);
    _follow->setChecked(on);
}

void TraceToolbar::setFilterCount(int count)
{
    _filterCount = count;
    const QSignalBlocker block(_filter);
    _filter->setChecked(count > 0);
    _filter->setText(count > 0 ? tr("Filter · %1").arg(count) : tr("Filter"));
    _filter->setToolTip(count > 0 ? tr("%n condition(s) hide frames in this tab. Click to change them", "", count)
                                  : tr("Hide frames of this tab by direction, ID or interface"));
}

void TraceToolbar::setPauseInfo(qint64 pending, qint64 dropped)
{
    if (!_pause->isChecked()) {
        _pauseChip->hide();
        return;
    }
    QString text = tr("Paused · %L1 new").arg(pending);
    if (dropped > 0) { text += tr(" · %L1 dropped").arg(dropped); }
    _pauseChip->setText(text);
    _pauseChip->setToolTip(tr("Frames recorded since this tab was paused. The trace keeps a limited number "
                              "of frames: frames it drops before you resume are not shown."));
    _pauseChip->show();
}

void TraceToolbar::setFindPosition(int current, int total)
{
    if (total < 0 || _find->text().trimmed().isEmpty()) {
        _findPosition->hide();
        _find->setTextMargins(0, 0, 0, 0);
        return;
    }
    _findPosition->setText(TraceFind::positionText(current, total));
    _findPosition->show();
    layoutFindPosition();
}

void TraceToolbar::layoutFindPosition()
{
    _findPosition->adjustSize();
    const int w = _findPosition->width();
    _find->setTextMargins(0, 0, w + 6, 0);
    _findPosition->move(_find->width() - w - 6, (_find->height() - _findPosition->height()) / 2);
}

void TraceToolbar::setAbsTimeEnabled(bool enabled, const QString &toolTip)
{
    _absTime->setEnabled(enabled);
    _absTime->setToolTip(toolTip.isEmpty()
        ? tr("Off: time since the previous frame (Δt). On: time since measurement start") : toolTip);
}

QString TraceToolbar::findText() const
{
    return _find->text().trimmed();
}

void TraceToolbar::setTabSlotVisible(bool visible)
{
    _slotSeparator->setVisible(visible);
    for (int i = 0; i < _tabSlot->count(); ++i) {
        if (QWidget *w = _tabSlot->itemAt(i)->widget()) { w->setVisible(visible); }
    }
}

void TraceToolbar::addToTabSlot(QWidget *widget)
{
    widget->setParent(this);
    _tabSlot->addWidget(widget);
    if (auto *button = qobject_cast<QToolButton *>(widget)) {
        button->setAutoRaise(true);
        button->setFixedHeight(button_height);
        button->setIconSize(QSize(16, 16));
        applyIcons();
    }
}

void TraceToolbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    setCompact(width() < compact_width);
    if (_findPosition->isVisible()) { layoutFindPosition(); }
}

void TraceToolbar::setCompact(bool compact)
{
    if (compact == _compact) { return; }
    _compact = compact;
    for (QToolButton *button : findChildren<QToolButton *>()) {
        if (button->icon().isNull() || button == _more) { continue; }
        button->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    }
}

bool TraceToolbar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _find && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            _find->clear();             // K-2: Esc only clears the Find box
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (_findDebounce.isActive()) {
                _findDebounce.stop();
                emit findTextChanged(findText());
            }
            if (key->modifiers() & Qt::ShiftModifier) { emit findPrevious(); } else { emit findNext(); }
            return true;
        }
    }
    if (watched == _find && event->type() == QEvent::Resize && _findPosition->isVisible()) {
        layoutFindPosition();
    }
    return QWidget::eventFilter(watched, event);
}

FollowWatcher::FollowWatcher(QAbstractScrollArea *area)
    : QObject(area), _area(area)
{
    // Scroll bar arrows, page clicks and dragging; the slider position is already the new one
    connect(area->verticalScrollBar(), &QAbstractSlider::actionTriggered, this, [this](int)
    {
        QScrollBar *bar = _area->verticalScrollBar();
        emit followChanged(bar->sliderPosition() >= bar->maximum());
    });
    area->viewport()->installEventFilter(this);
    area->installEventFilter(this);
}

void FollowWatcher::fromPosition()
{
    QScrollBar *bar = _area->verticalScrollBar();
    emit followChanged(bar->value() >= bar->maximum());
}

bool FollowWatcher::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _area->viewport()) {
        if (event->type() == QEvent::Wheel) {
            // After the view has scrolled
            QTimer::singleShot(0, this, &FollowWatcher::fromPosition);
        } else if (event->type() == QEvent::MouseButtonPress) {
            emit followChanged(false);
        }
    } else if (watched == _area && event->type() == QEvent::KeyPress) {
        switch (static_cast<QKeyEvent *>(event)->key()) {
            case Qt::Key_Up:
            case Qt::Key_Down:
                emit followChanged(false);      // moves the current row
                break;
            case Qt::Key_PageUp:
            case Qt::Key_PageDown:
            case Qt::Key_Home:
            case Qt::Key_End:
                QTimer::singleShot(0, this, &FollowWatcher::fromPosition);
                break;
            default:
                break;
        }
    }
    return QObject::eventFilter(watched, event);
}
