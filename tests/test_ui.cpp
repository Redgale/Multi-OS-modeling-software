// SPDX-License-Identifier: GPL-3.0-or-later
// UI workflow test: drives the real application window the way a person would
// (clicks in the 3D view, typed values, panel OK), and checks the resulting model.
// Brief 7.1: "UI smoke and workflow tests that drive the real app", with screenshots.
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QLineEdit>
#include <QSettings>
#include <QTableWidget>
#include <QtTest>

#include "FeaturePanels.hpp"
#include "InspectPanels.hpp"
#include "Icons.hpp"
#include "MainWindow.hpp"
#include "SketchEditor.hpp"
#include "Viewport.hpp"
#include "jig/commands.hpp"

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
        // the front half is gone; we look at the inside of the back half (brick red)
        QVERIFY2(after.red() > after.green() + 40, qPrintable(after.name()));
        QVERIFY2(!(before.red() > before.green() + 40), qPrintable(before.name()));
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
        QCOMPARE(int(regions.size()), 4);
        double total = 0;
        for (auto& r : regions) {
            total += r.area;
        }
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
        again->cancel();
        QTRY_VERIFY(w.featurePanel() == nullptr);
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
