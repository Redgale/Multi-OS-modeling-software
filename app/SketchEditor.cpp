// SPDX-License-Identifier: GPL-3.0-or-later
#include "SketchEditor.hpp"
#include "Translations.hpp"

#include "jig/kernel.hpp"

#include <cmath>
#include <numbers>

#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <QFrame>
#include <QGuiApplication>
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

// The circle through three points (none if they are in a straight line).
struct Circ
{
    double cx = 0, cy = 0, r = 0;
};
std::optional<Circ> circleThrough(QPointF a, QPointF e, QPointF m)
{
    const double d = 2 * (a.x() * (e.y() - m.y()) + e.x() * (m.y() - a.y()) + m.x() * (a.y() - e.y()));
    if (std::abs(d) < 1e-9) {
        return std::nullopt;
    }
    const double a2 = a.x() * a.x() + a.y() * a.y(), e2 = e.x() * e.x() + e.y() * e.y(),
                 m2 = m.x() * m.x() + m.y() * m.y();
    Circ c;
    c.cx = (a2 * (e.y() - m.y()) + e2 * (m.y() - a.y()) + m2 * (a.y() - e.y())) / d;
    c.cy = (a2 * (m.x() - e.x()) + e2 * (a.x() - m.x()) + m2 * (e.x() - a.x())) / d;
    c.r = std::hypot(a.x() - c.cx, a.y() - c.cy);
    return c;
}

// An arc leaving S in direction t and ending at E (counter-clockwise from a0 to a1;
// startAtS: S is the arc's start rather than its end).
struct TangentArc
{
    double cx = 0, cy = 0, r = 0, a0 = 0, a1 = 0;
    bool startAtS = true;
};
std::optional<TangentArc> tangentArc(QPointF S, QPointF t, QPointF E)
{
    const QPointF n(-t.y(), t.x()), d = E - S;
    const double denom = 2 * (d.x() * n.x() + d.y() * n.y());
    if (std::abs(denom) < 1e-9 || QLineF(S, E).length() < 1e-9) {
        return std::nullopt;  // straight ahead: that is a line
    }
    const double rs = (d.x() * d.x() + d.y() * d.y()) / denom;
    TangentArc a;
    a.cx = S.x() + n.x() * rs;
    a.cy = S.y() + n.y() * rs;
    a.r = std::abs(rs);
    const double as = std::atan2(S.y() - a.cy, S.x() - a.cx), ae = std::atan2(E.y() - a.cy, E.x() - a.cx);
    a.startAtS = rs > 0;  // turning left: counter-clockwise from S
    a.a0 = a.startAtS ? as : ae;
    a.a1 = a.startAtS ? ae : as;
    while (a.a1 <= a.a0) {
        a.a1 += 2 * kPi;
    }
    return a;
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
            if (startInput_) {  // a first point typed as X, Y
                const QStringList v = inputValues();
                auto x = doc_.evalLength(doc_.lengthInput(v.value(0).isEmpty() ? "0" : v.value(0).toStdString()));
                auto y = doc_.evalLength(doc_.lengthInput(v.value(1).isEmpty() ? "0" : v.value(1).toStdString()));
                if (!x || !y) {
                    message_ = QString::fromStdString((!x ? x.error() : y.error()).message);
                    emit stateChanged();
                    return;
                }
                startInput_ = false;
                hideInput();
                startAtTyped(x.value(), y.value());
                return;
            }
            if (dimTarget_ || editText_ || editArtwork_ || editConstraint_ || roundAt_ || !offsetChain_.empty()) {
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
    std::erase_if(selectedConstraints_, [&](int id) { return !work_.constraints().count(id); });
    profileTris_.clear();
    textEdges_.clear();
    if (it != doc_.state().sketches.end()) {
        for (auto& r : it->second->profiles.regions) {
            if (!r.key.empty() && (r.key[0] == 'T' || r.key[0] == 'G')) {
                for (TopExp_Explorer ee(r.face, TopAbs_EDGE); ee.More(); ee.Next()) {
                    BRepAdaptor_Curve c(TopoDS::Edge(ee.Current()));
                    std::vector<QVector3D> pts;
                    for (int k = 0; k <= 12; ++k) {
                        const gp_Pnt q = c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * k / 12);
                        pts.emplace_back(float(q.X()), float(q.Y()), float(q.Z()));
                    }
                    textEdges_.push_back(std::move(pts));
                }
            }
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
        message_ = coreText(r.error().message);
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
    // a "nice" step of about 12 screen pixels: 0.1, 0.2, 0.5, 1, 2, 5, 10 ... in metric
    // designs; 1/64, 1/32, … 1/2, 1, 2, 6, 12 inches in inch and foot designs
    const double raw = 12 * view_->worldPerPixel();
    if (doc_.units() == jig::LengthUnit::Inch || doc_.units() == jig::LengthUnit::Foot) {
        for (double in : {1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 6.0, 12.0, 24.0, 60.0}) {
            if (raw <= in * 25.4) {
                return in * 25.4;
            }
        }
        return 120 * 25.4;
    }
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
    else if (e.type == jig::EntityType::Ellipse || e.type == jig::EntityType::Spline) {
        for (auto [x, y] : s.curvePoints(e.id, 24)) {
            pts.emplace_back(x, y);
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
    if (t == Tool::PatternRect && !selection_.empty()) {
        first_ = cursor_;  // nothing to click: the counts and spacings are typed
        showInput({tr("Columns"), tr("Spacing")}, {"3", QString::fromStdString(doc_.defaultLength(10, 0.5))});
        inputFields_[0]->selectAll();
        inputFields_[0]->setFocus();
        inputBox_->move(QPoint(24, 24));
    }
    rebuildOverlay();
    emit stateChanged();
}

void SketchEditor::resetToolState()
{
    editConstraint_ = 0;
    editArtwork_ = 0;
    roundAt_.reset();
    trimHover_.reset();
    gridCols_ = 0;
    gridDx_ = 0;
    splinePts_.clear();
    startInput_ = false;
    offsetHover_.clear();
    offsetChain_.clear();
    first_.reset();
    second_.reset();
    editText_ = 0;
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
        case Tool::Arc3:
            return second_ ? tr("Click a point the arc passes through (this sets how much it bends).")
                 : first_  ? tr("Click where the arc ends.")
                           : tr("Click where the arc starts.");
        case Tool::TangentArc:
            return first_ ? tr("Click where the arc ends. It leaves the curve smoothly.")
                          : tr("Click the loose end of a line or arc to continue from.");
        case Tool::Circle2:
            return first_ ? tr("Click the other end of the diameter, or type the diameter and press Enter.")
                          : tr("Click one end of the circle's diameter.");
        case Tool::Circle3:
            return second_ ? tr("Click a third point on the circle.")
                 : first_  ? tr("Click a second point on the circle.")
                           : tr("Click a point on the circle.");
        case Tool::Rect3:
            return second_ ? tr("Click to set the depth, or type it and press Enter.")
                 : first_  ? tr("Click the other end of the first side, or type its length and press Enter.")
                           : tr("Click the first corner.");
        case Tool::Ellipse:
            return second_ ? tr("Click to set the other radius, or type it and press Enter.")
                 : first_  ? tr("Click the end of the long axis, or type its radius and press Enter.")
                           : tr("Click the centre of the ellipse.");
        case Tool::Spline:
            return splinePts_.empty()
                       ? tr("Click where the smooth curve starts.")
                       : tr("Click the next point it passes through. Enter, Esc or double-click ends; "
                            "click the first point to close it.");
        case Tool::Slot:
            return second_ ? tr("Click to set the width, or type it and press Enter.")
                 : first_  ? tr("Click the other end's centre, or type the centre-to-centre length.")
                           : tr("Click the centre of one end.");
        case Tool::Polygon:
            return first_ ? tr("Click a corner, or type the diameter (Tab: number of sides) and press Enter.")
                          : tr("Click the centre.");
        case Tool::Text:
            return first_ ? tr("Type the words, Tab, the size, then press Enter.")
                          : tr("Click where the middle of the text goes.");
        case Tool::Trim:
            return tr("Click the piece of a curve to remove. It is cut where other curves cross it.");
        case Tool::Extend:
            return tr("Click a curve near its loose end to lengthen it until it meets the next curve.");
        case Tool::Break:
            return tr("Click a curve to split it where other curves cross it (the pieces stay joined).");
        case Tool::Mirror:
            return selection_.empty()
                       ? tr("Select the curves to mirror first (Select tool, Shift-click for more), then choose Mirror.")
                       : tr("Click the line to mirror the %n selected curve(s) across.", "", int(selection_.size()));
        case Tool::Move:
            if (selection_.empty()) {
                return tr("Select the curves to move first (Select tool, Shift-click for more), then choose Move.");
            }
            return first_ ? tr("Click where the base point goes, or type how far across (Tab) up and press Enter. "
                               "Hold Ctrl to copy instead.")
                          : tr("Click a base point to move the selected curves by.");
        case Tool::PatternRect:
            if (selection_.empty()) {
                return tr("Select the curves to repeat first, then choose Grid copy.");
            }
            return second_ ? tr("Type the number of rows (1 for a single row) and their spacing upward, then Enter.")
                           : tr("Type the number of columns (counting the original) and their spacing to the right, then Enter.");
        case Tool::PatternCirc:
            if (selection_.empty()) {
                return tr("Select the curves to repeat first, then choose Ring copy.");
            }
            return first_ ? tr("Type how many in all (counting the original) and the angle to spread them over, then Enter.")
                          : tr("Click the centre to repeat the selected curves around.");
        case Tool::Project:
            return tr("Click an edge of a body (or a face, for its whole outline) to bring it into the sketch. "
                      "The copy is fixed in place; it does not follow later changes yet.");
        case Tool::Round:
            return roundAt_ ? tr("Type the radius and press Enter.")
                            : tr("Click the corner where two lines meet to round it.");
        case Tool::Offset:
            return offsetChain_.empty()
                       ? tr("Click a curve, or any curve of a closed outline, to copy it at a distance.")
                       : tr("Type the distance and press Enter: positive goes outward (or to the left of a line), "
                            "negative inward.");
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

QString SketchEditor::inUnits(double mm) const
{
    return QString::fromStdString(doc_.fmtLength(mm));  // prefill the value box, unit included
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

void SketchEditor::placeSecond(const Snap& cur, const QStringList& rawTyped)
{
    // Plain numbers typed into size fields are in the design's unit; store them explicitly.
    // (Angles, counts and the words of a text are left alone.)
    QStringList typed = rawTyped;
    for (int i = 0; i < typed.size(); ++i) {
        const bool notLength = (tool_ == Tool::Line && i == 1) || (tool_ == Tool::Arc && second_) ||
                               (tool_ == Tool::Polygon && i == 1) || (tool_ == Tool::Text && i == 0) ||
                               (tool_ == Tool::PatternRect && i == 0) || tool_ == Tool::PatternCirc;
        if (!notLength && !typed[i].trimmed().isEmpty()) {
            typed[i] = QString::fromStdString(doc_.lengthInput(typed[i].trimmed().toStdString()));
        }
    }
    auto typedLen = [&](int i) -> std::optional<double> {
        if (i >= typed.size() || typed[i].isEmpty()) {
            return std::nullopt;
        }
        auto r = doc_.evalLength(typed[i].toStdString());
        if (!r) {
            message_ = coreText(r.error().message);
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
        else if (e->type == jig::EntityType::Arc || e->type == jig::EntityType::Ellipse) {
            s.addConstraint(jig::ConstraintType::Radius, dimTarget_, 0, 0, expr);
        }
        commit(tr("Dimension"), s);
        dimFirst_ = dimTarget_ = dimSecond_ = 0;
        hideInput();
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (editConstraint_) {
        if (rawTyped.isEmpty() || rawTyped[0].trimmed().isEmpty()) {
            return;
        }
        jig::Sketch s = work_;
        auto it = s.constraints().find(editConstraint_);
        if (it == s.constraints().end()) {
            resetToolState();
            return;
        }
        const bool angle = it->second.type == jig::ConstraintType::Angle;
        const std::string expr = angle ? rawTyped[0].trimmed().toStdString() : doc_.lengthInput(rawTyped[0].trimmed().toStdString());
        const auto k = it->second;
        s.removeConstraint(editConstraint_);
        s.addConstraint(k.type, k.a, k.b, k.c, expr);
        if (commit(tr("Change dimension"), s)) {
            resetToolState();
        }
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (editArtwork_) {
        if (typed.isEmpty() || typed[0].isEmpty()) {
            return;
        }
        auto w = doc_.evalLength(typed[0].toStdString());
        if (!w || !(w.value() > 0)) {
            message_ = w ? tr("The width must be larger than zero.") : coreText(w.error().message);
            emit stateChanged();
            return;
        }
        jig::Sketch s = work_;
        if (jig::SketchArtwork* art = s.artwork(editArtwork_)) {
            art->width = typed[0].toStdString();
            art->widthValue = w.value();
        }
        if (commit(tr("Artwork size"), s)) {
            resetToolState();
        }
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (roundAt_) {
        if (typed.isEmpty() || typed[0].isEmpty()) {
            return;
        }
        auto r = doc_.evalLength(typed[0].toStdString());
        if (!r) {
            message_ = coreText(r.error().message);
            emit stateChanged();
            return;
        }
        jig::Sketch s = work_;
        auto arc = s.filletCorner(roundAt_->x(), roundAt_->y(), r.value(), typed[0].toStdString());
        if (!arc) {
            message_ = coreText(arc.error().message);
            emit stateChanged();
            return;
        }
        if (commit(tr("Round corner"), s)) {
            resetToolState();
        }
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (!offsetChain_.empty()) {
        if (typed.isEmpty() || typed[0].isEmpty()) {
            return;
        }
        auto d = doc_.evalLength(typed[0].toStdString());
        if (!d) {
            message_ = coreText(d.error().message);
            emit stateChanged();
            return;
        }
        jig::Sketch s = work_;
        auto made = jig::offsetCurves(s, offsetChain_, d.value());
        if (!made) {
            message_ = coreText(made.error().message);
            emit stateChanged();
            return;
        }
        if (commit(tr("Offset"), s)) {
            resetToolState();
        }
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (editText_) {
        jig::Sketch s = work_;
        jig::SketchText* t = s.text(editText_);
        if (!t || typed.isEmpty() || typed[0].trimmed().isEmpty()) {
            message_ = tr("Type the words for the text.");
            emit stateChanged();
            return;
        }
        if (typed.size() > 1 && !typed[1].isEmpty() && !typedLen(1)) {
            return;
        }
        t->text = typed[0].toStdString();
        if (typed.size() > 1 && !typed[1].isEmpty()) {
            t->height = typed[1].toStdString();
        }
        if (commit(tr("Edit text"), s)) {
            resetToolState();
        }
        rebuildOverlay();
        emit stateChanged();
        return;
    }
    if (!first_) {
        return;
    }
    // the arc's last step takes an angle and the polygon's second field a count;
    // those are checked where they are used
    const bool notLengths = (tool_ == Tool::Arc && second_) || tool_ == Tool::PatternCirc;
    for (int i = 0; i < typed.size() && !notLengths; ++i) {
        if ((tool_ == Tool::Polygon && i == 1) || (tool_ == Tool::Text && i == 0) ||
            (tool_ == Tool::PatternRect && i == 0)) {
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

    const std::vector<int> picked(selection_.begin(), selection_.end());
    auto countOf = [&](int i, int fallback) -> std::optional<int> {
        if (i >= typed.size() || typed[i].trimmed().isEmpty()) {
            return fallback;
        }
        auto q = jig::evaluate(typed[i].toStdString(), doc_.lookup());
        if (!q || q->dim != jig::Dim::none() || q->value < 1 || std::abs(q->value - std::round(q->value)) > 1e-9) {
            message_ = tr("Type a whole number for how many.");
            emit stateChanged();
            return std::nullopt;
        }
        return int(std::lround(q->value));
    };
    switch (tool_) {
        case Tool::TangentArc: {
            const jig::SketchEntity* from = s.entity(tangentFrom_);
            if (!from) {
                return;
            }
            auto ta = tangentArc(a, tangentDir_, b);
            if (!ta) {
                message_ = tr("That is straight ahead of the curve: use a line, or click off to one side.");
                break;
            }
            const int arc = s.addArc(ta->cx, ta->cy, ta->r, ta->a0, ta->a1);
            const jig::SketchEntity A = *s.entity(arc);
            const int sPt = ta->startAtS ? A.p1 : A.p2, ePt = ta->startAtS ? A.p2 : A.p1;
            s.addConstraint(jig::ConstraintType::Coincident, sPt, first_->point);
            s.addConstraint(jig::ConstraintType::Tangent, tangentFrom_, arc);
            if (cur.point && cur.point != first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, ePt, cur.point);
            }
            if (commit(tr("Tangent arc"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Circle2: {
            auto d = typedLen(0);
            const double L = std::hypot(dir.x(), dir.y());
            const double D = d ? *d : L;
            if (D < 1e-9) {
                return;
            }
            const QPointF u = L > 1e-9 ? dir / L : QPointF(1, 0);
            const QPointF far = a + u * D, mid = (a + far) / 2;
            const int c = s.addCircle(mid.x(), mid.y(), D / 2);
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::PointOnCurve, first_->point, c);
            }
            if (!d && cur.point && cur.point != first_->point) {
                s.addConstraint(jig::ConstraintType::PointOnCurve, cur.point, c);
            }
            if (d) {
                s.addConstraint(jig::ConstraintType::Diameter, c, 0, 0, typed[0].toStdString());
            }
            if (commit(tr("2-point circle"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Circle3: {
            if (!second_) {
                if (QLineF(a, b).length() < 1e-9) {
                    return;
                }
                second_ = cur;
                break;
            }
            auto ci = circleThrough(a, second_->local, b);
            if (!ci) {
                message_ = tr("The three points are in a straight line: click to one side.");
                break;
            }
            const int c = s.addCircle(ci->cx, ci->cy, ci->r);
            for (int pid : {first_->point, second_->point, cur.point}) {
                if (pid) {
                    s.addConstraint(jig::ConstraintType::PointOnCurve, pid, c);
                }
            }
            if (commit(tr("3-point circle"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Rect3: {
            if (!second_) {
                auto w = typedLen(0);
                const double L = std::hypot(dir.x(), dir.y());
                const double W = w ? *w : L;
                if (W < 1e-9) {
                    return;
                }
                const QPointF u = L > 1e-9 ? dir / L : QPointF(1, 0);
                second_ = Snap {a + u * W, w ? 0 : cur.point};
                arcRadiusExpr_ = w ? typed[0] : QString();  // the first side's length, used below
                showInput({tr("Depth")});
                break;
            }
            const QPointF B = second_->local;
            const QPointF u = (B - a) / QLineF(a, B).length(), n(-u.y(), u.x());
            auto h = typedLen(0);
            double H = QPointF::dotProduct(b - B, n);
            if (h) {
                H = H < 0 ? -*h : *h;
            }
            if (std::abs(H) < 1e-9) {
                return;
            }
            const QPointF C = B + n * H, D = a + n * H;
            const int pa = first_->point ? first_->point : s.addPoint(a.x(), a.y(), true);
            const int pb = second_->point ? second_->point : s.addPoint(B.x(), B.y(), true);
            const int pc = s.addPoint(C.x(), C.y(), true), pd = s.addPoint(D.x(), D.y(), true);
            const int ab = s.addLine(pa, pb), bc = s.addLine(pb, pc), cd = s.addLine(pc, pd), da = s.addLine(pd, pa);
            s.addConstraint(jig::ConstraintType::Perpendicular, ab, bc);
            s.addConstraint(jig::ConstraintType::Parallel, ab, cd);
            s.addConstraint(jig::ConstraintType::Parallel, bc, da);
            if (!arcRadiusExpr_.isEmpty()) {
                s.addConstraint(jig::ConstraintType::Distance, ab, 0, 0, arcRadiusExpr_.toStdString());
            }
            if (h) {
                s.addConstraint(jig::ConstraintType::Distance, bc, 0, 0, typed[0].toStdString());
            }
            if (commit(tr("3-point rectangle"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Ellipse: {
            if (!second_) {
                auto ra = typedLen(0);
                const double L = std::hypot(dir.x(), dir.y());
                const double R = ra ? *ra : L;
                if (R < 1e-9) {
                    return;
                }
                const QPointF u = L > 1e-9 ? dir / L : QPointF(1, 0);
                second_ = Snap {a + u * R, 0};
                arcRadiusExpr_ = ra ? typed[0] : QString();
                showInput({tr("Minor radius")});
                break;
            }
            const QPointF ax = second_->local;
            const QPointF u = (ax - a) / QLineF(a, ax).length();
            auto rb = typedLen(0);
            const double minor = rb ? *rb : std::abs(QPointF::dotProduct(b - a, QPointF(-u.y(), u.x())));
            if (minor < 1e-9) {
                return;
            }
            const int el = s.addEllipse(a.x(), a.y(), ax.x(), ax.y(), minor);
            const jig::SketchEntity E = *s.entity(el);
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, E.center, first_->point);
            }
            if (!arcRadiusExpr_.isEmpty()) {
                s.addConstraint(jig::ConstraintType::Distance, E.center, E.p1, 0, arcRadiusExpr_.toStdString());
            }
            if (rb) {
                s.addConstraint(jig::ConstraintType::Radius, el, 0, 0, typed[0].toStdString());
            }
            if (commit(tr("Ellipse"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::Move: {
            auto across = typedLen(0), up = typedLen(1);
            double dx = dir.x(), dy = dir.y();
            if (across || up) {
                dx = across ? *across : 0;
                dy = up ? *up : 0;
            }
            const bool copy = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
            if (copy) {
                auto r = s.copyCurves(picked, dx, dy);
                if (!r) {
                    message_ = coreText(r.error().message);
                    break;
                }
                if (commit(tr("Copy"), s)) {
                    selection_ = std::set<int>(r->begin(), r->end());
                    resetToolState();
                }
                break;
            }
            auto r = s.moveCurves(picked, dx, dy);
            if (!r) {
                message_ = coreText(r.error().message);
                break;
            }
            if (commit(tr("Move"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::PatternRect: {
            auto n = countOf(0, second_ ? 1 : 3);
            if (!n) {
                break;
            }
            auto sp = typedLen(1);
            if (typed.size() > 1 && !typed[1].isEmpty() && !sp) {
                break;
            }
            const double spacing = sp ? *sp : 10;
            if (!second_) {
                gridCols_ = *n;
                gridDx_ = spacing;
                second_ = cur;
                showInput({tr("Rows"), tr("Spacing")}, {"1", typed.size() > 1 ? typed[1] : QString()});
                inputFields_[0]->selectAll();
                inputFields_[0]->setFocus();
                break;
            }
            auto r = s.patternRect(picked, gridCols_, gridDx_, 0, *n, 0, spacing);
            if (!r) {
                message_ = coreText(r.error().message);
                break;
            }
            if (commit(tr("Grid copy"), s)) {
                resetToolState();
            }
            break;
        }
        case Tool::PatternCirc: {
            auto n = countOf(0, 6);
            if (!n) {
                break;
            }
            double total = 2 * kPi;
            if (typed.size() > 1 && !typed[1].trimmed().isEmpty()) {
                auto av = doc_.evalAngle(typed[1].toStdString());
                if (!av) {
                    message_ = coreText(av.error().message);
                    break;
                }
                total = av.value();
            }
            auto r = s.patternCircular(picked, a.x(), a.y(), *n, total);
            if (!r) {
                message_ = coreText(r.error().message);
                break;
            }
            if (commit(tr("Ring copy"), s)) {
                resetToolState();
            }
            break;
        }
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
                    message_ = coreText(av.error().message);
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
        case Tool::Arc3: {
            if (!second_) {
                if (QLineF(a, b).length() < 1e-9) {
                    return;
                }
                second_ = cur;
                break;
            }
            // the circle through start, end and the clicked point
            const QPointF e = second_->local, m = b;
            const double d = 2 * (a.x() * (e.y() - m.y()) + e.x() * (m.y() - a.y()) + m.x() * (a.y() - e.y()));
            if (std::abs(d) < 1e-9) {
                message_ = tr("The three points are in a straight line: click to one side to make it bend.");
                return;
            }
            const double a2 = a.x() * a.x() + a.y() * a.y(), e2 = e.x() * e.x() + e.y() * e.y(),
                         m2 = m.x() * m.x() + m.y() * m.y();
            const double cx = (a2 * (e.y() - m.y()) + e2 * (m.y() - a.y()) + m2 * (a.y() - e.y())) / d;
            const double cy = (a2 * (m.x() - e.x()) + e2 * (a.x() - m.x()) + m2 * (e.x() - a.x())) / d;
            const double R = std::hypot(a.x() - cx, a.y() - cy);
            auto ang = [&](QPointF q) { return std::atan2(q.y() - cy, q.x() - cx); };
            auto ccwFrom = [](double from, double to) {
                double v = std::fmod(to - from, 2 * kPi);
                return v < 0 ? v + 2 * kPi : v;
            };
            // counter-clockwise from start to end if the third point lies on that way round
            const bool startFirst = ccwFrom(ang(a), ang(m)) < ccwFrom(ang(a), ang(e));
            const double s0 = startFirst ? ang(a) : ang(e);
            const double sweep = startFirst ? ccwFrom(ang(a), ang(e)) : ccwFrom(ang(e), ang(a));
            const int arc = s.addArc(cx, cy, R, s0, s0 + sweep);
            const jig::SketchEntity A = *s.entity(arc);
            const int startPt = startFirst ? A.p1 : A.p2, endPt = startFirst ? A.p2 : A.p1;
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, startPt, first_->point);
            }
            if (second_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, endPt, second_->point);
            }
            if (commit(tr("3-point arc"), s)) {
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
        case Tool::Text: {
            if (typed.isEmpty() || typed[0].trimmed().isEmpty()) {
                message_ = tr("Type the words for the text.");
                return;
            }
            const QString hExpr = typed.size() > 1 && !typed[1].isEmpty() ? typed[1]
                                                                            : QString::fromStdString(doc_.defaultLength(5, 0.25));
            auto h = doc_.evalLength(hExpr.toStdString());
            if (!h || !(h.value() > 0)) {
                message_ = h ? tr("Text size must be larger than zero.") : coreText(h.error().message);
                return;
            }
            const int id = s.addText(a.x(), a.y(), typed[0].toStdString(), hExpr.toStdString(), h.value());
            if (first_->point) {
                s.addConstraint(jig::ConstraintType::Coincident, s.texts().at(id).anchor, first_->point);
            }
            if (commit(tr("Text"), s)) {
                lastTextHeight_ = hExpr;
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
    boxing_ = false;
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
        else if (en->type == jig::EntityType::Spline) {
            double best = 1e300;  // the fit point nearest the press
            for (int pid : en->pts) {
                const double d = QLineF(QPointF(screenOf(work_.pointXY(pid).first, work_.pointXY(pid).second)),
                                        pressScreen_).length();
                if (d < best) {
                    best = d;
                    dragPoint_ = pid;
                }
            }
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
    if (pressed_ && tool_ == Tool::Select && !dragPoint_ && !glyphAt(pressScreen_)) {
        if (boxing_ || QLineF(pos, pressScreen_).length() >= 4) {
            boxing_ = true;
            boxEnd_ = pos;
            rebuildOverlay();
            return true;
        }
    }
    cursor_ = snapAt(pos, tool_ == Tool::Line || tool_ == Tool::Rectangle, e->modifiers());
    cursorValid_ = toLocal(pos).has_value();
    if (tool_ == Tool::Offset && offsetChain_.empty()) {
        offsetHover_.clear();
        if (auto loc = toLocal(pos)) {
            auto near = work_.curveNear(loc->x(), loc->y());
            if (near && near->second < 8 * view_->worldPerPixel()) {
                offsetHover_ = work_.chainOf(near->first);
            }
        }
    }
    if (tool_ == Tool::Project && view_) {
        if (auto h = view_->pickEdgeAt(pos.toPoint())) {
            view_->setEdgeHighlight(h->body, {h->edge});
        }
        else {
            view_->setEdgeHighlight({}, {});
        }
    }
    if (tool_ == Tool::Trim || tool_ == Tool::Break) {
        trimHover_.reset();
        if (auto loc = toLocal(pos)) {
            auto near = work_.curveNear(loc->x(), loc->y());
            if (near && near->second < 8 * view_->worldPerPixel()) {
                trimHover_ = work_.trimPiece(near->first, loc->x(), loc->y());
            }
        }
    }
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
    if (boxing_) {
        boxing_ = false;
        // left to right: what is wholly inside; right to left: anything the box touches
        const bool crossing = pos.x() < pressScreen_.x();
        const auto inBox = entitiesInBox(QRectF(pressScreen_, pos).normalized(), crossing);
        if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier))) {
            selection_.clear();
            selectedConstraints_.clear();
        }
        selection_.insert(inBox.begin(), inBox.end());
        rebuildOverlay();
        emit stateChanged();
        return true;
    }
    cursor_ = snapAt(pos, tool_ == Tool::Line || tool_ == Tool::Rectangle, e->modifiers());
    // a click without a mouse move before it (a tablet tap, or the cursor already there)
    cursorValid_ = toLocal(pos).has_value();
    if (forcedCursor_) {  // a point typed on the keyboard, exactly where it was typed
        cursor_ = *forcedCursor_;
        cursorValid_ = true;
        forcedCursor_.reset();
    }
    switch (tool_) {
        case Tool::Select: {
            const bool add = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
            if (const int g = glyphAt(pos)) {  // a constraint glyph or dimension label
                selectConstraint(g, add);
                break;
            }
            const int hit = entityNear(pos);
            if (!add) {
                selection_.clear();
                selectedConstraints_.clear();
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
        case Tool::Project: {
            // the bodies as they were before this sketch (a sketch cannot project its own result)
            const jig::ModelState before = doc_.stateBefore(doc_.featureIndex(id_));
            std::vector<TopoDS_Shape> edges;
            if (auto h = view_->pickEdgeAt(pos.toPoint())) {
                auto it = before.bodies.find(h->body);
                // the view shows the current bodies: map the edge over by its name
                auto cur = doc_.state().bodies.find(h->body);
                if (it != before.bodies.end() && cur != doc_.state().bodies.end()) {
                    for (int e : it->second->findEdges(cur->second->edgeName(h->edge))) {
                        edges.push_back(it->second->edge(e));
                    }
                }
            }
            else if (auto f = view_->pickFaceAt(pos.toPoint())) {
                auto it = before.bodies.find(f->body);
                auto cur = doc_.state().bodies.find(f->body);
                if (it != before.bodies.end() && cur != doc_.state().bodies.end()) {
                    for (int fi : it->second->findFaces(cur->second->faceName(f->face))) {
                        for (int e : it->second->edgesOfFace(fi)) {
                            edges.push_back(it->second->edge(e));
                        }
                    }
                }
            }
            if (edges.empty()) {
                message_ = tr("Click an edge or face of a body made before this sketch.");
                break;
            }
            jig::Sketch s = work_;
            for (auto& edge : edges) {
                auto r = jig::projectEdge(s, frame_, edge);
                if (!r) {
                    message_ = coreText(r.error().message);
                    if (edges.size() == 1) {
                        break;
                    }
                }
            }
            commit(tr("Project"), s);
            break;
        }
        case Tool::Round: {
            auto loc = toLocal(pos);
            if (!loc) {
                break;
            }
            roundAt_ = *loc;
            showInput({tr("Radius")}, {QString::fromStdString(doc_.defaultLength(3, 0.125))});
            inputFields_[0]->selectAll();
            inputFields_[0]->setFocus();
            inputBox_->move(pos.toPoint() + QPoint(18, 18));
            break;
        }
        case Tool::Offset: {
            if (!offsetChain_.empty() || offsetHover_.empty()) {
                if (offsetHover_.empty()) {
                    message_ = tr("Click on a curve to offset it.");
                }
                break;
            }
            offsetChain_ = offsetHover_;
            showInput({tr("Distance")}, {QString::fromStdString(doc_.defaultLength(2, 0.0625))});
            inputFields_[0]->selectAll();
            inputFields_[0]->setFocus();
            inputBox_->move(pos.toPoint() + QPoint(18, 18));
            break;
        }
        case Tool::Extend:
        case Tool::Break: {
            auto loc = toLocal(pos);
            if (!loc) {
                break;
            }
            auto near = work_.curveNear(loc->x(), loc->y());
            if (!near || near->second > 8 * view_->worldPerPixel()) {
                message_ = tool_ == Tool::Extend ? tr("Click on a line or arc, near the end to lengthen.")
                                                 : tr("Click on a curve to break it.");
                break;
            }
            jig::Sketch s = work_;
            bool ok = false;
            if (tool_ == Tool::Extend) {
                auto r = s.extend(near->first, loc->x(), loc->y());
                ok = r.ok();
                message_ = ok ? QString() : coreText(r.error().message);
            }
            else {
                auto r = s.breakAt(near->first, loc->x(), loc->y());
                ok = r.ok();
                message_ = ok ? QString() : coreText(r.error().message);
            }
            if (ok) {
                commit(tool_ == Tool::Extend ? tr("Extend") : tr("Break"), s);
            }
            trimHover_.reset();
            break;
        }
        case Tool::Mirror: {
            auto loc = toLocal(pos);
            if (!loc) {
                break;
            }
            if (selection_.empty()) {
                message_ = tr("Select the curves to mirror first, then choose Mirror.");
                break;
            }
            auto near = work_.curveNear(loc->x(), loc->y(), true);
            const jig::SketchEntity* axis = near ? work_.entity(near->first) : nullptr;
            if (!axis || axis->type != jig::EntityType::Line || near->second > 8 * view_->worldPerPixel()) {
                message_ = tr("Click a straight line (it can be a construction line) to mirror across.");
                break;
            }
            jig::Sketch s = work_;
            auto r = s.mirror(std::vector<int>(selection_.begin(), selection_.end()), near->first);
            if (!r) {
                message_ = coreText(r.error().message);
                break;
            }
            if (commit(tr("Mirror"), s)) {
                selection_ = std::set<int>(r->begin(), r->end());
                message_.clear();
            }
            break;
        }
        case Tool::Trim: {
            auto loc = toLocal(pos);
            if (!loc) {
                break;
            }
            auto near = work_.curveNear(loc->x(), loc->y());
            if (!near || near->second > 8 * view_->worldPerPixel()) {
                message_ = tr("Click on a curve to trim it.");
                break;
            }
            jig::Sketch s = work_;
            if (s.trim(near->first, loc->x(), loc->y())) {
                commit(tr("Trim"), s);
            }
            trimHover_.reset();
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
                    showInput({tr("Distance")}, {inUnits(v)});
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
            else if (en->type == jig::EntityType::Spline) {
                dimTarget_ = 0;
                message_ = tr("A smooth curve has no single size: dimension the points it passes through instead.");
                break;
            }
            else if (en->type == jig::EntityType::Ellipse) {
                v = en->r;
                label = tr("Minor radius");  // the major radius: dimension the centre to the axis end
            }
            else {
                v = en->r;
                label = tr("Radius");
            }
            showInput({label}, {inUnits(v)});
            inputFields_[0]->selectAll();
            inputFields_[0]->setFocus();
            inputBox_->move(pos.toPoint() + QPoint(18, 18));
            break;
        }
        default: {
            if (!cursorValid_) {
                break;
            }
            if ((tool_ == Tool::Move || tool_ == Tool::PatternRect || tool_ == Tool::PatternCirc) && selection_.empty()) {
                message_ = tr("Select the curves first (Select tool, Shift-click for more).");
                break;
            }
            if (tool_ == Tool::PatternRect) {
                break;  // typed only
            }
            if (tool_ == Tool::Spline) {
                const QPointF start = splinePts_.empty() ? QPointF()
                                                         : QPointF(screenOf(splinePts_.front().local.x(),
                                                                            splinePts_.front().local.y()));
                if (splinePts_.size() >= 3 && QLineF(start, pos).length() < kHitPx) {
                    finishSpline(true);  // back on the first point: closed
                    break;
                }
                if (!splinePts_.empty() && QLineF(splinePts_.back().local, cursor_.local).length() < 1e-9) {
                    break;  // the same spot twice
                }
                splinePts_.push_back(cursor_);
                if (!first_) {
                    first_ = cursor_;
                }
                break;
            }
            if (tool_ == Tool::TangentArc && !first_) {
                // the loose end of exactly one line or arc
                tangentFrom_ = 0;
                if (cursor_.point) {
                    int owners = 0;
                    for (auto& [id, en] : work_.entities()) {
                        if ((en.type == jig::EntityType::Line || en.type == jig::EntityType::Arc) && !en.construction &&
                            (en.p1 == cursor_.point || en.p2 == cursor_.point)) {
                            ++owners;
                            tangentFrom_ = id;
                        }
                    }
                    if (owners != 1) {
                        tangentFrom_ = 0;
                    }
                }
                const jig::SketchEntity* from = work_.entity(tangentFrom_);
                if (!from) {
                    message_ = tr("Click the loose end of a line or arc to continue from it.");
                    break;
                }
                if (from->type == jig::EntityType::Line) {
                    auto [x1, y1] = work_.pointXY(from->p1);
                    auto [x2, y2] = work_.pointXY(from->p2);
                    QPointF t = from->p2 == cursor_.point ? QPointF(x2 - x1, y2 - y1) : QPointF(x1 - x2, y1 - y2);
                    tangentDir_ = t / std::max(1e-12, QLineF(QPointF(), t).length());
                }
                else {
                    const bool atEnd = from->p2 == cursor_.point;
                    const double ang = atEnd ? from->a1 : from->a0;
                    tangentDir_ = atEnd ? QPointF(-std::sin(ang), std::cos(ang)) : QPointF(std::sin(ang), -std::cos(ang));
                }
                message_.clear();
                first_ = cursor_;
                hideInput();
                break;
            }
            if (!first_) {
                first_ = cursor_;
                const bool two = tool_ != Tool::Circle;
                (void)two;
                if (tool_ == Tool::Circle3) {
                    hideInput();  // three clicks, nothing to type
                }
                else if (tool_ == Tool::Circle2) {
                    showInput({tr("Diameter")});
                }
                else if (tool_ == Tool::Rect3) {
                    showInput({tr("Length")});
                }
                else if (tool_ == Tool::Move) {
                    showInput({tr("Across"), tr("Up")});
                }
                else if (tool_ == Tool::Ellipse) {
                    showInput({tr("Major radius")});
                }
                else if (tool_ == Tool::PatternCirc) {
                    showInput({tr("How many"), tr("Angle")}, {"6", "360 deg"});
                    inputFields_[0]->selectAll();
                    inputFields_[0]->setFocus();
                }
                else if (tool_ == Tool::Polygon) {
                    showInput({tr("Diameter"), tr("Sides")}, {QString(), "6"});
                }
                else if (tool_ == Tool::Arc3) {
                    hideInput();  // three clicks, nothing to type
                }
                else if (tool_ == Tool::Text) {
                    showInput({tr("Text"), tr("Size")},
                              {QString(), lastTextHeight_.isEmpty() ? QString::fromStdString(doc_.defaultLength(5, 0.25))
                                                                    : lastTextHeight_});
                    inputFields_[0]->setFocus();
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

int SketchEditor::glyphAt(const QPointF& screen) const
{
    for (auto& [cid, box] : glyphBoxes_) {
        if (box.adjusted(-2, -2, 2, 2).contains(screen)) {
            return cid;
        }
    }
    return 0;
}

std::optional<QPointF> SketchEditor::glyphCenter(int constraintId) const
{
    for (auto& [cid, box] : glyphBoxes_) {
        if (cid == constraintId) {
            return box.center();
        }
    }
    return std::nullopt;
}

std::set<int> SketchEditor::entitiesInBox(const QRectF& box, bool crossing) const
{
    std::set<int> out;
    for (auto& [id, e] : work_.entities()) {
        std::vector<QPointF> samples;
        if (e.type == jig::EntityType::Point) {
            if (e.internal) {
                continue;
            }
            samples.push_back(screenOf(e.x, e.y));
        }
        else if (e.type == jig::EntityType::Line) {
            auto [ax, ay] = work_.pointXY(e.p1);
            auto [bx, by] = work_.pointXY(e.p2);
            for (int i = 0; i <= 16; ++i) {
                const double u = i / 16.0;
                samples.push_back(screenOf(ax + (bx - ax) * u, ay + (by - ay) * u));
            }
        }
        else if (e.type == jig::EntityType::Ellipse || e.type == jig::EntityType::Spline) {
            for (auto [x, y] : work_.curvePoints(id, 16)) {
                samples.push_back(screenOf(x, y));
            }
        }
        else {
            auto [cx, cy] = work_.pointXY(e.center);
            double a0 = 0, span = 2 * kPi;
            if (e.type == jig::EntityType::Arc) {
                a0 = e.a0;
                span = std::fmod(e.a1 - e.a0 + 4 * kPi, 2 * kPi);
            }
            for (int i = 0; i <= 48; ++i) {
                const double t = a0 + span * i / 48.0;
                samples.push_back(screenOf(cx + e.r * std::cos(t), cy + e.r * std::sin(t)));
            }
        }
        const auto inside = [&](const QPointF& q) { return box.contains(q); };
        const bool hit = crossing ? std::any_of(samples.begin(), samples.end(), inside)
                                  : std::all_of(samples.begin(), samples.end(), inside);
        if (hit && !samples.empty()) {
            out.insert(id);
        }
    }
    return out;
}

void SketchEditor::selectConstraint(int constraintId, bool add)
{
    if (!add) {
        selection_.clear();
        selectedConstraints_.clear();
    }
    if (add && selectedConstraints_.count(constraintId)) {
        selectedConstraints_.erase(constraintId);
    }
    else {
        selectedConstraints_.insert(constraintId);
    }
    message_ = tr("Constraint selected: press Delete to remove it. Double-click a dimension to change it.");
    rebuildOverlay();
    emit stateChanged();
}

bool SketchEditor::editDimension(int constraintId)
{
    auto it = work_.constraints().find(constraintId);
    using CT = jig::ConstraintType;
    if (it == work_.constraints().end() || !(it->second.type == CT::Distance || it->second.type == CT::Diameter ||
                                             it->second.type == CT::Radius || it->second.type == CT::Angle)) {
        return false;
    }
    resetToolState();
    editConstraint_ = constraintId;
    showInput({it->second.type == CT::Angle ? tr("Angle") : tr("Value")}, {QString::fromStdString(it->second.expr)});
    if (auto c = glyphCenter(constraintId)) {
        inputBox_->move(c->toPoint() + QPoint(14, 14));
    }
    inputFields_[0]->selectAll();
    inputFields_[0]->setFocus();
    emit stateChanged();
    return true;
}

bool SketchEditor::editArtworkAt(int anchorPoint)
{
    for (auto& [id, a] : work_.artworks()) {
        if (a.anchor == anchorPoint) {
            resetToolState();
            editArtwork_ = id;
            showInput({tr("Width")}, {a.width.empty() ? QString::fromStdString(doc_.fmtLength(a.naturalWidth))
                                                      : QString::fromStdString(a.width)});
            auto [x, y] = work_.pointXY(a.anchor);
            inputBox_->move(screenOf(x, y) + QPoint(18, 18));
            inputFields_[0]->selectAll();
            inputFields_[0]->setFocus();
            emit stateChanged();
            return true;
        }
    }
    return false;
}

bool SketchEditor::editTextAt(int anchorPoint)
{
    for (auto& [id, t] : work_.texts()) {
        if (t.anchor == anchorPoint) {
            resetToolState();
            editText_ = id;
            showInput({tr("Text"), tr("Size")}, {QString::fromStdString(t.text), QString::fromStdString(t.height)});
            auto [x, y] = work_.pointXY(t.anchor);
            inputBox_->move(screenOf(x, y) + QPoint(18, 18));
            inputFields_[0]->selectAll();
            inputFields_[0]->setFocus();
            emit stateChanged();
            return true;
        }
    }
    return false;
}

void SketchEditor::startAtTyped(double x, double y)
{
    Snap at;
    at.local = QPointF(x, y);
    // an existing point exactly there is used, as a click on it would
    for (auto& [id, e] : work_.entities()) {
        if (e.type == jig::EntityType::Point && std::hypot(e.x - x, e.y - y) < 1e-9) {
            at.point = id;
        }
    }
    forcedCursor_ = at;
    message_.clear();
    const QPointF screen = screenOf(x, y);
    QMouseEvent press(QEvent::MouseButtonPress, screen, view_->mapToGlobal(screen), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, screen, view_->mapToGlobal(screen), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    mousePress(&press);
    forcedCursor_ = at;
    mouseRelease(&release);
}

void SketchEditor::finishSpline(bool closed)
{
    if (splinePts_.size() < 2) {
        resetToolState();
        return;
    }
    jig::Sketch s = work_;
    std::vector<std::pair<double, double>> pts;
    for (auto& p : splinePts_) {
        pts.emplace_back(p.local.x(), p.local.y());
    }
    const int c = s.addSpline(pts, closed);
    const jig::SketchEntity S = *s.entity(c);
    for (size_t i = 0; i < splinePts_.size() && i < S.pts.size(); ++i) {
        if (splinePts_[i].point) {  // clicked on an existing point: start or pass there
            s.addConstraint(jig::ConstraintType::Coincident, S.pts[i], splinePts_[i].point);
        }
    }
    if (commit(closed ? tr("Closed smooth curve") : tr("Smooth curve"), s)) {
        resetToolState();
    }
}

bool SketchEditor::mouseDoubleClick(QMouseEvent* e)
{
    // Qt delivers the second press of a quick double click as this event instead of a
    // press. Unless the double click means something, treat it as the press it is, so a
    // fast second click is never lost.
    if (tool_ == Tool::Select) {
        if (const int g = glyphAt(e->position()); g && editDimension(g)) {
            pressed_ = false;
            return true;
        }
    }
    if (tool_ == Tool::Select && editArtworkAt(entityNear(e->position()))) {
        pressed_ = false;
        return true;
    }
    if (tool_ == Tool::Select && editTextAt(entityNear(e->position()))) {
        pressed_ = false;  // ignore the release that follows
        return true;
    }
    if (tool_ == Tool::Spline && !splinePts_.empty()) {
        mousePress(e);  // the second click places a point; then the curve ends there
        mouseRelease(e);
        finishSpline(false);
        pressed_ = false;
        emit stateChanged();
        return true;
    }
    if (tool_ == Tool::Line && first_) {
        resetToolState();  // double-click ends a chain of lines
        pressed_ = false;
        emit stateChanged();
        return true;
    }
    return mousePress(e);
}

bool SketchEditor::key(QKeyEvent* e)
{
    if (tool_ == Tool::Spline && !splinePts_.empty() &&
        (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        finishSpline(false);
        rebuildOverlay();
        emit stateChanged();
        return true;
    }
    if (e->key() == Qt::Key_Escape) {
        if (first_ || dimFirst_ || dimTarget_ || editConstraint_ || editText_ || editArtwork_ || roundAt_ ||
            !offsetChain_.empty()) {
            resetToolState();
        }
        else if (tool_ != Tool::Select) {
            setTool(Tool::Select);
        }
        else {
            selection_.clear();
            selectedConstraints_.clear();
        }
        rebuildOverlay();
        emit stateChanged();
        return true;
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelection();
        return true;
    }
    // Typing a number before the first click: the first point as X, Y (no mouse needed).
    static const std::set<Tool> drawing {Tool::Line,   Tool::Rectangle, Tool::CenterRectangle, Tool::Circle,
                                         Tool::Arc,    Tool::Arc3,      Tool::Slot,            Tool::Polygon,
                                         Tool::Ellipse, Tool::Spline,   Tool::Circle2,         Tool::Circle3,
                                         Tool::Rect3,  Tool::Point,     Tool::Text};
    if (!first_ && !startInput_ && splinePts_.empty() && drawing.count(tool_) && !e->text().isEmpty() &&
        (e->text()[0].isDigit() || e->text()[0] == '.' || e->text()[0] == '-')) {
        startInput_ = true;
        showInput({tr("X"), tr("Y")}, {e->text(), "0"});
        inputFields_[0]->setFocus();
        inputFields_[0]->deselect();
        inputFields_[0]->setCursorPosition(int(e->text().size()));
        inputBox_->move(QPoint(24, 24));
        message_ = tr("Type where the first point goes (X, Tab, Y) and press Enter.");
        emit stateChanged();
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

bool SketchEditor::applyCollinear()
{
    std::vector<int> lines;
    for (int id : selection_) {
        if (const auto* e = work_.entity(id); e && e->type == jig::EntityType::Line) {
            lines.push_back(id);
        }
    }
    if (lines.size() < 2) {
        message_ = tr("Select two or more lines to put on one line.");
        emit stateChanged();
        return false;
    }
    jig::Sketch s = work_;
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto* e = s.entity(lines[i]);
        s.addConstraint(jig::ConstraintType::PointOnCurve, e->p1, lines[0]);
        s.addConstraint(jig::ConstraintType::PointOnCurve, e->p2, lines[0]);
    }
    return commit(tr("Collinear"), s);
}

bool SketchEditor::applyConcentric()
{
    std::vector<int> rounds;
    for (int id : selection_) {
        if (const auto* e = work_.entity(id);
            e && (e->type == jig::EntityType::Circle || e->type == jig::EntityType::Arc)) {
            rounds.push_back(id);
        }
    }
    if (rounds.size() < 2) {
        message_ = tr("Select two or more circles or arcs to share one centre.");
        emit stateChanged();
        return false;
    }
    jig::Sketch s = work_;
    for (size_t i = 1; i < rounds.size(); ++i) {
        s.addConstraint(jig::ConstraintType::Coincident, s.entity(rounds[i])->center, s.entity(rounds[0])->center);
    }
    return commit(tr("Concentric"), s);
}

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
    if (selection_.empty() && selectedConstraints_.empty()) {
        return;
    }
    jig::Sketch s = work_;
    for (int cid : selectedConstraints_) {
        s.removeConstraint(cid);
    }
    selectedConstraints_.clear();
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
    for (auto& pts : textEdges_) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            ov.lines.push_back({pts[i], vc.text});
            ov.lines.push_back({pts[i + 1], vc.text});
        }
    }
    for (int id : offsetChain_.empty() ? offsetHover_ : offsetChain_) {
        if (const jig::SketchEntity* e = s.entity(id)) {
            addPoly(polyline(s, *e), kAccent, offsetChain_.empty());
        }
    }
    if ((tool_ == Tool::Trim || tool_ == Tool::Break) && trimHover_) {
        const jig::SketchEntity* e = s.entity(trimHover_->entity);
        if (e) {
            std::vector<QPointF> pts;
            if (trimHover_->whole) {
                pts = polyline(s, *e);
            }
            else if (e->type == jig::EntityType::Line) {
                auto [ax, ay] = s.pointXY(e->p1);
                auto [bx, by] = s.pointXY(e->p2);
                for (double u : {trimHover_->from, trimHover_->to}) {
                    pts.emplace_back(ax + (bx - ax) * u, ay + (by - ay) * u);
                }
            }
            else {
                auto [cx, cy] = s.pointXY(e->center);
                double a0 = trimHover_->from, a1 = trimHover_->to;
                while (a1 <= a0) {
                    a1 += 2 * kPi;
                }
                for (int k = 0; k <= 48; ++k) {
                    const double t = a0 + (a1 - a0) * k / 48;
                    pts.emplace_back(cx + e->r * std::cos(t), cy + e->r * std::sin(t));
                }
            }
            addPoly(pts, kGap, true);  // dashed red: this piece will go
        }
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
        else if (tool_ == Tool::TangentArc) {
            if (auto ta = tangentArc(a, tangentDir_, b)) {
                jig::Sketch tmp;
                addPoly(polyline(tmp, *tmp.entity(tmp.addArc(ta->cx, ta->cy, ta->r, ta->a0, ta->a1))), kAccent, false);
            }
            else {
                addPoly({a, b}, kAccent, true);
            }
        }
        else if (tool_ == Tool::Circle2) {
            jig::Sketch tmp;
            const QPointF mid = (a + b) / 2;
            const double r = QLineF(a, b).length() / 2;
            if (r > 1e-9) {
                addPoly(polyline(tmp, *tmp.entity(tmp.addCircle(mid.x(), mid.y(), r))), kAccent, false);
            }
        }
        else if (tool_ == Tool::Circle3) {
            if (!second_) {
                addPoly({a, b}, kAccent, true);
            }
            else if (auto ci = circleThrough(a, second_->local, b)) {
                jig::Sketch tmp;
                addPoly(polyline(tmp, *tmp.entity(tmp.addCircle(ci->cx, ci->cy, ci->r))), kAccent, false);
            }
        }
        else if (tool_ == Tool::Rect3) {
            if (!second_) {
                addPoly({a, b}, kAccent, false);
            }
            else {
                const QPointF B = second_->local;
                const double L = QLineF(a, B).length();
                if (L > 1e-9) {
                    const QPointF u = (B - a) / L, n(-u.y(), u.x());
                    const double H = QPointF::dotProduct(b - B, n);
                    addPoly({a, B, B + n * H, a + n * H, a}, kAccent, false);
                }
            }
        }
        else if (tool_ == Tool::Spline && !splinePts_.empty()) {
            jig::Sketch tmp;
            std::vector<std::pair<double, double>> pts;
            for (auto& q : splinePts_) {
                pts.emplace_back(q.local.x(), q.local.y());
            }
            pts.emplace_back(b.x(), b.y());
            const int c = tmp.addSpline(pts, false);
            addPoly(polyline(tmp, *tmp.entity(c)), kAccent, false);
        }
        else if (tool_ == Tool::Ellipse) {
            if (!second_) {
                addPoly({a, b}, kAccent, true);
            }
            else {
                jig::Sketch tmp;
                const QPointF ax = second_->local;
                const double L = QLineF(a, ax).length();
                if (L > 1e-9) {
                    const QPointF u = (ax - a) / L;
                    const double minor = std::abs(QPointF::dotProduct(b - a, QPointF(-u.y(), u.x())));
                    if (minor > 1e-9) {
                        addPoly(polyline(tmp, *tmp.entity(tmp.addEllipse(a.x(), a.y(), ax.x(), ax.y(), minor))), kAccent, false);
                    }
                }
            }
        }
        else if (tool_ == Tool::Arc3) {
            if (!second_) {
                addPoly({a, b}, kAccent, true);
            }
            else {
                const QPointF e = second_->local, m = b;
                const double d = 2 * (a.x() * (e.y() - m.y()) + e.x() * (m.y() - a.y()) + m.x() * (a.y() - e.y()));
                if (std::abs(d) > 1e-9) {
                    const double a2 = a.x() * a.x() + a.y() * a.y(), e2 = e.x() * e.x() + e.y() * e.y(),
                                 m2 = m.x() * m.x() + m.y() * m.y();
                    const double cx = (a2 * (e.y() - m.y()) + e2 * (m.y() - a.y()) + m2 * (a.y() - e.y())) / d;
                    const double cy = (a2 * (m.x() - e.x()) + e2 * (a.x() - m.x()) + m2 * (e.x() - a.x())) / d;
                    const double R = std::hypot(a.x() - cx, a.y() - cy);
                    auto ang = [&](QPointF q) { return std::atan2(q.y() - cy, q.x() - cx); };
                    auto ccwFrom = [](double from, double to) {
                        double v = std::fmod(to - from, 2 * kPi);
                        return v < 0 ? v + 2 * kPi : v;
                    };
                    const bool startFirst = ccwFrom(ang(a), ang(m)) < ccwFrom(ang(a), ang(e));
                    const double s0 = startFirst ? ang(a) : ang(e);
                    const double sw = startFirst ? ccwFrom(ang(a), ang(e)) : ccwFrom(ang(e), ang(a));
                    std::vector<QPointF> pts;
                    for (int k = 0; k <= 48; ++k) {
                        const double t = s0 + sw * k / 48;
                        pts.emplace_back(cx + R * std::cos(t), cy + R * std::sin(t));
                    }
                    addPoly(pts, kAccent, false);
                }
            }
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
    auto len = [this](double mm) { return QString::fromStdString(doc_.fmtLength(mm)); };
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
            owner.insert(e.pts.begin(), e.pts.end());
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
        glyphBoxes_.clear();
        auto num = [this](double mm) { return QString::fromStdString(jig::formatLength(mm, doc_.units(), false)); };
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
                    g = q ? num(q.value()) : "?";
                    if (k.b) {
                        at = k.b;
                    }
                    break;
                }
                case CT::Diameter: { auto q = doc_.evalLength(k.expr); g = "Ø" + (q ? num(q.value()) : "?"); break; }
                case CT::Radius: { auto q = doc_.evalLength(k.expr); g = "R" + (q ? num(q.value()) : "?"); break; }
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
            glyphBoxes_.emplace_back(cid, box);
            const bool picked = selectedConstraints_.count(cid) > 0;
            p.setPen(picked ? QPen(vc.selected, 2) : QPen(Qt::NoPen));  // selected: outlined, not just coloured
            QColor bg = picked ? vc.selected : vc.background;
            bg.setAlpha(picked ? 90 : 220);
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
                p.drawText(c + QPointF(10, -8), tr("gap %1").arg(len(g.gap)));
            }
        }
    }

    // Selection box: solid for "wholly inside", dashed for "anything it touches"
    if (boxing_) {
        const bool crossing = boxEnd_.x() < pressScreen_.x();
        const QRectF r = QRectF(pressScreen_, boxEnd_).normalized();
        p.setPen(QPen(kAccent, 1.2, crossing ? Qt::DashLine : Qt::SolidLine));
        QColor fill = kAccent;
        fill.setAlpha(28);
        p.setBrush(fill);
        p.drawRect(r);
        p.setPen(vc.text);
        p.drawText(r.bottomLeft() + QPointF(4, 14), crossing ? tr("touching") : tr("inside"));
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
                label += tr("%1  %2°").arg(len(std::hypot(d.x(), d.y()))).arg(fmt(std::atan2(d.y(), d.x()) * 180 / kPi));
            }
            else if (tool_ == Tool::Rectangle) {
                label += tr("%1 × %2").arg(len(std::abs(d.x()))).arg(len(std::abs(d.y())));
            }
            else if (tool_ == Tool::CenterRectangle) {
                label += tr("%1 × %2").arg(len(2 * std::abs(d.x()))).arg(len(2 * std::abs(d.y())));
            }
            else if (tool_ == Tool::Circle || tool_ == Tool::Polygon) {
                label += tr("Ø %1").arg(len(2 * std::hypot(d.x(), d.y())));
            }
            else if (tool_ == Tool::Arc) {
                if (!second_) {
                    label += tr("R %1").arg(len(std::hypot(d.x(), d.y())));
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
                    label += len(std::hypot(d.x(), d.y()));
                }
                else {
                    const QPointF c2 = second_->local;
                    const QLineF axis(first_->local, c2);
                    if (axis.length() > 1e-9) {
                        const QPointF n(-(c2.y() - first_->local.y()) / axis.length(),
                                        (c2.x() - first_->local.x()) / axis.length());
                        label += tr("width %1").arg(len(2 * std::abs(QPointF::dotProduct(d, n))));
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
