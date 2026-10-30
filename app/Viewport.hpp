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

#include "jig/kernel.hpp"

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
    };

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
};

}  // namespace jigapp
