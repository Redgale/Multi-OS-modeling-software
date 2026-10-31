// SPDX-License-Identifier: GPL-3.0-or-later
#include "SketchEditor.hpp"

#include <cmath>
#include <numbers>

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>

namespace jigapp
{
namespace
{

constexpr double kPi = std::numbers::pi;
constexpr double kSnapPx = 9;
constexpr double kHitPx = 7;
const QColor kFree(37, 110, 222);         // geometry that can still move
const QColor kConstruction(222, 132, 30);
const QColor kAccent(255, 170, 40);
const QColor kGap(214, 48, 49);

double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a, ap = p - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double t = len2 > 0 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / len2, 0.0, 1.0) : 0.0;
    return QLineF(a + ab * t, p).length();
}

QString fmt(double v)
{
    return QString::number(v, 'g', 5);
}

}  // namespace

SketchEditor::SketchEditor(Viewport* view, jig::Document& doc, std::string featureId, QObject* parent)
    : QObject(parent)
    , view_(view)
    , doc_(doc)
    , id_(std::move(featureId))
{
    // the value box near the cursor (Tab moves between fields, Enter places, Esc cancels)
    inputBox_ = new QFrame(view_);
    inputBox_->setFrameShape(QFrame::StyledPanel);
    inputBox_->setAutoFillBackground(true);
    auto* lay = new QHBoxLayout(inputBox_);
    lay->setContentsMargins(6, 4, 6, 4);
    for (int i = 0; i < 2; ++i) {
        auto* l = new QLabel(inputBox_);
        auto* f = new QLineEdit(inputBox_);
        f->setFixedWidth(84);
        f->installEventFilter(nullptr);
        lay->addWidget(l);
        lay->addWidget(f);
        inputLabels_.push_back(l);
        inputFields_.push_back(f);
        connect(f, &QLineEdit::returnPressed, this, [this] {
            if (dimTarget_) {
                placeSecond(cursor_, inputValues());
                return;
            }
            if (first_) {
                placeSecond(cursor_, inputValues());
            }
        });
        connect(f, &QLineEdit::textChanged, this, [this] { rebuildOverlay(); });
    }
    inputBox_->hide();
    refreshFromDocument();
}

SketchEditor::~SketchEditor()
{
    delete inputBox_.data();  // null if the viewport (its parent) already went
}

// ---------------------------------------------------------------- document sync

bool SketchEditor::refreshFromDocument()
{
    const jig::Feature* f = doc_.feature(id_);
    if (!f) {
        return false;
    }
    auto it = doc_.state().sketches.find(id_);
    if (it != doc_.state().sketches.end()) {
        frame_ = it->second->frame;
        work_ = it->second->solved;
        report_ = it->second->report;
    }
    else {
        // the sketch is in error (or after the rollback marker): edit its stored geometry
        auto sk = jig::Sketch::fromJson(f->params.value("geometry", nlohmann::json::object()));
        if (sk) {
            work_ = sk.value();
        }
        report_ = work_.solve(doc_.lookup());
        if (f->params.contains("plane")) {
            const std::string pl = f->params.value("plane", std::string("XY"));
            double off = 0;
            if (f->params.contains("offset")) {
                if (auto o = doc_.evalLength(f->params["offset"].get<std::string>())) {
                    off = o.value();
                }
            }
            frame_ = jig::originPlaneFrame(pl == "XZ" ? jig::OriginPlane::XZ
                                           : pl == "YZ" ? jig::OriginPlane::YZ
                                                        : jig::OriginPlane::XY,
                                           off);
        }
    }
    std::erase_if(selection_, [&](int id) { return !work_.entity(id); });
    profileTris_.clear();
    if (it != doc_.state().sketches.end()) {
        for (auto& r : it->second->profiles.regions) {
            jig::DisplayMesh m = jig::tessellate(r.face, 0.1);
            std::vector<QVector3D> tris;
            for (uint32_t idx : m.indices) {
                tris.emplace_back(m.positions[idx * 3], m.positions[idx * 3 + 1], m.positions[idx * 3 + 2]);
            }
            profileTris_.push_back(std::move(tris));
        }
    }
    rebuildOverlay();
    emit stateChanged();
    return true;
}

bool SketchEditor::commit(const QString& label, const jig::Sketch& next)
{
    jig::Sketch trial = next;
    jig::SolveReport rep = trial.solve(doc_.lookup());
    if (rep.state == jig::SolveState::Failed
        || (rep.state == jig::SolveState::OverConstrained && !rep.conflicting.empty())) {
        message_ = tr("Not applied: %1").arg(QString::fromStdString(rep.message));
        emit stateChanged();
        view_->update();
        return false;
    }
    auto r = doc_.transact(label.toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        jig::Feature* f = jig::Document::findFeature(m, id_);
        if (!f) {
            return jig::makeError("sketch.gone", "This sketch no longer exists.");
        }
        f->params["geometry"] = trial.toJson();
        return true;
    });
    if (!r) {
        message_ = QString::fromStdString(r.error().message);
        emit stateChanged();
        return false;
    }
    message_.clear();
    refreshFromDocument();
    emit committed();
    return true;
}

// ---------------------------------------------------------------- coordinates

QVector3D SketchEditor::toWorld(double x, double y) const
{
    const gp_XYZ p = frame_.Location().XYZ() + frame_.XDirection().XYZ() * x + frame_.YDirection().XYZ() * y;
    return {float(p.X()), float(p.Y()), float(p.Z())};
}

std::optional<QPointF> SketchEditor::toLocal(const QPointF& screen) const
{
    QVector3D o, d;
    view_->ray(screen, o, d);
    const gp_Dir n = frame_.Direction();
    const QVector3D nn(float(n.X()), float(n.Y()), float(n.Z()));
    const float denom = QVector3D::dotProduct(d, nn);
    if (std::abs(denom) < 1e-6f) {
        return std::nullopt;
    }
    const QVector3D origin = toWorld(0, 0);
    const float t = QVector3D::dotProduct(origin - o, nn) / denom;
    const QVector3D p = o + d * t - origin;
    const gp_Dir xd = frame_.XDirection(), yd = frame_.YDirection();
    return QPointF(QVector3D::dotProduct(p, QVector3D(float(xd.X()), float(xd.Y()), float(xd.Z()))),
                   QVector3D::dotProduct(p, QVector3D(float(yd.X()), float(yd.Y()), float(yd.Z()))));
}

QPoint SketchEditor::screenOf(double x, double y) const
{
    return view_->project(toWorld(x, y)).toPoint();
}

double SketchEditor::gridStep() const
{
    // a "nice" step of about 12 screen pixels: 0.1, 0.2, 0.5, 1, 2, 5, 10 ...
    const double raw = 12 * view_->worldPerPixel();
    const double p = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-6))));
    for (double m : {1.0, 2.0, 5.0, 10.0}) {
        if (raw <= m * p) {
            return m * p;
        }
    }
    return 10 * p;
}

int SketchEditor::pointNear(const QPointF& screen, int exclude) const
{
    int best = 0;
    double bestD = kSnapPx;
    for (auto& [id, e] : shown().entities()) {
        if (e.type != jig::EntityType::Point || id == exclude) {
            continue;
        }
        const double d = QLineF(view_->project(toWorld(e.x, e.y)), screen).length();
        if (d < bestD) {
            bestD = d;
            best = id;
        }
    }
    return best;
}

std::vector<QPointF> SketchEditor::polyline(const jig::Sketch& s, const jig::SketchEntity& e) const
{
    std::vector<QPointF> pts;
    if (e.type == jig::EntityType::Line) {
        auto [x1, y1] = s.pointXY(e.p1);
        auto [x2, y2] = s.pointXY(e.p2);
        pts = {QPointF(x1, y1), QPointF(x2, y2)};
    }
    else if (e.type == jig::EntityType::Circle || e.type == jig::EntityType::Arc) {
        auto [cx, cy] = s.pointXY(e.center);
        double a0 = 0, a1 = 2 * kPi;
        if (e.type == jig::EntityType::Arc) {
            a0 = e.a0;
            a1 = e.a1;
            while (a1 <= a0) {
                a1 += 2 * kPi;
            }
        }
        const int n = std::max(8, int(64 * (a1 - a0) / (2 * kPi)));
        for (int i = 0; i <= n; ++i) {
            const double a = a0 + (a1 - a0) * i / n;
            pts.emplace_back(cx + e.r * std::cos(a), cy + e.r * std::sin(a));
        }
    }
    return pts;
}

int SketchEditor::entityNear(const QPointF& screen) const
{
    if (int p = pointNear(screen)) {
        // prefer visible points (endpoints, standalone points); centres too
        return p;
    }
    int best = 0;
    double bestD = kHitPx;
    for (auto& [id, e] : shown().entities()) {
        if (e.type == jig::EntityType::Point) {
            continue;
        }
        auto pts = polyline(shown(), e);
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const double d = distToSegment(screen, view_->project(toWorld(pts[i].x(), pts[i].y())),
                                           view_->project(toWorld(pts[i + 1].x(), pts[i + 1].y())));
            if (d < bestD) {
                bestD = d;
                best = id;
            }
        }
    }
    return best;
}

SketchEditor::Snap SketchEditor::snapAt(const QPointF& screen, bool allowInference, Qt::KeyboardModifiers mods) const
{
    Snap s;
    auto loc = toLocal(screen);
    if (!loc) {
        return s;
    }
    s.local = *loc;
    if (mods & Qt::ControlModifier) {
        return s;  // Ctrl: no snapping, no automatic constraints
    }
    if (int p = pointNear(screen)) {
        auto [x, y] = shown().pointXY(p);
        s.local = QPointF(x, y);
        s.point = p;
        return s;
    }
    if (options.snapGrid) {
        const double g = gridStep();
        s.local = QPointF(std::round(s.local.x() / g) * g, std::round(s.local.y() / g) * g);
    }
    if (allowInference && first_) {
        const QPointF d = s.local - first_->local;
        const double ang = std::atan2(d.y(), d.x()) * 180 / kPi;
        auto near = [](double a, double t) { return std::abs(std::remainder(a - t, 180.0)) < 4; };
        if (std::hypot(d.x(), d.y()) > 1e-9) {
            if (near(ang, 0)) {
                s.local.setY(first_->local.y());
                s.horizontal = true;
            }
            else if (near(ang, 90)) {
                s.local.setX(first_->local.x());
                s.vertical = true;
            }
        }
    }
    return s;
}

// ---------------------------------------------------------------- tools

void SketchEditor::setTool(Tool t)
{
    resetToolState();
    tool_ = t;
    message_.clear();
    rebuildOverlay();
    emit stateChanged();
}

void SketchEditor::resetToolState()
{
    first_.reset();
    second_.reset();
    chainStart_ = 0;
    dimFirst_ = dimTarget_ = dimSecond_ = 0;
    hideInput();
    rebuildOverlay();
}

QString SketchEditor::toolHint() const
{
    switch (tool_) {
        case Tool::Select:
            return tr("Click to select, drag to move what is still free. Delete removes the selection.");
        case Tool::Line:
            return first_ ? tr("Click the end, or type a length (Tab: angle) and press Enter. Click the first point to close. Esc ends.")
                          : tr("Click where the line starts.");
        case Tool::Rectangle:
            return first_ ? tr("Click the opposite corner, or type width (Tab) height and press Enter.")
                          : tr("Click the first corner.");
        case Tool::CenterRectangle:
            return first_ ? tr("Click a corner, or type width (Tab) height and press Enter.")
                          : tr("Click the centre.");
        case Tool::Circle:
            return first_ ? tr("Click a point on the circle, or type the diameter and press Enter.")
                          : tr("Click the centre.");
        case Tool::Arc:
            return second_ ? tr("Click where the arc ends (it runs counter-clockwise), or type the angle it spans.")
                 : first_  ? tr("Click where the arc starts, or type the radius and press Enter.")
                           : tr("Click the centre of the arc.");
        case Tool::Slot:
            return second_ ? tr("Click to set the width, or type it and press Enter.")
                 : first_  ? tr("Click the other end's centre, or type the centre-to-centre length.")
                           : tr("Click the centre of one end.");
        case Tool::Polygon:
            return first_ ? tr("Click a corner, or type the diameter (Tab: number of sides) and press Enter.")
                          : tr("Click the centre.");
        case Tool::Point:
            return tr("Click to place a point.");
        case Tool::Dimension:
            return dimTarget_ ? tr("Type the value (a number, expression or parameter) and press Enter.")
                 : dimFirst_  ? tr("Click a second point or a line to measure the distance to.")
                              : tr("Click a line (length), circle (diameter), arc (radius) or a point.");
    }
    return {};
}

void SketchEditor::showInput(const QStringList& labels, const QStringList& prefill)
{
    for (size_t i = 0; i < inputFields_.size(); ++i) {
        const bool on = int(i) < labels.size();
        inputLabels_[i]->setVisible(on);
        inputFields_[i]->setVisible(on);
        if (on) {
            inputLabels_[i]->setText(labels[int(i)]);
            inputFields_[i]->setText(int(i) < prefill.size() ? prefill[int(i)] : QString());
            inputFields_[i]->setPlaceholderText(tr("type…"));
        }
    }
    inputBox_->adjustSize();
    inputBox_->show();
    inputBox_->raise();
}

void SketchEditor::hideInput()
{
    if (inputBox_) {
        inputBox_->hide();
        for (auto* f : inputFields_) {
            f->clear();
        }
    }
    view_->setFocus();
}

QStringList SketchEditor::inputValues() const
{
    QStringList v;
    for (auto* f : inputFields_) {
        v << (f->isVisible() ? f->text().trimmed() : QString());
    }
    return v;
}

void SketchEditor::typeValues(const QStringList& values)
{
    for (int i = 0; i < values.size() && i < int(inputFields_.size()); ++i) {
        inputFields_[size_t(i)]->setText(values[i]);
    }
    placeSecond(cursor_, inputValues());
}

void SketchEditor::placeSecond(const Snap& cur, const QStringList& typed)
{
    auto typedLen = [&](int i) -> std::optional<double> {
        if (i >= typed.size() || typed[i].isEmpty()) {
            return std::nullopt;
        }
        auto r = doc_.evalLength(typed[i].toStdString());
        if (!r) {
            message_ = QString::fromStdString(r.error().message);
            emit stateChanged();
            return std::nullopt;
        }
        return r.value();
    };
    auto badTyped = [&](int i) { return i < typed.size() && !typed[i].isEmpty() && !typedLen(i); };

    if (tool_ == Tool::Dimension && dimTarget_) {
        if (typed.isEmpty() || typed[0].isEmpty() || badTyped(0)) {
            return;
        }
        jig::Sketch s = work_;
        const auto* e = s.entity(dimTarget_);
        if (!e) {
            return;
        }
        const std::string expr = typed[0].toStdString();
        if (dimSecond_) {
            s.addConstraint(jig::ConstraintType::Distance, dimTarget_, dimSecond_, 0, expr);
        }
        else if (e->type == jig::EntityType::Line) {
            s.addConstraint(jig::ConstraintType::Distance, dimTarget_, 0, 0, expr);
        }
        else if (e->type == jig::EntityType::Circle) {
            s.addConstraint(jig::ConstraintType::Diameter, dimTarget_, 0, 0, expr);
        }
        else if (e->type == jig::EntityType::Arc) {
            s.addConstraint(jig::ConstraintType::Radius, dimTarget_, 0, 0, expr);
        }
        commit(tr("Dimension"), s);
        dimFirst_ = dimTarget_ = dimSecond_ = 0;
        hideInput();
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (!first_) {
        return;
    }
    // the arc's last step takes an angle and the polygon's second field a count;
    // those are checked where they are used
    const bool notLengths = tool_ == Tool::Arc && second_;
    for (int i = 0; i < typed.size() && !notLengths; ++i) {
        if (tool_ == Tool::Polygon && i == 1) {
            continue;
        }
        if (badTyped(i)) {
            return;  // message already set; keep the box open so the user can fix it
        }
    }
    jig::Sketch s = work_;
    const QPointF a = first_->local;
    QPointF b = cur.local;
    const QPointF dir = b - a;

    switch (tool_) {
        case Tool::Line: {
            auto len = typedLen(0);
            double ang = std::atan2(dir.y(), dir.x());
            bool typedAngle = false;
            if (typed.size() > 1 && !typed[1].isEmpty()) {
                if (auto av = doc_.evalAngle(typed[1].toStdString())) {
                    ang = av.value();
                    typedAngle = true;
                }
            }
            if (len || typedAngle) {
                const double L = len ? *len : std::hypot(dir.x(), dir.y());
                b = a + QPointF(L * std::cos(ang), L * std::sin(ang));
            }
            const bool useSnap = !len && !typedAngle && cur.point;
            if (QLineF(a, b).length() < 1e-9 && !useSnap) {
                return;
            }
            const int p1 = first_->point ? first_->point : s.addPoint(a.x(), a.y(), true);
            const int p2 = useSnap ? cur.point : s.addPoint(b.x(), b.y(), true);
            if (p1 == p2) {
                return;
            }
            const int l = s.addLine(p1, p2);
            if (!typedAngle && cur.horizontal) {
                s.addConstraint(jig::ConstraintType::Horizontal, l);
            }
            else if (!typedAngle && cur.vertical) {
                s.addConstraint(jig::ConstraintType::Vertical, l);
            }
            if (len) {
                s.addConstraint(jig::ConstraintType::Distance, l, 0, 0, typed[0].toStdString());
            }
            if (commit(tr("Line"), s)) {
                const bool closed = chainStart_ && p2 == chainStart_;
                if (!chainStart_) {
                    chainStart_ = p1;
                }
                if (closed) {
                    resetToolState();
                }
                else {
                    auto [x, y] = work_.pointXY(p2);
                    first_ = Snap {QPointF(x, y), p2};
                    showInput({tr("Length"), tr("Angle")});
                }
            }
            break;
        }
        case Tool::Rectangle:
        case Tool::CenterRectangle: {
            const bool centred = tool_ == Tool::CenterRectangle;
            auto w = typedLen(0), h = typedLen(1);
            double sx = dir.x() >= 0 ? 1 : -1, sy = dir.y() >= 0 ? 1 : -1;
            double W = w ? *w : std::abs(dir.x()) * (centred ? 2 : 1);
            double H = h ? *h : std::abs(dir.y()) * (centred ? 2 : 1);
            if (W < 1e-9 || H < 1e-9) {
                return;
            }
            const double x0 = centred ? a.x() - W / 2 : (sx > 0 ? a.x() : a.x() - W);
            const double y0 = centred ? a.y() - H / 2 : (sy > 0 ? a.y() : a.y() - H);
            const int A = s.addPoint(x0, y0, true), B = s.addPoint(x0 + W, y0, true),
                      C = s.addPoint(x0 + W, y0 + H, true), D = s.addPoint(x0, y0 + H, true);
            const int ab = s.addLine(A, B), bc = s.addLine(B, C), cd = s.addLine(C, D), da = s.addLine(D, A);
            s.addConstraint(jig::ConstraintType::Horizontal, ab);
            s.addConstraint(jig::ConstraintType::Horizontal, cd);
            s.addConstraint(jig::ConstraintType::Vertical, bc);
            s.addConstraint(jig::ConstraintType::Vertical, da);
            if (w) {
                s.addConstraint(jig::ConstraintType::Distance, ab, 0, 0, typed[0].toStdString());
            }
            if (h) {
                s.addConstraint(jig::ConstraintType::Distance, bc, 0, 0, typed[1].toStdString());
            }
            if (centred) {
                const int diag = s.addLine(A, C);
                s.setConstruction(diag, true);
                const int M = first_->point ? first_->point : s.addPoint(a.x(), a.y());
                s.addConstraint(jig::ConstraintType::Midpoint, M, diag);
            }
            else if (first_->point) {
                // first corner was snapped onto an existing point: keep them together
                s.addConstraint(jig::ConstraintType::Coincident, A, first_->point);
            }
            if (commit(centred ? tr("Center rectangle") : tr("Rectangle"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Circle: {
            auto d = typedLen(0);
            const double r = d ? *d / 2 : std::hypot(dir.x(), dir.y());
            if (r < 1e-9) {
                return;
            }
            const int c = s.addCircle(a.x(), a.y(), r);
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, s.entity(c)->center, first_->point);
            }
            if (d) {
                s.addConstraint(jig::ConstraintType::Diameter, c, 0, 0, typed[0].toStdString());
            }
            if (commit(tr("Circle"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Arc: {
            if (!second_) {
                auto r = typedLen(0);
                const double L = std::hypot(dir.x(), dir.y());
                const double R = r ? *r : L;
                if (R < 1e-9) {
                    return;
                }
                const QPointF u = L > 1e-9 ? dir / L : QPointF(1, 0);
                second_ = Snap {a + u * R, r ? 0 : cur.point};
                if (r) {
                    second_->point = 0;
                }
                arcRadiusExpr_ = r ? typed[0] : QString();
                showInput({tr("Angle")});
                break;
            }
            const QPointF st = second_->local;
            const double R = QLineF(a, st).length();
            const double a0 = std::atan2(st.y() - a.y(), st.x() - a.x());
            double sweep = 0;
            bool typedSweep = false;
            if (!typed.isEmpty() && !typed[0].isEmpty()) {
                auto av = doc_.evalAngle(typed[0].toStdString());
                if (!av) {
                    message_ = QString::fromStdString(av.error().message);
                    return;
                }
                sweep = av.value();
                typedSweep = true;
            }
            else {
                sweep = std::atan2(b.y() - a.y(), b.x() - a.x()) - a0;
                while (sweep <= 1e-9) {
                    sweep += 2 * kPi;
                }
            }
            if (sweep <= 1e-6 || sweep >= 2 * kPi - 1e-6) {
                message_ = tr("An arc must span more than 0° and less than 360°. Use Circle for a full circle.");
                return;
            }
            const int arc = s.addArc(a.x(), a.y(), R, a0, a0 + sweep);
            const jig::SketchEntity A = *s.entity(arc);
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, A.center, first_->point);
            }
            if (second_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, A.p1, second_->point);
            }
            if (!typedSweep && cur.point && cur.point != second_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, A.p2, cur.point);
            }
            if (!arcRadiusExpr_.isEmpty()) {
                s.addConstraint(jig::ConstraintType::Radius, arc, 0, 0, arcRadiusExpr_.toStdString());
            }
            if (commit(tr("Arc"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Slot: {
            if (!second_) {
                auto len = typedLen(0);
                const double L = std::hypot(dir.x(), dir.y());
                const double D = len ? *len : L;
                if (D < 1e-9) {
                    return;
                }
                const QPointF u = L > 1e-9 ? dir / L : QPointF(1, 0);
                second_ = Snap {a + u * D, len ? 0 : cur.point};
                arcRadiusExpr_ = len ? typed[0] : QString();  // centre distance, reused below
                showInput({tr("Width")});
                break;
            }
            const QPointF c2 = second_->local;
            auto w = typedLen(0);
            double W = 0;
            if (w) {
                W = *w;
            }
            else {
                // twice the distance from the cursor to the centre line
                const QLineF axis(a, c2);
                const QPointF n(-(c2.y() - a.y()) / axis.length(), (c2.x() - a.x()) / axis.length());
                W = 2 * std::abs(QPointF::dotProduct(b - a, n));
            }
            if (W < 1e-9) {
                return;
            }
            const auto ids = s.addSlot(a.x(), a.y(), c2.x(), c2.y(), W / 2);
            const jig::SketchEntity A1 = *s.entity(ids.arc1), A2 = *s.entity(ids.arc2);
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, A2.center, first_->point);
            }
            if (second_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, A1.center, second_->point);
            }
            if (!arcRadiusExpr_.isEmpty()) {
                s.addConstraint(jig::ConstraintType::Distance, A2.center, A1.center, 0, arcRadiusExpr_.toStdString());
            }
            if (w) {
                s.addConstraint(jig::ConstraintType::Radius, ids.arc1, 0, 0, "(" + typed[0].toStdString() + ") / 2");
            }
            if (commit(tr("Slot"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Polygon: {
            auto d = typedLen(0);
            int n = 6;
            if (typed.size() > 1 && !typed[1].isEmpty()) {
                auto nv = jig::evaluate(typed[1].toStdString(), doc_.lookup());
                if (!nv || !(nv->dim == jig::Dim::none()) || nv->value < 3 || nv->value > 64) {
                    message_ = tr("A polygon needs 3 to 64 sides.");
                    return;
                }
                n = int(std::lround(nv->value));
            }
            const double L = std::hypot(dir.x(), dir.y());
            const double R = d ? *d / 2 : L;
            if (R < 1e-9) {
                return;
            }
            const double a0 = L > 1e-9 ? std::atan2(dir.y(), dir.x()) : kPi / 2;
            const auto ids = s.addPolygon(a.x(), a.y(), R, n, a0);
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, s.entity(ids.circle)->center, first_->point);
            }
            if (d) {
                s.addConstraint(jig::ConstraintType::Diameter, ids.circle, 0, 0, typed[0].toStdString());
            }
            if (commit(tr("Polygon"), s)) {
                resetToolState();
            }
            break;
        }
        default:
            break;
    }
    rebuildOverlay();
    emit stateChanged();
}

// ---------------------------------------------------------------- mouse and keys

bool SketchEditor::mousePress(QMouseEvent* e)
{
    pressed_ = true;
    pressScreen_ = e->position();
    dragging_ = false;
    dragPoint_ = dragPoint2_ = 0;
    if (tool_ != Tool::Select) {
        return true;
    }
    const int hit = entityNear(pressScreen_);
    if (const auto* en = work_.entity(hit)) {
        if (en->type == jig::EntityType::Point) {
            dragPoint_ = hit;
        }
        else if (en->type == jig::EntityType::Line) {
            dragPoint_ = en->p1;
            dragPoint2_ = en->p2;
            auto [x1, y1] = work_.pointXY(en->p1);
            auto [x2, y2] = work_.pointXY(en->p2);
            dragOffset2_ = QPointF(x2 - x1, y2 - y1);
        }
        else {
            dragPoint_ = en->center;
        }
        if (auto loc = toLocal(pressScreen_)) {
            auto [px, py] = work_.pointXY(dragPoint_);
            dragStartLocal_ = *loc - QPointF(px, py);
        }
    }
    return true;
}

bool SketchEditor::mouseMove(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (pressed_ && tool_ == Tool::Select && dragPoint_) {
        if (!dragging_ && QLineF(pos, pressScreen_).length() < 3) {
            return true;
        }
        dragging_ = true;
        if (auto loc = toLocal(pos)) {
            const QPointF target = *loc - dragStartLocal_;
            dragSketch_ = work_;
            report_ = dragSketch_.drag(dragPoint_, target.x(), target.y(), doc_.lookup());
            if (dragPoint2_) {
                const QPointF t2 = target + dragOffset2_;
                report_ = dragSketch_.drag(dragPoint2_, t2.x(), t2.y(), doc_.lookup());
            }
            rebuildOverlay();
            emit stateChanged();
        }
        return true;
    }
    cursor_ = snapAt(pos, tool_ == Tool::Line || tool_ == Tool::Rectangle, e->modifiers());
    cursorValid_ = toLocal(pos).has_value();
    const int h = (tool_ == Tool::Select || tool_ == Tool::Dimension) ? entityNear(pos) : 0;
    if (h != hover_) {
        hover_ = h;
    }
    if (inputBox_->isVisible()) {
        inputBox_->move(QPoint(int(pos.x()) + 18, int(pos.y()) + 18));
    }
    rebuildOverlay();
    return true;
}

bool SketchEditor::mouseRelease(QMouseEvent* e)
{
    const bool wasPressed = pressed_;
    pressed_ = false;
    if (!wasPressed) {
        return true;
    }
    const QPointF pos = e->position();
    if (dragging_) {
        dragging_ = false;
        if (!commit(tr("Drag"), dragSketch_)) {
            refreshFromDocument();
        }
        return true;
    }
    cursor_ = snapAt(pos, tool_ == Tool::Line || tool_ == Tool::Rectangle, e->modifiers());
    switch (tool_) {
        case Tool::Select: {
            const int hit = entityNear(pos);
            const bool add = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
            if (!add) {
                selection_.clear();
            }
            if (hit) {
                if (selection_.count(hit) && add) {
                    selection_.erase(hit);
                }
                else {
                    selection_.insert(hit);
                }
            }
            break;
        }
        case Tool::Point: {
            if (!cursor_.point) {
                jig::Sketch s = work_;
                s.addPoint(cursor_.local.x(), cursor_.local.y());
                commit(tr("Point"), s);
            }
            break;
        }
        case Tool::Dimension: {
            const int hit = entityNear(pos);
            const auto* en = work_.entity(hit);
            if (!en) {
                break;
            }
            if (dimFirst_) {
                const auto* f = work_.entity(dimFirst_);
                if (en->type == jig::EntityType::Point || en->type == jig::EntityType::Line) {
                    dimTarget_ = dimFirst_;
                    dimSecond_ = hit;
                    auto [x1, y1] = work_.pointXY(f->id);
                    double v = 0;
                    if (en->type == jig::EntityType::Point) {
                        auto [x2, y2] = work_.pointXY(hit);
                        v = std::hypot(x2 - x1, y2 - y1);
                    }
                    else {
                        auto [ax, ay] = work_.pointXY(en->p1);
                        auto [bx, by] = work_.pointXY(en->p2);
                        v = std::abs((bx - ax) * (ay - y1) - (ax - x1) * (by - ay)) / std::hypot(bx - ax, by - ay);
                    }
                    showInput({tr("Distance")}, {fmt(v)});
                    inputFields_[0]->selectAll();
                    inputFields_[0]->setFocus();
                }
                break;
            }
            if (en->type == jig::EntityType::Point) {
                dimFirst_ = hit;
                break;
            }
            dimTarget_ = hit;
            double v = 0;
            QString label;
            if (en->type == jig::EntityType::Line) {
                auto [x1, y1] = work_.pointXY(en->p1);
                auto [x2, y2] = work_.pointXY(en->p2);
                v = std::hypot(x2 - x1, y2 - y1);
                label = tr("Length");
            }
            else if (en->type == jig::EntityType::Circle) {
                v = 2 * en->r;
                label = tr("Diameter");
            }
            else {
                v = en->r;
                label = tr("Radius");
            }
            showInput({label}, {fmt(v)});
            inputFields_[0]->selectAll();
            inputFields_[0]->setFocus();
            inputBox_->move(pos.toPoint() + QPoint(18, 18));
            break;
        }
        default: {
            if (!cursorValid_) {
                break;
            }
            if (!first_) {
                first_ = cursor_;
                const bool two = tool_ != Tool::Circle;
                (void)two;
                if (tool_ == Tool::Polygon) {
                    showInput({tr("Diameter"), tr("Sides")}, {QString(), "6"});
                }
                else {
                    showInput(tool_ == Tool::Line     ? QStringList {tr("Length"), tr("Angle")}
                              : tool_ == Tool::Circle ? QStringList {tr("Diameter")}
                              : tool_ == Tool::Arc    ? QStringList {tr("Radius")}
                              : tool_ == Tool::Slot   ? QStringList {tr("Length")}
                                                      : QStringList {tr("Width"), tr("Height")});
                }
                inputBox_->move(pos.toPoint() + QPoint(18, 18));
            }
            else {
                placeSecond(cursor_, inputValues());
            }
            break;
        }
    }
    rebuildOverlay();
    emit stateChanged();
    return true;
}

bool SketchEditor::mouseDoubleClick(QMouseEvent*)
{
    if (tool_ == Tool::Line) {
        resetToolState();  // double-click ends a chain of lines
        emit stateChanged();
    }
    return true;
}

bool SketchEditor::key(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (first_ || dimFirst_ || dimTarget_) {
            resetToolState();
        }
        else if (tool_ != Tool::Select) {
            setTool(Tool::Select);
        }
        else {
            selection_.clear();
        }
        rebuildOverlay();
        emit stateChanged();
        return true;
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelection();
        return true;
    }
    // Typing a number while a tool waits for its second click goes into the value box.
    if (first_ && !e->text().isEmpty() && (e->text()[0].isDigit() || e->text()[0] == '.' || e->text()[0] == '-')) {
        inputFields_[0]->setFocus();
        inputFields_[0]->setText(e->text());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- selection commands

bool SketchEditor::applyConstraint(jig::ConstraintType t)
{
    std::vector<int> pts, lines, rounds;
    for (int id : selection_) {
        const auto* e = work_.entity(id);
        if (!e) {
            continue;
        }
        if (e->type == jig::EntityType::Point) {
            pts.push_back(id);
        }
        else if (e->type == jig::EntityType::Line) {
            lines.push_back(id);
        }
        else {
            rounds.push_back(id);
        }
    }
    jig::Sketch s = work_;
    QString need;
    using CT = jig::ConstraintType;
    switch (t) {
        case CT::Horizontal:
        case CT::Vertical:
            if (!lines.empty()) {
                for (int l : lines) {
                    auto [x1, y1] = s.pointXY(s.entity(l)->p1);
                    auto [x2, y2] = s.pointXY(s.entity(l)->p2);
                    s.addConstraint(std::abs(x2 - x1) >= std::abs(y2 - y1) ? CT::Horizontal : CT::Vertical, l);
                }
            }
            else if (pts.size() == 2) {
                auto [x1, y1] = s.pointXY(pts[0]);
                auto [x2, y2] = s.pointXY(pts[1]);
                s.addConstraint(std::abs(x2 - x1) >= std::abs(y2 - y1) ? CT::Horizontal : CT::Vertical, pts[0], pts[1]);
            }
            else {
                need = tr("Select one or more lines, or two points.");
            }
            break;
        case CT::Parallel:
        case CT::Perpendicular:
            if (lines.size() == 2) {
                s.addConstraint(t, lines[0], lines[1]);
            }
            else {
                need = tr("Select two lines.");
            }
            break;
        case CT::Equal:
            if (lines.size() == 2) {
                s.addConstraint(t, lines[0], lines[1]);
            }
            else if (rounds.size() == 2) {
                s.addConstraint(t, rounds[0], rounds[1]);
            }
            else {
                need = tr("Select two lines, or two circles/arcs.");
            }
            break;
        case CT::Tangent:
            if (lines.size() == 1 && rounds.size() == 1) {
                s.addConstraint(t, lines[0], rounds[0]);
            }
            else if (rounds.size() == 2) {
                s.addConstraint(t, rounds[0], rounds[1]);
            }
            else {
                need = tr("Select a line and a circle, or two circles.");
            }
            break;
        case CT::Coincident:
            if (pts.size() == 2) {
                s.addConstraint(CT::Coincident, pts[0], pts[1]);
            }
            else if (pts.size() == 1 && lines.size() + rounds.size() == 1) {
                s.addConstraint(CT::PointOnCurve, pts[0], lines.empty() ? rounds[0] : lines[0]);
            }
            else {
                need = tr("Select two points, or a point and a curve.");
            }
            break;
        case CT::Midpoint:
            if (pts.size() == 1 && lines.size() == 1) {
                s.addConstraint(t, pts[0], lines[0]);
            }
            else {
                need = tr("Select a point and a line.");
            }
            break;
        case CT::Fix:
            if (!pts.empty()) {
                for (int p : pts) {
                    s.addConstraint(t, p);
                }
            }
            else {
                need = tr("Select the points to lock.");
            }
            break;
        case CT::Symmetric:
            if (pts.size() == 2 && lines.size() == 1) {
                s.addConstraint(t, pts[0], pts[1], lines[0]);
            }
            else {
                need = tr("Select two points and the line of symmetry.");
            }
            break;
        default:
            need = tr("That constraint is not available yet.");
    }
    if (!need.isEmpty()) {
        message_ = need;
        emit stateChanged();
        return false;
    }
    const bool ok = commit(tr("Constraint"), s);
    if (ok) {
        selection_.clear();
        rebuildOverlay();
        emit stateChanged();
    }
    return ok;
}

void SketchEditor::toggleConstruction()
{
    jig::Sketch s = work_;
    bool any = false;
    for (int id : selection_) {
        auto* e = s.entity(id);
        if (e && e->type != jig::EntityType::Point) {
            s.setConstruction(id, !e->construction);
            any = true;
        }
    }
    if (!any) {
        message_ = tr("Select lines or circles to turn into (or back from) construction geometry.");
        emit stateChanged();
        return;
    }
    commit(tr("Construction"), s);
}

void SketchEditor::deleteSelection()
{
    if (selection_.empty()) {
        return;
    }
    jig::Sketch s = work_;
    for (int id : selection_) {
        const auto* e = s.entity(id);
        if (!e) {
            continue;
        }
        if (e->type == jig::EntityType::Point) {
            // deleting a point removes the curves that use it
            std::vector<int> users;
            for (auto& [cid, c] : s.entities()) {
                if (c.p1 == id || c.p2 == id || c.center == id) {
                    users.push_back(cid);
                }
            }
            for (int u : users) {
                s.remove(u);
            }
        }
        s.remove(id);
    }
    selection_.clear();
    commit(tr("Delete"), s);
}

void SketchEditor::lookAt()
{
    const gp_Dir n = frame_.Direction(), y = frame_.YDirection();
    // centre on the geometry when there is some, else on the sketch origin
    double sx = 0, sy = 0;
    int n2 = 0;
    for (auto& [id, e] : work_.entities()) {
        if (e.type == jig::EntityType::Point) {
            sx += e.x;
            sy += e.y;
            ++n2;
        }
    }
    const QVector3D c = n2 ? toWorld(sx / n2, sy / n2) : toWorld(0, 0);
    view_->lookAlong(QVector3D(float(n.X()), float(n.Y()), float(n.Z())), QVector3D(float(y.X()), float(y.Y()), float(y.Z())), c);
}

void SketchEditor::setOptions(const Options& o)
{
    options = o;
    rebuildOverlay();
}

// ---------------------------------------------------------------- drawing

void SketchEditor::rebuildOverlay()
{
    SceneOverlay ov;
    const jig::Sketch& s = shown();
    const std::set<int> freeSet(report_.freeEntities.begin(), report_.freeEntities.end());
    const ViewColors& vc = view_->colors();
    auto addPoly = [&](const std::vector<QPointF>& pts, const QColor& c, bool dashed) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const QPointF a = pts[i], b = pts[i + 1];
            if (!dashed) {
                ov.lines.push_back({toWorld(a.x(), a.y()), c});
                ov.lines.push_back({toWorld(b.x(), b.y()), c});
                continue;
            }
            const double len = QLineF(a, b).length();
            const double dash = std::max(0.5, 7.0 * double(view_->worldPerPixel()));
            for (double t = 0; t < len; t += 2 * dash) {
                const QPointF p = a + (b - a) * (t / len), q = a + (b - a) * (std::min(len, t + dash) / len);
                ov.lines.push_back({toWorld(p.x(), p.y()), c});
                ov.lines.push_back({toWorld(q.x(), q.y()), c});
            }
        }
    };
    for (auto& [id, e] : s.entities()) {
        if (e.type == jig::EntityType::Point) {
            continue;
        }
        QColor c = e.construction ? kConstruction : freeSet.count(id) ? kFree : vc.text;
        if (selection_.count(id)) {
            c = vc.selected;
        }
        else if (hover_ == id) {
            c = vc.hover;
        }
        addPoly(polyline(s, e), c, e.construction);
    }
    if (options.showProfiles && !dragging_) {
        QColor pc = kAccent;
        pc.setAlpha(46);
        for (auto& tris : profileTris_) {
            for (auto& p : tris) {
                ov.tris.push_back({p, pc});
            }
        }
    }
    // rubber band for the tool in progress
    if (first_ && cursorValid_) {
        const QPointF a = first_->local, b = cursor_.local;
        if (tool_ == Tool::Line) {
            addPoly({a, b}, kAccent, false);
        }
        else if (tool_ == Tool::Rectangle) {
            addPoly({a, QPointF(b.x(), a.y()), b, QPointF(a.x(), b.y()), a}, kAccent, false);
        }
        else if (tool_ == Tool::CenterRectangle) {
            const QPointF d = b - a;
            addPoly({a - d, QPointF(a.x() + d.x(), a.y() - d.y()), a + d, QPointF(a.x() - d.x(), a.y() + d.y()), a - d},
                    kAccent, false);
        }
        else if (tool_ == Tool::Circle) {
            jig::Sketch tmp;
            const int cid = tmp.addCircle(a.x(), a.y(), std::max(1e-6, QLineF(a, b).length()));
            addPoly(polyline(tmp, *tmp.entity(cid)), kAccent, false);
        }
        else if (tool_ == Tool::Arc || tool_ == Tool::Slot || tool_ == Tool::Polygon) {
            jig::Sketch tmp;
            if (!second_ && tool_ != Tool::Polygon) {
                addPoly({a, b}, kAccent, tool_ == Tool::Arc);  // radius / centre line
            }
            else if (tool_ == Tool::Arc) {
                const QPointF st = second_->local;
                const double R = QLineF(a, st).length();
                const double a0 = std::atan2(st.y() - a.y(), st.x() - a.x());
                double a1 = std::atan2(b.y() - a.y(), b.x() - a.x());
                while (a1 <= a0 + 1e-9) {
                    a1 += 2 * kPi;
                }
                if (R > 1e-9) {
                    addPoly(polyline(tmp, *tmp.entity(tmp.addArc(a.x(), a.y(), R, a0, a1))), kAccent, false);
                }
            }
            else if (tool_ == Tool::Slot) {
                const QPointF c2 = second_->local;
                const QLineF axis(a, c2);
                if (axis.length() > 1e-9) {
                    const QPointF n(-(c2.y() - a.y()) / axis.length(), (c2.x() - a.x()) / axis.length());
                    const double r = std::max(1e-3, std::abs(QPointF::dotProduct(b - a, n)));
                    tmp.addSlot(a.x(), a.y(), c2.x(), c2.y(), r);
                }
            }
            else {
                const double R = QLineF(a, b).length();
                if (R > 1e-9) {
                    tmp.addPolygon(a.x(), a.y(), R, 6, std::atan2(b.y() - a.y(), b.x() - a.x()));
                }
            }
            for (auto& [id, e] : tmp.entities()) {
                if (e.type != jig::EntityType::Point && !e.construction) {
                    addPoly(polyline(tmp, e), kAccent, false);
                }
            }
        }
    }
    view_->setOverlay(std::move(ov));
}

void SketchEditor::paint(QPainter& p)
{
    const jig::Sketch& s = shown();
    const std::set<int> freeSet(report_.freeEntities.begin(), report_.freeEntities.end());
    const ViewColors& vc = view_->colors();
    auto scr = [&](double x, double y) { return view_->project(toWorld(x, y)); };

    // Points: a hollow circle means "can still move", a filled square means "fixed".
    // (Shape, not only colour, carries the state: brief principle 4.)
    std::set<int> owner;
    for (auto& [id, e] : s.entities()) {
        if (e.type != jig::EntityType::Point && freeSet.count(id)) {
            owner.insert(e.p1);
            owner.insert(e.p2);
        }
    }
    for (auto& [id, e] : s.entities()) {
        if (e.type != jig::EntityType::Point) {
            continue;
        }
        const QPointF c = scr(e.x, e.y);
        const bool free = freeSet.count(id) || owner.count(id);
        const bool sel = selection_.count(id) > 0, hov = hover_ == id;
        QColor col = sel ? vc.selected : hov ? vc.hover : free ? kFree : vc.text;
        p.setPen(QPen(col, 1.6));
        if (free) {
            p.setBrush(vc.background);
            p.drawEllipse(c, 3.6, 3.6);
        }
        else {
            p.setBrush(col);
            p.drawRect(QRectF(c.x() - 3, c.y() - 3, 6, 6));
        }
    }

    // Constraint glyphs and dimension values.
    if (options.showConstraints) {
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        auto mid = [&](int entity) -> std::optional<QPointF> {
            const auto* e = s.entity(entity);
            if (!e) {
                return std::nullopt;
            }
            if (e->type == jig::EntityType::Point) {
                return scr(e->x, e->y);
            }
            if (e->type == jig::EntityType::Line) {
                auto [x1, y1] = s.pointXY(e->p1);
                auto [x2, y2] = s.pointXY(e->p2);
                return scr((x1 + x2) / 2, (y1 + y2) / 2);
            }
            auto [cx, cy] = s.pointXY(e->center);
            return scr(cx + e->r * 0.7071, cy + e->r * 0.7071);
        };
        std::map<int, int> stack;  // per entity: offset glyphs so they don't overlap
        for (auto& [cid, k] : s.constraints()) {
            using CT = jig::ConstraintType;
            QString g;
            int at = k.a;
            switch (k.type) {
                case CT::Horizontal: g = "H"; break;
                case CT::Vertical: g = "V"; break;
                case CT::Parallel: g = "∥"; break;
                case CT::Perpendicular: g = "⊥"; break;
                case CT::Equal: g = "="; break;
                case CT::Tangent: g = "T"; break;
                case CT::Midpoint: g = "◇"; break;
                case CT::Fix: g = "🔒"; break;
                case CT::Symmetric: g = "⇋"; break;
                case CT::PointOnCurve: g = "∘"; break;
                case CT::Distance: {
                    auto q = doc_.evalLength(k.expr);
                    g = q ? fmt(q.value()) : "?";
                    if (k.b) {
                        at = k.b;
                    }
                    break;
                }
                case CT::Diameter: { auto q = doc_.evalLength(k.expr); g = "Ø" + (q ? fmt(q.value()) : "?"); break; }
                case CT::Radius: { auto q = doc_.evalLength(k.expr); g = "R" + (q ? fmt(q.value()) : "?"); break; }
                case CT::Angle: { auto q = doc_.evalAngle(k.expr); g = q ? fmt(q.value() * 180 / kPi) + "°" : "?"; break; }
                default: break;  // coincident, x/y placement: not labelled (too noisy)
            }
            if (g.isEmpty()) {
                continue;
            }
            auto m = mid(at);
            if (!m) {
                continue;
            }
            const int n = stack[at]++;
            const QPointF pos = *m + QPointF(8 + 16 * n, -10);
            const bool dim = k.type == CT::Distance || k.type == CT::Diameter || k.type == CT::Radius || k.type == CT::Angle;
            const QRectF box(pos.x() - 2, pos.y() - 11, p.fontMetrics().horizontalAdvance(g) + 6, 15);
            p.setPen(Qt::NoPen);
            QColor bg = vc.background;
            bg.setAlpha(220);
            p.setBrush(bg);
            p.drawRoundedRect(box, 3, 3);
            p.setPen(dim ? vc.text : kFree.darker(130));
            p.drawText(box, Qt::AlignCenter, g);
            if (dim && QString::fromStdString(k.expr).contains(QRegularExpression("[A-Za-z_]"))) {
                p.setPen(kFree);
                p.drawText(pos + QPointF(0, 14), QString::fromStdString(k.expr));  // show "base_w" etc.
            }
        }
    }

    // Gaps: open line ends that stop the outline from closing.
    if (options.showGaps && !dragging_) {
        for (auto& g : s.openEndpoints()) {
            const QPointF c = scr(g.x, g.y);
            p.setPen(QPen(kGap, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 8, 8);
            if (g.nearestPointId && g.gap > 0) {
                p.drawText(c + QPointF(10, -8), tr("gap %1 mm").arg(fmt(g.gap)));
            }
        }
    }

    // Cursor feedback: snap marker, inferred constraint, live measurement.
    if (cursorValid_ && tool_ != Tool::Select) {
        const QPointF c = scr(cursor_.local.x(), cursor_.local.y());
        p.setPen(QPen(kAccent, 1.5));
        p.setBrush(Qt::NoBrush);
        if (cursor_.point) {
            p.drawRect(QRectF(c.x() - 6, c.y() - 6, 12, 12));
        }
        QString label;
        if (cursor_.horizontal) {
            label = "H ";
        }
        if (cursor_.vertical) {
            label = "V ";
        }
        if (first_) {
            const QPointF d = cursor_.local - first_->local;
            if (tool_ == Tool::Line) {
                label += tr("%1 mm  %2°").arg(fmt(std::hypot(d.x(), d.y()))).arg(fmt(std::atan2(d.y(), d.x()) * 180 / kPi));
            }
            else if (tool_ == Tool::Rectangle) {
                label += tr("%1 × %2 mm").arg(fmt(std::abs(d.x()))).arg(fmt(std::abs(d.y())));
            }
            else if (tool_ == Tool::CenterRectangle) {
                label += tr("%1 × %2 mm").arg(fmt(2 * std::abs(d.x()))).arg(fmt(2 * std::abs(d.y())));
            }
            else if (tool_ == Tool::Circle || tool_ == Tool::Polygon) {
                label += tr("Ø %1 mm").arg(fmt(2 * std::hypot(d.x(), d.y())));
            }
            else if (tool_ == Tool::Arc) {
                if (!second_) {
                    label += tr("R %1 mm").arg(fmt(std::hypot(d.x(), d.y())));
                }
                else {
                    const QPointF st = second_->local - first_->local;
                    double sw = std::atan2(d.y(), d.x()) - std::atan2(st.y(), st.x());
                    while (sw <= 0) {
                        sw += 2 * kPi;
                    }
                    label += tr("%1°").arg(fmt(sw * 180 / kPi));
                }
            }
            else if (tool_ == Tool::Slot) {
                if (!second_) {
                    label += tr("%1 mm").arg(fmt(std::hypot(d.x(), d.y())));
                }
                else {
                    const QPointF c2 = second_->local;
                    const QLineF axis(first_->local, c2);
                    if (axis.length() > 1e-9) {
                        const QPointF n(-(c2.y() - first_->local.y()) / axis.length(),
                                        (c2.x() - first_->local.x()) / axis.length());
                        label += tr("width %1 mm").arg(fmt(2 * std::abs(QPointF::dotProduct(d, n))));
                    }
                }
            }
        }
        if (!label.isEmpty()) {
            p.setPen(vc.text);
            p.drawText(c + QPointF(14, -14), label);
        }
    }

    // Status badge: degrees of freedom in words, plus the tool hint.
    QString dof = report_.dof == 0 && report_.state != jig::SolveState::Failed
                      ? tr("■ Fully defined")
                      : tr("○ %n free degree(s) of freedom: hollow points can still move", "", report_.dof);
    if (report_.state == jig::SolveState::OverConstrained) {
        dof = tr("⚠ ") + QString::fromStdString(report_.message);
    }
    const QStringList lines {dof, toolHint(), message_};
    QFont f = view_->font();
    p.setFont(f);
    int y = view_->height() - 16 - 18 * int(lines.size());
    for (const QString& l : lines) {
        if (l.isEmpty()) {
            continue;
        }
        const QRectF box(10, y, p.fontMetrics().horizontalAdvance(l) + 14, 20);
        QColor bg = vc.background;
        bg.setAlpha(225);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(l == message_ ? kGap : vc.text);
        p.drawText(box, Qt::AlignCenter, l);
        y += 22;
    }
}

}  // namespace jigapp
