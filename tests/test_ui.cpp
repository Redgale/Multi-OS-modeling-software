// SPDX-License-Identifier: GPL-3.0-or-later
// UI workflow test: drives the real application window the way a person would
// (clicks in the 3D view, typed values, panel OK), and checks the resulting model.
// Brief 7.1: "UI smoke and workflow tests that drive the real app", with screenshots.
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <numbers>
#include <QProgressDialog>
#include <QLockFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QSysInfo>

#include "FeaturePanels.hpp"
#include "InspectPanels.hpp"
#include "Icons.hpp"
#include "MainWindow.hpp"
#include "MarkingMenu.hpp"
#include "Translations.hpp"
#include "SketchEditor.hpp"
#include "Tutorial.hpp"
#include "Viewport.hpp"
#include "jig/commands.hpp"
#include "jig/io.hpp"

using namespace jigapp;

namespace
{
double volume(MainWindow& w)
{
    return jig::massProperties(w.document().combinedShape()).volume;
}
QString shotPath(const char* name)
{
    return QDir::current().absoluteFilePath(QString("ui-%1.png").arg(name));
}
}  // namespace

class UiTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setApplicationName("Jigsmith-UiTest");
        QSettings().clear();
        QSettings().setValue("welcomeShown", true);
    }
    void init()
    {
        forgetRememberedValues();  // each test starts with the panels' first-use values
    }

    void everyCommandHasAnIcon()
    {
        for (auto& c : jig::CommandRegistry::instance().all()) {
            const QString id = QString::fromStdString(c.id);
            QVERIFY2(hasToolIcon(id), qPrintable(id));
            QImage img = toolIcon(id, Qt::black, Qt::blue).pixmap(24, 24).toImage();
            int inked = 0;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    inked += qAlpha(img.pixel(x, y)) > 0;
                }
            }
            QVERIFY2(inked > 20, qPrintable(id + " draws almost nothing"));
        }
        // and every kind of timeline step
        for (int k = 0; k <= int(jig::FeatureKind::Import); ++k) {
            const QString id = QString("feature.") + jig::toString(jig::FeatureKind(k));
            QVERIFY2(hasToolIcon(id), qPrintable(id));
        }
    }

    void sketchExtrudeFilletUndoEdit()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();

        // 1. Sketch: click the floor (XY) plane in the 3D view.
        w.runCommand("sketch.create");
        QTest::qWait(50);
        // a floor point in front of the XZ (front) plane, so the floor is the nearest plane hit
        const QPoint onFloor = view->project(QVector3D(20, -25, 0)).toPoint();
        QTest::mouseMove(view, onFloor);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, onFloor);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        SketchEditor* sk = w.sketchEditor();
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Line));
        QCOMPARE(QString::fromStdString(w.document().model().features[0].params.value("plane", std::string {})),
                 QString("XY"));

        // 2. Center rectangle: click the centre, move, type 60 Tab 40 Enter.
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        const QPoint c = sk->screenOf(0, 0);
        QTest::mouseMove(view, c);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60", "40"});
        int lines = 0, dims = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            lines += e.type == jig::EntityType::Line && !e.construction;
        }
        for (auto& [id, k] : sk->sketch().constraints()) {
            dims += k.type == jig::ConstraintType::Distance;
        }
        QCOMPARE(lines, 4);
        QCOMPARE(dims, 2);
        QVERIFY(w.document().state().sketches.begin()->second->profiles.regions.size() == 1);
        w.grab().save(shotPath("sketch"));

        // A dimension that contradicts the rectangle is refused, not applied.
        const size_t before = sk->sketch().constraints().size();
        sk->setTool(SketchEditor::Tool::Select);
        QVERIFY(!sk->applyConstraint(jig::ConstraintType::Parallel));  // nothing selected: explained
        QVERIFY(!sk->message().isEmpty());
        QCOMPARE(sk->sketch().constraints().size(), before);

        w.runCommand("sketch.finish");
        QVERIFY(w.sketchEditor() == nullptr);

        // 3. Extrude 10 mm with the panel (Auto picks "new body" for the first shape).
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("10 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(qAbs(volume(w) - 24000) < 1e-6);

        // 4. Fillet: pick the four top edges through the real GPU edge picker.
        w.runCommand("solid.fillet");
        auto* fp = qobject_cast<FilletPanel*>(w.featurePanel());
        QVERIFY(fp);
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(200);
        const auto& body = *w.document().state().bodies.begin()->second;
        const std::string bodyId = w.document().state().bodies.begin()->first;
        const int top = body.findFaces("e1/end")[0];
        int picked = 0;
        for (int e : body.edgesOfFace(top)) {
            auto fpnt = body.edgeFingerprint(e);
            const QPoint px = view->project(QVector3D(float(fpnt.cx), float(fpnt.cy), float(fpnt.cz))).toPoint();
            auto hit = view->pickEdgeAt(px);
            if (hit && hit->edge == e) {
                ++picked;
            }
            fp->toggleEdge(bodyId, e);
        }
        QVERIFY2(picked >= 2, "the GPU edge picker should find the visible top edges");
        QCOMPARE(int(fp->edgeCount()), 4);
        fp->radius()->setText("2 mm");
        QTRY_VERIFY(fp->previewOk());
        w.grab().save(shotPath("fillet-panel"));
        QVERIFY(fp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double filleted = volume(w);
        // a 2 mm round along the 200 mm top outline removes (4 - pi) * 200 = 171.7 mm^3, less the
        // four corner mitres the estimate counts twice (about r^3 (1 - pi/4) = 0.86 mm^3 each)
        QVERIFY2(std::abs(filleted - (24000 - (4 - M_PI) * 200)) < 5, qPrintable(QString::number(filleted)));

        // An impossible radius explains itself and offers a working value.
        w.runCommand("solid.fillet");
        auto* bad = qobject_cast<FilletPanel*>(w.featurePanel());
        QVERIFY(bad);
        const auto& body2 = *w.document().state().bodies.begin()->second;
        for (int e : body2.edgesOfFace(body2.findFaces("e1/start")[0])) {
            bad->toggleEdge(bodyId, e);
        }
        bad->radius()->setText("30 mm");
        QTRY_VERIFY(bad->statusText().contains("too big"));
        QVERIFY(!bad->previewOk());
        QVERIFY(!bad->accept());
        bad->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(volume(w), filleted);  // Cancel changed nothing

        // 5. Undo the fillet.
        w.runCommand("edit.undo");
        QVERIFY(qAbs(volume(w) - 24000) < 1e-6);

        // 6. Edit the extrude from the timeline: 20 mm.
        w.editFeature(w.document().featureIndex("e1"));
        auto* edit = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(edit);
        QCOMPARE(edit->distance()->text(), QString("10 mm"));
        edit->distance()->setText("20 mm");
        QTRY_VERIFY(edit->previewOk());
        QVERIFY(edit->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(qAbs(volume(w) - 48000) < 1e-6);
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(150);
        w.grab().save(shotPath("model"));
    }

    void penCupShellAndChamfer()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        QVERIFY(sk);
        sk->setTool(SketchEditor::Tool::Circle);
        const QPoint c = sk->screenOf(0, 0);
        QTest::mouseMove(view, c);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(view, sk->screenOf(20, 0));
        sk->typeValues({"80 mm"});
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("95 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);

        // Shell: click the top face in the 3D view to leave it open.
        w.runCommand("solid.shell");
        auto* sh = qobject_cast<ShellPanel*>(w.featurePanel());
        QVERIFY(sh);
        QTRY_VERIFY(sh->previewOk());     // closed hollow preview first
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(200);
        QSignalSpy clicks(view, &Viewport::faceClicked);
        const QPoint top = view->project(QVector3D(0, 0, 95)).toPoint();
        QTest::mouseMove(view, top);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, top);
        QCOMPARE(clicks.count(), 1);
        QCOMPARE(clicks.at(0).at(1).toString(), QString("e1/end"));
        QTRY_COMPARE(int(sh->faceCount()), 1);
        sh->thickness()->setText("2.4 mm");
        QTest::qWait(300);  // the preview is debounced: let it run and re-tessellate
        QTRY_VERIFY(sh->previewOk());
        QTRY_VERIFY(view->meshesReady());
        QTest::qWait(1500);
        w.grab().save(shotPath("shell-panel"));
        QVERIFY(sh->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double shelled = M_PI * 40 * 40 * 95 - M_PI * 37.6 * 37.6 * (95 - 2.4);
        QVERIFY2(std::abs(volume(w) - shelled) < 1, qPrintable(QString::number(volume(w))));

        // Chamfer the outside bottom edge so the print does not flare.
        w.runCommand("solid.chamfer");
        auto* ch = qobject_cast<FilletPanel*>(w.featurePanel());
        QVERIFY(ch);
        const auto& body = *w.document().state().bodies.begin()->second;
        const std::string bodyId = w.document().state().bodies.begin()->first;
        const auto bottom = body.findFaces("e1/start");
        QCOMPARE(int(bottom.size()), 1);
        for (int e : body.edgesOfFace(bottom[0])) {
            ch->toggleEdge(bodyId, e);
        }
        ch->radius()->setText("0.5 mm");
        QTRY_VERIFY(ch->previewOk());
        // the second-size fields appear only for the types that use them
        QVERIFY(!ch->second()->isVisible());
        ch->type()->setCurrentIndex(ch->type()->findData("two"));
        QVERIFY(ch->second()->isVisible());
        ch->second()->setText("1 mm");
        QTest::qWait(300);
        QTRY_VERIFY2(ch->previewOk(), qPrintable(ch->statusText()));
        ch->type()->setCurrentIndex(ch->type()->findData("equal"));
        QTest::qWait(300);
        QTRY_VERIFY(ch->previewOk());
        QVERIFY(ch->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(volume(w) < shelled);
        QCOMPARE(int(w.document().model().features.size()), 4);
        QVERIFY(w.document().model().features[3].kind == jig::FeatureKind::Chamfer);
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(150);
        w.grab().save(shotPath("pen-cup"));
    }

    // Polygon, Slot and Arc: clicks plus typed values, as a person would draw them.
    void polygonSlotArcTools()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        QVERIFY(sk);
        auto click = [&](double x, double y) {
            const QPoint p = sk->screenOf(x, y);
            QTest::mouseMove(view, p);
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, p);
        };

        // Hexagon: centre, then type diameter 20 and 6 sides.
        w.runCommand("sketch.polygon");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Polygon));
        click(0, 0);
        QTest::mouseMove(view, sk->screenOf(0, 10));
        sk->typeValues({"20", "6"});
        // Slot: two centre clicks, then type the width.
        w.runCommand("sketch.slot");
        click(30, 0);
        click(50, 0);
        QTest::mouseMove(view, sk->screenOf(40, 5));
        sk->typeValues({"8"});
        // Arc: centre, start, then type the angle it spans.
        w.runCommand("sketch.arc");
        click(0, 40);
        click(10, 40);
        QTest::mouseMove(view, sk->screenOf(0, 50));
        sk->typeValues({"90 deg"});

        // 3-point arc: start (30,30), end (50,30), through (40,35): radius 12.5 by hand
        w.runCommand("sketch.arc3");
        click(30, 30);
        click(50, 30);
        click(40, 35);
        bool r125 = false;
        for (auto& [id, e] : sk->sketch().entities()) {
            r125 = r125 || (e.type == jig::EntityType::Arc && std::abs(e.r - 12.5) < 1e-6);
        }
        QVERIFY(r125);

        int lines = 0, arcs = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            lines += e.type == jig::EntityType::Line && !e.construction;
            arcs += e.type == jig::EntityType::Arc;
        }
        QCOMPARE(lines, 6 + 2);
        QCOMPARE(arcs, 2 + 1 + 1);
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        const auto& regions = w.document().state().sketches.begin()->second->profiles.regions;
        QCOMPARE(int(regions.size()), 2);  // the open arc encloses nothing
        std::vector<double> areas;
        for (auto& r : regions) {
            areas.push_back(r.area);
        }
        std::sort(areas.begin(), areas.end());
        QVERIFY2(std::abs(areas[0] - (20 * 8 + M_PI * 16)) < 1e-3, qPrintable(QString::number(areas[0])));
        QVERIFY2(std::abs(areas[1] - 3 * std::sqrt(3.0) / 2 * 100) < 1e-3, qPrintable(QString::number(areas[1])));
        w.grab().save(shotPath("sketch-shapes"));
    }

    // Text: click, type the words and a size; double-click the anchor to change them.
    // The grid lies in the sketch's plane while sketching (not always on the floor), and
    // follows the zoom; the floor grid comes back after.
    void sketchGridInSketchPlane()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(!view->sketchGrid());
        QVERIFY(w.createSketchOn("XZ"));
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        QVERIFY(view->sketchGrid());
        QVERIFY(view->sketchGrid()->Direction().IsParallel(gp_Dir(0, 1, 0), 1e-9));  // the front plane, not the floor
        QTest::qWait(200);
        w.grab().save(shotPath("sketch-grid-front"));
        const double step = view->sketchGridStep();
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
        QVERIFY(!view->sketchGrid());
        QVERIFY(w.createSketchOn("YZ"));
        QVERIFY(view->sketchGrid()->Direction().IsParallel(gp_Dir(1, 0, 0), 1e-9));
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
        // on a block's top face: the grid on the face, and a finished sketch there stays visible
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=b plane=XY\nrect sketch=b center=0,0 w=60 h=40\nextrude id=e1 sketch=b distance=10\n"
                              "sketch id=t on=e1/end\ncircle sketch=t center=0,0 d=16\n").ok());
        }
        view->homeView();
        w.editFeature(int(w.document().model().features.size()) - 1);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        QVERIFY(view->sketchGrid()->Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
        QTest::qWait(200);
        w.grab().save(shotPath("sketch-on-face"));
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
        view->homeView();
        QTest::qWait(200);
        w.grab().save(shotPath("finished-sketch-on-face"));
        (void)step;
    }

    // A finished sketch stays visible until a later step uses it; the Browser can show it again.
    void finishedSketchIsDrawn()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"40", "20"});
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
        const std::string id = w.document().model().features[0].id;
        QVERIFY(w.sketchVisible(id));
        QVERIFY(view->datumSegments() >= 4);  // its four sides are drawn
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("5 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(!w.sketchVisible(id));  // used by the extrude: it hides itself
        QCOMPARE(view->datumSegments(), 0);
        w.setSketchVisible(id, true);   // shown again by hand (the Browser tick)
        QVERIFY(w.sketchVisible(id));
        QVERIFY(view->datumSegments() >= 4);
        QTest::qWait(200);
        w.grab().save(shotPath("finished-sketch"));
    }

    // Line midpoints are snap targets: a line started there stays at the middle.
    void snapToMidpoint()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        sk->setTool(SketchEditor::Tool::Line);
        click(0, 0);
        click(23, 0);
        QTest::keyClick(view, Qt::Key_Escape);  // end the chain
        sk->setTool(SketchEditor::Tool::Line);
        click(11.6, 0.3);  // near the middle (11.5, 0): it snaps there
        click(11.5, 15);
        int mids = 0;
        for (auto& [cid, c] : sk->sketch().constraints()) {
            mids += c.type == jig::ConstraintType::Midpoint;
        }
        QCOMPARE(mids, 1);
        // the constrained point is at the middle of the first line, wherever its end snapped to
        bool atMiddle = false;
        for (auto& [cid, c] : sk->sketch().constraints()) {
            if (c.type != jig::ConstraintType::Midpoint) {
                continue;
            }
            const jig::SketchEntity* L = sk->sketch().entity(c.b);
            QVERIFY(L);
            auto [x1, y1] = sk->sketch().pointXY(L->p1);
            auto [x2, y2] = sk->sketch().pointXY(L->p2);
            auto [px, py] = sk->sketch().pointXY(c.a);
            atMiddle = std::abs(px - (x1 + x2) / 2) < 1e-6 && std::abs(py - (y1 + y2) / 2) < 1e-6 && std::abs(x2 - x1) > 15;
        }
        QVERIFY(atMiddle);
    }

    // Deleting a smooth curve's point in the editor (this left the curve pointing at a point
    // that was gone, and the next solve crashed): the curve no longer passes through it.
    void deleteSmoothCurvePoint()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nspline sketch=s1 points=0,0;10,10;20,0;30,10\n").ok());
        }
        w.editFeature(0);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        SketchEditor* sk = w.sketchEditor();
        Viewport* view = w.viewport();
        auto splinePoints = [&] {
            for (auto& [id, e] : sk->sketch().entities()) {
                if (e.type == jig::EntityType::Spline) {
                    return int(e.pts.size());
                }
            }
            return 0;
        };
        QCOMPARE(splinePoints(), 4);
        sk->setTool(SketchEditor::Tool::Select);
        const QPoint at = sk->screenOf(10, 10);
        QTest::mouseMove(view, at);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
        QCOMPARE(int(sk->selection().size()), 1);
        QTest::keyClick(view, Qt::Key_Delete);
        QTRY_COMPARE(splinePoints(), 3);
        QVERIFY(w.document().status(0).level != jig::StatusLevel::Error);
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
    }

    // Text placed on a circle bends along it.
    void textAlongCircle()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=40 construction=yes\n").ok());
        }
        w.editFeature(0);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        SketchEditor* sk = w.sketchEditor();
        Viewport* view = w.viewport();
        w.runCommand("sketch.text");
        const QPoint at = sk->screenOf(0, 20);
        QTest::mouseMove(view, at);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
        sk->typeValues({"OOO", "5 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(int(sk->sketch().texts().size()), 1);
        QVERIFY(sk->sketch().texts().begin()->second.path != 0);
        // the outer letters come down round the circle
        double ymin = 1e9;
        for (auto& rg : w.document().state().sketches.at("s1")->profiles.regions) {
            GProp_GProps g;
            BRepGProp::SurfaceProperties(rg.face, g);
            ymin = std::min(ymin, g.CentreOfMass().Y());
        }
        QVERIFY2(ymin < 19.8, qPrintable(QString::number(ymin)));
    }

    void textToolPlaceAndEdit()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        QVERIFY(sk);
        w.runCommand("sketch.text");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Text));
        const QPoint at = sk->screenOf(0, 0);
        QTest::mouseMove(view, at);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
        sk->typeValues({"Hi", "10 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        auto regions = [&] { return int(w.document().state().sketches.begin()->second->profiles.regions.size()); };
        QCOMPARE(regions(), 3);  // H, i stem, i dot
        QCOMPARE(int(sk->sketch().texts().size()), 1);
        const int anchor = sk->sketch().texts().begin()->second.anchor;
        sk->setTool(SketchEditor::Tool::Select);
        QVERIFY(sk->editTextAt(anchor));
        sk->typeValues({"Hello", "12 mm"});
        QCOMPARE(regions(), 5);  // H e l l o
        QCOMPARE(QString::fromStdString(sk->sketch().texts().begin()->second.height), QString("12 mm"));
        w.grab().save(shotPath("sketch-text"));
        // Undo brings back the first words.
        w.runCommand("edit.undo");
        QTRY_COMPARE(regions(), 3);
        // the font: built-in Inter first; an installed family when there is one
        QVERIFY(sk->editTextAt(anchor));
        auto* font = view->findChild<QComboBox*>();
        QVERIFY(font && font->isVisible());
        QCOMPARE(font->currentData().toString(), QString());  // Inter (built in)
        if (font->count() > 1) {
            font->setCurrentIndex(1);
            const QString family = font->currentData().toString();
            sk->typeValues({"Hi", "10 mm"});
            QCOMPARE(QString::fromStdString(sk->sketch().texts().begin()->second.font), family);
            QVERIFY(regions() >= 2);
        }
    }

    // Measure: click a face, then an edge; Properties: volume and weight.
    void measureAndProperties()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        const QPoint c = sk->screenOf(0, 0);
        QTest::mouseMove(view, c);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60", "40"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(int(sk->sketch().entities().size()), 10);  // 4 corners, 4 sides, diagonal, centre
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("20 mm");
        QTRY_VERIFY2(ex->previewOk(), qPrintable(ex->statusText()));
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);

        w.runCommand("inspect.measure");
        auto* mp = qobject_cast<MeasurePanel*>(w.featurePanel());
        QVERIFY(mp);
        QCOMPARE(int(view->pickMode()), int(PickMode::Any));
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(200);
        // a click in the middle of the top face picks the face
        const QPoint top = view->project(QVector3D(5, 5, 20)).toPoint();
        QTest::mouseMove(view, top);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, top);
        QVERIFY2(mp->resultText().contains("flat face"), qPrintable(mp->resultText()));
        QVERIFY2(mp->resultText().contains("Area: 2400 mm²"), qPrintable(mp->resultText()));
        // a click on the front top edge picks the edge: they touch, 60 mm long
        const QPoint edge = view->project(QVector3D(10, -20, 20)).toPoint();
        QTest::mouseMove(view, edge);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, edge);
        QVERIFY2(mp->resultText().contains("Length: 60 mm"), qPrintable(mp->resultText()));
        QVERIFY2(mp->resultText().contains("they touch"), qPrintable(mp->resultText()));
        // top to bottom, by name: 20 mm apart, parallel
        const std::string body = w.document().state().bodies.begin()->first;
        mp->pick(body, "e1/end", false);
        mp->pick(body, "e1/start", false);
        QVERIFY2(mp->resultText().contains("Distance: 20 mm"), qPrintable(mp->resultText()));
        QVERIFY2(mp->resultText().contains("Angle: 0°"), qPrintable(mp->resultText()));
        QTest::qWait(150);  // let the panel lay out its longer text before the screenshot
        w.grab().save(shotPath("measure"));
        mp->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QTRY_COMPARE(int(view->pickMode()), int(PickMode::Faces));  // the panel is deleted later

        PropertiesDialog props(w.document(), &w);
        QCOMPARE(props.table()->item(0, 0)->text(), QString("48 cm³  (48000 mm³)"));
        QCOMPARE(props.table()->item(1, 0)->text(), QString("59.52 g"));  // PLA, 1.24 g/cm³
        props.material()->setCurrentIndex(props.material()->findText("Aluminium"));
        QCOMPARE(props.table()->item(1, 0)->text(), QString("129.6 g"));
    }

    // Construction planes: offset from a clicked face, sketch on it; a midplane.
    void constructionPlanes()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60", "40"});
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("20 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);

        // Offset plane 15 mm above the top face, picked by clicking the face.
        w.runCommand("construct.offset_plane");
        auto* pp = qobject_cast<PlanePanel*>(w.featurePanel());
        QVERIFY(pp);
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(200);
        const QPoint top = view->project(QVector3D(5, 5, 20)).toPoint();
        QTest::mouseMove(view, top);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, top);
        QVERIFY2(pp->base()->currentText().contains("e1/end"), qPrintable(pp->base()->currentText()));
        pp->distance()->setText("15 mm");
        QTest::qWait(300);
        QTRY_VERIFY2(pp->previewOk(), qPrintable(pp->statusText()));
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().planes.size()), 1);
        const gp_Ax3 pl = w.document().state().planes.begin()->second;
        QVERIFY(std::abs(pl.Location().Z() - 35) < 1e-9);
        QTest::qWait(200);
        w.grab().save(shotPath("construction-plane"));

        // The sketch picker offers the construction plane: click it above the body.
        w.runCommand("sketch.create");
        QTest::qWait(50);
        const QPoint onPlane = view->project(QVector3D(-30, 30, 35)).toPoint();
        QTest::mouseMove(view, onPlane);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, onPlane);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        const auto& feats = w.document().model().features;
        QCOMPARE(QString::fromStdString(feats.back().params.value("plane", std::string {})),
                 QString::fromStdString(w.document().state().planes.begin()->first));
        w.runCommand("sketch.finish");

        // Midplane between the top and bottom faces (chosen by name, as a click would).
        w.runCommand("construct.midplane");
        auto* mp = qobject_cast<PlanePanel*>(w.featurePanel());
        QVERIFY(mp);
        const std::string body = w.document().state().bodies.begin()->first;
        mp->useFace(body, "e1/end");
        mp->useFace(body, "e1/start");
        QTest::qWait(300);
        QTRY_VERIFY2(mp->previewOk(), qPrintable(mp->statusText()));
        QVERIFY(mp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        double z = -1;
        for (auto& [id, f] : w.document().state().planes) {
            if (id != w.document().state().planes.begin()->first) {
                z = f.Location().Z();
            }
        }
        QVERIFY2(std::abs(z - 10) < 1e-9, qPrintable(QString::number(z)));
    }

    // Move/copy, Combine and Split, each through its panel.
    void moveCombineSplit()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60", "40"});
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("20 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        auto bodyVolume = [&] {
            double v = 0;
            for (auto& [id, b] : w.document().state().bodies) {
                v += jig::massProperties(b->shape()).volume;
            }
            return v;
        };

        // A copy shifted 30 mm along X (the only body is ticked already).
        w.runCommand("solid.move");
        auto* mv = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(mv);
        QCOMPARE(mv->bodies()->item(0)->checkState(), Qt::Checked);
        mv->dx()->setText("30 mm");
        mv->copy()->setChecked(true);
        QTest::qWait(300);
        QTRY_VERIFY2(mv->previewOk(), qPrintable(mv->statusText()));
        QVERIFY(mv->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().bodies.size()), 2);
        QVERIFY(std::abs(bodyVolume() - 2 * 48000) < 1e-6);

        // Join them: 90 x 40 x 20.
        w.runCommand("solid.combine");
        auto* cb = qobject_cast<CombinePanel*>(w.featurePanel());
        QVERIFY(cb);
        QTest::qWait(300);
        QTRY_VERIFY2(cb->previewOk(), qPrintable(cb->statusText()));
        QVERIFY(cb->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().bodies.size()), 1);
        QVERIFY(std::abs(bodyVolume() - 90 * 40 * 20) < 1e-6);

        // Split along the side plane: 30 mm on the -X side, 60 mm on the +X side.
        w.runCommand("solid.split");
        auto* sp = qobject_cast<SplitPanel*>(w.featurePanel());
        QVERIFY(sp);
        QCOMPARE(sp->plane()->currentData().toString(), QString("\"YZ\""));
        QTest::qWait(300);
        QTRY_VERIFY2(sp->previewOk(), qPrintable(sp->statusText()));
        QVERIFY(sp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        std::vector<double> vols;
        for (auto& [id, b] : w.document().state().bodies) {
            vols.push_back(jig::massProperties(b->shape()).volume);
        }
        std::sort(vols.begin(), vols.end());
        QCOMPARE(int(vols.size()), 2);
        QVERIFY(std::abs(vols[0] - 30 * 40 * 20) < 1e-6);
        QVERIFY(std::abs(vols[1] - 60 * 40 * 20) < 1e-6);
        // editing the move from the timeline brings back its values
        w.editFeature(2);
        auto* again = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(again);
        QCOMPARE(again->dx()->text(), QString("30 mm"));
        QVERIFY(again->copy()->isChecked());
        again->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
    }

    // Split with a curved face: the underside of a lying cylinder takes a channel off a block.
    void splitWithCurvedFace()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=c plane=XZ\ncircle sketch=c center=0,30 d=50\n"
                              "extrude id=cyl sketch=c distance=40 direction=symmetric op=new\n").ok());
        }
        std::string round;
        const auto& cyl = *w.document().state().bodies.at("cyl");
        for (int i = 0; i < cyl.faceCount(); ++i) {
            if (jig::measureItem(cyl.face(i)).radius) {
                round = cyl.faceName(i);
            }
        }
        w.runCommand("solid.split");
        auto* sp = qobject_cast<SplitPanel*>(w.featurePanel());
        QVERIFY(sp);
        sp->body()->setCurrentIndex(sp->body()->findData("e1"));
        sp->useFace("cyl", round);
        QTest::qWait(300);
        QTRY_VERIFY2(sp->previewOk(), qPrintable(sp->statusText()));
        QVERIFY(sp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().bodies.size()), 3);  // the two pieces and the cylinder
        const double cap = jig::massProperties(w.document().state().bodies.at(w.document().model().features.back().id)->shape()).volume;
        QVERIFY2(std::abs(cap - 30 * ((300 + 625 * std::asin(0.6)) - 600)) < 1e-3, qPrintable(QString::number(cap)));
    }

    // Section view: the cut side disappears and the inside shows in the inside colour.
    void sectionView()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.openSample("knob"));
        QTRY_VERIFY(view->meshesReady());
        view->viewFront();
        QTest::qWait(300);
        const QPoint mid = view->project(QVector3D(5, 0, 14)).toPoint();
        auto pixel = [&] { return view->grabFramebuffer().pixelColor(mid * view->devicePixelRatioF()); };
        const QColor before = pixel();
        w.runCommand("inspect.section");
        auto* sp = qobject_cast<SectionPanel*>(w.featurePanel());
        QVERIFY(sp);
        QVERIFY(view->section().has_value());
        QTest::qWait(200);
        const QColor after = pixel();
        // the front half is gone and the cut is filled in (tan: warm, not the part's colour)
        QVERIFY(sp->capArea() > 0);
        QVERIFY2(after.red() > after.blue() + 60 && after != before, qPrintable(after.name() + " " + before.name()));
        w.grab().save(shotPath("section"));
        // the design itself is untouched, and closing restores the full view
        QCOMPARE(int(w.document().model().features.size()), 10);
        sp->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QTRY_VERIFY(!view->section().has_value());
    }

    // Dark is the default theme; an inch design reads plain numbers as inches and shows
    // every size in inches.
    void darkDefaultAndInchDesign()
    {
        QSettings().remove("appearance/theme");
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY2(w.palette().color(QPalette::Window).lightness() < 80,
                 qPrintable(w.palette().color(QPalette::Window).name()));
        Viewport* view = w.viewport();
        w.setDesignUnits(jig::LengthUnit::Inch);
        QCOMPARE(int(w.document().units()), int(jig::LengthUnit::Inch));
        QCOMPARE(QString::fromStdString(w.document().defaultLength(10, 0.5)), QString("0.5 in"));

        // a 2 x 1 rectangle typed as plain numbers
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"2", "1"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        // (the panel may show a value remembered from earlier in the session)
        ex->distance()->edit()->setText("0.25");               // a plain number: inches
        QCOMPARE(ex->distance()->text(), QString("0.25 in"));
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double in3 = 25.4 * 25.4 * 25.4;
        QVERIFY2(std::abs(volume(w) - 2 * 1 * 0.25 * in3) < 1e-6, qPrintable(QString::number(volume(w))));

        // Measure and Properties speak inches
        w.runCommand("inspect.measure");
        auto* mp = qobject_cast<MeasurePanel*>(w.featurePanel());
        QVERIFY(mp);
        const std::string body = w.document().state().bodies.begin()->first;
        mp->pick(body, "e1/end", false);
        mp->pick(body, "e1/start", false);
        QVERIFY2(mp->resultText().contains("Distance: 0.25 in"), qPrintable(mp->resultText()));
        QVERIFY2(mp->resultText().contains("Area: 2 in²"), qPrintable(mp->resultText()));
        mp->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
        PropertiesDialog props(w.document(), &w);
        QVERIFY2(props.table()->item(0, 0)->text().startsWith("0.5 in³"), qPrintable(props.table()->item(0, 0)->text()));
        QVERIFY2(props.table()->item(1, 0)->text().contains(" oz"), qPrintable(props.table()->item(1, 0)->text()));
        QVERIFY2(props.table()->item(4, 0)->text() == "2 × 1 × 0.25 in", qPrintable(props.table()->item(4, 0)->text()));

        // switching back to millimetres resizes nothing
        w.setDesignUnits(jig::LengthUnit::Millimetre);
        QVERIFY(std::abs(volume(w) - 2 * 1 * 0.25 * in3) < 1e-6);
        w.runCommand("edit.undo");  // the unit change is undoable
        QCOMPARE(int(w.document().units()), int(jig::LengthUnit::Inch));
        w.grab().save(shotPath("inch-dark"));
    }

    // Trim: hover shows the piece, a click removes it.
    void trimTool()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        click(0, 0);
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"20 mm", "20 mm"});
        sk->setTool(SketchEditor::Tool::Circle);
        click(10, 10);  // snaps to the rectangle's corner
        QTest::mouseMove(view, sk->screenOf(14, 10));
        sk->typeValues({"10 mm"});
        auto regions = [&] { return int(w.document().state().sketches.begin()->second->profiles.regions.size()); };
        QCOMPARE(regions(), 3);
        w.runCommand("sketch.trim");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Trim));
        const double r = 5 / std::sqrt(2.0);
        click(10 + r, 10 + r);  // on the circle, outside the rectangle
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(regions(), 2);  // the rectangle, split by the quarter arc that is left
        int arcs = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            arcs += e.type == jig::EntityType::Arc;
        }
        QCOMPARE(arcs, 1);
        w.runCommand("edit.undo");  // one undo brings the circle back
        QTRY_COMPARE(regions(), 3);

        // Offset: click the rectangle's side, type -3 mm: a smaller rectangle inside it
        w.runCommand("sketch.offset");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Offset));
        click(-10, 0);
        sk->typeValues({"-3 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(regions(), 5);  // the circle (back after the undo) also crosses the new corner
        int at7 = 0;               // the four new sides sit 3 mm inside, at x or y = ±7
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Line && !e.construction) {
                auto [ax, ay] = sk->sketch().pointXY(e.p1);
                auto [bx, by] = sk->sketch().pointXY(e.p2);
                at7 += (std::abs(std::abs(ax) - 7) < 1e-9 && std::abs(ax - bx) < 1e-9) ||
                       (std::abs(std::abs(ay) - 7) < 1e-9 && std::abs(ay - by) < 1e-9);
            }
        }
        QCOMPARE(at7, 4);

        // Round corner: click the rectangle's bottom-left corner, type 4 mm
        w.runCommand("sketch.fillet");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Round));
        click(-10, -10);
        sk->typeValues({"4 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        int arcsNow = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            arcsNow += e.type == jig::EntityType::Arc && std::abs(e.r - 4) < 1e-9;
        }
        QCOMPARE(arcsNow, 1);
    }

    // Extend a loose line to a rectangle, break it, and mirror the rectangle across a line.
    void extendBreakMirror()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, mods, sk->screenOf(x, y));
        };
        auto count = [&](jig::EntityType t) {
            int n = 0;
            for (auto& [id, e] : sk->sketch().entities()) {
                n += e.type == t && !e.construction;
            }
            return n;
        };
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        click(40, 0);
        QTest::mouseMove(view, sk->screenOf(45, 5));
        sk->typeValues({"40 mm", "40 mm"});  // x from 20 to 60, y from -20 to 20
        sk->setTool(SketchEditor::Tool::Line);
        click(5, 5);  // on the snap grid
        click(10, 5);
        QTest::keyClick(view, Qt::Key_Escape);
        QCOMPARE(count(jig::EntityType::Line), 5);

        w.runCommand("sketch.extend");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Extend));
        click(9, 5);  // near the loose right end: it runs on to the rectangle's left side at x = 20
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        double maxX = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Line && std::abs(sk->sketch().pointXY(e.p1).second - 5) < 1e-6 &&
                std::abs(sk->sketch().pointXY(e.p2).second - 5) < 1e-6) {
                maxX = std::max({maxX, sk->sketch().pointXY(e.p1).first, sk->sketch().pointXY(e.p2).first});
            }
        }
        QVERIFY(std::abs(maxX - 20) < 1e-6);

        w.runCommand("sketch.break");
        click(20, -7);  // the rectangle's left side, below where the line meets it
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(count(jig::EntityType::Line), 6);

        // mirror the rectangle's five sides across a construction line at x = 0
        sk->setTool(SketchEditor::Tool::Line);
        click(0, -40);
        click(0, 40);
        QTest::keyClick(view, Qt::Key_Escape);
        sk->setTool(SketchEditor::Tool::Select);
        click(0, 30);
        w.runCommand("sketch.construction");
        sk->setTool(SketchEditor::Tool::Select);
        click(60, 0);
        click(40, 20, Qt::ShiftModifier);
        click(40, -20, Qt::ShiftModifier);
        click(20, -7, Qt::ShiftModifier);  // away from the corners, which a click would pick first
        click(20, 12.5, Qt::ShiftModifier);
        QCOMPARE(int(sk->selection().size()), 5);
        w.runCommand("sketch.mirror");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Mirror));
        click(0, 30);
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(count(jig::EntityType::Line), 11);
        QVERIFY(sk->report().redundant.empty());
        QCOMPARE(int(w.document().state().sketches.begin()->second->profiles.regions.size()), 2);
    }

    // Move, copy (Ctrl), grid copy and ring copy of selected curves.
    void moveCopyAndPatterns()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, mods, sk->screenOf(x, y));
        };
        auto regions = [&] { return int(w.document().state().sketches.begin()->second->profiles.regions.size()); };
        sk->setTool(SketchEditor::Tool::Circle);
        click(20, 0);
        QTest::mouseMove(view, sk->screenOf(23, 0));
        sk->typeValues({"6 mm"});
        sk->setTool(SketchEditor::Tool::Select);
        click(23, 0);
        QCOMPARE(int(sk->selection().size()), 1);

        // move it 10 mm up by typing
        w.runCommand("sketch.move");
        click(20, 0);
        sk->typeValues({"0", "10 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        const jig::SketchEntity* c = nullptr;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Circle) {
                c = &e;
            }
        }
        QVERIFY(c);
        QVERIFY(std::abs(sk->sketch().pointXY(c->center).second - 10) < 1e-6);

        // ring copy: 4 around the origin
        sk->setTool(SketchEditor::Tool::Select);
        click(23, 10);
        w.runCommand("sketch.pattern_circ");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::PatternCirc));
        click(0, 0);
        sk->typeValues({"4", "360 deg"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(regions(), 4);

        // grid copy of the first circle: 2 columns 50 apart, 1 row
        sk->setTool(SketchEditor::Tool::Select);
        click(23, 10);
        w.runCommand("sketch.pattern_rect");
        sk->typeValues({"2", "50 mm"});
        sk->typeValues({"1", ""});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(regions(), 5);
        w.runCommand("edit.undo");
        QTRY_COMPARE(regions(), 4);

        // Ctrl-click the destination: a copy, the original stays
        sk->setTool(SketchEditor::Tool::Select);
        click(23, 10);
        w.runCommand("sketch.move");
        click(20, 10);
        click(20, 40, Qt::ControlModifier);
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(regions(), 5);

        // turn the copy a quarter turn around the origin: typed as the third value
        sk->setTool(SketchEditor::Tool::Select);
        click(23, 40);
        QCOMPARE(int(sk->selection().size()), 1);
        double cx = 0, cy = 0;  // where the copy landed (the Ctrl-click may have snapped)
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Circle) {
                auto [x, y] = sk->sketch().pointXY(e.center);
                if (std::abs(x - 20) < 1 && std::abs(y - 40) < 1) {
                    cx = x;
                    cy = y;
                }
            }
        }
        QVERIFY(cy > 30);
        w.runCommand("sketch.move");
        click(0, 0);
        sk->typeValues({"0", "0", "90 deg"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        bool turned = false;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Circle) {
                auto [x, y] = sk->sketch().pointXY(e.center);
                turned = turned || (std::abs(x + cy) < 1e-6 && std::abs(y - cx) < 1e-6);  // (x, y) -> (-y, x)
            }
        }
        QVERIFY(turned);
        QCOMPARE(regions(), 5);
    }

    // Extrude up to a clicked face: the new body ends at that face.
    void extrudeUpToFace()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=12\n"
                              "sketch id=s2 plane=XY\ncircle sketch=s2 center=40,0 d=10\n").ok());
        }
        w.runCommand("solid.extrude");
        auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ep);
        ep->useFace("e1", "e1/end");
        QCOMPARE(ep->extent()->currentData().toString(), QString("to"));
        QVERIFY(!ep->distance()->isVisible());  // the face decides how far
        ep->operation()->setCurrentIndex(ep->operation()->findData("new"));
        QTest::qWait(300);
        QTRY_VERIFY(ep->previewOk());
        QVERIFY(ep->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto& st = w.document().state();
        QCOMPARE(int(st.bodies.size()), 2);
        for (auto& [id, b] : st.bodies) {
            QVERIFY(std::abs(jig::massProperties(b->shape()).zmax - 12) < 1e-9);
        }
    }

    // Box selection of bodies (Move): the bodies in the box get ticked.
    void boxSelectBodies()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=s2 plane=XY\nrect sketch=s2 center=40,0 w=10 h=10\nextrude id=e2 sketch=s2 distance=10 op=new\n").ok());
        }
        w.refreshAll();  // the bodies are shown now, not when something else refreshes the window
        Viewport* view = w.viewport();
        w.runCommand("solid.move");
        auto* mv = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(mv);
        QTRY_VERIFY(view->meshesReady());
        view->fitAll();
        view->viewTop();
        QTRY_VERIFY(view->meshesReady());
        QTRY_COMPARE(view->meshedBodyCount(), 2);
        // settled: from above, the second block is under its centre
        QString seen;
        QTRY_VERIFY2([&] {
            const QPoint at = view->project(QVector3D(40, 0, 10)).toPoint();
            auto f = view->pickFaceAt(at);
            seen = QString("at %1,%2 of %3x%4, bodies %5, hit %6 cam %7")
                       .arg(at.x()).arg(at.y()).arg(view->width()).arg(view->height()).arg(view->meshedBodyCount())
                       .arg(f ? QString::fromStdString(f->body) : QString("none"))
                       .arg(QString::fromStdString(view->camera().dump()));
            return f && f->body == "e2";
        }(), qPrintable(seen));
        auto ticked = [&] {
            QStringList t;
            for (int i = 0; i < mv->bodies()->count(); ++i) {
                if (mv->bodies()->item(i)->checkState() == Qt::Checked) {
                    t << mv->bodies()->item(i)->data(Qt::UserRole).toString();
                }
            }
            return t;
        };
        QVERIFY(ticked().isEmpty());  // two bodies: none chosen yet
        // a box around the second block only (left to right: wholly inside)
        // 12 mm round it on the floor, as seen from above
        const QPointF p1 = view->project(QVector3D(28, 12, 0)), p2 = view->project(QVector3D(52, -12, 0));
        const QPoint a(int(std::min(p1.x(), p2.x())), int(std::min(p1.y(), p2.y()))),
            b(int(std::max(p1.x(), p2.x())), int(std::max(p1.y(), p2.y())));
        QVERIFY(!view->pickFaceAt(a));
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view, (a + b) / 2);
        QTest::mouseMove(view, b);
        QVERIFY(view->boxing());
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(ticked(), QStringList {"e2"});
        mv->cancel();
    }

    // Box selection of faces (Shell): only the faces seen in the box are picked.
    void boxSelectFaces()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n").ok());
        }
        Viewport* view = w.viewport();
        w.runCommand("solid.shell");
        auto* sh = qobject_cast<ShellPanel*>(w.featurePanel());
        QVERIFY(sh);
        QTRY_VERIFY(view->meshesReady());
        QTest::qWait(300);  // the preview has settled (it may frame the view)
        {
            // from the angled view: the top and two sides, never the hidden bottom
            view->homeView();
            QTest::qWait(100);
            const auto angled = view->facesInBox(QRectF(QPointF(0, 0), QSizeF(view->size())), true);
            QCOMPARE(int(angled.size()), 3);  // of six: the three facing the camera
        }
        view->viewTop();
        QTest::qWait(100);
        // from above only the top shows: the whole-view box picks it and nothing else
        const QRectF all(QPointF(0, 0), QSizeF(view->size()));
        const auto seen = view->facesInBox(all, true);
        QCOMPARE(int(seen.size()), 1);
        const QPoint a(4, 4), b(view->width() - 4, view->height() - 4);
        QVERIFY(!view->pickFaceAt(a));
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view, QPoint(200, 200));
        QTest::mouseMove(view, b);
        QVERIFY(view->boxing());
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(int(sh->faceCount()), 1);
        sh->thickness()->setText("2 mm");
        QTest::qWait(300);
        QTRY_VERIFY(sh->previewOk());
        QVERIFY(sh->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double v = jig::massProperties(w.document().state().bodies.at("e1")->shape()).volume;
        QVERIFY2(std::abs(v - (12000 - 36.0 * 26 * 8)) < 1e-3, qPrintable(QString::number(v)));  // open at the top
    }

    // Window selection in 3D: a box dragged over empty space picks edges for a fillet.
    void boxSelectEdges()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=12\n").ok());
        }
        w.runCommand("solid.fillet");
        auto* fp = qobject_cast<FilletPanel*>(w.featurePanel());
        QVERIFY(fp);
        Viewport* view = w.viewport();
        QTRY_VERIFY(view->meshesReady());
        QTest::qWait(200);
        // left to right over the whole view: every edge of the box is wholly inside
        const QPoint a(4, 4), b(view->width() - 4, view->height() - 4);
        QVERIFY(!view->pickEdgeAt(a));
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view, QPoint(200, 200));
        QTest::mouseMove(view, b);
        QVERIFY(view->boxing());
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, b);
        QVERIFY(!view->boxing());
        QCOMPARE(int(fp->edgeCount()), 12);
        // the same box with Ctrl takes them away again
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view, QPoint(200, 200));
        QTest::mouseMove(view, b);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::ControlModifier, b);
        QCOMPARE(int(fp->edgeCount()), 0);
        // a small box only touching the model from right to left picks what it crosses,
        // a box wholly inside nothing picks nothing
        const QRectF all(QPointF(0, 0), QSizeF(view->size()));
        QVERIFY(view->edgesInBox(all, false).size() == 12);
        QVERIFY(view->edgesInBox(QRectF(1, 1, 3, 3), true).empty());
        fp->radius()->setText("1 mm");
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(view, QPoint(200, 200));
        QTest::mouseMove(view, b);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, b);
        QTRY_VERIFY(fp->previewOk());
        QVERIFY(fp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().bodies.size()), 1);
    }

    // Pattern along a path: pegs along a sketch line, spread evenly.
    void patternAlongPath()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 corner=-10,-10 w=70 h=40\nextrude id=e1 sketch=s1 distance=3\n"
                              "sketch id=s2 on=e1/end\ncircle sketch=s2 center=0,0 d=4\nextrude id=e2 sketch=s2 distance=5 op=join\n"
                              "sketch id=p plane=XY\nline sketch=p p1=0,0 p2=40,0 construction=yes\n").ok());
        }
        const double before = jig::massProperties(w.document().state().bodies.at("e1")->shape()).volume;
        w.runCommand("solid.pattern_rect");
        auto* pp = qobject_cast<PatternPanel*>(w.featurePanel());
        QVERIFY(pp);
        pp->source()->setCurrentIndex(pp->source()->findData("e2"));
        pp->kind()->setCurrentIndex(pp->kind()->findData("path"));
        pp->pathSketch()->setCurrentIndex(pp->pathSketch()->findData("p"));
        pp->pathCount()->setText("5");
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double after = jig::massProperties(w.document().state().bodies.at("e1")->shape()).volume;
        QVERIFY2(std::abs(after - before - 4 * 4 * M_PI * 5) < 1e-3, qPrintable(QString::number(after - before)));
        w.editFeature(int(w.document().model().features.size()) - 1);
        pp = qobject_cast<PatternPanel*>(w.featurePanel());
        QVERIFY(pp);
        QCOMPARE(pp->kind()->currentData().toString(), QString("path"));
        QCOMPARE(pp->pathSketch()->currentData().toString(), QString("p"));
        pp->cancel();
    }

    // Extrude up to a curved face clicked in the view: the rod stops under the cylinder.
    void extrudeUpToCurvedFace()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=c plane=XZ\ncircle sketch=c center=0,30 d=40\nextrude id=cyl sketch=c distance=30 direction=symmetric\n"
                              "sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=6\n").ok());
        }
        std::string round;
        const auto& cyl = *w.document().state().bodies.at("cyl");
        for (int i = 0; i < cyl.faceCount(); ++i) {
            if (jig::measureItem(cyl.face(i)).radius) {
                round = cyl.faceName(i);
            }
        }
        w.runCommand("solid.extrude");
        auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ep);
        ep->useFace("cyl", round);
        QCOMPARE(ep->extent()->currentData().toString(), QString("to"));
        ep->operation()->setCurrentIndex(ep->operation()->findData("new"));
        QTest::qWait(300);
        QTRY_VERIFY(ep->previewOk());
        QVERIFY(ep->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto mp = jig::massProperties(w.document().state().bodies.at(w.document().model().features.back().id)->shape());
        QVERIFY(std::abs(mp.zmax - (30 - std::sqrt(391.0))) < 1e-6);
    }

    // B-015: the Parameters dialog changes the design as one undoable step; the timeline's
    // right-click menu suppresses a step and brings it back.
    void parametersAndTimelineMenu()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("param h = 10 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\n"
                              "extrude id=e1 sketch=s1 distance=h\nsketch id=s2 on=e1/end\n"
                              "circle sketch=s2 center=0,0 d=10\nextrude id=e2 sketch=s2 distance=5 op=join\n").ok());
        }
        w.showAllBodies();
        const double before = volume(w);
        {
            ParametersDialog d(w.document(), &w);
            auto* table = d.findChild<QTableWidget*>();
            QVERIFY(table);
            QCOMPARE(table->item(0, 0)->text(), QString("h"));
            table->item(0, 1)->setText("20 mm");
            QVERIFY(d.apply());
        }
        QVERIFY(std::abs(volume(w) - (before + 40 * 30 * 10)) < 1e-6);
        w.runCommand("edit.undo");  // one step back
        QTRY_VERIFY(std::abs(volume(w) - before) < 1e-6);
        // a bad value is refused with a reason, and nothing changes
        {
            ParametersDialog d(w.document(), &w);
            d.findChild<QTableWidget*>()->item(0, 1)->setText("ten");
            QVERIFY(!d.apply());
            bool explained = false;
            for (auto* l : d.findChildren<QLabel*>()) {
                explained |= l->text().contains("attention");
            }
            QVERIFY(explained);
        }
        QVERIFY(std::abs(volume(w) - before) < 1e-6);

        // right-click the last step in the timeline, choose Suppress
        auto* timeline = w.findChild<QListWidget*>("Timeline");
        if (!timeline) {
            for (auto* l : w.findChildren<QListWidget*>()) {
                if (l->count() == int(w.document().model().features.size())) {
                    timeline = l;
                }
            }
        }
        QVERIFY(timeline);
        auto chooseInMenu = [](const QString& text) {
            QTimer::singleShot(150, [text] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                QVERIFY(menu);
                for (QAction* a : menu->actions()) {
                    if (a->text().startsWith(text)) {
                        a->trigger();
                        menu->close();
                        return;
                    }
                }
                menu->close();
            });
        };
        const int last = timeline->count() - 1;
        const QPoint at = timeline->visualItemRect(timeline->item(last)).center();
        chooseInMenu("Suppress");
        QMetaObject::invokeMethod(timeline, "customContextMenuRequested", Q_ARG(QPoint, at));
        QTRY_VERIFY(w.document().model().features.back().suppressed);
        QVERIFY(std::abs(volume(w) - 40 * 30 * 10) < 1e-6);  // the post is gone
        chooseInMenu("Unsuppress");
        QMetaObject::invokeMethod(timeline, "customContextMenuRequested", Q_ARG(QPoint, at));
        QTRY_VERIFY(!w.document().model().features.back().suppressed);
        QVERIFY(std::abs(volume(w) - before) < 1e-6);

        // groups: the post's sketch and extrude in one group; folding hides the second step
        w.setStepGroup("s2", "Post");
        w.setStepGroup("e2", "Post");
        auto* tl = w.timeline();
        const int first = w.document().featureIndex("s2");
        QVERIFY(tl->item(first)->text().contains("Post"));
        w.setGroupFolded("Post", true);
        QVERIFY(tl->item(first)->text().contains("2 steps"));
        QVERIFY(tl->item(first + 1)->isHidden());
        w.setGroupFolded("Post", false);
        QVERIFY(!tl->item(first + 1)->isHidden());
        QVERIFY(std::abs(volume(w) - before) < 1e-6);  // grouping never changes the part
    }

    // Select Other: a long press lists the faces under the cursor (front and back of a block),
    // and picking the hidden one selects it.
    void selectOther()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n").ok());
        }
        w.showAllBodies();
        Viewport* view = w.viewport();
        QTRY_VERIFY(view->meshesReady());
        view->viewTop();
        QTest::qWait(100);
        const QPoint at = view->project(QVector3D(5, 5, 10)).toPoint();
        const auto under = view->facesUnder(at);
        QCOMPARE(int(under.size()), 2);  // the top, then the bottom behind it
        // a long press opens the list
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, at);
        QMenu* menu = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((menu = w.findChild<QMenu*>()) != nullptr && menu->isVisible(), 3000);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, at);
        QList<QAction*> picks;
        for (QAction* a : menu->actions()) {
            if (!a->isSeparator() && a->isEnabled() && a->text().contains("e1/")) {
                picks << a;
            }
        }
        QCOMPARE(picks.size(), 2);
        QVERIFY(picks[0]->text().contains("e1/end"));    // nearest first
        QVERIFY(picks[1]->text().contains("e1/start"));
        QSignalSpy clicked(view, &Viewport::faceClicked);
        picks[1]->trigger();
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.first().at(1).toString(), QString("e1/start"));
        QTRY_VERIFY(w.detailsText().contains("e1/start"));
        menu->close();
    }

    // Drag the extrude arrow: grab its tip, pull it to twice the length, the distance doubles.
    void extrudeDragHandle()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\n").ok());
        }
        Viewport* view = w.viewport();
        view->homeView();
        w.runCommand("solid.extrude");
        auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ep);
        ep->distance()->setText("10 mm");
        QTRY_VERIFY(ep->previewOk());
        QTRY_VERIFY(ep->handleTip().has_value());
        const QPointF tip = *ep->handleTip();
        const QPointF base = view->project(QVector3D(0, 0, 0));
        const QPointF to = base + (tip - base) * 2.0;
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, tip.toPoint());
        QMouseEvent mv(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &mv);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
        const double d = ep->distance()->value();
        QVERIFY2(std::abs(d - 20) <= 1.0, qPrintable(ep->distance()->text()));
        QVERIFY(std::abs(d * 2 - std::round(d * 2)) < 1e-9);  // snapped to 0.5 mm
        QTRY_VERIFY(ep->previewOk());
        QVERIFY(ep->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(std::abs(volume(w) - 1200 * d) < 1e-6);
    }

    // A join that misses the body: the panel offers "Make it a new body", which fixes it.
    void oneClickFixNewBody()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=s2 plane=XY\nrect sketch=s2 center=50,0 w=10 h=10\n").ok());
        }
        w.runCommand("solid.extrude");
        auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ep);
        ep->operation()->setCurrentIndex(ep->operation()->findData("join"));
        QTest::qWait(300);
        QPushButton* fix = nullptr;
        QTRY_VERIFY([&] {
            for (auto* b : ep->findChildren<QPushButton*>()) {
                if (b->text().contains("new body")) {
                    fix = b;
                }
            }
            return fix != nullptr;
        }());
        QVERIFY(!ep->previewOk());
        fix->click();
        QCOMPARE(ep->operation()->currentData().toString(), QString("new"));
        QTRY_VERIFY(ep->previewOk());
        QVERIFY(ep->accept());
        QTRY_COMPARE(int(w.document().state().bodies.size()), 2);
    }

    // A counterbore narrower than its hole: the Hole panel offers a width that works.
    void oneClickFixCounterbore()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=s2 on=e1/end\npoint sketch=s2 at=0,0\n").ok());
        }
        w.runCommand("solid.hole");
        auto* hp = qobject_cast<HolePanel*>(w.featurePanel());
        QVERIFY(hp);
        hp->type()->setCurrentIndex(hp->type()->findData("counterbore"));
        hp->diameter()->setText("4 mm");
        hp->cboreDiameter()->setText("3 mm");
        QTest::qWait(300);
        QPushButton* fix = nullptr;
        QTRY_VERIFY([&] {
            for (auto* b : hp->findChildren<QPushButton*>()) {
                if (b->isVisible() && b->text().startsWith("Use ")) {
                    fix = b;
                }
            }
            return fix != nullptr;
        }());
        QVERIFY(!hp->previewOk());
        fix->click();
        QVERIFY(hp->cboreDiameter()->value() > 4);
        QTRY_VERIFY(hp->previewOk());
        QVERIFY(hp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
    }

    // Translations: in the pseudo-language every menu entry and toolbar button is translated
    // (an untranslatable string would stay plain), and the window still lays out.
    void pseudoTranslation()
    {
        if (!availableLanguages().contains("qps")) {
            QSKIP("built without Qt LinguistTools: no translations in this build");
        }
        QCOMPARE(installLanguage("qps"), QString("qps"));
        QStringList plain;
        {
            MainWindow w;
            w.resize(1280, 820);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            for (QAction* a : w.findChildren<QAction*>()) {
                const QString t = a->text();
                // translated text is wrapped in [ ]; separators and file names have none
                if (!t.isEmpty() && !a->isSeparator() && !t.startsWith('[') && !t.contains(".jig") && !t.contains('/')) {
                    plain << a->objectName() + ": " + t;
                }
            }
            w.grab().save(shotPath("pseudo-language"));
        }
        // a message from the core is translated too
        QVERIFY(coreText("That step no longer exists.").startsWith('['));
        // and one built from values: the pattern is translated, the values come back as they were
        const std::string built = jig::fmt("The axis '%1' does not exist (or comes later in the timeline).", {"ax7"});
        const QString t = coreText(built);
        QVERIFY2(t.startsWith('[') && t.contains("'ax7'"), qPrintable(t));
        // two such sentences side by side are translated one by one
        const QString two = coreText(jig::fmt("%1 of the edges this fillet rounded no longer exist.", {"2"}) + " " +
                                     jig::fmt("%1 edges were split, and Jigsmith can't tell which piece you meant.", {"3"}));
        QVERIFY2(two.count('[') == 2 && two.contains("2") && two.contains("3"), qPrintable(two));
        // a value that is one of the core's words ("extrude") is translated inside the sentence
        const QString kind = coreText(jig::fmt("Some of the closed areas this %1 used are gone from the sketch.",
                                               {jig::word("extrude")}));
        QVERIFY2(kind.count('[') == 2 && !kind.contains(" extrude "), qPrintable(kind));
        // something the core never says is left as it is
        QCOMPARE(coreText("Not a message from the core"), QString("Not a message from the core"));
        installLanguage("en");
        QCOMPARE(coreText(built), QString::fromStdString(built));
        plain.removeDuplicates();
        QVERIFY2(plain.size() <= 3, qPrintable(plain.join("\n")));
    }

    // Keyboard only: find Sketch by typing, pick the floor with 1, type a rectangle from a
    // typed first corner, finish, extrude with Enter. No mouse.
    void keyboardOnlyPart()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));  // shortcuts go to the active window
        Viewport* view = w.viewport();
        auto palette = [&](const char* words) {
            QTimer::singleShot(150, [words] {
                QWidget* pop = QApplication::activePopupWidget() ? QApplication::activePopupWidget()
                                                                 : QApplication::activeModalWidget();
                QLineEdit* f = pop ? pop->findChild<QLineEdit*>() : nullptr;  // the search box
                if (!f) {
                    qWarning() << "no palette" << pop << QApplication::focusWidget();
                    if (pop) {
                        pop->close();
                    }
                    return;
                }
                QTest::keyClicks(f, words);
                QTest::keyClick(f, Qt::Key_Return);
            });
            QTest::keyClick(&w, Qt::Key_K, Qt::ControlModifier);
        };
        palette("create sketch");
        QTest::keyClick(view, Qt::Key_1);  // the floor
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        SketchEditor* sk = w.sketchEditor();
        QTest::keyClick(view, Qt::Key_R);  // Rectangle
        QTRY_COMPARE(int(sk->tool()), int(SketchEditor::Tool::Rectangle));
        QTest::keyClick(view, Qt::Key_5);  // first corner X...
        QVERIFY(QApplication::focusWidget());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        QTest::keyClicks(QApplication::focusWidget(), "5");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
        QTest::keyClick(view, Qt::Key_4);  // ...then width 40 and height 30
        QTest::keyClicks(QApplication::focusWidget(), "0");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        QTest::keyClicks(QApplication::focusWidget(), "30");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        const auto& regions = w.document().state().sketches.begin()->second->profiles.regions;
        QCOMPARE(int(regions.size()), 1);
        QVERIFY(std::abs(regions[0].area - 1200) < 1e-6);
        palette("finish sketch");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
        QTest::keyClick(&w, Qt::Key_E);  // Extrude
        QTRY_VERIFY(qobject_cast<ExtrudePanel*>(w.featurePanel()));
        auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
        ep->distance()->setFocus();
        QTRY_VERIFY(ep->previewOk());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(volume(w) > 0);
        // the corner went exactly where it was typed
        const auto mp = jig::massProperties(w.document().state().bodies.begin()->second->shape());
        QVERIFY(std::abs(mp.xmin - 5) < 1e-9 && std::abs(mp.ymin - 5) < 1e-9);
    }

    // Accessibility: every control in every tool panel, and every toolbar button, has a
    // name a screen reader can say (its own text, or an accessible name).
    void accessibleNames()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openSample("knob"));
        QStringList missing;
        auto check = [&](QWidget* root, const QString& where) {
            for (QWidget* c : root->findChildren<QWidget*>()) {
                if (!c->isVisibleTo(root)) {
                    continue;
                }
                const bool input = qobject_cast<QLineEdit*>(c) || qobject_cast<QComboBox*>(c) ||
                                   qobject_cast<QAbstractSpinBox*>(c) || qobject_cast<QListWidget*>(c) ||
                                   qobject_cast<QTreeWidget*>(c) || qobject_cast<QAbstractButton*>(c);
                if (!input || qobject_cast<QComboBox*>(c->parentWidget())) {
                    continue;  // (a combo box's own line edit speaks through the combo)
                }
                QString name = c->accessibleName();
                if (auto* b = qobject_cast<QAbstractButton*>(c)) {
                    name += b->text();
                    if (auto* tb = qobject_cast<QToolButton*>(c); tb && tb->defaultAction()) {
                        name += tb->defaultAction()->text();
                    }
                }
                if (name.trimmed().isEmpty()) {
                    missing << where + ": " + c->metaObject()->className() + " " + c->objectName();
                }
            }
        };
        check(&w, "window");
        for (const char* cmd : {"solid.extrude", "solid.fillet", "solid.chamfer", "solid.shell", "solid.revolve",
                                "solid.hole", "solid.pattern_circ", "solid.mirror", "construct.offset_plane",
                                "construct.axis", "solid.combine", "solid.split", "solid.move", "solid.presspull",
                                "inspect.measure", "inspect.section"}) {
            w.runCommand(cmd);
            if (auto* panel = w.featurePanel()) {
                check(panel, cmd);
                panel->cancel();
                QTRY_VERIFY(w.featurePanel() == nullptr);
            }
        }
        missing.removeDuplicates();
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join("\n")));
    }

    // Navigation settings: preset, zoom direction and orbit speed apply and are remembered.
    void navigationSettings()
    {
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            Viewport* view = w.viewport();
            w.setNavigation(NavPreset::Blender, true, 2.0f);
            QCOMPARE(int(view->navPreset()), int(NavPreset::Blender));
            QVERIFY(view->zoomInverted());
            // a right drag of 10 px turns twice as far at speed 2
            const float yaw0 = view->yaw();
            const QPoint at(400, 300);
            QTest::mousePress(view, Qt::RightButton, Qt::NoModifier, at);
            QMouseEvent mv(QEvent::MouseMove, QPointF(at + QPoint(10, 0)), view->mapToGlobal(QPointF(at + QPoint(10, 0))),
                           Qt::NoButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(view, &mv);
            QTest::mouseRelease(view, Qt::RightButton, Qt::NoModifier, at + QPoint(10, 0));
            QVERIFY(std::abs((yaw0 - view->yaw()) - 8.0f) < 1e-4);
        }
        {
            MainWindow again;  // a new window starts with the saved choices
            QCOMPARE(int(again.viewport()->navPreset()), int(NavPreset::Blender));
            QVERIFY(again.viewport()->zoomInverted());
            again.setNavigation(NavPreset::Default, false, 1.0f);
        }
    }

    // Marking menu: a right click (no drag) opens eight commands around the cursor.
    void markingMenu()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        const QPoint at(400, 300);
        QTest::mousePress(view, Qt::RightButton, Qt::NoModifier, at);
        QTest::mouseRelease(view, Qt::RightButton, Qt::NoModifier, at);
        MarkingMenu* m = nullptr;
        QTRY_VERIFY((m = w.findChild<MarkingMenu*>()) != nullptr && m->isVisible());
        QCOMPARE(m->buttonCount(), 8);
        QToolButton* measure = nullptr;
        for (int i = 0; i < m->buttonCount(); ++i) {
            if (m->button(i)->defaultAction()->objectName() == "inspect.measure") {
                measure = m->button(i);
            }
        }
        QVERIFY(measure);
        QSignalSpy fired(measure->defaultAction(), &QAction::triggered);
        measure->click();
        QCOMPARE(fired.count(), 1);
        QTRY_VERIFY(w.findChild<MarkingMenu*>() == nullptr);  // closed after the pick
        if (w.featurePanel()) {
            qobject_cast<ToolPanel*>(w.featurePanel())->cancel();
        }
        QTRY_VERIFY(w.featurePanel() == nullptr);
        // in a sketch it offers drawing tools; number keys pick, Esc closes
        QVERIFY(w.createSketchOn("XY"));
        auto* sm = w.openMarkingMenu(at);
        bool hasLine = false;
        for (int i = 0; i < sm->buttonCount(); ++i) {
            hasLine |= sm->button(i)->defaultAction()->objectName() == "sketch.line";
        }
        QVERIFY(hasLine);
        sm->grab().save(shotPath("marking-menu"));
        QTest::keyClick(sm, Qt::Key_3);  // the third: Circle
        QTRY_COMPARE(int(w.sketchEditor()->tool()), int(SketchEditor::Tool::Circle));
        // a right drag still orbits and opens nothing
        QTRY_VERIFY(w.findChild<MarkingMenu*>() == nullptr);
        QTest::mousePress(view, Qt::RightButton, Qt::NoModifier, at);
        QMouseEvent mv(QEvent::MouseMove, QPointF(at + QPoint(40, 10)), view->mapToGlobal(QPointF(at + QPoint(40, 10))),
                       Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(view, &mv);
        QTest::mouseRelease(view, Qt::RightButton, Qt::NoModifier, at + QPoint(40, 10));
        QTest::qWait(50);
        QVERIFY(w.findChild<MarkingMenu*>() == nullptr);
    }

    // Selection filter: click a body's face with "Pick bodies" and the details describe the body.
    void selectionFilter()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n").ok());
        }
        w.showAllBodies();
        Viewport* view = w.viewport();
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        const QPoint top = view->project(QVector3D(0, 0, 10)).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, top);
        QTRY_VERIFY(w.detailsText().contains("Face"));
        w.setSelectFilter("bodies");
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, top);
        QTRY_VERIFY(w.detailsText().contains("Body"));
        QVERIFY(w.detailsText().contains("Volume"));
        w.setSelectFilter("edges");
        QCOMPARE(int(view->pickMode()), int(PickMode::Edges));
        // a tool takes over picking and gives it back when closed
        w.runCommand("solid.fillet");
        QVERIFY(w.featurePanel());
        qobject_cast<FeaturePanel*>(w.featurePanel())->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QTRY_COMPARE(int(view->pickMode()), int(PickMode::Edges));
    }

    // A sketch of open lines: Extrude offers them as a wall and makes it.
    void extrudeOpenCurvesAsWall()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=30,0\nline sketch=s1 p1=30,0 p2=30,20\n").ok());
        }
        w.runCommand("solid.extrude");
        auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ep);
        QVERIFY(ep->thinWall()->isChecked());  // the open curves are picked, as a wall
        ep->thickness()->setText("2 mm");
        ep->distance()->setText("10 mm");
        QTest::qWait(300);
        QTRY_VERIFY(ep->previewOk());
        QVERIFY(ep->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(std::abs(volume(w) - 1000) < 1e-6);
        QCOMPARE(int(w.document().status(0).level), int(jig::StatusLevel::Ok));  // the sketch is fine now
    }

    // Browser: untick a body to hide it, isolate one, show all again.
    void browserVisibility()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 corner=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
                              "pattern id=p1 body=e1 kind=rectangular dir1=X count1=3 spacing1=20mm\n").ok());
        }
        w.showAllBodies();  // (also redraws after the script)
        QTreeWidgetItem* e1 = nullptr;
        int bodies = 0;
        for (QTreeWidgetItemIterator it(w.browser()); *it; ++it) {
            if (!(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
                ++bodies;
                if ((*it)->data(0, Qt::UserRole).toString() == "e1") {
                    e1 = *it;
                }
            }
        }
        QCOMPARE(bodies, 3);
        QVERIFY(e1);
        e1->setCheckState(0, Qt::Unchecked);
        QTRY_VERIFY(!w.bodyVisible("e1"));
        QCOMPARE(w.viewport()->bodyCount(), 2);
        bool saysHidden = false;
        for (QTreeWidgetItemIterator it(w.browser()); *it; ++it) {
            saysHidden |= (*it)->data(0, Qt::UserRole).toString() == "e1" && (*it)->text(0).contains("hidden");
        }
        QVERIFY(saysHidden);  // written out, not only an unticked box
        // choosing a body in the Browser finds the step that made it in the timeline
        QListWidget* timeline = nullptr;
        for (auto* l : w.findChildren<QListWidget*>()) {
            if (l->count() == int(w.document().model().features.size())) {
                timeline = l;
            }
        }
        QVERIFY(timeline);
        for (QTreeWidgetItemIterator it(w.browser()); *it; ++it) {
            if ((*it)->data(0, Qt::UserRole).toString() == "e1") {
                w.browser()->setCurrentItem(*it);
            }
        }
        QCOMPARE(timeline->currentRow(), w.document().featureIndex("e1"));
        w.isolateBody("p1.1");
        QCOMPARE(w.viewport()->bodyCount(), 1);
        w.showAllBodies();
        QCOMPARE(w.viewport()->bodyCount(), 3);
        QVERIFY(std::abs(volume(w) - 3000) < 1e-6);  // hiding never changes the design
    }

    // Press pull a flat face; on a hole wall it opens the hole instead.
    void pressPull()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=s2 on=e1/end\npoint sketch=s2 at=10,5\nhole id=h1 sketch=s2 diameter=4mm depth=5mm\n").ok());
        }
        const double before = volume(w);
        w.runCommand("solid.presspull");
        auto* pp = qobject_cast<PressPullPanel*>(w.featurePanel());
        QVERIFY(pp);
        QVERIFY(!pp->accept());  // no face yet
        QVERIFY(pp->useFace("e1", "e1/start"));
        pp->distance()->setText("3 mm");
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(std::abs(volume(w) - (before + 40 * 30 * 3)) < 1e-6);
        // the hole's wall is not flat: press pull opens the hole step to change it
        std::string wall;
        const auto& body = *w.document().state().bodies.at("e1");
        for (int i = 0; i < body.faceCount(); ++i) {
            if (body.faceName(i).rfind("h1/wall", 0) == 0) {
                wall = body.faceName(i);
            }
        }
        QVERIFY(!wall.empty());
        QVERIFY(!w.pressPullFace("e1", wall).isEmpty());
        QVERIFY(qobject_cast<HolePanel*>(w.featurePanel()));
        qobject_cast<FeaturePanel*>(w.featurePanel())->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
        // a whole round face in the panel: a round boss pulled out 1 mm all round
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s3 plane=XY\ncircle sketch=s3 center=60,0 d=10\nextrude id=e3 sketch=s3 distance=5 op=new\n").ok());
        }
        std::string round;
        const auto& boss = *w.document().state().bodies.at("e3");
        for (int i = 0; i < boss.faceCount(); ++i) {
            if (jig::measureItem(boss.face(i)).radius) {
                round = boss.faceName(i);
            }
        }
        const double v0 = volume(w);
        w.runCommand("solid.presspull");
        pp = qobject_cast<PressPullPanel*>(w.featurePanel());
        QVERIFY(pp);
        QVERIFY(pp->useFace("e3", round));
        pp->distance()->setText("1 mm");
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY2(std::abs(volume(w) - v0 - M_PI * (36 - 25) * 5) < 1e-3, qPrintable(QString::number(volume(w) - v0)));
    }

    // Mirror one step (a hole) across a clicked face's plane; pattern a whole body.
    void mirrorStepAndPatternBody()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=s2 on=e1/end\npoint sketch=s2 at=10,5\nhole id=h1 sketch=s2 diameter=4mm extent=all\n"
                              "plane id=pl1 base=YZ distance=0mm\n").ok());
        }
        w.runCommand("solid.mirror");
        auto* mp = qobject_cast<MirrorPanel*>(w.featurePanel());
        QVERIFY(mp);
        mp->what()->setCurrentIndex(mp->what()->findData("step:h1"));
        mp->plane()->setCurrentIndex(mp->plane()->findData("pl1"));
        QTest::qWait(300);
        QTRY_VERIFY(mp->previewOk());
        QVERIFY(mp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY(std::abs(volume(w) - (12000 - 2 * M_PI * 4 * 10)) < 1e-3);

        w.runCommand("solid.pattern_rect");
        auto* pp = qobject_cast<PatternPanel*>(w.featurePanel());
        QVERIFY(pp);
        pp->source()->setCurrentIndex(pp->source()->findData("body:e1"));
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().bodies.size()), 3);  // 3 columns by default, separate bodies
    }

    // Construction axis from a clicked edge; the move panel then offers it to turn around.
    void constructionAxis()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\n"
                              "extrude id=e1 sketch=s1 distance=10\n").ok());
        }
        w.runCommand("construct.axis");
        auto* ap = qobject_cast<AxisPanel*>(w.featurePanel());
        QVERIFY(ap);
        QVERIFY(!ap->accept());  // nothing picked yet
        ap->useFace("e1", "e1/end");  // a flat face: the axis where it meets a second one
        QCOMPARE(ap->kind()->currentData().toString(), QString("planes"));
        ap->useEdge("e1", "E[e1/side:5|e1/side:6]");
        QCOMPARE(ap->kind()->currentData().toString(), QString("through"));
        QTest::qWait(300);
        QTRY_VERIFY(ap->previewOk());
        QVERIFY(ap->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().axes.size()), 1);
        const std::string axisId = w.document().state().axes.begin()->first;
        w.runCommand("solid.move");
        QComboBox* turn = nullptr;
        for (auto* c : w.featurePanel()->findChildren<QComboBox*>()) {
            if (c->findData(QString::fromStdString(axisId)) >= 0) {
                turn = c;
            }
        }
        QVERIFY(turn);  // the new axis is offered
        qobject_cast<FeaturePanel*>(w.featurePanel())->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
    }

    // Delete a body from the Browser: a Remove step, undoable.
    void deleteBody()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
                              "sketch id=s2 plane=XY\nrect sketch=s2 center=40,0 w=10 h=10\nextrude id=e2 sketch=s2 distance=5 op=new\n").ok());
        }
        QCOMPARE(int(w.document().state().bodies.size()), 2);
        QVERIFY(w.removeBody("e2"));
        QCOMPARE(int(w.document().state().bodies.size()), 1);
        QCOMPARE(int(w.document().model().features.back().kind), int(jig::FeatureKind::Remove));
        w.runCommand("edit.undo");
        QTRY_COMPARE(int(w.document().state().bodies.size()), 2);
        QVERIFY(!w.removeBody("no-such-body"));
    }

    // Tangent circle: two lines, a click on the side, a typed radius.
    void tangentCircle()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=40,0\nline sketch=s1 p1=0,0 p2=20,30\n").ok());
        }
        w.editFeature(0);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        SketchEditor* sk = w.sketchEditor();
        Viewport* view = w.viewport();
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        w.runCommand("sketch.circle_tangent");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::CircleTangent));
        click(30, 0);   // the floor line
        click(10, 15);  // the slanted line (it runs through (10, 15)), the circle goes between them
        sk->typeValues({"4 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        const jig::SketchEntity* c = nullptr;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Circle) {
                c = &e;
            }
        }
        QVERIFY(c);
        QVERIFY(std::abs(c->r - 4) < 1e-6);
        auto [cx, cy] = sk->sketch().pointXY(c->center);
        QVERIFY(std::abs(cy - 4) < 1e-6);  // 4 above the floor line
        // and 4 from the slanted line through the origin with direction (2, 3)
        const double dist = std::abs(3 * cx - 2 * cy) / std::sqrt(13.0);
        QVERIFY2(std::abs(dist - 4) < 1e-6, qPrintable(QString::number(dist)));
        QVERIFY(cx > 0);  // inside the corner, between the lines
        // a click that is not on a line is explained
        w.runCommand("sketch.circle_tangent");
        click(30, 30);
        QVERIFY(!sk->message().isEmpty());
    }

    // Line tool: press on the chain's end and drag to draw a tangent arc, then lines go on.
    void lineDragToTangentArc()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::Line);
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        click(0, 0);
        click(20, 0);  // a line along X; the chain now ends at (20, 0)
        // press on the end, drag up and round, let go at (20, 20): a half circle bulging to +X
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(20, 0));
        QTest::mouseMove(view, sk->screenOf(26, 4));
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::TangentArc));
        QTest::mouseMove(view, sk->screenOf(20, 20));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(20, 20));
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Line));  // back to lines
        int arcs = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Arc) {
                ++arcs;
                QVERIFY(std::abs(e.r - 10) < 1e-6);
            }
        }
        QCOMPARE(arcs, 1);
        // the next click goes on from the arc's end with a line
        click(0, 20);
        int lines = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            lines += e.type == jig::EntityType::Line;
        }
        QCOMPARE(lines, 2);
        click(0, 0);  // closing the outline: a D shape, 20 x 20 plus a half circle of radius 10
        double a = 0;
        for (auto& rg : w.document().state().sketches.begin()->second->profiles.regions) {
            a += rg.area;
        }
        QVERIFY2(std::abs(a - (400 + M_PI * 50)) < 1e-3, qPrintable(QString::number(a)));
    }

    // Slice while sketching: the view is cut at the sketch plane, and only while sketching.
    void sketchSlice()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=20\n"
                              "plane id=pl1 base=XY distance=10mm\n").ok());
        }
        QVERIFY(w.createSketchOn("pl1"));
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        Viewport* view = w.viewport();
        QVERIFY(!view->section());
        w.setSketchSlice(true);
        QVERIFY(view->section());
        const QVector4D s = *view->section();
        QVERIFY(std::abs(s.z() - 1) < 1e-6 && std::abs(s.w() - 10) < 1e-2);  // hides z > 10: the half in front
        // the checkbox in the sketch palette says so too (not only the picture)
        bool ticked = false;
        for (auto* cb : w.findChildren<QCheckBox*>()) {
            ticked |= cb->text().startsWith("Slice") && cb->isChecked();
        }
        QVERIFY(ticked);
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
        QVERIFY(!view->section());  // finishing the sketch takes the slice away
    }

    // Parameters from inside a sketch: the toolbar has them, and the sketch follows the change.
    void parametersFromSketch()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("param side = 20 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=side h=side\n").ok());
        }
        w.editFeature(0);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        auto* bar = w.findChild<QToolBar*>("SketchBar");
        QVERIFY(bar && bar->isVisible());
        bool offered = false;
        for (auto* a : bar->actions()) {
            offered |= a->objectName() == "modify.parameters" || a->data().toString() == "modify.parameters";
        }
        QVERIFY(offered);
        QTimer::singleShot(300, [] {
            for (auto* d : QApplication::topLevelWidgets()) {
                if (auto* pd = qobject_cast<ParametersDialog*>(d); pd && pd->isVisible()) {
                    pd->findChild<QTableWidget*>()->item(0, 1)->setText("30 mm");
                    if (pd->apply()) {
                        pd->accept();
                    }
                }
            }
        });
        w.runCommand("modify.parameters");
        QVERIFY(w.sketchEditor() != nullptr);  // still sketching
        auto area = [&] {
            double a = 0;
            for (auto& rg : w.document().state().sketches.at("s1")->profiles.regions) {
                a += rg.area;
            }
            return a;
        };
        QTRY_VERIFY(std::abs(area() - 900) < 1e-6);
        // the editor shows the new size too
        double maxX = -1e9;
        for (auto& [id, e] : w.sketchEditor()->sketch().entities()) {
            if (e.type == jig::EntityType::Point) {
                maxX = std::max(maxX, w.sketchEditor()->sketch().pointXY(id).first);
            }
        }
        QVERIFY(std::abs(maxX - 15) < 1e-6);
    }

    // Shell outside: the block becomes the hollow, walls around it.
    void shellOutside()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=60 h=40\nextrude id=e1 sketch=s1 distance=20\n").ok());
        }
        w.runCommand("solid.shell");
        auto* sh = qobject_cast<ShellPanel*>(w.featurePanel());
        QVERIFY(sh);
        sh->toggleFaceByName("e1", "e1/end");
        sh->direction()->setCurrentIndex(sh->direction()->findData("outside"));
        QTest::qWait(300);
        QTRY_VERIFY(sh->previewOk());
        QVERIFY(sh->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double v = jig::massProperties(w.document().state().bodies.begin()->second->shape()).volume;
        QVERIFY2(std::abs(v - (64.0 * 44 * 22 - 60.0 * 40 * 20)) < 1e-3, qPrintable(QString::number(v)));
        w.editFeature(2);
        sh = qobject_cast<ShellPanel*>(w.featurePanel());
        QVERIFY(sh);
        QCOMPARE(sh->direction()->currentData().toString(), QString("outside"));
        sh->cancel();
    }

    // Named views: saved with the design, listed in the Browser, restored by activating them.
    void namedViews()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n").ok());
        }
        Viewport* view = w.viewport();
        view->viewTop();
        const auto top = view->camera();
        w.saveNamedView("From above");
        view->homeView();
        QVERIFY(view->camera() != top);
        // the Browser lists it; activating it brings the view back
        QTreeWidgetItem* item = nullptr;
        for (auto* it : w.findChildren<QTreeWidget*>()) {
            for (auto* found : it->findItems("From above", Qt::MatchRecursive)) {
                item = found;
            }
        }
        QVERIFY(item);
        emit item->treeWidget()->itemActivated(item, 0);
        QCOMPARE(view->camera()["yaw"].get<float>(), top["yaw"].get<float>());
        QCOMPARE(view->camera()["pitch"].get<float>(), top["pitch"].get<float>());
        QVERIFY(w.isWindowModified());  // a saved view is a change to save
        QVERIFY(!w.restoreNamedView("No such view"));
    }

    // Import STEP: the solid arrives as a timeline step and can be built on.
    void importStep()
    {
        const QString step = QDir::temp().filePath("jigsmith-ui-import.step");
        {
            jig::Document src;
            jig::ScriptRunner r(src);
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n").ok());
            QVERIFY(jig::exportStep(src.combinedShape(), step.toStdString()).ok());
        }
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.importStepFile(step));
        QCOMPARE(int(w.document().model().features.size()), 1);
        QCOMPARE(int(w.document().model().features[0].kind), int(jig::FeatureKind::Import));
        QCOMPARE(int(w.document().state().bodies.size()), 1);
        QVERIFY(std::abs(jig::massProperties(w.document().state().bodies.begin()->second->shape()).volume - 12000) < 1e-3);
        QTRY_VERIFY(w.viewport()->meshesReady());
        // undo takes it away again; a file that is not STEP is refused with a message
        w.runCommand("edit.undo");
        QTRY_COMPARE(int(w.document().state().bodies.size()), 0);
        QTimer::singleShot(200, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                box->accept();
            }
        });
        const QString notStep = QDir::temp().filePath("jigsmith-ui-not-a.step");
        {
            QFile f(notStep);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("hello");
        }
        QVERIFY(!w.importStepFile(notStep));
        QCOMPARE(int(w.document().model().features.size()), 0);
    }

    // Move by dragging: the X arrow's grip pulled along X moves the body that way.
    void moveByDragging()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\nextrude id=e1 sketch=s1 distance=10\n").ok());
        }
        Viewport* view = w.viewport();
        w.runCommand("solid.move");
        auto* mv = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(mv);
        QTRY_VERIFY(view->meshesReady());
        auto tip = mv->arrowTip(0);
        QVERIFY(tip);
        // drag the grip 25 mm further along X, measured on screen
        QVector3D base(0, 0, 5);
        const QPointF perMm = (view->project(base + QVector3D(10, 0, 0)) - view->project(base)) / 10.0;
        const QPoint from = tip->toPoint(), to = (*tip + perMm * 25).toPoint();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view, (from + to) / 2);
        QTest::mouseMove(view, to);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(mv->dx()->valid());
        QVERIFY2(std::abs(mv->dx()->value() - 25) <= 1.0, qPrintable(mv->dx()->text()));  // within a snap or a pixel
        QCOMPARE(mv->dy()->value(), 0.0);
        const double dx = mv->dx()->value();
        QTest::qWait(300);
        QTRY_VERIFY(mv->previewOk());
        QVERIFY(mv->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto mp = jig::massProperties(w.document().state().bodies.at("e1")->shape());
        QVERIFY2(std::abs(mp.xmin - (-10 + dx)) < 1e-6, qPrintable(QString::number(mp.xmin)));
    }

    // B-009: a rebuild runs on a worker thread; the window keeps its event loop going (timers
    // fire, it repaints), clicks wait, and a progress dialog says how far it has got.
    void rebuildOffTheUiThread()
    {
        qputenv("JIG_PROGRESS_AFTER_MS", "0");  // show the dialog at once
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openSample("phone_stand"));
        const auto& doc = w.document();
        int ticks = 0;
        bool sawDialog = false, sawProgress = false;
        QTimer tick;
        tick.setInterval(2);
        QObject::connect(&tick, &QTimer::timeout, [&] {
            ++ticks;
            if (auto* d = w.findChild<QProgressDialog*>("RebuildProgress"); d && d->isVisible()) {
                sawDialog = true;
            }
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->objectName() == "RebuildProgress" && top->isVisible()) {
                    sawDialog = true;
                }
            }
            sawProgress |= doc.recomputing() && doc.recomputeProgress().second > 0;
        });
        tick.start();
        const int before = ticks;
        jig::ScriptRunner r(w.document());
        QVERIFY(r.runText("param base_w = 120 mm\n").ok());  // the first step changes: all rebuilt
        tick.stop();
        qunsetenv("JIG_PROGRESS_AFTER_MS");
        QCOMPARE(doc.recomputedLastTime(), int(doc.model().features.size()));
        QVERIFY2(ticks > before, "the event loop ran during the rebuild");
        QVERIFY(sawProgress);
        QVERIFY(sawDialog);
        QVERIFY(!doc.recomputing());
        for (QWidget* top : QApplication::topLevelWidgets()) {
            QVERIFY(!(top->objectName() == "RebuildProgress" && top->isVisible()));  // gone again
        }
        for (int i = 0; i < int(doc.model().features.size()); ++i) {
            QVERIFY(doc.status(i).level == jig::StatusLevel::Ok);
        }
    }

    // Two windows at once: each autosaves to its own file under its own lock, so neither
    // takes the other's for a crash; a session that died (its lock's process is gone) is found.
    void recoveryPerWindow()
    {
        auto w1 = std::make_unique<MainWindow>();
        auto w2 = std::make_unique<MainWindow>();
        {
            jig::ScriptRunner r(w1->document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\n").ok());
        }
        w1->autosave();
        const QDir dir(MainWindow::recoveryDir());
        const QStringList mine = dir.entryList({"autosave-*.jig"});
        QVERIFY(!mine.isEmpty());
        for (const QString& f : w2->leftoverSessions()) {
            QVERIFY2(!mine.contains(QFileInfo(f).fileName()), qPrintable(f));  // w1 is running
        }
        // a session that crashed: its autosave, and a lock naming a process that is gone
        const QString dead = dir.filePath("autosave-deadbeef.jig");
        QVERIFY(QFile::copy(dir.filePath(mine.front()), dead));
        {
            QFile lock(dir.filePath("autosave-deadbeef.lock"));
            QVERIFY(lock.open(QIODevice::WriteOnly));
            lock.write(QByteArray::number(4194301) + "\n" + QCoreApplication::applicationName().toUtf8() + "\n" +
                       QSysInfo::machineHostName().toUtf8() + "\n" + QSysInfo::machineUniqueId() + "\n" +
                       QSysInfo::bootUniqueId() + "\n");
        }
        QVERIFY(w2->leftoverSessions().contains(dead));
        QFile::remove(dead);
        QFile::remove(dir.filePath("autosave-deadbeef.lock"));
        // a window that ends normally takes its autosave with it
        w1.reset();
        for (const QString& f : mine) {
            QVERIFY2(!dir.exists(f), qPrintable(f));
        }
    }

    // Junk typed into the value box of every drawing tool: never a crash, and the sketch stays
    // solvable (whatever was refused is explained in the message line).
    void typedJunkIsHandled()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\n").ok());
        }
        w.editFeature(0);
        QTRY_VERIFY(w.sketchEditor() != nullptr);
        SketchEditor* sk = w.sketchEditor();
        Viewport* view = w.viewport();
        using T = SketchEditor::Tool;
        const QList<QStringList> junk {{"", ""},       {"abc", "x"},  {"-5", "-5"},       {"0", "0"},
                                       {"1e9", "1e9"}, {"5 deg", "3"}, {"1/0", "2"},       {"10", "-90 deg"},
                                       {"nan", "inf"}, {"10 mm + ", ")"}, {"3", "3", "3"}};
        int tried = 0;
        for (T tool : {T::Line, T::Rectangle, T::CenterRectangle, T::Circle, T::Arc, T::Arc3, T::Circle2, T::Circle3,
                       T::Rect3, T::Slot, T::Ellipse, T::Polygon, T::Text}) {
            for (const QStringList& values : junk) {
                sk->setTool(tool);
                // one click to start the shape (most tools then ask for sizes), then the junk
                const QPoint at = sk->screenOf(60, 40);
                QTest::mouseMove(view, at);
                QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
                sk->typeValues(values);
                QTest::keyClick(view, Qt::Key_Escape);
                ++tried;
                for (auto& [id, e] : sk->sketch().entities()) {
                    if (e.type == jig::EntityType::Point) {
                        QVERIFY2(std::isfinite(e.x) && std::isfinite(e.y) && std::abs(e.x) < 1e7 && std::abs(e.y) < 1e7,
                                 qPrintable(QString("tool %1, values %2: point %3,%4")
                                                .arg(int(tool)).arg(values.join('|')).arg(e.x).arg(e.y)));
                    }
                    QVERIFY(std::isfinite(e.r) && std::abs(e.r) < 1e7);
                }
                QVERIFY2(w.document().status(0).level != jig::StatusLevel::Error,
                         qPrintable(QString("tool %1, values %2: %3")
                                        .arg(int(tool))
                                        .arg(values.join('|'), QString::fromStdString(w.document().status(0).message))));
            }
        }
        QCOMPARE(tried, 13 * int(junk.size()));
        w.runCommand("sketch.finish");
        QTRY_VERIFY(w.sketchEditor() == nullptr);
    }

    // B-031: the real app is killed while it has unsaved work; its autosave is left with a lock
    // whose process is gone, which is how the next start recognises a crash (Linux: the data
    // folder is moved with XDG_DATA_HOME so a developer's own recovery folder is not touched).
    void realCrashLeavesARecoverableAutosave()
    {
#if !defined(__linux__)
        QSKIP("uses XDG_DATA_HOME to keep the test's files apart");
#else
        const QString app = QCoreApplication::applicationDirPath() + "/../app/jigsmith";
        if (!QFile::exists(app)) {
            QSKIP("the app is not built next to the tests");
        }
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString script = tmp.filePath("box.jigs");
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=10\nextrude id=e1 sketch=s1 distance=5\n");
        }
        QProcess child;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_DATA_HOME", tmp.filePath("data"));
        env.insert("XDG_CONFIG_HOME", tmp.filePath("config"));
        env.insert("JIG_AUTOSAVE_MS", "200");
        child.setProcessEnvironment(env);
        child.start(app, {script});
        QVERIFY2(child.waitForStarted(10000), qPrintable(child.errorString()));
        const QDir rec(tmp.filePath("data/Jigsmith/Jigsmith/recovery"));
        QTRY_VERIFY_WITH_TIMEOUT(!rec.entryList({"autosave-*.jig"}).isEmpty(), 30000);
        const QString saved = rec.filePath(rec.entryList({"autosave-*.jig"}).front());
        const QString lockPath = QFileInfo(saved).absolutePath() + "/" + QFileInfo(saved).completeBaseName() + ".lock";
        QLockFile probe(lockPath);
        probe.setStaleLockTime(0);
        QVERIFY2(!probe.tryLock(0), "the running app holds its lock");
        child.kill();  // a hard crash: nothing is cleaned up
        QVERIFY(child.waitForFinished(10000));
        QVERIFY(QFile::exists(saved));
        QVERIFY2(probe.tryLock(0), "the dead app's lock is stale: the autosave is offered on the next start");
        probe.unlock();
        auto doc = jig::loadDocument(saved.toStdString());
        QVERIFY(doc.ok());
        QCOMPARE(int((*doc)->state().bodies.size()), 1);
#endif
    }

    // Loft from the panel: tick two sketches, the preview runs, OK makes the frustum.
    void loftPanel()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\n"
                              "sketch id=s2 plane=XY offset=30\nrect sketch=s2 center=0,0 w=10 h=10\n").ok());
        }
        w.refreshAll();
        w.runCommand("solid.loft");
        auto* lp = qobject_cast<LoftPanel*>(w.featurePanel());
        QVERIFY(lp);
        QCOMPARE(lp->sections()->count(), 2);
        QVERIFY(!lp->previewOk());
        for (int i = 0; i < 2; ++i) {
            lp->sections()->item(i)->setCheckState(Qt::Checked);
        }
        QTest::qWait(300);
        QTRY_VERIFY(lp->previewOk());
        w.viewport()->homeView();
        QTRY_VERIFY(w.viewport()->meshesReady());
        QTest::qWait(200);
        w.grab().save(shotPath("loft-panel"));
        QVERIFY(lp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto& st = w.document().state();
        QCOMPARE(int(st.bodies.size()), 1);
        QVERIFY(std::abs(jig::massProperties(st.bodies.begin()->second->shape()).volume - 7000) < 1e-3);
        // the sketches it uses are hidden, and editing the loft reopens the panel with both ticked
        w.editFeature(2);
        auto* again = qobject_cast<LoftPanel*>(w.featurePanel());
        QVERIFY(again);
        QCOMPARE(again->sections()->item(0)->checkState(), Qt::Checked);
        QCOMPARE(again->sections()->item(1)->checkState(), Qt::Checked);
        again->cancel();
    }

    // Sweep from the panel: the shape's sketch, the path's sketch, OK makes the rod.
    void sweepPanel()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=4\n"
                              "sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,20\n"
                              "arc sketch=s2 center=10,20 r=10 from=90deg to=180deg\n").ok());
        }
        w.refreshAll();
        w.runCommand("solid.sweep");
        auto* sp = qobject_cast<SweepPanel*>(w.featurePanel());
        QVERIFY(sp);
        QCOMPARE(sp->sketch()->currentData().toString(), QString("s1"));
        QCOMPARE(sp->path()->currentData().toString(), QString("s2"));
        QCOMPARE(sp->areas()->count(), 1);
        QTRY_VERIFY(sp->previewOk());
        w.viewport()->homeView();
        QTRY_VERIFY(w.viewport()->meshesReady());
        QTest::qWait(200);
        w.grab().save(shotPath("sweep-panel"));
        QVERIFY(sp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double want = std::numbers::pi * 4 * (20 + 5 * std::numbers::pi);
        const auto& st = w.document().state();
        QCOMPARE(int(st.bodies.size()), 1);
        QVERIFY(std::abs(jig::massProperties(st.bodies.begin()->second->shape()).volume - want) < want * 1e-3);
        QVERIFY(w.document().status(1).level == jig::StatusLevel::Ok);  // the path sketch: no warning
    }

    // Pipe from the panel: only a path sketch; hollow with a wall; edit it again later.
    void pipePanel()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,20\n"
                              "arc sketch=s2 center=10,20 r=10 from=90deg to=180deg\n").ok());
        }
        w.refreshAll();
        w.runCommand("solid.pipe");
        auto* pp = qobject_cast<PipePanel*>(w.featurePanel());
        QVERIFY(pp);
        QCOMPARE(pp->path()->currentData().toString(), QString("s2"));
        QVERIFY(!pp->wall()->isEnabled());
        pp->diameter()->setText("6 mm");
        pp->hollow()->setChecked(true);
        QVERIFY(pp->wall()->isEnabled());
        pp->wall()->setText("1 mm");
        QTRY_VERIFY(pp->previewOk());
        w.viewport()->homeView();
        QTRY_VERIFY(w.viewport()->meshesReady());
        QTest::qWait(200);
        w.grab().save(shotPath("pipe-panel"));
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double want = std::numbers::pi * (9 - 4) * (20 + 5 * std::numbers::pi);
        const auto& st = w.document().state();
        QCOMPARE(int(st.bodies.size()), 1);
        QVERIFY(std::abs(jig::massProperties(st.bodies.begin()->second->shape()).volume - want) < want * 1e-3);
        QVERIFY(w.document().status(0).level == jig::StatusLevel::Ok);  // the path sketch: no warning
        // a wall that fills the pipe: the panel says why and OK waits
        w.editFeature(int(w.document().model().features.size()) - 1);
        pp = qobject_cast<PipePanel*>(w.featurePanel());
        QVERIFY(pp);
        QVERIFY(pp->hollow()->isChecked());
        QCOMPARE(pp->wall()->text(), QString("1 mm"));
        pp->wall()->setText("3 mm");
        QTRY_VERIFY(pp->statusText().contains("fills the whole pipe"));
        QVERIFY(!pp->previewOk());
        pp->cancel();
    }

    // Coil from the panel: no sketch needed; sizes typed; a pitch too small is explained.
    void coilPanel()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.runCommand("solid.coil");
        auto* cp = qobject_cast<CoilPanel*>(w.featurePanel());
        QVERIFY(cp);
        QCOMPARE(cp->plane()->currentData().toString(), QString("XY"));
        cp->diameter()->setText("20 mm");
        cp->pitch()->setText("5 mm");
        cp->turns()->setText("4");
        cp->size()->setText("2 mm");
        QTRY_VERIFY(cp->previewOk());
        // a pitch no larger than the wire: the panel says why, OK waits
        cp->pitch()->setText("1.5 mm");
        QTRY_VERIFY(cp->statusText().contains("run into the next"));
        QVERIFY(!cp->previewOk());
        cp->pitch()->setText("5 mm");
        QTRY_VERIFY(cp->previewOk());
        w.viewport()->homeView();
        QTRY_VERIFY(w.viewport()->meshesReady());
        QTest::qWait(200);
        w.grab().save(shotPath("coil-panel"));
        QVERIFY(cp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double want = std::numbers::pi * (2 * std::numbers::pi * 10) * 4;
        const auto& st = w.document().state();
        QCOMPARE(int(st.bodies.size()), 1);
        QVERIFY(std::abs(jig::massProperties(st.bodies.begin()->second->shape()).volume - want) < want * 1e-3);
        // editing it shows the saved values; a square wire changes the volume to s^2 x 2 pi R x turns
        w.editFeature(0);
        cp = qobject_cast<CoilPanel*>(w.featurePanel());
        QVERIFY(cp);
        QCOMPARE(cp->turns()->text(), QString("4"));
        cp->section()->setCurrentIndex(cp->section()->findData("square"));
        QTRY_VERIFY(cp->previewOk());
        QVERIFY(cp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double sq = 4 * (2 * std::numbers::pi * 10) * 4;
        QVERIFY(std::abs(jig::massProperties(w.document().state().bodies.begin()->second->shape()).volume - sq) < sq * 1e-3);
    }

    // Areas of a sketch are picked by clicking them in the 3D view: a square with a circle in it
    // can become a round post (the circle) or a block with a hole (the ring), not only a block.
    void clickAreasToExtrude()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=40\n"
                              "circle sketch=s1 center=0,0 d=10\n").ok());
        }
        w.refreshAll();
        Viewport* view = w.viewport();
        auto extrudeBy = [&](QVector3D at) -> double {
            w.runCommand("solid.extrude");
            auto* ep = qobject_cast<ExtrudePanel*>(w.featurePanel());
            if (!ep) {
                return -1;
            }
            ep->distance()->setText("10 mm");
            view->viewTop();
            // two areas: none picked yet, the panel says how to pick, OK waits
            QTest::qWait(300);
            if (ep->previewOk() || !ep->statusText().contains("Click")) {
                return -2;
            }
            const QPoint p = view->project(at).toPoint();
            QTest::mouseMove(view, p);
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, p);
            QTest::qWait(300);
            if (at.x() > 1) {
                view->homeView();
                (void)QTest::qWaitFor([&] { return view->meshesReady(); }, 5000);
                QTest::qWait(200);
                w.grab().save(shotPath("pick-areas"));
            }
            if (!ep->previewOk() || !ep->accept()) {
                return -3;
            }
            return jig::massProperties(w.document().state().bodies.rbegin()->second->shape()).volume;
        };
        // the circle: a round post
        const double post = extrudeBy(QVector3D(0, 0, 0));
        QVERIFY2(std::abs(post - std::numbers::pi * 25 * 10) < 1e-3, qPrintable(QString::number(post)));
        QTRY_VERIFY(w.featurePanel() == nullptr);
        w.runCommand("edit.undo");
        QTRY_COMPARE(int(w.document().state().bodies.size()), 0);
        // the ring between them: a block with a round hole
        const double ring = extrudeBy(QVector3D(15, 15, 0));
        QVERIFY2(std::abs(ring - (1600 - std::numbers::pi * 25) * 10) < 1e-3, qPrintable(QString::number(ring)));
    }

    // Move by the free grip: seen from above it moves in XY, from the front in XZ.
    void moveFreely()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\nextrude id=e1 sketch=s1 distance=10\n").ok());
        }
        w.refreshAll();
        Viewport* view = w.viewport();
        w.runCommand("solid.move");
        auto* mv = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(mv);
        QTRY_VERIFY(view->meshesReady());
        view->viewTop();
        QCOMPARE(mv->freePlaneNormal(), 2);
        auto grip = mv->centreGrip();
        QVERIFY(grip);
        // 30 mm along X and 12 mm along Y, measured on screen at the grip's height (z = 5)
        const QVector3D base(0, 0, 5);
        const QPointF target = view->project(base + QVector3D(30, 12, 0));
        const QPoint from = grip->toPoint(), to = target.toPoint();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view, (from + to) / 2);
        QTest::mouseMove(view, to);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY2(std::abs(mv->dx()->value() - 30) <= 1.0, qPrintable(mv->dx()->text()));  // a snap or a pixel
        QVERIFY2(std::abs(mv->dy()->value() - 12) <= 1.0, qPrintable(mv->dy()->text()));
        QCOMPARE(mv->dz()->value(), 0.0);
        QTest::qWait(300);
        w.grab().save(shotPath("move-free"));
        // from the front the grip moves in XZ: up 8 mm, Y stays
        view->viewFront();
        QCOMPARE(mv->freePlaneNormal(), 1);
        const double y = mv->dy()->value(), x = mv->dx()->value();
        grip = mv->centreGrip();
        QVERIFY(grip);
        const QVector3D now(float(x), float(y), 5);
        const QPoint f2 = grip->toPoint(), t2 = view->project(now + QVector3D(0, 0, 8)).toPoint();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, f2);
        QTest::mouseMove(view, t2);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, t2);
        QVERIFY2(std::abs(mv->dz()->value() - 8) <= 1.0, qPrintable(mv->dz()->text()));
        QCOMPARE(mv->dy()->value(), y);
        QVERIFY(std::abs(mv->dx()->value() - x) <= 1.0);
        const double dz = mv->dz()->value(), dx = mv->dx()->value();
        QTest::qWait(300);
        QTRY_VERIFY(mv->previewOk());
        QVERIFY(mv->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto mp = jig::massProperties(w.document().state().bodies.at("e1")->shape());
        QVERIFY(std::abs(mp.xmin - (-10 + dx)) < 1e-6);
        QVERIFY(std::abs(mp.zmin - dz) < 1e-6);
    }

    // Move a body from one construction point to another.
    void movePointToPoint()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                              "point id=p1 through=face:e1/end\npoint id=p2 at=50,20,0\n").ok());
        }
        w.runCommand("solid.move");
        auto* mv = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(mv);
        mv->fromPoint()->setCurrentIndex(mv->fromPoint()->findData("p1"));
        mv->toPoint()->setCurrentIndex(mv->toPoint()->findData("p2"));
        QTest::qWait(300);
        QTRY_VERIFY(mv->previewOk());
        QVERIFY(mv->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto mp = jig::massProperties(w.document().state().bodies.at("e1")->shape());
        QVERIFY(std::abs(mp.xmin - 30) < 1e-6 && std::abs(mp.zmax) < 1e-6);
        w.editFeature(4);
        mv = qobject_cast<MovePanel*>(w.featurePanel());
        QVERIFY(mv);
        QCOMPARE(mv->toPoint()->currentData().toString(), QString("p2"));
        mv->cancel();
    }

    // Revolve up to a plane: the angle follows the plane, and the field for it is hidden.
    void revolveUpToPlane()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XZ\nrect sketch=s1 corner=10,0 w=10 h=16\n").ok());
        }
        w.runCommand("solid.revolve");
        auto* rv = qobject_cast<RevolvePanel*>(w.featurePanel());
        QVERIFY(rv);
        rv->extent()->setCurrentIndex(rv->extent()->findData("to"));
        QVERIFY(!rv->angle()->isVisible());
        QComboBox* to = nullptr;
        for (auto* c : rv->findChildren<QComboBox*>()) {
            if (c->isVisible() && c->findData("\"YZ\"") >= 0) {
                to = c;
            }
        }
        QVERIFY(to);
        to->setCurrentIndex(to->findData("\"YZ\""));
        QTest::qWait(300);
        QTRY_VERIFY(rv->previewOk());
        QVERIFY(rv->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double quarter = M_PI * (20 * 20 - 10 * 10) * 16 / 4;
        QVERIFY2(std::abs(volume(w) - quarter) < 1e-3, qPrintable(QString::number(volume(w))));
        // reopening it from the timeline keeps the choice
        w.editFeature(1);
        rv = qobject_cast<RevolvePanel*>(w.featurePanel());
        QVERIFY(rv);
        QCOMPARE(rv->extent()->currentData().toString(), QString("to"));
        rv->cancel();
    }

    // Construction points: typed, then at the middle of a clicked edge; an axis through them.
    void constructionPoint()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\n"
                              "extrude id=e1 sketch=s1 distance=10\n").ok());
        }
        w.runCommand("construct.point");
        auto* pp = qobject_cast<PointPanel*>(w.featurePanel());
        QVERIFY(pp);
        pp->coord(2)->setText("25 mm");
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        w.runCommand("construct.point");
        pp = qobject_cast<PointPanel*>(w.featurePanel());
        QVERIFY(pp);
        pp->useEdge("e1", "E[e1/side:5|e1/side:6]");
        QCOMPARE(pp->kind()->currentData().toString(), QString("through"));
        QVERIFY(pp->end()->isVisible());  // an edge: which part of it
        pp->end()->setCurrentIndex(pp->end()->findData("mid"));
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const auto& pts = w.document().state().points;
        QCOMPARE(int(pts.size()), 2);
        QCOMPARE(pts.begin()->second.Z(), 25.0);
        QCOMPARE(std::next(pts.begin())->second.Z(), 5.0);
        // an axis through the two points
        w.runCommand("construct.axis");
        auto* ap = qobject_cast<AxisPanel*>(w.featurePanel());
        QVERIFY(ap);
        ap->kind()->setCurrentIndex(ap->kind()->findData("points"));
        std::vector<QComboBox*> ends;  // From point, To point
        for (auto* c : ap->findChildren<QComboBox*>()) {
            if (c->isVisible() && c->findData("O") >= 0) {
                ends.push_back(c);
            }
        }
        QCOMPARE(int(ends.size()), 2);
        ends[0]->setCurrentIndex(ends[0]->findData(QString::fromStdString(pts.begin()->first)));
        ends[1]->setCurrentIndex(ends[1]->findData(QString::fromStdString(std::next(pts.begin())->first)));
        QTest::qWait(300);
        QTRY_VERIFY(ap->previewOk());
        QVERIFY(ap->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().axes.size()), 1);
        // editing a point from the timeline opens its panel with its values
        w.editFeature(2);
        pp = qobject_cast<PointPanel*>(w.featurePanel());
        QVERIFY(pp);
        QCOMPARE(pp->coord(2)->text(), QString("25 mm"));
        pp->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);

        // where the corner edge crosses the floor plane: z = 0
        w.runCommand("construct.point");
        pp = qobject_cast<PointPanel*>(w.featurePanel());
        QVERIFY(pp);
        pp->useEdge("e1", "E[e1/side:5|e1/side:6]");
        pp->end()->setCurrentIndex(pp->end()->findData("plane"));
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().points.size()), 3);
        const std::string crossing = w.document().model().features.back().id;
        QVERIFY(std::abs(w.document().state().points.at(crossing).Z()) < 1e-9);

        // an axis square to the block's top through that point
        w.runCommand("construct.axis");
        ap = qobject_cast<AxisPanel*>(w.featurePanel());
        QVERIFY(ap);
        ap->kind()->setCurrentIndex(ap->kind()->findData("normal"));
        ap->useFace("e1", "e1/end");
        QCOMPARE(ap->kind()->currentData().toString(), QString("normal"));
        QComboBox* at = nullptr;
        for (auto* c : ap->findChildren<QComboBox*>()) {
            if (c->isVisible() && c->findData(QString::fromStdString(crossing)) >= 0) {
                at = c;
            }
        }
        QVERIFY(at);
        at->setCurrentIndex(at->findData(QString::fromStdString(crossing)));
        QTest::qWait(300);
        QTRY_VERIFY(ap->previewOk());
        QVERIFY(ap->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const gp_Ax1 sq = w.document().state().axes.at(w.document().model().features.back().id);
        QVERIFY(sq.Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
    }

    // Tangent arc from a line's end, 2- and 3-point circles, a rectangle at an angle.
    void moreShapes()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        auto count = [&](jig::EntityType t) {
            int n = 0;
            for (auto& [id, e] : sk->sketch().entities()) {
                n += e.type == t && !e.construction;
            }
            return n;
        };
        auto regions = [&] { return w.document().state().sketches.begin()->second->profiles.regions; };
        // a line, then a tangent arc from its right end turning up and back
        sk->setTool(SketchEditor::Tool::Line);
        click(-40, 0);
        click(-20, 0);
        QTest::keyClick(view, Qt::Key_Escape);
        w.runCommand("sketch.arc_tangent");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::TangentArc));
        click(-20, 0);
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        click(-20, 20);  // a half circle of radius 10, centre (-20, 10)
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(count(jig::EntityType::Arc), 1);
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Arc) {
                QVERIFY(std::abs(e.r - 10) < 1e-6);
                QVERIFY(std::abs(sk->sketch().pointXY(e.center).second - 10) < 1e-6);
            }
        }
        QVERIFY(sk->report().redundant.empty());

        // 2-point circle across (0,0)-(20,0); 3-point circle through three grid points
        w.runCommand("sketch.circle2");
        click(0, -30);
        click(20, -30);
        w.runCommand("sketch.circle3");
        click(40, -30);
        click(50, -20);
        click(60, -30);
        QCOMPARE(count(jig::EntityType::Circle), 2);
        bool r10 = false, r10b = false;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Circle) {
                (sk->sketch().pointXY(e.center).first < 30 ? r10 : r10b) = std::abs(e.r - 10) < 1e-6;
            }
        }
        QVERIFY(r10 && r10b);

        // 3-point rectangle: a side at 45°, then 10 mm deep
        const size_t before = regions().size();
        w.runCommand("sketch.rect3");
        click(0, 20);
        click(20, 40);
        QTest::mouseMove(view, sk->screenOf(10, 50));
        sk->typeValues({"10 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(regions().size(), before + 1);
        double area = 0;
        for (auto& r : regions()) {
            area = std::max(area, std::abs(r.area - 10 * std::sqrt(800.0)) < 1e-6 ? r.area : area);
        }
        QVERIFY(area > 0);
    }

    // Ellipse by clicks and typing; a closed smooth curve by clicking round and back to the start.
    void ellipseAndSpline()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        auto regions = [&] { return w.document().state().sketches.begin()->second->profiles.regions; };
        w.runCommand("sketch.ellipse");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Ellipse));
        click(-30, 0);
        sk->typeValues({"15 mm"});        // the long radius, towards the cursor
        QTest::mouseMove(view, sk->screenOf(-30, 5));
        sk->typeValues({"6 mm"});
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(int(regions().size()), 1);
        QVERIFY(std::abs(regions()[0].area - std::acos(-1.0) * 15 * 6) < 1e-6);
        QCOMPARE(sk->report().dof, 3);  // only its place and turn are free

        w.runCommand("sketch.spline");
        click(10, 0);
        click(30, 0);
        click(30, 20);
        click(10, 20);
        click(10, 0);  // back on the first point: closed
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        QCOMPARE(int(regions().size()), 2);
        int splines = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            splines += e.type == jig::EntityType::Spline && e.closed && e.pts.size() == 4;
        }
        QCOMPARE(splines, 1);
        // an open one ends with Enter
        click(40, 30);
        click(50, 35);
        click(60, 30);
        QTest::keyClick(view, Qt::Key_Return);
        int open = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            open += e.type == jig::EntityType::Spline && !e.closed;
        }
        QCOMPARE(open, 1);
        // drag a fit point: the curve follows
        sk->setTool(SketchEditor::Tool::Select);
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(50, 35));
        QMouseEvent mv(QEvent::MouseMove, QPointF(sk->screenOf(50, 45)), view->mapToGlobal(QPointF(sk->screenOf(50, 45))),
                       Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &mv);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(50, 45));
        bool moved = false;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Spline && !e.closed) {
                moved = std::abs(sk->sketch().pointXY(e.pts[1]).second - 45) < 0.5;
            }
        }
        QVERIFY(moved);
        w.grab().save(shotPath("sketch-curves"));
    }

    // Drag a box in Select: left to right takes what is inside, right to left what it touches.
    void windowSelection()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x, y));
        };
        auto box = [&](double x0, double y0, double x1, double y1) {
            QTest::mouseMove(view, sk->screenOf(x0, y0));
            QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x0, y0));
            for (int i = 1; i <= 5; ++i) {
                const double u = i / 5.0;
                QMouseEvent mv(QEvent::MouseMove, QPointF(sk->screenOf(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u)),
                               view->mapToGlobal(QPointF(sk->screenOf(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u))),
                               Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view, &mv);
            }
            QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(x1, y1));
        };
        auto curves = [&] {
            int n = 0;
            for (int id : sk->selection()) {
                n += sk->sketch().entity(id)->type != jig::EntityType::Point;
            }
            return n;
        };
        sk->setTool(SketchEditor::Tool::Circle);
        click(0, 0);
        QTest::mouseMove(view, sk->screenOf(5, 0));
        sk->typeValues({"10 mm"});
        click(30, 0);
        QTest::mouseMove(view, sk->screenOf(35, 0));
        sk->typeValues({"10 mm"});
        sk->setTool(SketchEditor::Tool::Select);
        box(-10, -10, 10, 10);  // left to right round the first circle only
        QCOMPARE(curves(), 1);
        box(28, -10, -10, 10);  // right to left, ending part-way over the second: touches both
        QCOMPARE(curves(), 2);
        box(3, -10, 20, 10);    // left to right, cutting through the first: nothing wholly inside
        QCOMPARE(curves(), 0);
    }

    // Project: click a face of an earlier body; its outline comes into the sketch, fixed.
    void projectTool()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60 mm", "40 mm"});
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("10 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);

        QVERIFY(w.createSketchOn("XY"));
        sk = w.sketchEditor();
        QTRY_VERIFY(view->meshesReady());
        w.runCommand("sketch.project");
        QCOMPARE(int(sk->tool()), int(SketchEditor::Tool::Project));
        const QPoint at = sk->screenOf(5, 5);  // the block's top face, seen from above
        QTest::mouseMove(view, at);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY2(sk->message().isEmpty(), qPrintable(sk->message()));
        int lines = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            lines += e.type == jig::EntityType::Line && !e.construction;
        }
        QCOMPARE(lines, 4);
        QCOMPARE(sk->report().dof, 0);
        auto& regs = w.document().state().sketches.rbegin()->second->profiles.regions;
        QCOMPARE(int(regs.size()), 1);
        QVERIFY(std::abs(regs.front().area - 2400) < 1e-6);
    }

    // Constraints and dimensions: click a glyph to select it, Delete removes it;
    // double-click a dimension to change its value.
    void selectDeleteAndEditConstraints()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60 mm", "40 mm"});
        sk->setTool(SketchEditor::Tool::Select);
        std::vector<int> dims;
        for (auto& [cid, k] : sk->sketch().constraints()) {
            if (k.type == jig::ConstraintType::Distance) {
                dims.push_back(cid);
            }
        }
        QCOMPARE(int(dims.size()), 2);
        view->update();
        QTRY_VERIFY(sk->glyphCenter(dims[0]).has_value());  // painted
        const int dofBefore = sk->report().dof;

        // double-click the first dimension, type 50 mm
        const QPoint g0 = sk->glyphCenter(dims[0])->toPoint();
        QTest::mouseMove(view, g0);
        QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, g0);
        sk->typeValues({"50 mm"});
        double width = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Line && !e.construction) {
                auto [ax, ay] = sk->sketch().pointXY(e.p1);
                auto [bx, by] = sk->sketch().pointXY(e.p2);
                width = std::max(width, std::abs(bx - ax));
            }
        }
        QVERIFY2(std::abs(width - 50) < 1e-6, qPrintable(QString::number(width)));

        // click the other dimension's label and delete it: one more degree of freedom
        view->update();
        QTest::qWait(50);
        int other = 0;
        for (auto& [cid, k] : sk->sketch().constraints()) {
            if (k.type == jig::ConstraintType::Distance && k.expr == "40 mm") {
                other = cid;
            }
        }
        QVERIFY(other);
        QTRY_VERIFY(sk->glyphCenter(other).has_value());
        const QPoint g1 = sk->glyphCenter(other)->toPoint();
        QTest::qWait(600);  // not a double click
        QTest::mouseMove(view, g1);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, g1);
        QCOMPARE(int(sk->selectedConstraints().size()), 1);
        QVERIFY(sk->selectedConstraints().count(other));
        QTest::keyClick(view, Qt::Key_Delete);
        QVERIFY(!sk->sketch().constraints().count(other));
        QCOMPARE(sk->report().dof, dofBefore + 1);
    }

    // Extrude options in the panel: thin wall, then edit to two sides with a taper.
    void extrudeThinTwoSidesTaper()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"20 mm", "20 mm"});
        w.runCommand("sketch.finish");
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("10 mm");
        QVERIFY(!ex->thickness()->isVisible());  // only shown when asked for
        ex->thinWall()->setChecked(true);
        QVERIFY(ex->thickness()->isVisible());
        ex->thickness()->setText("2 mm");
        QTest::qWait(300);
        QTRY_VERIFY2(ex->previewOk(), qPrintable(ex->statusText()));
        QVERIFY(ex->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY2(std::abs(volume(w) - (22 * 22 - 18 * 18) * 10) < 1e-6, qPrintable(QString::number(volume(w))));

        w.editFeature(1);
        auto* ed = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ed);
        QVERIFY(ed->thinWall()->isChecked());
        ed->thinWall()->setChecked(false);
        ed->direction()->setCurrentIndex(ed->direction()->findData("two"));
        QVERIFY(ed->distance2()->isVisible());
        ed->distance2()->setText("4 mm");
        ed->taper()->setText("5 deg");
        QTest::qWait(300);
        QTRY_VERIFY2(ed->previewOk(), qPrintable(ed->statusText()));
        QVERIFY(ed->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double t5 = std::tan(5 * M_PI / 180);
        auto frustum = [&](double h) {
            const double c = 20 - 2 * h * t5;
            return h / 3 * (400 + 20 * c + c * c);
        };
        QVERIFY2(std::abs(volume(w) - (frustum(10) + frustum(4))) < 1e-6, qPrintable(QString::number(volume(w))));
    }

    // Insert > DXF: an inch drawing comes into a new floor sketch at the right size.
    void importDxfDrawing()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.importDxfFile(QString(JIG_SOURCE_DIR) + "/tests/data/plate_inch.dxf"));
        QVERIFY(w.sketchEditor() != nullptr);
        const auto& regions = w.document().state().sketches.begin()->second->profiles.regions;
        int shapes = 0;  // the drawing's areas; its text label comes in as letters too
        double total = 0;
        for (auto& r : regions) {
            if (r.key.rfind("T", 0) != 0) {
                ++shapes;
                total += r.area;
            }
        }
        QCOMPARE(shapes, 4);
        QCOMPARE(int(w.sketchEditor()->sketch().texts().size()), 1);
        const double in2 = 25.4 * 25.4;
        QVERIFY2(std::abs(total - (8 + 2 + M_PI * 0.25) * in2) < 1e-6, qPrintable(QString::number(total)));
        w.runCommand("edit.undo");  // one undo takes the whole import back
        QTRY_COMPARE(int(w.document().state().sketches.begin()->second->profiles.regions.size()), 0);
    }

    // Insert > SVG: artwork at its natural size; double-click its centre to set the width.
    void importSvgArtwork()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.importSvgFile(QString(JIG_SOURCE_DIR) + "/tests/data/badge.svg"));
        SketchEditor* sk = w.sketchEditor();
        QVERIFY(sk);
        auto total = [&] {
            double t = 0;
            for (auto& r : w.document().state().sketches.begin()->second->profiles.regions) {
                t += r.area;
            }
            return t;
        };
        const double natural = total();
        QCOMPARE(int(w.document().state().sketches.begin()->second->profiles.regions.size()), 3);
        const int anchor = sk->sketch().artworks().begin()->second.anchor;
        QVERIFY(sk->editArtworkAt(anchor));
        sk->typeValues({"36 mm"});  // half the natural 72 mm
        QVERIFY2(std::abs(total() - natural / 4) < 1e-6, qPrintable(QString::number(total())));
    }

    // Timeline reorder: drag a step (through the list's own model, as a drop does).
    void reorderTimeline()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openSample("pen_cup"));
        const double v = volume(w);
        auto order = [&] {
            QStringList o;
            for (auto& f : w.document().model().features) {
                o << QString::fromStdString(f.id);
            }
            return o.join(" ");
        };
        QCOMPARE(order(), QString("s1 e1 sh1 f1 f2 s2 e2"));
        QListWidget* timeline = nullptr;
        for (auto* l : w.findChildren<QListWidget*>()) {
            if (l->accessibleName() == "Timeline of steps") {
                timeline = l;
            }
        }
        QVERIFY(timeline);
        // drag the rim round (f2) in front of the base round (f1)
        QVERIFY(timeline->model()->moveRow(QModelIndex(), 4, QModelIndex(), 3));
        QTRY_COMPARE(order(), QString("s1 e1 sh1 f2 f1 s2 e2"));
        QVERIFY(std::abs(volume(w) - v) < 1e-6);
        QCOMPARE(timeline->count(), 7);
        // the menu's "Move later" path, back again
        QVERIFY(w.moveStep("f2", 4));
        QCOMPARE(order(), QString("s1 e1 sh1 f1 f2 s2 e2"));
    }

    // Versions: save two, restore the first (undoable).
    void saveAndRestoreVersions()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openSample("pen_cup"));
        const QString path = QDir::temp().filePath("jigsmith-ui-versions.jig");
        QFile::remove(path);
        QVERIFY(w.saveTo(path));
        QVERIFY(w.saveNamedVersion("tall"));
        const double tall = volume(w);
        {
            jig::ScriptRunner r(w.document());
            QVERIFY(r.runLine("param height = 60 mm").ok());
        }
        QVERIFY(w.saveNamedVersion("short"));
        const double shortCup = volume(w);
        QVERIFY(shortCup < tall);
        auto list = jig::listVersions(path.toStdString());
        QVERIFY(list.ok());
        QCOMPARE(int(list->size()), 2);
        QVERIFY(w.restoreVersion(1));
        QVERIFY(std::abs(volume(w) - tall) < 1e-6);
        w.runCommand("edit.undo");  // the restore is one change
        QTRY_VERIFY(std::abs(volume(w) - shortCup) < 1e-6);
        QFile::remove(path);
    }

    // The first-run tutorial follows real work: each step moves on only when it is done.
    void tutorialFollowsTheWork()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        w.runCommand("help.tutorial");
        TutorialCard* t = w.tutorial();
        QVERIFY(t);
        QVERIFY(t->isVisible());
        QCOMPARE(t->step(), 0);
        t->check();
        QCOMPARE(t->step(), 0);  // nothing done yet: no progress
        QVERIFY(w.createSketchOn("XY"));
        QTRY_COMPARE(t->step(), 1);
        SketchEditor* sk = w.sketchEditor();
        sk->setTool(SketchEditor::Tool::CenterRectangle);
        QTest::mouseMove(view, sk->screenOf(0, 0));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, sk->screenOf(0, 0));
        QTest::mouseMove(view, sk->screenOf(10, 8));
        sk->typeValues({"60", "30"});
        QTRY_COMPARE(t->step(), 2);
        w.runCommand("sketch.finish");
        QTRY_COMPARE(t->step(), 3);
        w.runCommand("solid.extrude");
        auto* ex = qobject_cast<ExtrudePanel*>(w.featurePanel());
        QVERIFY(ex);
        ex->distance()->setText("5 mm");
        QTRY_VERIFY(ex->previewOk());
        QVERIFY(ex->accept());
        QTRY_COMPARE(t->step(), 4);
        w.runCommand("solid.fillet");
        auto* fp = qobject_cast<FilletPanel*>(w.featurePanel());
        QVERIFY(fp);
        const std::string body = w.document().state().bodies.begin()->first;
        const auto& b = *w.document().state().bodies.begin()->second;
        for (int e : b.edgesOfFace(b.findFaces("e1/end").at(0))) {
            fp->toggleEdge(body, e);
        }
        fp->radius()->setText("2 mm");
        QTRY_VERIFY(fp->previewOk());
        QVERIFY(fp->accept());
        QTRY_COMPARE(t->step(), 5);
        QVERIFY(!t->finished());  // exporting is the last step
        QTest::qWait(100);
        w.grab().save(shotPath("tutorial"));
    }

    // Concentric and collinear, from a selection made by clicking.
    void concentricAndCollinear()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();
        QVERIFY(w.createSketchOn("XY"));
        SketchEditor* sk = w.sketchEditor();
        auto click = [&](double x, double y, Qt::KeyboardModifiers m = Qt::NoModifier) {
            QTest::mouseMove(view, sk->screenOf(x, y));
            QTest::mouseClick(view, Qt::LeftButton, m, sk->screenOf(x, y));
        };
        sk->setTool(SketchEditor::Tool::Circle);
        click(0, 0);
        QTest::mouseMove(view, sk->screenOf(5, 0));
        sk->typeValues({"10 mm"});
        sk->setTool(SketchEditor::Tool::Circle);
        click(30, 10);
        QTest::mouseMove(view, sk->screenOf(36, 10));
        sk->typeValues({"20 mm"});
        sk->setTool(SketchEditor::Tool::Select);
        QTest::qWait(600);
        click(5, 0);                          // the small circle's outline
        QTest::qWait(600);
        click(40, 10, Qt::ShiftModifier);     // and the big one's
        QCOMPARE(int(sk->selection().size()), 2);
        QVERIFY(sk->applyConcentric());
        std::vector<std::pair<double, double>> centres;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Circle) {
                centres.push_back(sk->sketch().pointXY(e.center));
            }
        }
        QCOMPARE(int(centres.size()), 2);
        QVERIFY(std::hypot(centres[0].first - centres[1].first, centres[0].second - centres[1].second) < 1e-6);

        // two separate lines, the second slanted; collinear puts both its ends on the first
        sk->setTool(SketchEditor::Tool::Line);
        click(-40, -30);
        click(-20, -30);
        QTest::keyClick(view, Qt::Key_Escape);
        sk->setTool(SketchEditor::Tool::Line);
        click(0, -36);
        click(20, -33);
        QTest::keyClick(view, Qt::Key_Escape);
        sk->setTool(SketchEditor::Tool::Select);
        QTest::qWait(600);
        click(-30, -30);
        QTest::qWait(600);
        click(10, -34.5, Qt::ShiftModifier);
        QCOMPARE(int(sk->selection().size()), 2);
        QVERIFY(sk->applyCollinear());
        // all four ends on one line (the solver may move either line to get there)
        std::vector<std::pair<double, double>> ends;
        for (auto& [id, e] : sk->sketch().entities()) {
            if (e.type == jig::EntityType::Line && !e.construction) {
                ends.push_back(sk->sketch().pointXY(e.p1));
                ends.push_back(sk->sketch().pointXY(e.p2));
            }
        }
        QCOMPARE(int(ends.size()), 4);
        const double dx = ends[1].first - ends[0].first, dy = ends[1].second - ends[0].second;
        const double len = std::hypot(dx, dy);
        for (int k = 2; k < 4; ++k) {
            const double off = std::abs((ends[k].first - ends[0].first) * dy - (ends[k].second - ends[0].second) * dx) / len;
            QVERIFY2(off < 1e-6, qPrintable(QString::number(off)));
        }
    }

    // Revolve a ring, drill it from its top face, repeat the hole around the axis,
    // and mirror the result: the M3 panels driven like a person would.
    void ringRevolveHolePatternMirror()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        Viewport* view = w.viewport();

        // 1. A 10 x 16 rectangle on the front plane, 10 mm from the vertical axis.
        QVERIFY(w.createSketchOn("XZ"));
        SketchEditor* sk = w.sketchEditor();
        QVERIFY(sk);
        sk->setTool(SketchEditor::Tool::Rectangle);
        const QPoint corner = sk->screenOf(10, 0);
        QTest::mouseMove(view, corner);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, corner);
        QTest::mouseMove(view, sk->screenOf(15, 8));
        sk->typeValues({"10", "16"});
        w.runCommand("sketch.finish");

        // 2. Revolve around the Z axis (the default when the sketch has no construction line).
        w.runCommand("solid.revolve");
        auto* rv = qobject_cast<RevolvePanel*>(w.featurePanel());
        QVERIFY(rv);
        QCOMPARE(rv->axis()->currentData().toString(), QString("Z"));
        QTRY_VERIFY(rv->previewOk());
        QVERIFY(rv->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double ring = M_PI * (20 * 20 - 10 * 10) * 16;
        QVERIFY2(std::abs(volume(w) - ring) < 1e-3, qPrintable(QString::number(volume(w))));

        // 3. Sketch on the flat top (z = 16) and place one point at radius 15.
        const std::string bodyId = w.document().state().bodies.begin()->first;
        const auto& body = *w.document().state().bodies.begin()->second;
        int top = -1;
        for (int i = 0; i < body.faceCount(); ++i) {
            GProp_GProps g;
            BRepGProp::SurfaceProperties(body.face(i), g);
            if (std::abs(g.CentreOfMass().Z() - 16) < 1e-6 && std::abs(g.Mass() - M_PI * 300) < 1e-3) {
                top = i;
            }
        }
        QVERIFY(top >= 0);
        QVERIFY(w.createSketchOn({}, bodyId, top));
        sk = w.sketchEditor();
        QVERIFY(sk);
        sk->setTool(SketchEditor::Tool::Point);
        QTRY_VERIFY(view->meshesReady());
        const QPoint at = view->project(QVector3D(15, 0, 16)).toPoint();
        QTest::mouseMove(view, at);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
        w.runCommand("sketch.finish");

        // 4. A 4 mm hole straight through.
        w.runCommand("solid.hole");
        auto* hp = qobject_cast<HolePanel*>(w.featurePanel());
        QVERIFY(hp);
        hp->diameter()->setText("4 mm");
        QComboBox* extent = nullptr;
        for (auto* c : hp->findChildren<QComboBox*>()) {
            if (c->findData("all") >= 0) {
                extent = c;
            }
        }
        QVERIFY(extent);
        extent->setCurrentIndex(extent->findData("all"));
        QTest::qWait(300);
        QTRY_VERIFY(hp->previewOk());
        QVERIFY(hp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const double hole = M_PI * 4 * 16;
        QVERIFY2(std::abs(volume(w) - (ring - hole)) < 1e-3, qPrintable(QString::number(volume(w))));

        // 5. Ring copy of the hole: six around the axis.
        w.runCommand("solid.pattern_circ");
        auto* pp = qobject_cast<PatternPanel*>(w.featurePanel());
        QVERIFY(pp);
        QCOMPARE(pp->source()->currentData().toString().left(1), QString("h"));
        pp->count()->setText("6");
        QTest::qWait(300);
        QTRY_VERIFY(pp->previewOk());
        QVERIFY(pp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QVERIFY2(std::abs(volume(w) - (ring - 6 * hole)) < 1e-3, qPrintable(QString::number(volume(w))));
        QTRY_VERIFY(view->meshesReady());
        view->homeView();
        QTest::qWait(300);
        w.grab().save(shotPath("ring-pattern"));

        // 6. Mirror under the floor as a separate body.
        w.runCommand("solid.mirror");
        auto* mp = qobject_cast<MirrorPanel*>(w.featurePanel());
        QVERIFY(mp);
        mp->plane()->setCurrentIndex(mp->plane()->findData("XY"));
        QComboBox* op = nullptr;
        for (auto* c : mp->findChildren<QComboBox*>()) {
            if (c->findData("new") >= 0) {
                op = c;
            }
        }
        QVERIFY(op);
        op->setCurrentIndex(op->findData("new"));
        QTest::qWait(300);
        QTRY_VERIFY(mp->previewOk());
        QVERIFY(mp->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        QCOMPARE(int(w.document().state().bodies.size()), 2);

        // Editing the hole from the timeline reopens its panel with the stored values.
        w.editFeature(3);
        auto* again = qobject_cast<HolePanel*>(w.featurePanel());
        QVERIFY(again);
        QCOMPARE(again->diameter()->text(), QString("4 mm"));
        // pick a screw size: the table fills in (and locks) the diameter
        again->size()->setCurrentIndex(again->size()->findData("M3"));
        again->fit()->setCurrentIndex(again->fit()->findData("close"));
        QCOMPARE(again->diameter()->text(), QString("3.2 mm"));
        QVERIFY(!again->diameter()->isEnabled());
        again->bottom()->setCurrentIndex(again->bottom()->findData("118 deg"));
        QVERIFY(!again->bottom()->isVisible());  // through holes have no bottom
        QTest::qWait(300);
        QTRY_VERIFY(again->previewOk());
        QVERIFY(again->accept());
        QTRY_VERIFY(w.featurePanel() == nullptr);
        const jig::Feature& hf = w.document().model().features[3];
        QCOMPARE(QString::fromStdString(hf.params.value("size", std::string {})), QString("M3"));
        QVERIFY(!hf.params.contains("diameter"));
        QVERIFY(!hf.params.contains("tip"));
    }
};

int main(int argc, char** argv)
{
    configureSurfaceFormat();  // exactly as the application does
    QApplication app(argc, argv);
    UiTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "test_ui.moc"
