// SPDX-License-Identifier: GPL-3.0-or-later
#include "Icons.hpp"

#include <map>

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QStringList>

namespace jigapp
{
namespace
{

// Drawing language, one token stream per icon (24x24 grid, y down):
//   M x y   L x y   Q cx cy x y   C c1x c1y c2x c2y x y   Z      path commands
//   A cx cy r start span     arc from the current point (degrees, counter-clockwise)
//   O cx cy r   E cx cy rx ry   R x y w h                     closed shapes
//   style switches (each starts a new shape):
//   s ink stroke (default)   a accent stroke   f accent fill   F ink fill
//   d dashed ink stroke      t thin ink stroke
// clang-format off
const std::map<QString, QString>& icons()
{
    static const std::map<QString, QString> m = {
        // file / edit / view
        {"file.new", "M6 3 L14 3 L19 8 L19 21 L6 21 Z M14 3 L14 8 L19 8 a M12.5 11 L12.5 17 M9.5 14 L15.5 14"},
        {"file.open", "M3 6 L9 6 L11 8 L21 8 L21 19 L3 19 Z a M8 13 L16 13 M13 10 L16 13 L13 16"},
        {"file.save", "M4 4 L17 4 L20 7 L20 20 L4 20 Z M8 4 L8 9 L15 9 L15 4 f R7 13 10 5"},
        {"file.save_as", "M4 4 L15 4 L18 7 L18 12 M4 4 L4 20 L11 20 M8 4 L8 9 L14 9 L14 4 a M13 21 L14 17 L20 11 L22 13 L16 19 Z"},
        {"file.export_stl", "M3 19 L12 4 L21 19 Z M7.5 11.5 L16.5 11.5 M12 19 L7.5 11.5 M12 19 L16.5 11.5 a M17 3 L21 3 L21 7 M21 3 L15 9"},
        {"file.export_step", "M4 8 L12 4 L20 8 L20 17 L12 21 L4 17 Z M4 8 L12 12 L20 8 M12 12 L12 21 a M16 2 L22 2 L22 8 M22 2 L17 7"},
        {"file.save_version", "M4 4 L15 4 L18 7 L18 20 L4 20 Z M8 4 L8 9 L14 9 L14 4 a O15 16 4 M15 14 L15 16 L16.5 17"},
        {"file.versions", "O12 12 9 a M12 7 L12 12 L15 14 M3 12 L1 10 M3 12 L5 10"},
        {"file.export_3mf", "M4 4 L20 4 L20 15 L4 15 Z M8 15 L8 20 L16 20 L16 15 f R9 9 6 3 a M12 4 L12 9"},
        {"edit.undo", "M9 6 L4 11 L9 16 M4 11 L14 11 Q20 11 20 16 Q20 20 15 20"},
        {"edit.redo", "M15 6 L20 11 L15 16 M20 11 L10 11 Q4 11 4 16 Q4 20 9 20"},
        {"edit.palette", "O10 10 6 M14.5 14.5 L20 20 a M7.5 10 L12.5 10"},
        {"view.fit", "M4 9 L4 4 L9 4 M15 4 L20 4 L20 9 M20 15 L20 20 L15 20 M9 20 L4 20 L4 15 f R9 9 6 6"},
        {"view.home", "M4 11 L12 4 L20 11 M6 9.5 L6 20 L18 20 L18 9.5 a M10 20 L10 14 L14 14 L14 20"},
        {"view.projection", "M5 7 L13 7 L13 15 L5 15 Z M5 7 L10 3 L19 3 L13 7 M19 3 L19 11 L13 15 d M5 15 L10 11 L19 11 M10 11 L10 3"},
        // sketch
        {"sketch.create", "M3 17 L9 11 L21 11 L15 17 Z a M14 3 L18 7 L11 14 L8 15 L9 12 Z"},
        {"sketch.finish", "O12 12 9 a M7.5 12.5 L10.5 15.5 L16.5 9"},
        {"sketch.line", "M5 19 L19 5 f O5 19 2 O19 5 2"},
        {"sketch.rectangle", "M5 7 L19 7 L19 17 L5 17 Z f O5 7 1.6 O19 17 1.6"},
        {"sketch.circle", "O12 12 8 f O12 12 1.6"},
        {"sketch.arc", "M4 17 A12 17 8 180 -180 f O4 17 1.6 O20 17 1.6 O12 17 1.4"},
        {"sketch.arc3", "M4 17 A12 17 8 180 -180 f O4 17 1.6 O20 17 1.6 O12 9 1.6"},
        {"sketch.arc_tangent", "M3 17 L10 17 A10 10 7 270 180 f O10 17 1.6 O10 3 1.6"},
        {"sketch.circle_tangent", "O13 11 6 a M3 5 L21 5 M3 21 L9 3"},
        {"sketch.circle2", "O12 12 8 d M4 12 L20 12 f O4 12 1.6 O20 12 1.6"},
        {"sketch.circle3", "O12 12 8 f O4 12 1.6 O17.7 6.3 1.6 O14.1 19.7 1.6"},
        {"sketch.rect3", "M4 14 L14 4 L20 10 L10 20 Z f O4 14 1.6 O14 4 1.6 O20 10 1.6"},
        {"sketch.polygon", "M12 3 L19.8 7.5 L19.8 16.5 L12 21 L4.2 16.5 L4.2 7.5 Z f O12 12 1.4"},
        {"sketch.ellipse", "E12 12 9 5.5 f O12 12 1.4"},
        {"sketch.slot", "M8 8 L16 8 A16 12 4 90 -180 L8 16 A8 12 4 270 -180 Z f O8 12 1.3 O16 12 1.3"},
        {"sketch.spline", "M3 17 C7 3 12 21 21 6 f O3 17 1.6 O12 12 1.6 O21 6 1.6"},
        {"sketch.point", "M12 5 L12 9 M12 15 L12 19 M5 12 L9 12 M15 12 L19 12 f O12 12 2"},
        {"sketch.text", "M5 5 L19 5 M12 5 L12 20 M9 20 L15 20 a M5 5 L5 7.5 M19 5 L19 7.5"},
        {"sketch.mirror", "M9 6 L3 18 L9 18 Z a M15 6 L21 18 L15 18 Z d M12 3 L12 21"},
        {"sketch.pattern_rect", "R4 4 6 6 R14 4 6 6 R4 14 6 6 f R14 14 6 6"},
        {"sketch.pattern_circ", "t O12 12 7 f O12 5 2 O19 12 2 O12 19 2 O5 12 2"},
        {"sketch.project", "M3 18 L9 13 L21 13 L15 18 Z a M12 3 L12 12 M9 9 L12 12 L15 9"},
        {"sketch.dimension", "M4 7 L4 17 M20 7 L20 17 M4 12 L20 12 M7 9.5 L4 12 L7 14.5 M17 9.5 L20 12 L17 14.5 a M10 9 L14 9"},
        {"sketch.trim", "M3 12 L21 12 a M7 6 L11 10 M7 10 L11 6 d M12 3 L12 21"},
        {"sketch.extend", "M3 12 L13 12 M17 4 L17 20 d M13 12 L17 12 a M11 9 L14 12 L11 15"},
        {"sketch.break", "M3 12 L10 12 M14 12 L21 12 a M10 8 L14 16 f O10 12 1.5 O14 12 1.5"},
        {"sketch.offset", "M4 19 L4 9 Q4 4 9 4 L20 4 a M8 20 L8 12 Q8 8 12 8 L20 8"},
        {"sketch.fillet", "M5 20 L5 12 Q5 5 12 5 L20 5 d M5 12 L5 5 L12 5"},
        {"sketch.move", "M12 3 L12 21 M3 12 L21 12 M9 6 L12 3 L15 6 M9 18 L12 21 L15 18 M6 9 L3 12 L6 15 M18 9 L21 12 L18 15"},
        {"sketch.construction", "d M4 20 L20 4 a M14 20 L20 20"},
        // constraints
        {"constraint.coincident", "M4 20 L12 12 M12 12 L20 16 f O12 12 2.6"},
        {"constraint.collinear", "M3 15 L10 11 M14 9 L21 5 d M10 11 L14 9"},
        {"constraint.concentric", "O12 12 8 O12 12 4 f O12 12 1.4"},
        {"constraint.midpoint", "M3 16 L21 8 f O12 12 2.4 a M9 6 L12 9 L15 6"},
        {"constraint.fix", "R6 11 12 9 M8.5 11 L8.5 8 Q8.5 4 12 4 Q15.5 4 15.5 8 L15.5 11 f O12 15.5 1.6"},
        {"constraint.parallel", "M6 20 L12 4 M12 20 L18 4"},
        {"constraint.perpendicular", "M4 20 L20 20 M10 20 L10 5 a M10 15 L15 15 L15 20"},
        {"constraint.horizontal", "M3 12 L21 12 a M6 6 L6 10 M6 8 L10 8 M10 6 L10 10 M14 14 L16 18 L18 14"},
        {"constraint.tangent", "O10 13 6 M3 7 L21 7 f O10 7 1.6"},
        {"constraint.symmetric", "d M12 3 L12 21 f O6 12 2 O18 12 2 a M8 12 L10 12 M14 12 L16 12"},
        {"constraint.equal", "M5 9 L19 9 M5 15 L19 15"},
        // solid: create
        {"solid.extrude", "M3 18 L8 14 L20 14 L15 18 Z f M8 14 L8 7 L20 7 L20 14 Z a M13 12 L13 3 M10 5.5 L13 2.5 L16 5.5"},
        {"solid.sweep", "M4 18 L4 14 L8 14 L8 18 Z a M6 14 L6 9 A12 9 6 180 -90 L18 3"},
        {"solid.pipe", "M4 21 L4 12 Q4 5 11 5 L20 5 M9 21 L9 12 Q9 10 11 10 L20 10 f E20 7.5 1.5 2.5"},
        {"solid.coil", "E12 4 7 2 E12 9 7 2 E12 14 7 2 E12 19 7 2"},
        {"solid.box", "M4 8 L12 4 L20 8 L20 17 L12 21 L4 17 Z M4 8 L12 12 L20 8 M12 12 L12 21"},
        {"solid.cylinder", "E12 6 7 2.5 M5 6 L5 18 Q12 22 19 18 L19 6"},
        {"solid.sphere", "O12 12 8 a E12 12 8 3"},
        {"solid.torus", "E12 12 9 5 E12 12 4 1.8"},
        {"solid.rib", "M4 20 L20 20 L20 17 L7 17 L7 4 L4 4 Z f M7 17 L7 12 L15 17 Z"},
        {"solid.emboss", "M5 4 L5 20 M19 4 L19 20 E12 4 7 2 a M9 9 L12 15 L15 9"},
        {"solid.draft", "M3 20 L21 20 M7 20 L9 6 L15 6 L17 20 d M12 3 L12 21"},
        {"solid.scale", "R3 13 8 8 a R3 3 18 18 M11 13 L19 5 M14 5 L19 5 L19 10"},
        {"solid.thread", "M8 3 L8 21 M16 3 L16 21 a M8 5 L16 8 M8 9 L16 12 M8 13 L16 16 M8 17 L16 20"},
        {"solid.loft", "M5 19 L19 19 L16 13 L8 13 Z M8 13 L10 5 L14 5 L16 13"},
        {"solid.revolve", "M8 4 L8 20 d M12 3 L12 21 a M17 9 A12 12 5 0 -270 M12 7 L15 6"},
        {"solid.hole", "M3 9 L9 5 L21 5 L15 9 Z M3 9 L3 19 L15 19 L15 9 M15 19 L21 15 L21 5 f E12 7 3 1.2 a M9 9 L9 16 M15 9 L15 16"},
        {"solid.mirror", "M3 7 L9 4 L9 18 L3 19 Z d M12 2 L12 22 a M21 7 L15 4 L15 18 L21 19 Z"},
        {"solid.pattern_rect", "M3 6 L7 4 L11 6 L7 8 Z M13 6 L17 4 L21 6 L17 8 Z M3 16 L7 14 L11 16 L7 18 Z f M13 16 L17 14 L21 16 L17 18 Z"},
        {"solid.pattern_circ", "t O12 12 7.5 f R10 3 4 4 R17 10 4 4 R10 17 4 4 R3 10 4 4"},
        {"solid.presspull", "M4 16 L10 12 L20 12 L14 16 Z a M12 3 L12 10 M9 7 L12 10 L15 7 M17 20 L17 15 M15 17 L17 15 L19 17"},
        {"solid.fillet", "M4 20 L4 4 L20 4 f M4 20 L12 20 Q20 20 20 12 L20 4 L4 4 Z"},
        {"solid.chamfer", "M4 4 L20 4 L20 20 L4 20 Z f M12 20 L20 12 L20 20 Z a M12 20 L20 12"},
        {"solid.shell", "M4 7 L12 3 L20 7 L20 17 L12 21 L4 17 Z f M8 8.5 L12 6.5 L16 8.5 L12 10.5 Z"},
        {"solid.combine", "R3 3 12 12 a R9 9 12 12"},
        {"solid.split", "M4 7 L12 3 L20 7 L20 17 L12 21 L4 17 Z a M2 15 L22 9"},
        {"solid.move", "R8 8 8 8 a M12 2 L12 6 M10 4 L12 2 L14 4 M22 12 L18 12 M20 10 L22 12 L20 14"},
        // construct
        {"view.save_named", "R3 8 18 12 a O12 14 3.5 f R8 5 6 3"},
        {"construct.offset_plane", "M2 19 L7 15 L22 15 L17 19 Z a M2 11 L7 7 L22 7 L17 11 Z t M12 10 L12 16"},
        {"construct.angle_plane", "M2 19 L8 15 L22 15 L16 19 Z a M8 15 L5 5 L19 5 L22 15 t M5 19 A5 19 4 90 -40"},
        {"construct.midplane", "M2 7 L7 4 L22 4 L17 7 Z M2 21 L7 18 L22 18 L17 21 Z a M2 14 L7 11 L22 11 L17 14 Z"},
        {"construct.tangent_plane", "O9 14 6 a M15 4 L15 22"},
        {"construct.points_plane", "M2 18 L8 8 L22 8 L16 18 Z f O6 16 1.8 O11 9 1.8 O18 15 1.8"},
        {"construct.path_plane", "M3 20 Q3 6 21 6 a M7 6 L7 18 L13 14 L13 2 Z"},
        {"construct.axis", "M4 20 L20 4 a M14 4 L20 4 L20 10 M10 20 L4 20 L4 14"},
        {"construct.point", "M3 17 L9 12 L21 12 L15 17 Z f O12 8 2.4 a M12 10 L12 14.5"},
        // inspect / insert / modify / help
        {"inspect.measure", "M3 15 L15 3 L21 9 L9 21 Z M7 11 L9 13 M10 8 L13 11 M13 5 L15 7"},
        {"inspect.section", "M4 8 L12 4 L20 8 L20 17 L12 21 L4 17 Z f M4 12.5 L12 8.5 L20 12.5 L12 16.5 Z"},
        {"inspect.properties", "O12 12 9 M12 11 L12 17 f O12 7.5 1.3"},
        {"insert.step", "M6 3 L14 3 L19 8 L19 21 L6 21 Z a M3 14 L11 14 M8 11 L11 14 L8 17"},
        {"insert.mesh", "M12 3 L20 8 L20 17 L12 21 L4 17 L4 8 Z M4 8 L12 12 L20 8 M12 12 L12 21 M4 8 L20 17 M20 8 L4 17"},
        {"insert.svg", "M6 3 L14 3 L19 8 L19 21 L6 21 Z a M9 17 C9 10 16 16 16 10 f O9 17 1.3 O16 10 1.3"},
        {"insert.dxf", "M6 3 L14 3 L19 8 L19 21 L6 21 Z a M9 11 L16 11 L16 18 L9 18 Z"},
        {"modify.parameters", "M4 7 L20 7 M4 12 L20 12 M4 17 L20 17 f O8 7 2 O15 12 2 O10 17 2"},
        {"help.tutorial", "M2 9 L12 4 L22 9 L12 14 Z M6 11 L6 16 Q12 20 18 16 L18 11 a M22 9 L22 15"},
        // feature kinds (timeline) and statuses
        {"feature.sketch", "M3 17 L9 11 L21 11 L15 17 Z a M14 3 L18 7 L11 14 L8 15 L9 12 Z"},
        {"feature.extrude", "M3 18 L8 14 L20 14 L15 18 Z f M8 14 L8 7 L20 7 L20 14 Z a M13 12 L13 3 M10 5.5 L13 2.5 L16 5.5"},
        {"feature.fillet", "M4 20 L4 4 L20 4 f M4 20 L12 20 Q20 20 20 12 L20 4 L4 4 Z"},
        {"feature.chamfer", "M4 4 L20 4 L20 20 L4 20 Z f M12 20 L20 12 L20 20 Z a M12 20 L20 12"},
        {"feature.shell", "M4 7 L12 3 L20 7 L20 17 L12 21 L4 17 Z f M8 8.5 L12 6.5 L16 8.5 L12 10.5 Z"},
        {"feature.plane", "M2 19 L7 15 L22 15 L17 19 Z a M2 11 L7 7 L22 7 L17 11 Z t M12 10 L12 16"},
        {"feature.combine", "R3 3 12 12 a R9 9 12 12"},
        {"feature.split", "M4 7 L12 3 L20 7 L20 17 L12 21 L4 17 Z a M2 15 L22 9"},
        {"feature.move", "R8 8 8 8 a M12 2 L12 6 M10 4 L12 2 L14 4 M22 12 L18 12 M20 10 L22 12 L20 14"},
        {"feature.revolve", "M8 4 L8 20 d M12 3 L12 21 a M17 9 A12 12 5 0 -270 M12 7 L15 6"},
        {"feature.remove", "M5 7 L19 7 M9 7 L9 4 L15 4 L15 7 M7 7 L8 20 L16 20 L17 7 a M10 10 L10 17 M14 10 L14 17"},
        {"feature.hole", "M3 9 L9 5 L21 5 L15 9 Z M3 9 L3 19 L15 19 L15 9 M15 19 L21 15 L21 5 f E12 7 3 1.2 a M9 9 L9 16 M15 9 L15 16"},
        {"feature.pattern", "t O12 12 7.5 f R10 3 4 4 R17 10 4 4 R10 17 4 4 R3 10 4 4"},
        {"feature.mirror", "M3 7 L9 4 L9 18 L3 19 Z d M12 2 L12 22 a M21 7 L15 4 L15 18 L21 19 Z"},
    };
    return m;
}
// clang-format on

class VectorIconEngine : public QIconEngine
{
public:
    VectorIconEngine(QString spec, QColor ink, QColor accent)
        : spec_(std::move(spec))
        , ink_(std::move(ink))
        , accent_(std::move(accent))
    {}
    QIconEngine* clone() const override
    {
        return new VectorIconEngine(spec_, ink_, accent_);
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft());
        const double s = std::min(rect.width(), rect.height()) / 24.0;
        p->scale(s, s);
        QColor ink = ink_, accent = accent_;
        if (mode == QIcon::Disabled) {
            ink.setAlphaF(0.38);
            accent = ink;
        }
        draw(*p, ink, accent);
        p->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

private:
    void draw(QPainter& p, const QColor& ink, const QColor& accent) const
    {
        // "M6 3 L14 3" -> "M 6 3 L 14 3": split command letters from glued numbers.
        QString norm = spec_;
        norm.replace(QRegularExpression("([MLQCZAOERsafFdt])(?=[-0-9.])"), "\\1 ");
        const QStringList tok = norm.split(' ', Qt::SkipEmptyParts);
        QPainterPath path;
        QChar style = 's';
        auto flush = [&] {
            if (path.isEmpty()) {
                return;
            }
            QPen pen(style == 'a' || style == 'f' ? accent : ink, style == 't' ? 1.1 : 1.7, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            if (style == 'd') {
                pen.setDashPattern({2.0, 1.6});
            }
            p.setPen(pen);
            p.setBrush(style == 'f' ? QBrush(accent) : style == 'F' ? QBrush(ink) : Qt::NoBrush);
            p.drawPath(path);
            path = QPainterPath();
        };
        int i = 0;
        QChar last = 'M';
        auto num = [&] { return i < tok.size() ? tok[i++].toDouble() : 0.0; };
        while (i < tok.size()) {
            const QString t = tok[i];
            QChar c = t[0];
            bool isNumber = c.isDigit() || c == '-' || c == '.';
            if (isNumber) {
                c = last;  // implicit repeat of the previous command ("O 5 19 2 19 5 2")
            }
            else {
                ++i;
            }
            switch (c.toLatin1()) {
                case 's': case 'a': case 'f': case 'F': case 'd': case 't':
                    flush();
                    style = c;
                    break;
                case 'M': { double x = num(), y = num(); path.moveTo(x, y); break; }
                case 'L': { double x = num(), y = num(); path.lineTo(x, y); break; }
                case 'Q': { double a = num(), b = num(), x = num(), y = num(); path.quadTo(a, b, x, y); break; }
                case 'C': {
                    double a = num(), b = num(), c2 = num(), d = num(), x = num(), y = num();
                    path.cubicTo(a, b, c2, d, x, y);
                    break;
                }
                case 'Z': path.closeSubpath(); break;
                case 'A': {
                    double cx = num(), cy = num(), r = num(), st = num(), sp = num();
                    path.arcTo(QRectF(cx - r, cy - r, 2 * r, 2 * r), st, sp);
                    break;
                }
                case 'O': { double cx = num(), cy = num(), r = num(); path.addEllipse(QPointF(cx, cy), r, r); break; }
                case 'E': { double cx = num(), cy = num(), rx = num(), ry = num(); path.addEllipse(QPointF(cx, cy), rx, ry); break; }
                case 'R': { double x = num(), y = num(), w = num(), h = num(); path.addRect(x, y, w, h); break; }
                default:
                    if (isNumber) {
                        ++i;  // stray number: skip it rather than loop forever
                    }
                    break;
            }
            if (!(c == 's' || c == 'a' || c == 'f' || c == 'F' || c == 'd' || c == 't' || c == 'Z')) {
                last = c;
            }
        }
        flush();
    }
    QString spec_;
    QColor ink_, accent_;
};

}  // namespace

namespace
{
// timeline steps without a drawing of their own use the tool that makes them
QString iconKey(const QString& id)
{
    static const std::map<QString, QString> sameAs {{"feature.axis", "construct.axis"},
                                                    {"feature.point", "construct.point"},
                                                    {"feature.presspull", "solid.presspull"},
                                                    {"feature.import", "insert.step"},
                                                    {"feature.move", "solid.move"}};
    auto alias = sameAs.find(id);
    return alias != sameAs.end() && !icons().count(id) ? alias->second : id;
}
}  // namespace

bool hasToolIcon(const QString& id)
{
    return icons().count(iconKey(id)) > 0;
}

QIcon toolIcon(const QString& id, const QColor& ink, const QColor& accent)
{
    auto it = icons().find(iconKey(id));
    const QString spec = it != icons().end() ? it->second : QStringLiteral("f O12 12 3");
    return QIcon(new VectorIconEngine(spec, ink, accent));
}

}  // namespace jigapp
