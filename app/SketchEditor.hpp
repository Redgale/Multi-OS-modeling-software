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
    void typeValues(const QStringList& values);  // as if typed into the value box + Enter

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
