// SPDX-License-Identifier: GPL-3.0-or-later
// Interactive sketch editing (milestone M1). Works on a copy of one sketch feature's
// geometry; every completed action (a line, a rectangle, a drag, a dimension) is
// solved locally first and committed as one undoable transaction. Actions the solver
// would reject (conflicting constraints) are refused with an explanation instead of
// breaking the sketch.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QPointer>
#include <QString>
#include <gp_Ax3.hxx>

#include "Viewport.hpp"
#include "jig/document.hpp"

class QFrame;
class QLabel;
class QLineEdit;

namespace jigapp
{

class SketchEditor : public QObject, public Interactor
{
    Q_OBJECT
public:
    enum class Tool
    {
        Select,
        Line,
        Rectangle,
        CenterRectangle,
        Circle,
        Arc,      // centre, start, end (counter-clockwise)
        Arc3,     // start, end, then a point the arc passes through
        TangentArc,  // the loose end of a line or arc, then where the arc ends
        Circle2,  // the two ends of a diameter
        Circle3,  // three points on the circle
        Rect3,    // one side (two corners), then the depth
        Slot,     // centre, centre, width
        Ellipse,  // centre, end of the major axis, then the minor radius
        Spline,   // click the points it passes through; Enter, Esc or double-click ends, first point closes
        Polygon,  // centre, corner; sides typed (default 6)
        Text,     // click the centre, type the words and the size
        Trim,     // click the piece of a curve to remove (between its crossings)
        Extend,   // click near a loose end to lengthen it to the next curve
        Break,    // click a curve to split it where others cross it
        Mirror,   // with curves selected, click the line to mirror them across
        Move,     // with curves selected: click a base point and where it goes (Ctrl: copy)
        PatternRect,  // with curves selected: type columns and spacing, then rows and spacing
        PatternCirc,  // with curves selected: click the centre, type the count and angle
        Offset,   // click a curve (or a closed outline), type the distance
        Round,    // click a corner where two lines meet, type the radius
        Project,  // click a body edge (or a face for its outline) to bring it into the sketch
        Point,
        Dimension
    };

    SketchEditor(Viewport* view, jig::Document& doc, std::string featureId, QObject* parent = nullptr);
    ~SketchEditor() override;

    const std::string& featureId() const
    {
        return id_;
    }
    void setTool(Tool t);
    Tool tool() const
    {
        return tool_;
    }
    // Constraint buttons act on the current selection.
    bool applyConstraint(jig::ConstraintType t);
    // Two lines on one infinite line (both ends of the second lie on the first); circles
    // and arcs that share a centre. Made from point-on-curve and coincident constraints.
    bool applyCollinear();
    bool applyConcentric();
    void toggleConstruction();
    void deleteSelection();
    void lookAt();
    // Re-read the sketch after undo/redo or a parameter change. Returns false when the
    // sketch feature no longer exists.
    bool refreshFromDocument();

    struct Options
    {
        bool snapGrid = true;
        bool showConstraints = true;
        bool showProfiles = true;
        bool showGaps = true;
    } options;
    void setOptions(const Options& o);

    const jig::SolveReport& report() const
    {
        return report_;
    }
    QString message() const
    {
        return message_;
    }
    QString toolHint() const;
    const std::set<int>& selection() const
    {
        return selection_;
    }
    const jig::Sketch& sketch() const
    {
        return work_;
    }

    // ---- for UI tests: drive the editor in sketch coordinates
    QPoint screenOf(double x, double y) const;
    // Curves and points in a screen box: wholly inside, or (crossing) any part inside.
    std::set<int> entitiesInBox(const QRectF& box, bool crossing) const;
    void typeValues(const QStringList& values);  // as if typed into the value box + Enter
    bool editTextAt(int anchorPoint);            // opens the value box on an existing text
    bool editArtworkAt(int anchorPoint);         // … or on imported artwork (its width)
    // Constraint glyphs and dimension labels: where they are drawn (after a paint), and
    // selecting / editing them as a click or double-click would.
    std::optional<QPointF> glyphCenter(int constraintId) const;
    void selectConstraint(int constraintId, bool add = false);
    bool editDimension(int constraintId);
    const std::set<int>& selectedConstraints() const
    {
        return selectedConstraints_;
    }

    // Interactor
    bool mousePress(QMouseEvent* e) override;
    bool mouseMove(QMouseEvent* e) override;
    bool mouseRelease(QMouseEvent* e) override;
    bool mouseDoubleClick(QMouseEvent* e) override;
    bool key(QKeyEvent* e) override;
    void paint(QPainter& p) override;

signals:
    void committed();         // the document changed
    void stateChanged();      // DOF, message, tool or selection changed
    void finishRequested();

private:
    struct Snap
    {
        QPointF local;
        int point = 0;           // existing point snapped to, 0 = none
        bool horizontal = false;  // inferred from the tool's first point
        bool vertical = false;
    };

    QVector3D toWorld(double x, double y) const;
    std::optional<QPointF> toLocal(const QPointF& screen) const;
    Snap snapAt(const QPointF& screen, bool allowInference, Qt::KeyboardModifiers mods) const;
    int pointNear(const QPointF& screen, int exclude = 0) const;
    int entityNear(const QPointF& screen) const;  // curves and points
    double gridStep() const;
    bool commit(const QString& label, const jig::Sketch& next);
    void rebuildOverlay();
    void resetToolState();
    void placeSecond(const Snap& s, const QStringList& typed);
    void showInput(const QStringList& labels, const QStringList& prefill = {});
    void hideInput();
    QStringList inputValues() const;
    QString inUnits(double mm) const;
    std::vector<QPointF> polyline(const jig::Sketch& s, const jig::SketchEntity& e) const;
    const jig::Sketch& shown() const
    {
        return dragging_ ? dragSketch_ : work_;
    }

    Viewport* view_;
    jig::Document& doc_;
    std::string id_;
    gp_Ax3 frame_;
    jig::Sketch work_, dragSketch_;
    jig::SolveReport report_;
    QString message_;
    Tool tool_ = Tool::Select;

    // tool state
    std::optional<Snap> first_;      // first click of a two-click tool
    std::optional<Snap> second_;     // second click of a three-click tool (arc, slot)
    QString arcRadiusExpr_;          // typed at the second step (arc radius, slot length)
    int editText_ = 0;
    int editArtwork_ = 0;
    std::optional<jig::Sketch::TrimPiece> trimHover_;
    std::vector<Snap> splinePts_;
    int tangentFrom_ = 0;   // Tangent arc: the curve it continues
    QPointF tangentDir_;    // and the direction it leaves in  // Spline: the points clicked so far
    void finishSpline(bool closed);
    int gridCols_ = 0;     // Grid copy: columns typed in the first step
    double gridDx_ = 0;    // and their spacing  // the piece Trim would remove
    std::vector<int> offsetHover_, offsetChain_;        // Offset: under the cursor / chosen
    std::vector<std::pair<int, QRectF>> glyphBoxes_;     // constraint glyphs as last painted
    std::set<int> selectedConstraints_;
    int editConstraint_ = 0;
    std::optional<QPointF> roundAt_;                     // Round: the corner clicked                             // dimension being changed (double-click)
    int glyphAt(const QPointF& screen) const;
    QString lastTextHeight_;  // empty: the design's default size               // text being changed (double-click on its anchor)
    std::vector<std::vector<QVector3D>> textEdges_;  // glyph outlines, for drawing
    int chainStart_ = 0;             // line chains: the first point, clicking it closes
    Snap cursor_;
    bool cursorValid_ = false;
    int hover_ = 0;
    std::set<int> selection_;
    int dimFirst_ = 0;               // dimension tool: first picked entity
    int dimTarget_ = 0, dimSecond_ = 0;

    // dragging
    bool pressed_ = false, dragging_ = false;
    QPointF pressScreen_;
    bool boxing_ = false;  // Select: dragging a selection box from empty space
    QPointF boxEnd_;
    int dragPoint_ = 0, dragPoint2_ = 0;
    QPointF dragOffset2_;
    QPointF dragStartLocal_;

    // value box
    QPointer<QFrame> inputBox_;  // owned by the viewport
    std::vector<QLabel*> inputLabels_;
    std::vector<QLineEdit*> inputFields_;

    std::vector<std::vector<QVector3D>> profileTris_;
};

}  // namespace jigapp
