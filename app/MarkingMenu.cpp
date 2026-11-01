// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkingMenu.hpp"

#include <cmath>
#include <numbers>

#include <QAction>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QToolButton>

namespace jigapp
{
namespace
{
// buttons sit on an ellipse (wider than tall, so the labels do not overlap)
constexpr int kRx = 170, kRy = 120;
constexpr int kButtonW = 150, kButtonH = 34;
}  // namespace

MarkingMenu::MarkingMenu(const std::vector<QAction*>& actions, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAccessibleName(tr("Marking menu"));
    const int w = 2 * kRx + kButtonW + 8, h = 2 * kRy + kButtonH + 8;
    resize(w, h);
    const QPoint c(w / 2, h / 2);
    for (size_t i = 0; i < actions.size() && i < 8; ++i) {
        QAction* a = actions[i];
        if (!a) {
            continue;
        }
        auto* b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setFixedSize(kButtonW, kButtonH);
        b->setText(QString("%1  %2").arg(i + 1).arg(a->text()));  // the number key picks it too
        b->setAccessibleName(a->text());
        b->setEnabled(a->isEnabled());
        const double ang = std::numbers::pi / 2 - i * std::numbers::pi / 4;  // clockwise from the top
        const QPoint at = c + QPoint(int(std::lround(kRx * std::cos(ang))), -int(std::lround(kRy * std::sin(ang))));
        b->move(at - QPoint(kButtonW / 2, kButtonH / 2));
        connect(b, &QToolButton::clicked, this, &QWidget::close);
        buttons_.push_back(b);
    }
}

void MarkingMenu::popupAt(const QPoint& globalPos)
{
    QPoint tl = globalPos - QPoint(width() / 2, height() / 2);
    if (QScreen* s = QGuiApplication::screenAt(globalPos)) {
        const QRect r = s->availableGeometry();
        tl.setX(std::clamp(tl.x(), r.left(), r.right() - width()));
        tl.setY(std::clamp(tl.y(), r.top(), r.bottom() - height()));
    }
    move(tl);
    show();
    setFocus();
}

int MarkingMenu::buttonCount() const
{
    return int(buttons_.size());
}

QToolButton* MarkingMenu::button(int i) const
{
    return i >= 0 && i < int(buttons_.size()) ? buttons_[size_t(i)] : nullptr;
}

void MarkingMenu::paintEvent(QPaintEvent*)
{
    // a faint ring and a centre dot where the click was
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ring = palette().color(QPalette::Base);
    ring.setAlpha(170);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(ring);
    const QPoint c(width() / 2, height() / 2);
    p.drawEllipse(c, kRx - 20, kRy - 20);
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawEllipse(c, 4, 4);
}

void MarkingMenu::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    const int n = e->key() - Qt::Key_1;  // 1–8 pick the buttons in order
    if (n >= 0 && n < 8) {
        for (auto* b : buttons_) {
            if (b->text().startsWith(QString::number(n + 1) + " ")) {
                b->click();
                return;
            }
        }
    }
    QWidget::keyPressEvent(e);
}

}  // namespace jigapp
