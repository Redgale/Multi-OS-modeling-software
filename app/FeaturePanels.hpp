// SPDX-License-Identifier: GPL-3.0-or-later
// Feature panels (milestone M2): non-modal panels in the side dock with a live
// preview in the 3D view and OK / Cancel. Nothing changes until OK; Cancel and Esc
// leave the document exactly as it was (Easy Contract principle 2). Values are
// remembered per command for the session.
#pragma once

#include <set>
#include <string>

#include <QDialog>
#include <QPointer>
#include <QWidget>
#include <QVector3D>

#include <optional>

#include "jig/document.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace jigapp
{

class Viewport;

// A numeric field that accepts units, expressions and parameter names, and shows
// the evaluated value (or a plain-language error) right under it.
class ExprField : public QWidget
{
    Q_OBJECT
public:
    ExprField(jig::Document& doc, jig::Dim dim, QWidget* parent = nullptr);
    QString text() const;
    void setText(const QString& t);
    bool valid() const;
    double value() const;  // base units (mm, rad)
    QLineEdit* edit() const
    {
        return edit_;
    }

signals:
    void changed();

private:
    void evaluate();
    jig::Document& doc_;
    jig::Dim dim_;
    QLineEdit* edit_;
    QLabel* result_;
    bool valid_ = false;
    double value_ = 0;
};

// Gives every input under `root` the text of the label shown just before it (or the label
// that names it as its buddy) as its accessible name, so screen readers say what it is.
void nameInputsFromLabels(QWidget* root);

// Panels remember the values last used in this session (a fillet radius, a hole type);
// this forgets them (each UI test starts fresh).
void forgetRememberedValues();

// Anything shown in the side dock while a tool runs (feature panels, Measure).
class ToolPanel : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual void cancel() = 0;

signals:
    void finished(bool committed);
};

class FeaturePanel : public ToolPanel
{
    Q_OBJECT
public:
    FeaturePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent);
    ~FeaturePanel() override;
    const std::string& editId() const
    {
        return editId_;
    }
    // OK/Cancel from outside (keyboard shortcuts, UI tests)
    bool accept();
    void cancel() override;
    QString statusText() const;
    bool previewOk() const
    {
        return previewOk_;
    }

protected:
    // Enter presses OK (when the preview is good), Esc cancels: no mouse needed.
    void keyPressEvent(QKeyEvent* e) override;
    virtual jig::Feature draft() const = 0;
    virtual QString title() const = 0;
    virtual void rememberValues() {}
    // Called after each preview, e.g. to draw a construction plane that has no body.
    virtual void previewed(const jig::FeatureOutcome&) {}
    void schedulePreview();
    void addRow(const QString& label, QWidget* w);
    // Which field holds a step parameter, so a one-click fix can set it.
    void bindParam(const std::string& param, QWidget* field)
    {
        paramFields_[param] = field;
    }
    int insertIndex() const;
    std::shared_ptr<const jig::ModelState> stateBefore() const;

    jig::Document& doc_;
    QPointer<Viewport> view_;  // may be gone first when the window closes
    std::string editId_;
    QVBoxLayout* form_;
    QLabel* status_;
    QWidget* fixes_;
    std::map<std::string, QWidget*> paramFields_;
    QPushButton* ok_;
    QTimer* timer_;
    bool previewOk_ = false;
    bool done_ = false;

private:
    void runPreview();
};

class ExtrudeHandle;

class ExtrudePanel : public FeaturePanel
{
    Q_OBJECT
public:
    ExtrudePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~ExtrudePanel() override;
    // The arrow on the canvas: where its tip is (screen), and dragging it to a screen point.
    std::optional<QPointF> handleTip() const;
    void dragHandleTo(const QPointF& screen);
    ExprField* distance() const
    {
        return distance_;
    }
    QComboBox* operation() const
    {
        return op_;
    }
    QComboBox* direction() const
    {
        return direction_;
    }
    ExprField* distance2() const
    {
        return distance2_;
    }
    ExprField* taper() const
    {
        return taper_;
    }
    QCheckBox* thinWall() const
    {
        return thinOn_;
    }
    ExprField* thickness() const
    {
        return thin_;
    }
    QComboBox* extent() const
    {
        return extent_;
    }
    QComboBox* upTo() const
    {
        return to_;
    }
    void useFace(const std::string& body, const std::string& name);  // "up to" this face

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void rememberValues() override;

private:
    void fillProfiles();
    void updateVisibility();
    QString autoOperation() const;
    QComboBox *sketch_, *extent_, *direction_, *op_, *thinSide_, *to_;
    ExprField* toOffset_;
    void previewed(const jig::FeatureOutcome& o) override;
    friend class ExtrudeHandle;
    std::unique_ptr<ExtrudeHandle> handle_;
    std::optional<QVector3D> handleBase_;  // the arrow starts at the profile's centre
    QVector3D handleDir_;                  // along the extrude direction
    QListWidget* profiles_;
    ExprField *distance_, *distance2_, *taper_, *thin_;
    QCheckBox* thinOn_;
};

// Fillet (round) and Chamfer (bevel) share this panel: pick edges, type one size.
class FilletPanel : public FeaturePanel
{
    Q_OBJECT
public:
    FilletPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr,
                bool chamfer = false);
    ~FilletPanel() override;
    // Add/remove an edge of the body as it was before this step (and its tangent chain).
    void toggleEdge(const std::string& body, int edge);
    // A click in the view, by persistent edge name (works on the preview body too).
    void toggleEdgeByName(const std::string& body, const std::string& name);
    // a selection box: add every edge (Ctrl held: take them away)
    void addEdgesByName(const std::string& body, const QStringList& names, bool remove);
    ExprField* radius() const
    {
        return radius_;
    }
    size_t edgeCount() const
    {
        return edges_.size();
    }
    QComboBox* type() const
    {
        return type_;
    }
    ExprField* second() const
    {
        return second_;
    }
    ExprField* angle() const
    {
        return angle_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void rememberValues() override;

private:
    void refreshEdges();
    void updateVisibility();
    bool chamfer_ = false;
    std::string body_;
    std::set<int> edges_;
    ExprField* radius_;
    QComboBox* type_;
    ExprField *second_, *angle_;
    QCheckBox* flip_;
    QCheckBox* chain_;
    QLabel* count_;
};

// Shell: hollow a body; click the faces to leave open.
class ShellPanel : public FeaturePanel
{
    Q_OBJECT
public:
    ShellPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~ShellPanel() override;
    void toggleFace(const std::string& body, int face);       // face of the body before this step
    void toggleFaceByName(const std::string& body, const std::string& name);  // a click in the view
    ExprField* thickness() const
    {
        return thickness_;
    }
    size_t faceCount() const
    {
        return faces_.size();
    }
    QComboBox* direction() const
    {
        return direction_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void rememberValues() override;

private:
    void refreshFaces();
    std::string body_;
    std::set<int> faces_;
    ExprField* thickness_;
    QComboBox* direction_;
    QLabel* count_;
};

// Revolve: spin closed areas of a sketch around a sketch line or an origin axis.
class RevolvePanel : public FeaturePanel
{
    Q_OBJECT
public:
    RevolvePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ExprField* angle() const
    {
        return angle_;
    }
    QComboBox* axis() const
    {
        return axis_;
    }
    QComboBox* extent() const
    {
        return extent_;
    }
    // a click on a flat face: turn up to it
    void useFace(const std::string& body, const std::string& name);

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    void fillFromSketch();
    void updateVisibility();
    QComboBox *sketch_, *axis_, *op_, *extent_, *to_;
    QListWidget* profiles_;
    ExprField* angle_;
};

// Hole: plain, counterbored or countersunk holes at the points of a sketch.
class HolePanel : public FeaturePanel
{
    Q_OBJECT
public:
    HolePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ExprField* diameter() const
    {
        return diameter_;
    }
    QComboBox* type() const
    {
        return type_;
    }
    QComboBox* size() const
    {
        return size_;
    }
    QComboBox* fit() const
    {
        return fit_;
    }
    QComboBox* bottom() const
    {
        return bottom_;
    }
    ExprField* cboreDiameter() const
    {
        return cboreDiameter_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void rememberValues() override;

private:
    void updateVisibility();
    void fillFromSize();  // a screw size fills (and locks) the sizes it decides
    QComboBox *sketch_, *type_, *extent_, *size_, *fit_, *bottom_;
    ExprField *diameter_, *depth_, *cboreDiameter_, *cboreDepth_, *csinkDiameter_, *csinkAngle_;
    QLabel* points_;
};

// Press pull: click a flat face, type how far to pull it out (or push it in, negative).
class PressPullPanel : public FeaturePanel
{
    Q_OBJECT
public:
    PressPullPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~PressPullPanel() override;
    // a face click: true when taken (flat), false when another step should handle it
    bool useFace(const std::string& body, const std::string& name);
    ExprField* distance() const
    {
        return distance_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    QLabel* face_;
    nlohmann::json faceRef_;
    ExprField* distance_;
};

// Construction axis: along a straight edge, through a round edge or face, or where two
// planes meet.
class AxisPanel : public FeaturePanel
{
    Q_OBJECT
public:
    AxisPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~AxisPanel() override;
    void useFace(const std::string& body, const std::string& name);  // a click in the view
    void useEdge(const std::string& body, const std::string& name);
    QComboBox* kind() const
    {
        return kind_;
    }
    QComboBox* through() const
    {
        return through_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void previewed(const jig::FeatureOutcome& o) override;

private:
    void updateVisibility();
    QComboBox *kind_, *through_, *base_, *base2_, *from_, *to_, *at_;
    bool nextIsSecond_ = false;
};

// Construction point: typed coordinates, an end, the middle or the centre of an edge, or
// the centre of a face.
class PointPanel : public FeaturePanel
{
    Q_OBJECT
public:
    PointPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~PointPanel() override;
    void useFace(const std::string& body, const std::string& name);  // a click in the view
    void useEdge(const std::string& body, const std::string& name);
    QComboBox* kind() const
    {
        return kind_;
    }
    QComboBox* end() const
    {
        return end_;
    }
    ExprField* coord(int i) const
    {
        return xyz_[i];
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void previewed(const jig::FeatureOutcome& o) override;

private:
    void updateVisibility();
    QComboBox *kind_, *through_, *end_, *plane_;
    ExprField* xyz_[3];
};

// Pattern: repeat an Extrude, Revolve or Hole around an axis or in a grid.
class PatternPanel : public FeaturePanel
{
    Q_OBJECT
public:
    PatternPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr,
                 bool circular = true);
    QComboBox* source() const
    {
        return source_;
    }
    ExprField* count() const
    {
        return count_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    void updateVisibility();
    QComboBox *source_, *kind_, *axis_, *dir1_, *dir2_, *bodyOp_;
    ExprField *count_, *angle_, *count1_, *spacing1_, *count2_, *spacing2_;
    QWidget *circular_, *grid_;
};

// Mirror: copy a body across an origin plane.
class MirrorPanel : public FeaturePanel
{
    Q_OBJECT
public:
    MirrorPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~MirrorPanel() override;
    QComboBox* plane() const
    {
        return plane_;
    }
    QComboBox* what() const
    {
        return body_;
    }
    void useFace(const std::string& body, const std::string& name);  // a flat face to mirror across

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    QComboBox *body_, *plane_, *op_;
};

// Construction plane: offset from a plane or flat face, turned around an axis or straight
// edge, or halfway between two parallel faces. Click faces and edges in the view.
class PlanePanel : public FeaturePanel
{
    Q_OBJECT
public:
    PlanePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr,
               const std::string& kind = "offset");
    ~PlanePanel() override;
    void useFace(const std::string& body, const std::string& name);  // a click in the view
    void useEdge(const std::string& body, const std::string& name);
    QComboBox* kind() const
    {
        return kind_;
    }
    QComboBox* base() const
    {
        return base_;
    }
    QComboBox* base2() const
    {
        return base2_;
    }
    ExprField* distance() const
    {
        return distance_;
    }
    ExprField* angle() const
    {
        return angle_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void previewed(const jig::FeatureOutcome& o) override;

private:
    void updateVisibility();
    QComboBox *kind_, *base_, *base2_, *axis_;
    ExprField *distance_, *angle_;
    bool nextIsSecond_ = false;  // midplane: the next clicked face fills the second plane
};

// Combine: join, cut or intersect bodies into a target body.
class CombinePanel : public FeaturePanel
{
    Q_OBJECT
public:
    CombinePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    QComboBox* target() const
    {
        return target_;
    }
    QListWidget* tools() const
    {
        return tools_;
    }
    QComboBox* operation() const
    {
        return op_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    void fillTools();
    QComboBox *target_, *op_;
    QListWidget* tools_;
    QCheckBox* keep_;
};

// Split body: cut a body in two with a plane (origin, construction plane or flat face).
class SplitPanel : public FeaturePanel
{
    Q_OBJECT
public:
    SplitPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~SplitPanel() override;
    void useFace(const std::string& body, const std::string& name);
    QComboBox* plane() const
    {
        return plane_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    QComboBox *body_, *plane_;
};

// Move / copy bodies: shift along X, Y, Z and turn around an origin axis.
class MovePanel : public FeaturePanel
{
    Q_OBJECT
public:
    MovePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    QListWidget* bodies() const
    {
        return bodies_;
    }
    ExprField* dx() const
    {
        return d_[0];
    }
    ExprField* dy() const
    {
        return d_[1];
    }
    ExprField* dz() const
    {
        return d_[2];
    }
    ExprField* angle() const
    {
        return angle_;
    }
    QCheckBox* copy() const
    {
        return copy_;
    }
    QComboBox* fromPoint() const
    {
        return from_;
    }
    QComboBox* toPoint() const
    {
        return to_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;

private:
    QListWidget* bodies_;
    ExprField* d_[3];
    QComboBox *axis_, *from_, *to_;
    ExprField* angle_;
    QCheckBox* copy_;
};

// Modify ▸ Parameters: the named values (width, wall, clearance) that drive the design.
class ParametersDialog : public QDialog
{
    Q_OBJECT
public:
    ParametersDialog(jig::Document& doc, QWidget* parent = nullptr);
    // Applies the table as one undoable change. Returns false (and explains) if a
    // name is invalid or a value does not work.
    bool apply();

private:
    void addRow(const jig::Parameter& p);
    void revalidate();
    jig::Document& doc_;
    QTableWidget* table_;
    QLabel* note_;
};

}  // namespace jigapp
