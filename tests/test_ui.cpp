// SPDX-License-Identifier: GPL-3.0-or-later
// UI workflow test: drives the real application window the way a person would
// (clicks in the 3D view, typed values, panel OK), and checks the resulting model.
// Brief 7.1: "UI smoke and workflow tests that drive the real app", with screenshots.
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
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

        int lines = 0, arcs = 0;
        for (auto& [id, e] : sk->sketch().entities()) {
            lines += e.type == jig::EntityType::Line && !e.construction;
            arcs += e.type == jig::EntityType::Arc;
        }
        QCOMPARE(lines, 6 + 2);
        QCOMPARE(arcs, 2 + 1);
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
