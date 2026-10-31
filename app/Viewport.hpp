// SPDX-License-Identifier: GPL-3.0-or-later
// The 3D canvas. A small custom renderer on OpenGL 3.3 core fed by OCCT
// tessellation (D-003): shaded faces, edges, ground grid, origin axes, MSAA,
// GPU ID-buffer picking of faces, a clickable view gizmo, and navigation presets.
// Rendering lives behind this widget so a Qt RHI / Metal backend can replace the
// GL path later without touching the rest of the app.
#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>
#include <QVector4D>

#include "jig/kernel.hpp"

class QKeyEvent;
class QOpenGLFramebufferObject;

namespace jigapp
{

enum class NavPreset
{
    Default,     // middle-drag pan, Shift+middle-drag orbit, wheel zoom to cursor
    Blender,     // middle-drag orbit, Shift+middle-drag pan
    SolidWorks,  // middle-drag orbit, Ctrl+middle-drag pan
    Trackpad     // two-finger scroll orbits, Shift+scroll pans, pinch zooms, Alt+drag orbits
};

struct ViewColors
{
    QColor background, backgroundBottom, model, edge, hover, selected, grid, gridMajor, text;
};

struct PickResult
{
    std::string body;
    int face = -1;
};

struct EdgePick
{
    std::string body;
    int edge = -1;
};

enum class PickMode
{
    Faces,  // default: hover/click faces
    Edges,  // fillet and friends: hover/click edges
    Any,    // measure: an edge when the cursor is on one, otherwise a face
    None    // a tool handles all clicks itself
};

// A tool that wants the left mouse button and keys (sketch tools, handles). The
// viewport keeps navigation (middle/right buttons, wheel, gizmo) for itself.
class Interactor
{
public:
    virtual ~Interactor() = default;
    virtual bool mousePress(QMouseEvent*) { return false; }
    virtual bool mouseMove(QMouseEvent*) { return false; }
    virtual bool mouseRelease(QMouseEvent*) { return false; }
    virtual bool mouseDoubleClick(QMouseEvent*) { return false; }
    virtual bool key(QKeyEvent*) { return false; }
    virtual void paint(QPainter&) {}  // 2D overlay in widget coordinates
};

// 3D lines and translucent triangles drawn on top of the model (sketches,
// profiles, planes, handles). Colours carry alpha.
struct OverlayVertex
{
    QVector3D p;
    QColor c;
};
struct SceneOverlay
{
    std::vector<OverlayVertex> lines;  // pairs
    std::vector<OverlayVertex> tris;   // triples
};

// Call once before QApplication is created (app and UI tests): OpenGL 3.3 core,
// 24-bit depth, 4x MSAA, shared contexts.
void configureSurfaceFormat();

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    // Replace what is shown. Tessellation runs on a worker thread; the view updates
    // when it is ready (architecture rule 6).
    struct BodyInput
    {
        std::string id;
        std::shared_ptr<const jig::NamedShape> shape;
    };
    void setBodies(std::vector<BodyInput> bodies);

    void setNavPreset(NavPreset p)
    {
        nav_ = p;
    }
    NavPreset navPreset() const
    {
        return nav_;
    }
    void setColors(const ViewColors& c);
    void setEmptyHint(const QString& text)
    {
        emptyHint_ = text;
        update();
    }
    bool perspective() const
    {
        return perspective_;
    }
    bool meshesReady() const
    {
        return pending_ == 0;
    }
    std::optional<PickResult> selection() const
    {
        return selected_;
    }
    // Selects a face of the first body as if clicked (screenshots and UI tests).
    void selectFaceForScreenshot(int face);

    // ---- tools
    void setInteractor(Interactor* i);
    Interactor* interactor() const
    {
        return interactor_;
    }
    void setOverlay(SceneOverlay o);
    void setPickMode(PickMode m);
    PickMode pickMode() const
    {
        return pickMode_;
    }
    // Section view: hide everything where dot(normal, p) > offset (x, y, z = normal,
    // w = offset). Surfaces seen from inside the material are drawn in a distinct colour.
    void setSection(std::optional<QVector4D> plane);
    std::optional<QVector4D> section() const
    {
        return section_;
    }
    void setUnits(jig::LengthUnit u)  // for hover descriptions
    {
        units_ = u;
    }
    // Construction planes and their names: drawn under any tool overlay.
    void setDatums(SceneOverlay geometry, std::vector<std::pair<QString, QVector3D>> labels);
    void setEdgeHighlight(const std::string& body, std::vector<int> edges);
    void setFaceHighlight(const std::string& body, std::vector<int> faces);
    // Model colour override for previews; nullopt restores the theme colour.
    void setModelTint(std::optional<QColor> c);
    std::optional<PickResult> pickFaceAt(const QPoint& p)
    {
        return pickAt(p);
    }
    std::optional<EdgePick> pickEdgeAt(const QPoint& p);
    // Camera ray through a widget pixel, and the reverse.
    void ray(const QPointF& px, QVector3D& origin, QVector3D& dir) const;
    QPointF project(const QVector3D& world) const;
    float worldPerPixel() const;
    // Look straight at a plane (sketching): normal toward the viewer, `up` on screen up.
    void lookAlong(const QVector3D& normal, const QVector3D& up, const QVector3D& center);
    const ViewColors& colors() const
    {
        return colors_;
    }

public slots:
    void fitAll();
    void homeView();
    void viewTop();
    void viewFront();
    void viewRight();
    void setPerspective(bool on);

signals:
    void hovered(const QString& description);
    void selectionChanged(const QString& bodyId, int faceIndex);
    // Clicks report the persistent name of what is on screen, so tools never have to
    // guess which version of a body (preview or not) an index belongs to.
    void edgeClicked(const QString& bodyId, const QString& edgeName, Qt::KeyboardModifiers mods);
    void faceClicked(const QString& bodyId, const QString& faceName, Qt::KeyboardModifiers mods);
    void meshesUpdated();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool event(QEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

public:
    void paintOverlay(QPainter& p);  // called by the overlay widget

private:
    QWidget* overlay_ = nullptr;

private:
    struct GpuMesh
    {
        std::string body;
        std::shared_ptr<const jig::NamedShape> shape;
        QOpenGLVertexArrayObject vaoTri, vaoEdge;
        QOpenGLBuffer vbo {QOpenGLBuffer::VertexBuffer}, ibo {QOpenGLBuffer::IndexBuffer},
            ebo {QOpenGLBuffer::VertexBuffer};
        int indexCount = 0, edgeVertexCount = 0;
        int pickBase = 0;  // first pick id of this body's faces
        QVector3D bmin, bmax;
        std::vector<float> edgeLines;   // CPU copy for edge picking and highlights
        std::vector<uint32_t> segEdge;
        std::vector<float> positions;   // CPU copy for face highlights
        std::vector<uint32_t> indices, triFace;
    };
    void uploadOverlay();
    void drawOverlay();

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;
    QVector3D eye() const;
    void uploadMesh(const std::string& body, std::shared_ptr<const jig::NamedShape> shape, jig::DisplayMesh mesh);
    void buildGrid();
    void drawScene(bool pickPass);
    std::optional<PickResult> pickAt(const QPoint& p);
    void drawGizmo(QPainter& p);
    int gizmoHit(const QPoint& p) const;
    void orbit(const QPoint& delta);
    void pan(const QPoint& delta);
    void zoomAt(double factor, const QPoint& at);
    QString describe(const PickResult& r) const;

    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::unique_ptr<QOpenGLShaderProgram> meshProg_, lineProg_, pickProg_;
    QOpenGLVertexArrayObject gridVao_;
    QOpenGLBuffer gridVbo_ {QOpenGLBuffer::VertexBuffer};
    int gridMinorCount_ = 0, gridMajorCount_ = 0, axisCount_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> pickFbo_;
    bool glReady_ = false;
    std::vector<BodyInput> waiting_;  // set before GL was initialised

    // camera (Z-up)
    QVector3D target_ {0, 0, 0};
    float distance_ = 300, yaw_ = -60, pitch_ = 30;  // degrees
    bool perspective_ = true;

    NavPreset nav_ = NavPreset::Default;
    ViewColors colors_;
    QString emptyHint_;
    QPoint lastPos_;
    enum class Drag
    {
        None,
        Orbit,
        Pan
    } drag_ = Drag::None;
    std::optional<PickResult> hover_, selected_;
    std::atomic<int> generation_ {0};
    int pending_ = 0;

    Interactor* interactor_ = nullptr;
    PickMode pickMode_ = PickMode::Faces;
    SceneOverlay overlayData_;
    SceneOverlay datums_;
    std::optional<QVector4D> section_;
    jig::LengthUnit units_ = jig::LengthUnit::Millimetre;                                     // construction planes, always shown
    std::vector<std::pair<QString, QVector3D>> datumLabels_;
    bool overlayDirty_ = false;
    std::unique_ptr<QOpenGLShaderProgram> colorProg_;
    QOpenGLVertexArrayObject ovVao_;
    QOpenGLBuffer ovVbo_ {QOpenGLBuffer::VertexBuffer};
    int ovLineCount_ = 0, ovTriCount_ = 0;
    std::optional<EdgePick> hoverEdge_;
    std::string hlBody_;
    std::vector<int> hlEdges_;
    std::string hlFaceBody_;
    std::vector<int> hlFaces_;
    std::optional<QColor> tint_;
};

}  // namespace jigapp
