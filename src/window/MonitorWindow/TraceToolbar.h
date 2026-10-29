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

#include <QTimer>
#include <QWidget>

class QAbstractScrollArea;
class QCheckBox;
class QDomElement;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QToolButton;

// The toolbar of every trace tab (GUI_DESIGN.md 3.1):
//   Scroll · Expand · Abs time · Pause · Erase | Find ↑ ↓ Only matches | Filter | Follow | tab slot … ⋯
// The Monitor tab has one, the protocol tabs share another and swap its State
// when the tab changes. Both have the same buttons in the same places; what a
// tab adds goes into the tab slot only.
class TraceToolbar : public QWidget
{
    Q_OBJECT
public:
    // Everything a tab keeps of the toolbar; saved in the workspace
    struct State
    {
        bool scroll = false;        // V-1: off on every tab
        bool expand = false;
        bool absTime = false;
        bool paused = false;
        bool follow = true;
        bool onlyMatches = false;
        QString findText;

        void save(QDomElement &el) const;
        // Attributes missing from older workspaces keep the values already in `this`
        void load(const QDomElement &el);
    };

    explicit TraceToolbar(QWidget *parent = nullptr);

    [[nodiscard]] State state() const;
    // Shows a tab's state without emitting any signal
    void setState(const State &state);

    void setFollow(bool on);                         // T-14, without a signal
    void setFilterCount(int count);                  // 0 = no filter
    void setPauseInfo(qint64 pending, qint64 dropped);
    void setFindPosition(int current, int total);    // total < 0 hides it
    void setAbsTimeEnabled(bool enabled, const QString &toolTip = QString());
    [[nodiscard]] QString findText() const;

    // Tab-specific widgets (XCP IDs…) go here; nothing else moves
    void addToTabSlot(QWidget *widget);
    // Shows the slot and the line before it on the tab that has one
    void setTabSlotVisible(bool visible);
    // Filled by the owner each time the ⋯ menu opens
    [[nodiscard]] QMenu *moreMenu() const { return _moreMenu; }

signals:
    void scrollToggled(bool on);
    void expandToggled(bool on);
    void absTimeToggled(bool on);
    void pauseToggled(bool on);
    void eraseClicked();
    void findTextChanged(const QString &text);  // after typing stops
    void findNext();
    void findPrevious();
    void onlyMatchesToggled(bool on);
    void filterClicked();
    void followToggled(bool on);
    void moreMenuAboutToShow(QMenu *menu);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QToolButton *_scroll;
    QToolButton *_expand;
    QToolButton *_absTime;
    QToolButton *_pause;
    QLabel *_pauseChip;
    QToolButton *_erase;
    QLineEdit *_find;
    QLabel *_findPosition;
    QToolButton *_findUp;
    QToolButton *_findDown;
    QCheckBox *_onlyMatches;
    QToolButton *_filter;
    QToolButton *_follow;
    QFrame *_slotSeparator;
    QHBoxLayout *_tabSlot;
    QToolButton *_more;
    QMenu *_moreMenu;
    QTimer _findDebounce;
    int _filterCount = 0;
    bool _compact = false;

    QToolButton *makeButton(const QString &text, const QString &icon, bool checkable, const QString &toolTip);
    void applyIcons();
    void applyStyle();
    void setCompact(bool compact);
    void layoutFindPosition();
};

// Follow (T-14) for a list in a QAbstractScrollArea: reports the user's own
// scrolling (wheel, scroll bar, page keys) and row picking, not the view's
// scrolling to new rows
class FollowWatcher : public QObject
{
    Q_OBJECT
public:
    explicit FollowWatcher(QAbstractScrollArea *area);

signals:
    // on = the user is at the bottom now; off = scrolled up or picked a row
    void followChanged(bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QAbstractScrollArea *_area;
    void fromPosition();
};
