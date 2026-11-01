// SPDX-License-Identifier: GPL-3.0-or-later
#include "Viewport.hpp"

#include <chrono>
#include <cmath>
#include <future>
#include <map>
#include <set>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QResizeEvent>
#include <QOpenGLContext>
#include <QTimer>

#include <IntCurvesFace_ShapeIntersector.hxx>
#include <gp_Lin.hxx>
#include <QWheelEvent>

namespace jigapp
{
namespace
{

constexpr float kFovDeg = 35.0f;

const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aPick;
uniform mat4 uMvp;
uniform mat4 uView;
uniform vec4 uClip;  // section view: keep dot(uClip.xyz, p) <= uClip.w
out vec3 vN;
out vec3 vPosV;
flat out int vPick;
void main() {
    vN = mat3(uView) * aNormal;
    vPosV = (uView * vec4(aPos, 1.0)).xyz;
    vPick = int(aPick + 0.5);
    gl_ClipDistance[0] = uClip.w - dot(uClip.xyz, aPos);
    gl_Position = uMvp * vec4(aPos, 1.0);
})";

const char* kMeshFs = R"(#version 330 core
in vec3 vN;
in vec3 vPosV;
flat in int vPick;
uniform vec3 uColor;
uniform vec3 uHover;
uniform vec3 uSel;
uniform int uHoverId;
uniform int uSelId;
uniform int uSection;   // 1 while a section view is on
uniform vec3 uInside;   // colour of surfaces seen from inside the material
out vec4 frag;
void main() {
    vec3 n = normalize(vN);
    if (!gl_FrontFacing) n = -n;
    vec3 L = normalize(vec3(0.35, 0.55, 1.0));
    vec3 V = normalize(-vPosV);
    float d = max(dot(n, L), 0.0);
    float s = pow(max(dot(reflect(-L, n), V), 0.0), 40.0) * 0.18;
    vec3 base = vPick == uSelId ? uSel : (vPick == uHoverId ? uHover : uColor);
    if (uSection == 1 && !gl_FrontFacing) {  // looking into the cut: flat, unlit
        frag = vec4(uInside, 1.0);
        return;
    }
    frag = vec4(base * (0.38 + 0.62 * d) + vec3(s), 1.0);
})";

const char* kPickFs = R"(#version 330 core
flat in int vPick;
in vec3 vN;
in vec3 vPosV;
out vec4 frag;
void main() {
    int id = vPick + 1;
    frag = vec4(float(id & 255) / 255.0, float((id >> 8) & 255) / 255.0,
                float((id >> 16) & 255) / 255.0, 1.0);
})";

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
uniform vec4 uClip;
void main() {
    gl_ClipDistance[0] = uClip.w - dot(uClip.xyz, aPos);
    gl_Position = uMvp * vec4(aPos, 1.0);
})";

const char* kLineFs = R"(#version 330 core
uniform vec4 uColor;
out vec4 frag;
void main() { frag = uColor; })";

const char* kColorVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMvp;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uMvp * vec4(aPos, 1.0); })";

const char* kColorFs = R"(#version 330 core
in vec4 vColor;
out vec4 frag;
void main() { frag = vColor; })";

QVector3D col(const QColor& c)
{
    return {float(c.redF()), float(c.greenF()), float(c.blueF())};
}

}  // namespace

namespace
{
struct Job
{
    int generation;
    std::string body;
    std::shared_ptr<const jig::NamedShape> shape;
    std::future<jig::DisplayMesh> mesh;
};
std::vector<Job>& jobs(const Viewport* v)
{
    static std::map<const Viewport*, std::vector<Job>> all;
    return all[v];
}
}  // namespace

void configureSurfaceFormat()
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
}

Viewport::Viewport(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat f = format();
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setSamples(4);
    f.setDepthBufferSize(24);
    setFormat(f);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    // Overlay painted with the raster engine: QPainter on a core-profile GL surface is
    // not reliable on every driver (it drew nothing on Mesa llvmpipe in testing).
    class Overlay : public QWidget
    {
    public:
        explicit Overlay(Viewport* v)
            : QWidget(v)
            , v_(v)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);
            setAttribute(Qt::WA_NoSystemBackground);
            setAttribute(Qt::WA_TranslucentBackground);
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            v_->paintOverlay(p);
        }

    private:
        Viewport* v_;
    };
    overlay_ = new Overlay(this);
    setColors({QColor(244, 245, 247), QColor(225, 228, 233), QColor(176, 186, 199), QColor(40, 44, 52),
               QColor(255, 196, 92), QColor(66, 140, 255), QColor(214, 218, 224), QColor(190, 196, 205),
               QColor(60, 64, 72)});
    // Finished tessellations are collected on the UI thread (no cross-thread callbacks).
    longPress_ = new QTimer(this);
    longPress_->setSingleShot(true);
    longPress_->setInterval(600);
    connect(longPress_, &QTimer::timeout, this, [this] {
        longPressed_ = true;
        emit selectOtherRequested(lastPos_);
    });
    auto* poll = new QTimer(this);
    poll->setInterval(25);
    connect(poll, &QTimer::timeout, this, [this] {
        if (pending_ > 0) {
            update();
        }
    });
    poll->start();
}

Viewport::~Viewport()
{
    jobs(this).clear();  // waits for running tessellations
    makeCurrent();
    meshes_.clear();
    pickFbo_.reset();
    doneCurrent();
}

void Viewport::setColors(const ViewColors& c)
{
    colors_ = c;
    update();
}

// ---------------------------------------------------------------- data

void Viewport::setBodies(std::vector<BodyInput> bodies)
{
    const int gen = ++generation_;
    shownBodies_ = int(bodies.size());
    auto& js = jobs(this);
    for (auto& b : bodies) {
        auto shape = b.shape;
        js.push_back({gen, b.id, shape, std::async(std::launch::async, [shape] {
                          return jig::tessellate(shape->shape(), 0.05);
                      })});
    }
    pending_ = int(js.size());
    if (bodies.empty()) {
        // Nothing to mesh: clear now.
        makeCurrent();
        meshes_.clear();
        doneCurrent();
        hover_.reset();
        selected_.reset();
        emit meshesUpdated();
    }
    update();
}

void Viewport::uploadMesh(const std::string& body, std::shared_ptr<const jig::NamedShape> shape, jig::DisplayMesh m)
{
    auto g = std::make_unique<GpuMesh>();
    g->body = body;
    g->shape = std::move(shape);
    int base = 0;
    for (auto& o : meshes_) {
        base = std::max(base, o->pickBase + o->shape->faceCount());
    }
    g->pickBase = base;
    const size_t nv = m.positions.size() / 3;
    std::vector<float> vert(nv * 7);
    std::vector<float> vertFace(nv, 0);
    for (size_t t = 0; t < m.triFace.size(); ++t) {
        for (int k = 0; k < 3; ++k) {
            vertFace[m.indices[t * 3 + k]] = float(base + int(m.triFace[t]));
        }
    }
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (size_t i = 0; i < nv; ++i) {
        for (int k = 0; k < 3; ++k) {
            vert[i * 7 + k] = m.positions[i * 3 + k];
            vert[i * 7 + 3 + k] = m.normals[i * 3 + k];
        }
        vert[i * 7 + 6] = vertFace[i];
        QVector3D p(m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]);
        lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
        hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
    }
    g->bmin = lo;
    g->bmax = hi;

    g->vaoTri.create();
    g->vaoTri.bind();
    g->vbo.create();
    g->vbo.bind();
    g->vbo.allocate(vert.data(), int(vert.size() * sizeof(float)));
    g->ibo.create();
    g->ibo.bind();
    g->ibo.allocate(m.indices.data(), int(m.indices.size() * sizeof(uint32_t)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    g->vaoTri.release();
    g->indexCount = int(m.indices.size());

    g->vaoEdge.create();
    g->vaoEdge.bind();
    g->ebo.create();
    g->ebo.bind();
    g->ebo.allocate(m.edgeLines.data(), int(m.edgeLines.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    g->vaoEdge.release();
    g->edgeVertexCount = int(m.edgeLines.size() / 3);
    g->edgeLines = std::move(m.edgeLines);
    g->segEdge = std::move(m.segEdge);
    g->positions = std::move(m.positions);
    g->indices = std::move(m.indices);
    g->triFace = std::move(m.triFace);
    meshes_.push_back(std::move(g));
}

void Viewport::buildGrid()
{
    std::vector<float> v;
    // metric designs: 10 mm squares, a heavier line every 50 mm; inch and foot designs:
    // quarter-inch squares, a heavier line every inch
    const bool inch = units_ == jig::LengthUnit::Inch || units_ == jig::LengthUnit::Foot;
    const float minor = inch ? 6.35f : 10.0f;
    const int perMajor = inch ? 4 : 5, n = inch ? 80 : 50;  // about ±500 mm either way
    const float ext = minor * float(n);
    auto line = [&](float x1, float y1, float z1, float x2, float y2, float z2) {
        v.insert(v.end(), {x1, y1, z1, x2, y2, z2});
    };
    for (int i = -n; i <= n; ++i) {
        if (i % perMajor == 0) {
            continue;
        }
        const float t = minor * float(i);
        line(t, -ext, 0, t, ext, 0);
        line(-ext, t, 0, ext, t, 0);
    }
    gridMinorCount_ = int(v.size() / 3);
    for (int i = -n; i <= n; i += perMajor) {
        const float t = minor * float(i);
        line(t, -ext, 0, t, ext, 0);
        line(-ext, t, 0, ext, t, 0);
    }
    gridMajorCount_ = int(v.size() / 3) - gridMinorCount_;
    // origin axes X, Y, Z (also labelled in the gizmo: never colour alone)
    line(0, 0, 0, 60, 0, 0);
    line(0, 0, 0, 0, 60, 0);
    line(0, 0, 0, 0, 0, 60);
    axisCount_ = 6;
    if (!gridVao_.isCreated()) {
        gridVao_.create();
    }
    gridVao_.bind();
    if (!gridVbo_.isCreated()) {
        gridVbo_.create();
    }
    gridVbo_.bind();
    gridVbo_.allocate(v.data(), int(v.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    gridVao_.release();
}

// ---------------------------------------------------------------- GL

void Viewport::initializeGL()
{
    // Without OpenGL 3.3 core the function table below is empty: calling it would crash.
    const QSurfaceFormat got = context() ? context()->format() : QSurfaceFormat();
    const bool versionOk = got.majorVersion() > 3 || (got.majorVersion() == 3 && got.minorVersion() >= 3);
    if (!context() || !versionOk || !initializeOpenGLFunctions()) {
        glFailed_ = true;
        const QString have = context() ? QString("%1.%2").arg(got.majorVersion()).arg(got.minorVersion()) : tr("none");
        QTimer::singleShot(0, this, [this, have] {
            emit graphicsProblem(tr("The 3D view needs OpenGL 3.3, and this computer's graphics driver offers %1. "
                                    "Updating the graphics driver usually fixes this. Designs still open, save "
                                    "and export; only the view stays empty.")
                                     .arg(have));
        });
        return;
    }
    meshProg_ = std::make_unique<QOpenGLShaderProgram>();
    meshProg_->addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVs);
    meshProg_->addShaderFromSourceCode(QOpenGLShader::Fragment, kMeshFs);
    meshProg_->link();
    pickProg_ = std::make_unique<QOpenGLShaderProgram>();
    pickProg_->addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVs);
    pickProg_->addShaderFromSourceCode(QOpenGLShader::Fragment, kPickFs);
    pickProg_->link();
    lineProg_ = std::make_unique<QOpenGLShaderProgram>();
    lineProg_->addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVs);
    lineProg_->addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFs);
    lineProg_->link();
    colorProg_ = std::make_unique<QOpenGLShaderProgram>();
    colorProg_->addShaderFromSourceCode(QOpenGLShader::Vertex, kColorVs);
    colorProg_->addShaderFromSourceCode(QOpenGLShader::Fragment, kColorFs);
    colorProg_->link();
    ovVao_.create();
    ovVbo_.create();
    buildGrid();
    glReady_ = true;
}

void Viewport::resizeGL(int, int)
{
    pickFbo_.reset();
}

QVector3D Viewport::eye() const
{
    const float y = qDegreesToRadians(yaw_), p = qDegreesToRadians(pitch_);
    QVector3D dir(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p));
    return target_ + dir * distance_;
}

QMatrix4x4 Viewport::viewMatrix() const
{
    QMatrix4x4 v;
    QVector3D up(0, 0, 1);
    if (std::abs(pitch_) > 89.0f) {
        const float y = qDegreesToRadians(yaw_);
        up = QVector3D(-std::cos(y), -std::sin(y), 0) * (pitch_ > 0 ? 1.0f : -1.0f);
    }
    v.lookAt(eye(), target_, up);
    return v;
}

QMatrix4x4 Viewport::projMatrix() const
{
    QMatrix4x4 p;
    const float aspect = float(std::max(width(), 1)) / float(std::max(height(), 1));
    const float nearZ = std::max(0.01f, distance_ * 0.01f), farZ = distance_ * 20 + 2000;
    if (perspective_) {
        p.perspective(kFovDeg, aspect, nearZ, farZ);
    }
    else {
        const float h = distance_ * std::tan(qDegreesToRadians(kFovDeg / 2));
        p.ortho(-h * aspect, h * aspect, -h, h, -farZ, farZ);
    }
    return p;
}

void Viewport::drawScene(bool pickPass)
{
    const QMatrix4x4 view = viewMatrix(), mvp = projMatrix() * view;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    // section view: everything on the far side of the plane is clipped away
    const QVector4D clip = section_ ? *section_ : QVector4D(0, 0, 0, 1);  // 1 - 0 > 0 keeps all
    glEnable(GL_CLIP_DISTANCE0);
    auto* prog = pickPass ? pickProg_.get() : meshProg_.get();
    prog->bind();
    prog->setUniformValue("uMvp", mvp);
    prog->setUniformValue("uView", view);
    prog->setUniformValue("uClip", clip);
    if (!pickPass) {
        auto pickId = [&](const std::optional<PickResult>& r) {
            if (!r) {
                return -1;
            }
            for (auto& m : meshes_) {
                if (m->body == r->body) {
                    return m->pickBase + r->face;
                }
            }
            return -1;
        };
        prog->setUniformValue("uColor", col(tint_ ? *tint_ : colors_.model));
        prog->setUniformValue("uHover", col(colors_.hover));
        prog->setUniformValue("uSel", col(colors_.selected));
        prog->setUniformValue("uHoverId", pickId(hover_));
        prog->setUniformValue("uSelId", pickId(selected_));
        prog->setUniformValue("uSection", section_ ? 1 : 0);
        prog->setUniformValue("uInside", col(QColor(205, 92, 72)));  // brick red, also lighter than the model
    }
    for (auto& m : meshes_) {
        m->vaoTri.bind();
        glDrawElements(GL_TRIANGLES, m->indexCount, GL_UNSIGNED_INT, nullptr);
        m->vaoTri.release();
    }
    prog->release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (pickPass) {
        return;
    }
    lineProg_->bind();
    lineProg_->setUniformValue("uMvp", mvp);
    lineProg_->setUniformValue("uClip", clip);
    lineProg_->setUniformValue("uColor", colors_.edge);
    for (auto& m : meshes_) {
        m->vaoEdge.bind();
        glDrawArrays(GL_LINES, 0, m->edgeVertexCount);
        m->vaoEdge.release();
    }
    lineProg_->setUniformValue("uClip", QVector4D(0, 0, 0, 1));  // the grid is never cut
    glDisable(GL_CLIP_DISTANCE0);
    // grid last, depth-tested, so the model hides it
    glEnable(GL_BLEND);
    // blend colour only: the widget composites its framebuffer, so its alpha must stay 1
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    gridVao_.bind();
    lineProg_->setUniformValue("uColor", colors_.grid);
    glDrawArrays(GL_LINES, 0, gridMinorCount_);
    lineProg_->setUniformValue("uColor", colors_.gridMajor);
    glDrawArrays(GL_LINES, gridMinorCount_, gridMajorCount_);
    const QColor axis[3] = {QColor(214, 64, 69), QColor(64, 170, 90), QColor(60, 110, 230)};
    for (int i = 0; i < 3; ++i) {
        lineProg_->setUniformValue("uColor", axis[i]);
        glDrawArrays(GL_LINES, gridMinorCount_ + gridMajorCount_ + 2 * i, 2);
    }
    gridVao_.release();
    glDisable(GL_BLEND);
    lineProg_->release();
    drawOverlay();
}

// ---------------------------------------------------------------- overlay (tools)

void Viewport::setOverlay(SceneOverlay o)
{
    overlayData_ = std::move(o);
    overlayDirty_ = true;
    update();
}

void Viewport::setUnits(jig::LengthUnit u)
{
    const bool wasInch = units_ == jig::LengthUnit::Inch || units_ == jig::LengthUnit::Foot;
    units_ = u;
    const bool inch = u == jig::LengthUnit::Inch || u == jig::LengthUnit::Foot;
    if (wasInch != inch && glReady_) {
        makeCurrent();
        buildGrid();
        doneCurrent();
        update();
    }
}

void Viewport::setSection(std::optional<QVector4D> plane)
{
    section_ = plane;
    update();
}

void Viewport::setDatums(SceneOverlay geometry, std::vector<std::pair<QString, QVector3D>> labels)
{
    datums_ = std::move(geometry);
    datumLabels_ = std::move(labels);
    overlayDirty_ = true;
    update();
}

void Viewport::setEdgeHighlight(const std::string& body, std::vector<int> edges)
{
    hlBody_ = body;
    hlEdges_ = std::move(edges);
    overlayDirty_ = true;
    update();
}

void Viewport::setFaceHighlight(const std::string& body, std::vector<int> faces)
{
    hlFaceBody_ = body;
    hlFaces_ = std::move(faces);
    overlayDirty_ = true;
    update();
}

void Viewport::setModelTint(std::optional<QColor> c)
{
    tint_ = c;
    update();
}

void Viewport::setInteractor(Interactor* i)
{
    interactor_ = i;
    if (hover_) {
        hover_.reset();
        emit hovered({});
    }
    update();
}

void Viewport::setPickMode(PickMode m)
{
    pickMode_ = m;
    hover_.reset();
    hoverEdge_.reset();
    overlayDirty_ = true;
    update();
}

void Viewport::uploadOverlay()
{
    std::vector<float> v;
    auto push = [&](const QVector3D& p, const QColor& c) {
        v.insert(v.end(), {p.x(), p.y(), p.z(), float(c.redF()), float(c.greenF()), float(c.blueF()),
                           float(c.alphaF())});
    };
    // highlighted (selected) edges, then the hovered edge, then the tool's overlay
    auto edgeSegs = [&](const std::string& body, int edge, const QColor& c) {
        for (auto& m : meshes_) {
            if (m->body != body) {
                continue;
            }
            for (size_t s = 0; s < m->segEdge.size(); ++s) {
                if (int(m->segEdge[s]) == edge) {
                    const float* f = &m->edgeLines[s * 6];
                    push({f[0], f[1], f[2]}, c);
                    push({f[3], f[4], f[5]}, c);
                }
            }
        }
    };
    for (int e : hlEdges_) {
        edgeSegs(hlBody_, e, colors_.selected);
    }
    if (hoverEdge_) {
        edgeSegs(hoverEdge_->body, hoverEdge_->edge, colors_.hover);
    }
    for (auto& ov : datums_.lines) {
        push(ov.p, ov.c);
    }
    for (auto& ov : overlayData_.lines) {
        push(ov.p, ov.c);
    }
    ovLineCount_ = int(v.size() / 7);
    for (auto& ov : datums_.tris) {
        push(ov.p, ov.c);
    }
    for (auto& ov : overlayData_.tris) {
        push(ov.p, ov.c);
    }
    if (!hlFaces_.empty()) {
        QColor fc = colors_.selected;
        fc.setAlpha(110);
        const std::set<int> want(hlFaces_.begin(), hlFaces_.end());
        for (auto& m : meshes_) {
            if (m->body != hlFaceBody_) {
                continue;
            }
            for (size_t t = 0; t < m->triFace.size(); ++t) {
                if (!want.count(int(m->triFace[t]))) {
                    continue;
                }
                for (int k = 0; k < 3; ++k) {
                    const uint32_t idx = m->indices[t * 3 + k];
                    push({m->positions[idx * 3], m->positions[idx * 3 + 1], m->positions[idx * 3 + 2]}, fc);
                }
            }
        }
    }
    ovTriCount_ = int(v.size() / 7) - ovLineCount_;
    ovVao_.bind();
    ovVbo_.bind();
    ovVbo_.allocate(v.data(), int(v.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    ovVao_.release();
    overlayDirty_ = false;
}

void Viewport::drawOverlay()
{
    if (overlayDirty_) {
        uploadOverlay();
    }
    if (ovLineCount_ + ovTriCount_ == 0) {
        return;
    }
    colorProg_->bind();
    colorProg_->setUniformValue("uMvp", projMatrix() * viewMatrix());
    ovVao_.bind();
    glEnable(GL_BLEND);
    // blend colour only: the widget composites its framebuffer, so its alpha must stay 1
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    // Tool geometry is drawn on top so sketches on faces never disappear into them.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (ovTriCount_ > 0) {
        glDrawArrays(GL_TRIANGLES, ovLineCount_, ovTriCount_);
    }
    glDrawArrays(GL_LINES, 0, ovLineCount_);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    ovVao_.release();
    colorProg_->release();
}

// ---------------------------------------------------------------- geometry helpers

void Viewport::ray(const QPointF& px, QVector3D& origin, QVector3D& dir) const
{
    const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted();
    const float nx = float(2.0 * px.x() / std::max(width(), 1) - 1), ny = float(1 - 2.0 * px.y() / std::max(height(), 1));
    QVector3D a = inv.map(QVector3D(nx, ny, -1)), b = inv.map(QVector3D(nx, ny, 1));
    origin = a;
    dir = (b - a).normalized();
}

nlohmann::json Viewport::camera() const
{
    return {{"target", {target_.x(), target_.y(), target_.z()}},
            {"distance", distance_},
            {"yaw", yaw_},
            {"pitch", pitch_},
            {"perspective", perspective_}};
}

void Viewport::setCamera(const nlohmann::json& c)
{
    if (!c.is_object()) {
        return;
    }
    const auto t = c.value("target", nlohmann::json::array({0, 0, 0}));
    if (t.is_array() && t.size() == 3) {
        target_ = QVector3D(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());
    }
    distance_ = std::clamp(c.value("distance", distance_), 0.5f, 50000.0f);
    yaw_ = c.value("yaw", yaw_);
    pitch_ = std::clamp(c.value("pitch", pitch_), -89.9f, 89.9f);
    setPerspective(c.value("perspective", perspective_));
    update();
}

QPointF Viewport::project(const QVector3D& w) const
{
    QVector3D n = (projMatrix() * viewMatrix()).map(w);
    return {(n.x() + 1) * 0.5 * width(), (1 - n.y()) * 0.5 * height()};
}

float Viewport::worldPerPixel() const
{
    return 2 * distance_ * std::tan(qDegreesToRadians(kFovDeg / 2)) / float(std::max(height(), 1));
}

void Viewport::lookAlong(const QVector3D& normal, const QVector3D& up, const QVector3D& center)
{
    QVector3D n = normal.normalized();
    pitch_ = std::clamp(float(qRadiansToDegrees(std::asin(std::clamp(n.z(), -1.0f, 1.0f)))), -89.9f, 89.9f);
    if (std::abs(n.z()) > 0.999f) {
        // looking straight down/up: choose yaw so the plane's "up" is up on screen
        const float s = n.z() > 0 ? 1.0f : -1.0f;
        yaw_ = float(qRadiansToDegrees(std::atan2(-up.y() * s, -up.x() * s)));
    }
    else {
        yaw_ = float(qRadiansToDegrees(std::atan2(n.y(), n.x())));
    }
    target_ = center;
    update();
}

std::vector<EdgePick> Viewport::edgesInBox(const QRectF& box, bool crossing) const
{
    std::vector<EdgePick> out;
    if (!glReady_) {
        return out;
    }
    for (auto& m : meshes_) {
        // per edge: did any segment touch the box, did every segment lie inside it
        std::map<int, std::pair<bool, bool>> seen;  // edge -> (any, all)
        for (size_t s = 0; s < m->segEdge.size(); ++s) {
            const float* f = &m->edgeLines[s * 6];
            const QVector3D a(f[0], f[1], f[2]), b(f[3], f[4], f[5]);
            const QVector3D mid = (a + b) * 0.5f;
            if (section_ && QVector3D::dotProduct(section_->toVector3D(), mid) > section_->w()) {
                continue;
            }
            const QPointF pa = project(a), pb = project(b);
            const bool inA = box.contains(pa), inB = box.contains(pb);
            bool touches = inA || inB;
            if (!touches) {
                // a segment can cross the box with both ends outside
                const QLineF seg(pa, pb);
                const QLineF sides[4] = {{box.topLeft(), box.topRight()}, {box.topRight(), box.bottomRight()},
                                         {box.bottomRight(), box.bottomLeft()}, {box.bottomLeft(), box.topLeft()}};
                for (auto& side : sides) {
                    if (seg.intersects(side) == QLineF::BoundedIntersection) {
                        touches = true;
                        break;
                    }
                }
            }
            auto [it, fresh] = seen.try_emplace(int(m->segEdge[s]), false, true);
            it->second.first = it->second.first || touches;
            it->second.second = it->second.second && inA && inB;
        }
        for (auto& [edge, st] : seen) {
            if (crossing ? st.first : st.second) {
                out.push_back({m->body, edge});
            }
        }
    }
    return out;
}

std::optional<EdgePick> Viewport::pickEdgeAt(const QPoint& pos)
{
    if (!glReady_ || meshes_.empty()) {
        return std::nullopt;
    }
    struct Cand
    {
        double dist;
        std::string body;
        int edge;
        QVector3D world;
    };
    std::vector<Cand> cands;
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    for (auto& m : meshes_) {
        for (size_t s = 0; s < m->segEdge.size(); ++s) {
            const float* f = &m->edgeLines[s * 6];
            QVector3D a(f[0], f[1], f[2]), b(f[3], f[4], f[5]);
            QPointF pa = project(a), pb = project(b);
            QPointF ab = pb - pa, ap = QPointF(pos) - pa;
            double len2 = ab.x() * ab.x() + ab.y() * ab.y();
            double t = len2 > 0 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / len2, 0.0, 1.0) : 0.0;
            QPointF c = pa + ab * t;
            double d = QLineF(c, QPointF(pos)).length();
            const QVector3D w = a + (b - a) * float(t);
            const bool cutAway = section_ && QVector3D::dotProduct(section_->toVector3D(), w) > section_->w();
            if (d < 8 && !cutAway) {
                cands.push_back({d, m->body, int(m->segEdge[s]), w});
            }
        }
    }
    if (cands.empty()) {
        return std::nullopt;
    }
    std::sort(cands.begin(), cands.end(), [](auto& x, auto& y) { return x.dist < y.dist; });
    // Reject edges hidden behind faces using the depth of the pick pass.
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    const QSize sz(int(width() * dpr), int(height() * dpr));
    if (!pickFbo_ || pickFbo_->size() != sz) {
        pickFbo_ = std::make_unique<QOpenGLFramebufferObject>(sz, QOpenGLFramebufferObject::Depth);
    }
    pickFbo_->bind();
    glViewport(0, 0, sz.width(), sz.height());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene(true);
    std::optional<EdgePick> result;
    for (size_t i = 0; i < std::min<size_t>(cands.size(), 8) && !result; ++i) {
        QVector3D n = mvp.map(cands[i].world);
        QPointF px = project(cands[i].world);
        float depth = 1;
        const int x = std::clamp(int(px.x() * dpr), 0, sz.width() - 1);
        const int y = std::clamp(sz.height() - 1 - int(px.y() * dpr), 0, sz.height() - 1);
        glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        if (n.z() * 0.5f + 0.5f <= depth + 2e-3f) {
            result = EdgePick {cands[i].body, cands[i].edge};
        }
    }
    pickFbo_->release();
    doneCurrent();
    return result;
}

void Viewport::keyPressEvent(QKeyEvent* e)
{
    if (interactor_ && interactor_->key(e)) {
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::paintGL()
{
    if (glFailed_) {
        // nothing can be drawn: let finished tessellations go, keep the rest of the app working
        auto& js = jobs(this);
        std::erase_if(js, [](auto& j) { return j.mesh.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        pending_ = int(js.size());
        return;
    }
    // Collect finished tessellations.
    auto& js = jobs(this);
    bool changed = false;
    for (auto it = js.begin(); it != js.end();) {
        if (it->mesh.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        jig::DisplayMesh m = it->mesh.get();
        if (it->generation == generation_) {
            if (!changed) {
                // first result of a new generation replaces the old meshes
                meshes_.clear();
                hover_.reset();
                selected_.reset();
            }
            uploadMesh(it->body, it->shape, std::move(m));
            changed = true;
            overlayDirty_ = true;
        }
        it = js.erase(it);
    }
    pending_ = int(js.size());
    if (changed && pending_ == 0) {
        emit meshesUpdated();
    }

    glClearColor(colors_.background.redF(), colors_.background.greenF(), colors_.background.blueF(), 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene(false);
    glDisable(GL_DEPTH_TEST);
    overlay_->update();  // gizmo and hints are painted by the raster overlay widget
}

void Viewport::paintOverlay(QPainter& p)
{
    p.setRenderHint(QPainter::Antialiasing);
    if (interactor_) {
        p.save();
        interactor_->paint(p);
        p.restore();
    }
    if (!datumLabels_.empty()) {
        p.save();
        p.setPen(colors_.text);
        for (auto& [text, at] : datumLabels_) {
            p.drawText(project(at) + QPointF(4, -4), text);
        }
        p.restore();
    }
    if (boxing_) {
        // solid for "wholly inside", dashed for "anything it touches" (not colour alone)
        const bool crossing = boxEnd_.x() < boxStart_.x();
        const QRectF r = QRectF(boxStart_, boxEnd_).normalized();
        p.save();
        p.setPen(QPen(colors_.selected, 1.2, crossing ? Qt::DashLine : Qt::SolidLine));
        QColor fill = colors_.selected;
        fill.setAlpha(28);
        p.setBrush(fill);
        p.drawRect(r);
        p.setPen(colors_.text);
        p.drawText(r.bottomLeft() + QPointF(4, 14), crossing ? tr("touching") : tr("inside"));
        p.restore();
    }
    drawGizmo(p);
    if (meshes_.empty() && !emptyHint_.isEmpty() && pending_ == 0) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        p.setFont(f);
        QRect box(width() / 2 - 260, height() / 2 - 70, 520, 140);
        p.setPen(Qt::NoPen);
        QColor bg = colors_.background.lighter(104);
        bg.setAlpha(235);
        p.setBrush(bg);
        p.drawRoundedRect(box, 10, 10);
        p.setPen(colors_.text);
        p.drawText(box.adjusted(18, 12, -18, -12), Qt::AlignCenter | Qt::TextWordWrap, emptyHint_);
    }
    if (pending_ > 0) {
        p.setPen(colors_.text);
        p.drawText(QRect(12, height() - 30, 300, 20), Qt::AlignLeft, tr("Updating the view…"));
    }
}

void Viewport::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);
    overlay_->setGeometry(rect());
}

// ---------------------------------------------------------------- picking

std::vector<PickResult> Viewport::facesInBox(const QRectF& box, bool crossing)
{
    std::vector<PickResult> out;
    if (!glReady_ || meshes_.empty()) {
        return out;
    }
    // what the pick buffer shows inside the box: the faces seen there
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    const QSize sz(int(width() * dpr), int(height() * dpr));
    if (!pickFbo_ || pickFbo_->size() != sz) {
        pickFbo_ = std::make_unique<QOpenGLFramebufferObject>(sz, QOpenGLFramebufferObject::Depth);
    }
    pickFbo_->bind();
    glViewport(0, 0, sz.width(), sz.height());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    drawScene(true);
    const QRect r = QRect(QPoint(int(box.left() * dpr), int(box.top() * dpr)), QPoint(int(box.right() * dpr), int(box.bottom() * dpr)))
                        .intersected(QRect(QPoint(0, 0), sz));
    std::vector<unsigned char> px(size_t(std::max(0, r.width() * r.height() * 4)));
    if (!r.isEmpty()) {
        glReadPixels(r.left(), sz.height() - 1 - r.bottom(), r.width(), r.height(), GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
    pickFbo_->release();
    doneCurrent();
    // a face counts when some pixel of it has the same face on all four sides: one-pixel
    // slivers along silhouette edges (hidden faces peeking through) do not
    std::set<int> seen;
    const int W = r.width(), H = r.height();
    auto idAt = [&](int x, int y) {
        const size_t i = (size_t(y) * size_t(W) + size_t(x)) * 4;
        return int(px[i]) | (int(px[i + 1]) << 8) | (int(px[i + 2]) << 16);
    };
    for (int y = 1; y + 1 < H; ++y) {
        for (int x = 1; x + 1 < W; ++x) {
            const int id = idAt(x, y);
            if (id && !seen.count(id) && idAt(x - 1, y) == id && idAt(x + 1, y) == id && idAt(x, y - 1) == id &&
                idAt(x, y + 1) == id) {
                seen.insert(id);
            }
        }
    }
    for (auto& m : meshes_) {
        for (int f = 0; f < m->shape->faceCount(); ++f) {
            if (!seen.count(m->pickBase + f + 1)) {
                continue;
            }
            bool inside = true;
            if (!crossing) {  // window: every corner of the face's triangles inside the box
                for (size_t t = 0; t < m->triFace.size() && inside; ++t) {
                    if (int(m->triFace[t]) != f) {
                        continue;
                    }
                    for (int k = 0; k < 3 && inside; ++k) {
                        const uint32_t v = m->indices[t * 3 + size_t(k)];
                        const QVector3D p(m->positions[v * 3], m->positions[v * 3 + 1], m->positions[v * 3 + 2]);
                        inside = box.adjusted(-1, -1, 1, 1).contains(project(p));
                    }
                }
            }
            if (inside) {
                out.push_back({m->body, f});
            }
        }
    }
    return out;
}

std::optional<PickResult> Viewport::pickAt(const QPoint& pos)
{
    if (!glReady_ || meshes_.empty()) {
        return std::nullopt;
    }
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    const QSize sz(int(width() * dpr), int(height() * dpr));
    if (!pickFbo_ || pickFbo_->size() != sz) {
        pickFbo_ = std::make_unique<QOpenGLFramebufferObject>(sz, QOpenGLFramebufferObject::Depth);
    }
    pickFbo_->bind();
    glViewport(0, 0, sz.width(), sz.height());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    drawScene(true);
    unsigned char px[4] = {0, 0, 0, 0};
    const int x = int(pos.x() * dpr), y = sz.height() - 1 - int(pos.y() * dpr);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    pickFbo_->release();
    doneCurrent();
    const int id = int(px[0]) | (int(px[1]) << 8) | (int(px[2]) << 16);
    if (id == 0) {
        return std::nullopt;
    }
    for (auto& m : meshes_) {
        const int local = id - 1 - m->pickBase;
        if (local >= 0 && local < m->shape->faceCount()) {
            return PickResult {m->body, local};
        }
    }
    return std::nullopt;
}

QString Viewport::describe(const PickResult& r) const
{
    for (auto& m : meshes_) {
        if (m->body == r.body) {
            auto fp = m->shape->faceFingerprint(r.face);
            return tr("Face “%1” · %2 · %3")
                .arg(QString::fromStdString(m->shape->faceName(r.face)), QString::fromStdString(fp.type),
                     QString::fromStdString(jig::formatArea(fp.size, units_)));
        }
    }
    return {};
}

// ---------------------------------------------------------------- gizmo

namespace
{
struct GizmoAxis
{
    QVector3D dir;
    const char* label;
};
const GizmoAxis kAxes[6] = {{{1, 0, 0}, "X"},  {{0, 1, 0}, "Y"},  {{0, 0, 1}, "Z"},
                            {{-1, 0, 0}, ""},  {{0, -1, 0}, ""}, {{0, 0, -1}, ""}};
const QColor kAxisCol[3] = {QColor(214, 64, 69), QColor(64, 170, 90), QColor(60, 110, 230)};
}  // namespace

int Viewport::gizmoHit(const QPoint& p) const
{
    const QPointF c(width() - 72, 72);
    const QMatrix4x4 v = viewMatrix();
    for (int i = 0; i < 6; ++i) {
        QVector3D d = v.mapVector(kAxes[i].dir);
        QPointF s = c + QPointF(d.x(), -d.y()) * 46;
        if (QLineF(s, p).length() < (i < 3 ? 13 : 9)) {
            return i;
        }
    }
    if (QRectF(width() - 128, 140, 52, 22).contains(p)) {
        return 6;  // Home
    }
    if (QRectF(width() - 70, 140, 52, 22).contains(p)) {
        return 7;  // Fit
    }
    return -1;
}

void Viewport::drawGizmo(QPainter& p)
{
    const QPointF c(width() - 72, 72);
    const QMatrix4x4 v = viewMatrix();
    p.setPen(Qt::NoPen);
    QColor disc = colors_.background.darker(108);
    disc.setAlpha(150);
    p.setBrush(disc);
    p.drawEllipse(c, 62, 62);
    // draw back-to-front
    int order[6] = {0, 1, 2, 3, 4, 5};
    std::sort(order, order + 6, [&](int a, int b) {
        return v.mapVector(kAxes[a].dir).z() < v.mapVector(kAxes[b].dir).z();
    });
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    for (int i : order) {
        QVector3D d = v.mapVector(kAxes[i].dir);
        QPointF s = c + QPointF(d.x(), -d.y()) * 46;
        QColor cc = kAxisCol[i % 3];
        if (i < 3) {
            p.setPen(QPen(cc, 2.5));
            p.drawLine(c, s);
            p.setPen(Qt::NoPen);
            p.setBrush(cc);
            p.drawEllipse(s, 12, 12);
            p.setPen(Qt::white);
            p.drawText(QRectF(s.x() - 12, s.y() - 12, 24, 24), Qt::AlignCenter, kAxes[i].label);
        }
        else {
            p.setPen(QPen(cc, 1.5));
            p.setBrush(colors_.background);
            p.drawEllipse(s, 8, 8);
        }
    }
    f.setBold(false);
    p.setFont(f);
    for (auto [rect, text] : {std::pair {QRectF(width() - 128, 140, 52, 22), tr("Home")},
                              std::pair {QRectF(width() - 70, 140, 52, 22), tr("Fit")}}) {
        p.setPen(colors_.gridMajor);
        p.setBrush(colors_.background);
        p.drawRoundedRect(rect, 5, 5);
        p.setPen(colors_.text);
        p.drawText(rect, Qt::AlignCenter, text);
    }
}

// ---------------------------------------------------------------- navigation

void Viewport::orbit(const QPoint& d)
{
    yaw_ -= d.x() * 0.4f * orbitSpeed_;
    pitch_ = std::clamp(pitch_ + d.y() * 0.4f * orbitSpeed_, -89.9f, 89.9f);
    update();
}

void Viewport::pan(const QPoint& d)
{
    const float wpp = 2 * distance_ * std::tan(qDegreesToRadians(kFovDeg / 2)) / float(std::max(height(), 1));
    const QMatrix4x4 v = viewMatrix();
    const QVector3D right(v(0, 0), v(0, 1), v(0, 2)), up(v(1, 0), v(1, 1), v(1, 2));
    target_ -= right * (d.x() * wpp) - up * (d.y() * wpp);
    update();
}

void Viewport::zoomAt(double factor, const QPoint& at)
{
    // keep the point under the cursor fixed while zooming
    const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted();
    const QVector3D tn = (projMatrix() * viewMatrix()).map(target_);
    const float nx = 2.0f * at.x() / std::max(width(), 1) - 1, ny = 1 - 2.0f * at.y() / std::max(height(), 1);
    const QVector3D under = inv.map(QVector3D(nx, ny, tn.z()));
    distance_ = std::clamp(float(distance_ * factor), 0.5f, 50000.0f);
    target_ = under + (target_ - under) * float(factor);
    update();
}

void Viewport::mousePressEvent(QMouseEvent* e)
{
    lastPos_ = e->position().toPoint();
    const bool mid = e->button() == Qt::MiddleButton;
    const bool shift = e->modifiers() & Qt::ShiftModifier, ctrl = e->modifiers() & Qt::ControlModifier,
               alt = e->modifiers() & Qt::AltModifier;
    drag_ = Drag::None;
    switch (nav_) {
        case NavPreset::Default:
            if (mid) {
                drag_ = shift ? Drag::Orbit : Drag::Pan;
            }
            break;
        case NavPreset::Blender:
            if (mid) {
                drag_ = shift ? Drag::Pan : Drag::Orbit;
            }
            break;
        case NavPreset::SolidWorks:
            if (mid) {
                drag_ = ctrl ? Drag::Pan : Drag::Orbit;
            }
            break;
        case NavPreset::Trackpad:
            if (e->button() == Qt::LeftButton && alt) {
                drag_ = shift ? Drag::Pan : Drag::Orbit;
            }
            break;
    }
    // Universal fallback for people without a middle button: right-drag orbits.
    if (e->button() == Qt::RightButton) {
        drag_ = shift ? Drag::Pan : Drag::Orbit;
        rightPress_ = lastPos_;
        rightMoved_ = false;
    }
    longPressed_ = false;
    if (drag_ == Drag::None && e->button() == Qt::LeftButton && gizmoHit(lastPos_) < 0) {
        if (!interactor_ && (pickMode_ == PickMode::Faces || pickMode_ == PickMode::Any) && !meshes_.empty()) {
            longPress_->start();
        }
        if (interactor_) {
            interactor_->mousePress(e);
        }
        // a press on empty space while picking edges may become a selection box
        boxArmed_ = !interactor_ && ((pickMode_ == PickMode::Edges && !pickEdgeAt(lastPos_)) ||
                                     (pickMode_ == PickMode::Faces && faceBoxing_ && !pickAt(lastPos_)));
        boxStart_ = boxEnd_ = lastPos_;
        boxing_ = false;
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    const QPoint d = pos - lastPos_;
    lastPos_ = pos;
    if ((e->buttons() & Qt::RightButton) && (pos - rightPress_).manhattanLength() > 4) {
        rightMoved_ = true;
    }
    if (longPress_->isActive() && d.manhattanLength() > 2) {
        longPress_->stop();  // moving is not a long press
    }
    if (boxArmed_ && (e->buttons() & Qt::LeftButton)) {
        if (!boxing_ && (pos - boxStart_).manhattanLength() > 4) {
            boxing_ = true;
        }
        if (boxing_) {
            boxEnd_ = pos;
            overlay_->update();
            return;
        }
    }
    if (drag_ == Drag::Orbit) {
        orbit(d);
        return;
    }
    if (drag_ == Drag::Pan) {
        pan(d);
        return;
    }
    if (interactor_ && interactor_->mouseMove(e)) {
        return;
    }
    if (e->buttons() == Qt::NoButton && (pickMode_ == PickMode::Edges || pickMode_ == PickMode::Any)) {
        auto h = pickEdgeAt(pos);
        if (!h && pickMode_ == PickMode::Any) {
            if (hoverEdge_) {
                hoverEdge_.reset();
                overlayDirty_ = true;
            }
            auto f = pickAt(pos);
            const bool same = (f.has_value() == hover_.has_value()) && (!f || (f->body == hover_->body && f->face == hover_->face));
            if (!same) {
                hover_ = f;
                emit hovered(f ? describe(*f) : QString());
                update();
            }
            return;
        }
        if (h && pickMode_ == PickMode::Any && hover_) {
            hover_.reset();
        }
        bool same = (h.has_value() == hoverEdge_.has_value()) && (!h || (h->body == hoverEdge_->body && h->edge == hoverEdge_->edge));
        if (!same) {
            hoverEdge_ = h;
            overlayDirty_ = true;
            if (h) {
                for (auto& m : meshes_) {
                    if (m->body == h->body) {
                        emit hovered(tr("Edge “%1”").arg(QString::fromStdString(m->shape->edgeName(h->edge))));
                    }
                }
            }
            else {
                emit hovered({});
            }
            update();
        }
        return;
    }
    if (e->buttons() == Qt::NoButton && pickMode_ == PickMode::Faces) {
        auto h = pickAt(pos);
        bool same = (h.has_value() == hover_.has_value()) && (!h || (h->body == hover_->body && h->face == hover_->face));
        if (!same) {
            hover_ = h;
            emit hovered(h ? describe(*h) : QString());
            update();
        }
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e)
{
    longPress_->stop();
    if (e->button() == Qt::LeftButton && longPressed_) {
        longPressed_ = false;  // the menu of hidden faces took this press
        return;
    }
    if (e->button() == Qt::RightButton) {
        drag_ = Drag::None;
        if (!rightMoved_) {
            emit menuRequested(e->position().toPoint());
        }
        return;
    }
    if (e->button() == Qt::LeftButton && boxing_) {
        boxArmed_ = boxing_ = false;
        boxEnd_ = e->position().toPoint();
        overlay_->update();
        const bool crossing = boxEnd_.x() < boxStart_.x();
        if (pickMode_ == PickMode::Faces) {
            const auto faces = facesInBox(QRectF(boxStart_, boxEnd_).normalized(), crossing);
            if (!faces.empty()) {
                const std::string body = faces.front().body;
                for (auto& m : meshes_) {
                    if (m->body != body) {
                        continue;
                    }
                    QStringList names;
                    for (auto& f : faces) {
                        if (f.body == body && f.face < m->shape->faceCount()) {
                            names << QString::fromStdString(m->shape->faceName(f.face));
                        }
                    }
                    emit facesBoxed(QString::fromStdString(body), names, e->modifiers());
                    break;
                }
            }
            return;
        }
        const auto hits = edgesInBox(QRectF(boxStart_, boxEnd_).normalized(), crossing);
        if (!hits.empty()) {
            // one body per box: the first one met (fillets work on one body at a time)
            const std::string body = hits.front().body;
            for (auto& m : meshes_) {
                if (m->body != body) {
                    continue;
                }
                QStringList names;
                for (auto& h : hits) {
                    if (h.body == body && h.edge < m->shape->edgeCount()) {
                        names << QString::fromStdString(m->shape->edgeName(h.edge));
                    }
                }
                emit edgesBoxed(QString::fromStdString(body), names, e->modifiers());
                break;
            }
        }
        return;
    }
    boxArmed_ = false;
    if (e->button() == Qt::LeftButton && drag_ == Drag::None) {
        const QPoint pos = e->position().toPoint();
        switch (gizmoHit(pos)) {
            case 0: yaw_ = 0; pitch_ = 0; update(); return;          // from +X: right
            case 1: yaw_ = 90; pitch_ = 0; update(); return;         // from +Y: back
            case 2: viewTop(); return;
            case 3: yaw_ = 180; pitch_ = 0; update(); return;        // left
            case 4: viewFront(); return;
            case 5: yaw_ = -90; pitch_ = -89.9f; update(); return;   // bottom
            case 6: homeView(); return;
            case 7: fitAll(); return;
            default: break;
        }
        if (interactor_ && interactor_->mouseRelease(e)) {
            drag_ = Drag::None;
            return;
        }
        if (pickMode_ == PickMode::Edges || pickMode_ == PickMode::Any) {
            auto h = pickEdgeAt(pos);
            if (!h && pickMode_ == PickMode::Any) {
                if (auto f = pickAt(pos)) {
                    for (auto& m : meshes_) {
                        if (m->body == f->body && f->face < m->shape->faceCount()) {
                            emit faceClicked(QString::fromStdString(f->body),
                                             QString::fromStdString(m->shape->faceName(f->face)), e->modifiers());
                        }
                    }
                }
                drag_ = Drag::None;
                return;
            }
            if (h) {
                for (auto& m : meshes_) {
                    if (m->body == h->body && h->edge < m->shape->edgeCount()) {
                        emit edgeClicked(QString::fromStdString(h->body),
                                         QString::fromStdString(m->shape->edgeName(h->edge)), e->modifiers());
                    }
                }
            }
            drag_ = Drag::None;
            return;
        }
        if (pickMode_ == PickMode::None) {
            drag_ = Drag::None;
            return;
        }
        selected_ = pickAt(pos);
        emit selectionChanged(selected_ ? QString::fromStdString(selected_->body) : QString(),
                              selected_ ? selected_->face : -1);
        if (selected_) {
            for (auto& m : meshes_) {
                if (m->body == selected_->body && selected_->face < m->shape->faceCount()) {
                    emit faceClicked(QString::fromStdString(selected_->body),
                                     QString::fromStdString(m->shape->faceName(selected_->face)), e->modifiers());
                }
            }
        }
        update();
    }
    drag_ = Drag::None;
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && interactor_ && interactor_->mouseDoubleClick(e)) {
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        fitAll();
    }
}

void Viewport::wheelEvent(QWheelEvent* e)
{
    const QPoint px = e->pixelDelta();
    if (nav_ == NavPreset::Trackpad && !px.isNull()) {
        if (e->modifiers() & Qt::ShiftModifier) {
            pan(-px);
        }
        else {
            orbit(-px);
        }
        return;
    }
    const double steps = e->angleDelta().y() / 120.0 * (zoomInverted_ ? -1 : 1);
    zoomAt(std::pow(0.85, steps), e->position().toPoint());
}

bool Viewport::event(QEvent* e)
{
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomAt(zoomInverted_ ? 1.0 + g->value() : 1.0 / (1.0 + g->value()), g->position().toPoint());
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

void Viewport::leaveEvent(QEvent*)
{
    if (hover_) {
        hover_.reset();
        emit hovered({});
        update();
    }
}

std::vector<PickResult> Viewport::facesUnder(const QPoint& px) const
{
    QVector3D o, d;
    ray(px, o, d);
    const gp_Lin line(gp_Pnt(o.x(), o.y(), o.z()), gp_Dir(d.x(), d.y(), d.z()));
    std::vector<std::pair<double, PickResult>> hits;
    for (auto& m : meshes_) {
        IntCurvesFace_ShapeIntersector inter;
        inter.Load(m->shape->shape(), 1e-6);
        inter.Perform(line, 0, 1e9);
        for (int i = 1; i <= inter.NbPnt(); ++i) {
            const TopoDS_Face& f = inter.Face(i);
            for (int k = 0; k < m->shape->faceCount(); ++k) {
                if (m->shape->face(k).IsSame(f)) {
                    bool dup = false;
                    for (auto& h : hits) {
                        dup |= h.second.body == m->body && h.second.face == k;
                    }
                    if (!dup) {
                        hits.push_back({inter.WParameter(i), PickResult {m->body, k}});
                    }
                }
            }
        }
    }
    std::sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<PickResult> out;
    for (auto& h : hits) {
        out.push_back(h.second);
    }
    return out;
}

void Viewport::selectFace(const std::string& body, int face)
{
    for (auto& m : meshes_) {
        if (m->body == body && face >= 0 && face < m->shape->faceCount()) {
            selected_ = PickResult {body, face};
            emit selectionChanged(QString::fromStdString(body), face);
            emit faceClicked(QString::fromStdString(body), QString::fromStdString(m->shape->faceName(face)),
                             Qt::NoModifier);
            update();
            return;
        }
    }
}

void Viewport::selectFaceForScreenshot(int face)
{
    if (meshes_.empty() || face < 0 || face >= meshes_.front()->shape->faceCount()) {
        return;
    }
    selected_ = PickResult {meshes_.front()->body, face};
    emit selectionChanged(QString::fromStdString(selected_->body), face);
    update();
}

void Viewport::fitAll()
{
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (auto& m : meshes_) {
        lo = QVector3D(std::min(lo.x(), m->bmin.x()), std::min(lo.y(), m->bmin.y()), std::min(lo.z(), m->bmin.z()));
        hi = QVector3D(std::max(hi.x(), m->bmax.x()), std::max(hi.y(), m->bmax.y()), std::max(hi.z(), m->bmax.z()));
    }
    if (meshes_.empty()) {
        lo = QVector3D(-50, -50, 0);
        hi = QVector3D(50, 50, 20);
    }
    target_ = (lo + hi) * 0.5f;
    const float r = std::max((hi - lo).length() * 0.5f, 1.0f);
    const float aspect = float(std::max(width(), 1)) / float(std::max(height(), 1));
    const float half = qDegreesToRadians(kFovDeg / 2) * std::min(1.0f, aspect);
    distance_ = r / std::sin(half) * 1.08f;
    update();
}

void Viewport::homeView()
{
    yaw_ = -60;
    pitch_ = 28;
    fitAll();
}

void Viewport::viewTop()
{
    yaw_ = -90;
    pitch_ = 89.9f;
    update();
}

void Viewport::viewFront()
{
    yaw_ = -90;
    pitch_ = 0;
    update();
}

void Viewport::viewRight()
{
    yaw_ = 0;
    pitch_ = 0;
    update();
}

void Viewport::setPerspective(bool on)
{
    perspective_ = on;
    update();
}

}  // namespace jigapp
