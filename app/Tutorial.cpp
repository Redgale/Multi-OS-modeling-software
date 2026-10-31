// SPDX-License-Identifier: GPL-3.0-or-later
#include "Tutorial.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "MainWindow.hpp"
#include "SketchEditor.hpp"

namespace jigapp
{

TutorialCard::TutorialCard(MainWindow* w, QWidget* parent)
    : QFrame(parent)
    , w_(w)
{
    setObjectName("TutorialCard");
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setAccessibleName(tr("Tutorial"));
    setFixedWidth(360);
    auto* lay = new QVBoxLayout(this);
    progress_ = new QLabel(this);
    title_ = new QLabel(this);
    text_ = new QLabel(this);
    text_->setWordWrap(true);
    text_->setTextFormat(Qt::RichText);
    QFont f = title_->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    title_->setFont(f);
    back_ = new QPushButton(tr("Back"), this);
    skip_ = new QPushButton(tr("Skip step"), this);
    showMe_ = new QPushButton(tr("Show me"), this);
    auto* close = new QPushButton(tr("Close"), this);
    auto* row = new QHBoxLayout();
    row->addWidget(back_);
    row->addWidget(showMe_);
    row->addStretch();
    row->addWidget(skip_);
    row->addWidget(close);
    lay->addWidget(progress_);
    lay->addWidget(title_);
    lay->addWidget(text_);
    lay->addLayout(row);

    auto doc = [w]() -> jig::Document& { return w->document(); };
    auto sketchOnFloor = [doc] {
        for (auto& f : doc().model().features) {
            if (f.kind == jig::FeatureKind::Sketch && f.params.value("plane", std::string {}) == "XY") {
                return true;
            }
        }
        return false;
    };
    steps_ = {
        {tr("Start a sketch"),
         tr("Every part starts as a flat drawing. Click <b>Sketch</b> on the toolbar, then click the blue "
            "<b>floor</b> plane (XY) in the 3D view."),
         sketchOnFloor, "sketch.create"},
        {tr("Draw a rectangle"),
         tr("Choose <b>Center rectangle</b>, click the middle of the grid, move the mouse, then type "
            "<b>60</b>, press Tab, type <b>30</b> and press Enter. The area inside turns shaded: it is closed and "
            "ready to become 3D."),
         [doc] {
             for (auto& [id, sk] : doc().state().sketches) {
                 if (!sk->profiles.regions.empty()) {
                     return true;
                 }
             }
             return false;
         },
         "sketch.rectangle"},
        {tr("Finish the sketch"),
         tr("Click <b>Finish sketch</b> (or press Esc twice) to go back to 3D tools."),
         [w] { return w->sketchEditor() == nullptr && !w->document().state().sketches.empty(); }, "sketch.finish"},
        {tr("Make it 3D"),
         tr("Click <b>Extrude</b>, type <b>5</b> for the distance and press Enter (or click OK). A plain number "
            "is in the design's unit."),
         [doc] { return !doc().state().bodies.empty(); }, "solid.extrude"},
        {tr("Round the edges"),
         tr("Click <b>Fillet</b>, click the four top edges in the 3D view, type <b>2</b> and press Enter. Rounded "
            "edges look finished and are nicer to hold."),
         [doc] {
             for (int i = 0; i < int(doc().model().features.size()); ++i) {
                 if (doc().model().features[size_t(i)].kind == jig::FeatureKind::Fillet &&
                     doc().status(i).level != jig::StatusLevel::Error) {
                     return true;
                 }
             }
             return false;
         },
         "solid.fillet"},
        {tr("Get it ready to print"),
         tr("Choose <b>File ▸ Export STL…</b> (or 3MF) and save it: that file goes to your slicer. Use "
            "<b>Inspect ▸ Properties</b> to see how much it weighs."),
         [w] { return w->exportedSinceStart(); }, "file.export_stl"},
    };
    connect(back_, &QPushButton::clicked, this, [this] { showStep(index_ - 1); });
    connect(skip_, &QPushButton::clicked, this, [this] { showStep(index_ + 1); });
    connect(close, &QPushButton::clicked, this, [this] {
        hide();
        emit closed();
    });
    connect(showMe_, &QPushButton::clicked, this, [this] {
        if (finished()) {
            return;
        }
        // point at the tool: flash its toolbar button
        if (QAction* a = w_->commandAction(steps_[size_t(index_)].showMe)) {
            for (auto* bar : w_->findChildren<QToolBar*>()) {
                if (auto* b = qobject_cast<QToolButton*>(bar->widgetForAction(a)); b && b->isVisible()) {
                    const QString old = b->styleSheet();
                    b->setStyleSheet("QToolButton { border: 3px solid #f0a020; border-radius: 6px; }");
                    QTimer::singleShot(1800, b, [b, old] { b->setStyleSheet(old); });
                }
            }
        }
    });
    timer_ = new QTimer(this);
    timer_->setInterval(300);
    connect(timer_, &QTimer::timeout, this, [this] { check(); });
    timer_->start();
    showStep(0);
}

void TutorialCard::showStep(int i)
{
    index_ = std::clamp(i, 0, int(steps_.size()));
    back_->setEnabled(index_ > 0);
    if (finished()) {
        progress_->setText(tr("Done"));
        title_->setText(tr("Your first part is ready"));
        text_->setText(tr("That is the whole loop: <b>sketch</b>, <b>feature</b>, <b>refine</b>. Next, try "
                          "<b>File ▸ Open a sample</b>, or press <b>Ctrl+K</b> and type what you want to do, e.g. "
                          "“hole” or “round the edges”."));
        skip_->setEnabled(false);
        showMe_->setEnabled(false);
        timer_->stop();
    }
    else {
        const Step& s = steps_[size_t(index_)];
        progress_->setText(tr("Step %1 of %2").arg(index_ + 1).arg(steps_.size()));
        title_->setText(s.title);
        text_->setText(s.text);
        skip_->setEnabled(true);
        showMe_->setEnabled(!s.showMe.isEmpty());
        timer_->start();
    }
    adjustSize();
}

void TutorialCard::check()
{
    while (!finished() && steps_[size_t(index_)].done && steps_[size_t(index_)].done()) {
        showStep(index_ + 1);
    }
}

}  // namespace jigapp
