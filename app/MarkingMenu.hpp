// SPDX-License-Identifier: GPL-3.0-or-later
// Marking menu: a right click in the view opens up to eight commands in a ring around the
// cursor. Click one, or press its number key (1–8); Esc or a click elsewhere closes it.
#pragma once

#include <QPointer>
#include <QWidget>

#include <vector>

class QAction;
class QToolButton;

namespace jigapp
{

class MarkingMenu : public QWidget
{
    Q_OBJECT
public:
    // `actions` go clockwise from the top; null entries leave a gap.
    MarkingMenu(const std::vector<QAction*>& actions, QWidget* parent);
    // Shows it centred on `globalPos` (kept on screen).
    void popupAt(const QPoint& globalPos);
    int buttonCount() const;
    QToolButton* button(int i) const;

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    std::vector<QToolButton*> buttons_;
};

}  // namespace jigapp
