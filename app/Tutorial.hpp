// SPDX-License-Identifier: GPL-3.0-or-later
// First-run tutorial (milestone M7): a small card over the 3D view that walks a beginner
// through a first part. Each step moves on by itself when the design shows it is done;
// nothing is done for the user, and the card never blocks the window.
#pragma once

#include <functional>
#include <vector>

#include <QFrame>
#include <QPointer>

class QLabel;
class QPushButton;
class QTimer;

namespace jigapp
{

class MainWindow;

class TutorialCard : public QFrame
{
    Q_OBJECT
public:
    struct Step
    {
        QString title, text;
        std::function<bool()> done;
        QString showMe;  // command id whose button "Show me" points at
    };
    TutorialCard(MainWindow* w, QWidget* parent);
    int step() const
    {
        return index_;
    }
    int stepCount() const
    {
        return int(steps_.size());
    }
    bool finished() const
    {
        return index_ >= int(steps_.size());
    }
    void check();  // advance when the current step is done (also runs on a timer)

signals:
    void closed();

private:
    void showStep(int i);
    MainWindow* w_;
    std::vector<Step> steps_;
    int index_ = 0;
    QLabel *progress_, *title_, *text_;
    QPushButton *back_, *skip_, *showMe_;
    QTimer* timer_;
};

}  // namespace jigapp
