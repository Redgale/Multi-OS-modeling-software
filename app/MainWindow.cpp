// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.hpp"

#include "MarkingMenu.hpp"
#include "Translations.hpp"

#include <array>

#include <QAction>
#include <QLocale>
#include <QLockFile>
#include <QUuid>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QPainter>
#include <QInputDialog>
#include <QGridLayout>
#include <QColorDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QShortcut>
#include <QStackedWidget>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QPointer>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWhatsThis>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProgressDialog>
#include <QThread>
#include <atomic>
#include <thread>

#include "FeaturePanels.hpp"
#include "InspectPanels.hpp"
#include "Tutorial.hpp"
#include "Icons.hpp"
#include "SketchEditor.hpp"
#include "Viewport.hpp"
#include "jig/commands.hpp"
#include "jig/io.hpp"
#include "jig/picture.hpp"
#include "jig/version.hpp"

// Resources live in the static jigsmith_ui library: register them explicitly.
static void initJigResources()
{
    Q_INIT_RESOURCE(resources);
}

namespace jigapp
{
namespace
{

QColor accentFor(const QPalette& pal)
{
    // blue on light backgrounds, amber on dark ones
    return pal.color(QPalette::Window).lightness() > 128 ? QColor(37, 110, 222) : QColor(255, 190, 80);
}

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

// Status is never shown by colour alone: every state has its own symbol and word.
QString statusGlyph(jig::StatusLevel l)
{
    switch (l) {
        case jig::StatusLevel::Ok:
            return QStringLiteral("✓");
        case jig::StatusLevel::Warning:
            return QStringLiteral("⚠");
        case jig::StatusLevel::Error:
            return QStringLiteral("✕");
        case jig::StatusLevel::Suppressed:
            return QStringLiteral("⏸");
        case jig::StatusLevel::RolledBack:
            return QStringLiteral("…");
    }
    return {};
}

QString statusWord(jig::StatusLevel l)
{
    switch (l) {
        case jig::StatusLevel::Ok:
            return MainWindow::tr("OK");
        case jig::StatusLevel::Warning:
            return MainWindow::tr("Check this");
        case jig::StatusLevel::Error:
            return MainWindow::tr("Problem");
        case jig::StatusLevel::Suppressed:
            return MainWindow::tr("Suppressed");
        case jig::StatusLevel::RolledBack:
            return MainWindow::tr("After the rollback marker");
    }
    return {};
}

QString kindWord(jig::FeatureKind k)
{
    switch (k) {
        case jig::FeatureKind::Sketch:
            return MainWindow::tr("Sketch");
        case jig::FeatureKind::Extrude:
            return MainWindow::tr("Extrude");
        case jig::FeatureKind::Fillet:
            return MainWindow::tr("Fillet");
        case jig::FeatureKind::Chamfer:
            return MainWindow::tr("Chamfer");
        case jig::FeatureKind::Shell:
            return MainWindow::tr("Shell");
        case jig::FeatureKind::Revolve:
            return MainWindow::tr("Revolve");
        case jig::FeatureKind::Hole:
            return MainWindow::tr("Hole");
        case jig::FeatureKind::Pattern:
            return MainWindow::tr("Pattern");
        case jig::FeatureKind::Mirror:
            return MainWindow::tr("Mirror");
        case jig::FeatureKind::Plane:
            return MainWindow::tr("Plane");
        case jig::FeatureKind::Axis:
            return MainWindow::tr("Axis");
        case jig::FeatureKind::Point:
            return MainWindow::tr("Point");
        case jig::FeatureKind::Import:
            return MainWindow::tr("Import");
        case jig::FeatureKind::Remove:
            return MainWindow::tr("Remove");
        case jig::FeatureKind::PressPull:
            return MainWindow::tr("Press pull");
        case jig::FeatureKind::Combine:
            return MainWindow::tr("Combine");
        case jig::FeatureKind::Split:
            return MainWindow::tr("Split");
        case jig::FeatureKind::Loft:
            return MainWindow::tr("Loft");
        case jig::FeatureKind::Sweep:
            return MainWindow::tr("Sweep");
        case jig::FeatureKind::Pipe:
            return MainWindow::tr("Pipe");
        case jig::FeatureKind::Coil:
            return MainWindow::tr("Coil");
        case jig::FeatureKind::Primitive:
            return MainWindow::tr("Basic shape");
        case jig::FeatureKind::Rib:
            return MainWindow::tr("Rib");
        case jig::FeatureKind::Emboss:
            return MainWindow::tr("Emboss");
        case jig::FeatureKind::Draft:
            return MainWindow::tr("Draft");
        case jig::FeatureKind::Scale:
            return MainWindow::tr("Scale");
        case jig::FeatureKind::Thread:
            return MainWindow::tr("Thread");
        case jig::FeatureKind::Canvas:
            return MainWindow::tr("Picture");
        case jig::FeatureKind::Joint:
            return MainWindow::tr("Joint");
        case jig::FeatureKind::Fastener:
            return MainWindow::tr("Fastener");
        case jig::FeatureKind::Move:
            return MainWindow::tr("Move");
    }
    return {};
}

// ---------------------------------------------------------------- command palette

class CommandPalette : public QDialog
{
    Q_OBJECT  // (so tr() uses this class's context, which is what translators see)
public:
    explicit CommandPalette(QWidget* parent)
        : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
    {
        setAccessibleName(tr("Find a tool"));
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(10, 10, 10, 10);
        edit_ = new QLineEdit(this);
        edit_->setPlaceholderText(tr("What do you want to do? e.g. “round the edges”, “hollow”, “export”"));
        edit_->setAccessibleName(tr("Search tools"));
        list_ = new QListWidget(this);
        list_->setAccessibleName(tr("Matching tools"));
        note_ = new QLabel(this);
        note_->setWordWrap(true);
        lay->addWidget(edit_);
        lay->addWidget(list_);
        lay->addWidget(note_);
        resize(560, 420);
        connect(edit_, &QLineEdit::textChanged, this, [this](const QString& t) { fill(t); });
        connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { choose(); });
        connect(list_, &QListWidget::currentRowChanged, this, [this](int) { describe(); });
        edit_->installEventFilter(this);
        fill({});
    }
    QString chosen;

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == edit_ && e->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) {
                int r = list_->currentRow() + (k->key() == Qt::Key_Down ? 1 : -1);
                list_->setCurrentRow(std::clamp(r, 0, list_->count() - 1));
                return true;
            }
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
                choose();
                return true;
            }
        }
        return QDialog::eventFilter(o, e);
    }

private:
    void fill(const QString& text)
    {
        list_->clear();
        for (auto& m : jig::CommandRegistry::instance().search(text.toStdString())) {
            const auto& c = *m.info;
            QString line = QCoreApplication::translate("Command", c.label.c_str());
            if (c.standardName != c.label) {
                line += "  —  " + q(c.standardName);  // always show the standard CAD name
            }
            if (!c.shortcut.empty()) {
                line += "   [" + QKeySequence(q(c.shortcut)).toString(QKeySequence::NativeText) + "]";
            }
            if (!c.available) {
                line += "   " + tr("(coming in %1)").arg(q(c.comingIn.empty() ? "a later version" : c.comingIn));
            }
            auto* it = new QListWidgetItem(toolIcon(q(c.id), palette().color(QPalette::Text), accentFor(palette())),
                                           line, list_);
            it->setData(Qt::UserRole, q(c.id));
            it->setToolTip(q(c.tooltip));
            if (!c.available) {
                it->setForeground(palette().color(QPalette::PlaceholderText));
            }
        }
        list_->setCurrentRow(0);
        describe();
    }
    void describe()
    {
        auto* it = list_->currentItem();
        if (!it) {
            note_->setText(tr("No tool matches. Try other words, like “cut”, “round” or “print”."));
            return;
        }
        const auto* c = jig::CommandRegistry::instance().find(it->data(Qt::UserRole).toString().toStdString());
        QString t = q(c->tooltip);
        if (!c->available) {
            t += "\n" + tr("Not in the app yet: arrives in milestone %1.").arg(q(c->comingIn));
            if (c->scripted) {
                t += " " + tr("It already works in scripts (jigsmith-cli).");
            }
        }
        note_->setText(t);
    }
    void choose()
    {
        auto* it = list_->currentItem();
        if (!it) {
            return;
        }
        const auto* c = jig::CommandRegistry::instance().find(it->data(Qt::UserRole).toString().toStdString());
        if (!c->available) {
            describe();
            return;
        }
        chosen = q(c->id);
        accept();
    }
    QLineEdit* edit_;
    QListWidget* list_;
    QLabel* note_;
};

}  // namespace

// ---------------------------------------------------------------- construction

// ---------------------------------------------------------------- plane picker

// "Create sketch": shows the three origin planes; click one, or a flat face of a body.
// Corners of the square that shows a construction plane, half-size s.
std::array<QVector3D, 4> planeCorners(const gp_Ax3& f, float s)
{
    auto at = [&](double u, double v) {
        const gp_XYZ p = f.Location().XYZ() + f.XDirection().XYZ() * u + f.YDirection().XYZ() * v;
        return QVector3D(float(p.X()), float(p.Y()), float(p.Z()));
    };
    return {at(-s, -s), at(s, -s), at(s, s), at(-s, s)};
}

const QColor kDatum(222, 150, 40);  // construction planes: amber, with a name label

class PlanePicker : public Interactor
{
public:
    PlanePicker(MainWindow* w, Viewport* v)
        : w_(w)
        , v_(v)
    {
        rebuild();
    }
    ~PlanePicker() override
    {
        v_->setOverlay({});
    }
    bool mouseMove(QMouseEvent* e) override
    {
        const Hit h = hitAt(e->position());
        if (h.plane != hover_.plane || h.face != hover_.face) {
            hover_ = h;
            rebuild();
        }
        return true;
    }
    bool key(QKeyEvent* e) override
    {
        // without the mouse: 1 floor, 2 front, 3 side
        static const char* planes[] = {"XY", "XZ", "YZ"};
        const int n = e->key() - Qt::Key_1;
        if (n >= 0 && n < 3) {
            w_->createSketchOn(planes[n]);
            return true;
        }
        return false;
    }
    bool mouseRelease(QMouseEvent* e) override
    {
        const Hit h = hitAt(e->position());
        if (h.face >= 0) {
            w_->createSketchOn({}, h.body, h.face);
        }
        else if (!h.plane.isEmpty()) {
            w_->createSketchOn(h.plane.toStdString());
        }
        return true;
    }
    void paint(QPainter& p) override
    {
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        const QString msg = QObject::tr("Click a plane, a construction plane or a flat face to sketch on, or press 1 (floor), 2 (front) "
                                           "or 3 (side).  Esc cancels.");
        const QRectF box(10, 10, p.fontMetrics().horizontalAdvance(msg) + 20, 26);
        QColor bg = v_->colors().background;
        bg.setAlpha(230);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(box, 5, 5);
        p.setPen(v_->colors().text);
        p.drawText(box, Qt::AlignCenter, msg);
        const float s = size();
        const std::pair<QString, QVector3D> labels[] = {{QObject::tr("1 · XY · floor"), {s * 0.75f, -s * 0.75f, 0}},
                                                         {QObject::tr("2 · XZ · front"), {s * 0.75f, 0, s * 0.85f}},
                                                         {QObject::tr("3 · YZ · side"), {0, s * 0.75f, s * 0.85f}}};
        for (auto& [t, w] : labels) {
            p.drawText(v_->project(w), t);
        }
    }

private:
    struct Hit
    {
        QString plane;
        std::string body;
        int face = -1;
    };
    float size() const
    {
        float s = 60;
        for (auto& [id, b] : w_->document().state().bodies) {
            auto mp = jig::massProperties(b->shape());
            s = std::max({s, float(mp.xmax - mp.xmin), float(mp.ymax - mp.ymin), float(mp.zmax - mp.zmin)});
        }
        return s * 0.6f;
    }
    Hit hitAt(const QPointF& px) const
    {
        Hit h;
        if (auto f = v_->pickFaceAt(px.toPoint())) {
            auto it = w_->document().state().bodies.find(f->body);
            if (it != w_->document().state().bodies.end() && it->second->faceFingerprint(f->face).type == "plane") {
                h.body = f->body;
                h.face = f->face;
                return h;
            }
        }
        QVector3D o, d;
        v_->ray(px, o, d);
        const float s = size();
        float best = 1e30f;
        auto test = [&](const QString& name, int axis) {
            const float denom = axis == 0 ? d.z() : axis == 1 ? d.y() : d.x();
            if (std::abs(denom) < 1e-6f) {
                return;
            }
            const float t = -(axis == 0 ? o.z() : axis == 1 ? o.y() : o.x()) / denom;
            const QVector3D p = o + d * t;
            const float a = axis == 2 ? p.y() : p.x(), b = axis == 0 ? p.y() : p.z();
            if (t > 0 && std::abs(a) <= s && std::abs(b) <= s && t < best) {
                best = t;
                h.plane = name;
            }
        };
        test("XY", 0);
        test("XZ", 1);
        test("YZ", 2);
        for (auto& [id, f] : w_->document().state().planes) {
            const gp_Dir n = f.Direction();
            const QVector3D N(float(n.X()), float(n.Y()), float(n.Z()));
            const float denom = QVector3D::dotProduct(N, d);
            if (std::abs(denom) < 1e-6f) {
                continue;
            }
            const QVector3D L(float(f.Location().X()), float(f.Location().Y()), float(f.Location().Z()));
            const float t = QVector3D::dotProduct(L - o, N) / denom;
            const QVector3D q = o + d * t - L;
            const gp_Dir X = f.XDirection(), Y = f.YDirection();
            const float u = QVector3D::dotProduct(q, QVector3D(float(X.X()), float(X.Y()), float(X.Z())));
            const float v = QVector3D::dotProduct(q, QVector3D(float(Y.X()), float(Y.Y()), float(Y.Z())));
            if (t > 0 && std::abs(u) <= s && std::abs(v) <= s && t < best) {
                best = t;
                h.plane = QString::fromStdString(id);
            }
        }
        return h;
    }
    void rebuild()
    {
        SceneOverlay ov;
        const float s = size();
        auto quad = [&](const QString& name, QVector3D a, QVector3D b, QVector3D c, QVector3D d, QColor col) {
            QColor fill = col;
            fill.setAlpha(hover_.plane == name ? 90 : 38);
            for (auto& p : {a, b, c, a, c, d}) {
                ov.tris.push_back({p, fill});
            }
            col.setAlpha(200);
            for (auto [p, q2] : {std::pair {a, b}, std::pair {b, c}, std::pair {c, d}, std::pair {d, a}}) {
                ov.lines.push_back({p, col});
                ov.lines.push_back({q2, col});
            }
        };
        quad("XY", {-s, -s, 0}, {s, -s, 0}, {s, s, 0}, {-s, s, 0}, QColor(60, 110, 230));
        quad("XZ", {-s, 0, -s}, {s, 0, -s}, {s, 0, s}, {-s, 0, s}, QColor(64, 170, 90));
        quad("YZ", {0, -s, -s}, {0, s, -s}, {0, s, s}, {0, -s, s}, QColor(214, 64, 69));
        for (auto& [id, f] : w_->document().state().planes) {
            if (hover_.plane == QString::fromStdString(id)) {
                auto c = planeCorners(f, s);
                quad(hover_.plane, c[0], c[1], c[2], c[3], kDatum);
            }
        }
        v_->setOverlay(std::move(ov));
        if (hover_.face >= 0) {
            v_->selectFaceForScreenshot(-1);
        }
    }
    MainWindow* w_;
    Viewport* v_;
    Hit hover_;
};

namespace
{
// Recomputes run on a worker thread (B-009) while this thread keeps its event loop going:
// the window repaints, clicks and keys wait until the rebuild is done, and a long rebuild
// shows how far it has got. JIG_PROGRESS_AFTER_MS sets when that dialog appears (tests).
void installRecomputeRunner()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    jig::Document::setRecomputeRunner([](const jig::Document& doc, const std::function<void()>& work) {
        if (!qApp || QThread::currentThread() != qApp->thread()) {
            work();
            return;
        }
        std::atomic<bool> finished {false};
        std::thread worker([&] {
            work();
            finished = true;
        });
        const int showAfter = qEnvironmentVariableIsSet("JIG_PROGRESS_AFTER_MS")
                                  ? qEnvironmentVariableIntValue("JIG_PROGRESS_AFTER_MS")
                                  : 400;
        QElapsedTimer clock;
        clock.start();
        QEventLoop loop;
        std::unique_ptr<QProgressDialog> dialog;
        QTimer tick;
        tick.setInterval(20);
        QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
            if (finished) {
                loop.quit();
                return;
            }
            const auto [stepsDone, total] = doc.recomputeProgress();
            if (!dialog && clock.elapsed() >= showAfter) {
                dialog = std::make_unique<QProgressDialog>(
                    QCoreApplication::translate("MainWindow", "Rebuilding the design…"), QString(), 0,
                    std::max(total, 1), QApplication::activeWindow());
                dialog->setObjectName("RebuildProgress");
                // not modal: a modal one would handle clicks and keys itself whenever its value
                // changes; this loop holds them back until the rebuild is done
                dialog->setWindowModality(Qt::NonModal);
                dialog->setMinimumDuration(0);
                dialog->setAutoClose(false);
                dialog->show();
            }
            if (dialog) {
                dialog->setLabelText(QCoreApplication::translate("MainWindow", "Rebuilding the design: step %1 of %2…")
                                         .arg(std::min(stepsDone + 1, total))
                                         .arg(total));
                dialog->setValue(std::min(stepsDone, total));
            }
        });
        tick.start();
        if (!finished) {
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        }
        worker.join();
    });
}
}  // namespace

MainWindow::MainWindow()
{
    initJigResources();
    installRecomputeRunner();
    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    setWindowTitle(tr("Jigsmith"));
    setObjectName("MainWindow");
    view_ = new Viewport(this);
    view_->setAccessibleName(tr("3D view"));
    view_->setWhatsThis(tr("The 3D view. Middle-drag pans, Shift+middle-drag orbits, the wheel zooms "
                           "toward the cursor; right-drag also orbits. Click a face to select it. Use the "
                           "axis gizmo at the top right for standard views, Home and Fit."));
    setCentralWidget(view_);
    buildActions();
    buildMenus();
    buildToolbar();
    buildDocks();

    statusSel_ = new QLabel(this);
    statusInfo_ = new QLabel(this);
    statusBar()->addWidget(statusSel_, 1);
    statusBar()->addPermanentWidget(statusInfo_);
    connect(view_, &Viewport::hovered, this, [this](const QString& d) { statusSel_->setText(d); });
    connect(view_, &Viewport::menuRequested, this, [this](const QPoint& p) { openMarkingMenu(p); });
    connect(view_, &Viewport::graphicsProblem, this, [this](const QString& why) {
        statusBar()->showMessage(why);
        auto* box = new QMessageBox(QMessageBox::Warning, tr("3D view unavailable"), why, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();  // not modal to the event loop: everything else keeps working
    });
    connect(view_, &Viewport::selectOtherRequested, this, [this](const QPoint& p) { openSelectOther(p); });
    selectFilter_ = new QComboBox(this);
    selectFilter_->addItem(tr("Pick faces"), "faces");
    selectFilter_->addItem(tr("Pick edges"), "edges");
    selectFilter_->addItem(tr("Pick bodies"), "bodies");
    selectFilter_->setAccessibleName(tr("Selection filter"));
    selectFilter_->setToolTip(tr("What a click in the view picks when no tool is open."));
    statusBar()->addPermanentWidget(selectFilter_);
    connect(selectFilter_, &QComboBox::currentIndexChanged, this, [this] { setSelectFilter(selectFilter()); });
    connect(view_, &Viewport::edgeClicked, this, [this](const QString& body, const QString& name, Qt::KeyboardModifiers) {
        if (panel_ || sketch_ || selectFilter() != "edges" || !doc_) {
            return;
        }
        auto it = doc_->state().bodies.find(body.toStdString());
        if (it == doc_->state().bodies.end()) {
            return;
        }
        auto hits = it->second->findEdges(name.toStdString());
        if (hits.empty()) {
            return;
        }
        const jig::MeasureItem m = jig::measureItem(it->second->edge(hits[0]));
        QString html = tr("<h3>Edge</h3><p><b>Name:</b> %1<br><b>Kind:</b> %2<br><b>Length:</b> %3")
                           .arg(name.toHtmlEscaped(), q(m.kind), q(doc_->fmtLength(m.size)));
        if (m.radius) {
            html += tr("<br><b>Diameter:</b> %1").arg(q(doc_->fmtLength(2 * *m.radius)));
        }
        details_->setHtml(html + tr("<br><b>Body:</b> %1</p>").arg(body));
    });
    connect(view_, &Viewport::selectionChanged, this, [this](const QString& body, int face) {
        if (face < 0 || !doc_) {
            details_->setHtml(tr("<p>Nothing selected.</p>"));
            return;
        }
        auto it = doc_->state().bodies.find(body.toStdString());
        if (it == doc_->state().bodies.end()) {
            return;
        }
        if (!panel_ && selectFilter() == "bodies") {
            // the whole body the face belongs to
            const auto mp = jig::massProperties(it->second->shape());
            const jig::Feature* f = doc_->feature(body.toStdString());
            details_->setHtml(tr("<h3>Body</h3><p><b>Name:</b> %1<br><b>Volume:</b> %2<br><b>Size:</b> %3 × %4 × %5<br>"
                                 "<b>Faces:</b> %6</p><p>Right-click it in the Browser to hide, isolate or rename it.</p>")
                                  .arg(f ? q(f->name).toHtmlEscaped() : body, q(jig::formatVolume(mp.volume, doc_->units())),
                                       q(doc_->fmtLength(mp.xmax - mp.xmin)), q(doc_->fmtLength(mp.ymax - mp.ymin)),
                                       q(doc_->fmtLength(mp.zmax - mp.zmin)))
                                  .arg(it->second->faceCount()));
            browser_->blockSignals(true);  // (only mark the row: the details stay on the body)
            for (QTreeWidgetItemIterator bi(browser_); *bi; ++bi) {
                if ((*bi)->data(0, Qt::UserRole).toString() == body) {
                    browser_->setCurrentItem(*bi);
                }
            }
            browser_->blockSignals(false);
            return;
        }
        auto fp = it->second->faceFingerprint(face);
        details_->setHtml(tr("<h3>Face</h3><p><b>Name:</b> %1<br><b>Surface:</b> %2<br><b>Area:</b> %3<br>"
                             "<b>Body:</b> %4</p><p>Faces keep their name when earlier steps change, so "
                             "fillets and sketches stay attached.</p>")
                              .arg(q(it->second->faceName(face)).toHtmlEscaped(), q(fp.type))
                              .arg(q(jig::formatArea(fp.size, doc_->units())))
                              .arg(body));
    });
    // A design just opened is framed once its bodies are meshed, unless the camera was moved
    // meanwhile (it used to jump back to the home view whenever the first meshes arrived).
    connect(view_, &Viewport::meshesUpdated, this, [this] {
        if (homeWhenMeshed_ && doc_ && !doc_->state().bodies.empty()) {
            homeWhenMeshed_ = false;
            view_->homeView();
        }
    });
    connect(view_, &Viewport::cameraMoved, this, [this] { homeWhenMeshed_ = false; });

    QSettings st;
    // Dark is the default. The key is new: the old "theme" key was written with Light on
    // every first start, so it does not tell us what anyone actually chose.
    theme_ = Theme(st.value("appearance/theme", int(Theme::Dark)).toInt());
    view_->setNavPreset(NavPreset(st.value("navigation", int(NavPreset::Default)).toInt()));
    view_->setZoomInverted(st.value("navigation/zoom_inverted", false).toBool());
    view_->setOrbitSpeed(st.value("navigation/orbit_speed", 1.0).toFloat());
    setTheme(theme_);
    restoreGeometry(st.value("geometry").toByteArray());

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(qEnvironmentVariableIsSet("JIG_AUTOSAVE_MS")  // (tests: a short interval)
                                    ? std::max(50, qEnvironmentVariableIntValue("JIG_AUTOSAVE_MS"))
                                    : 60 * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer_->start();

    setDocument(blankDocument(), {});
    resize(1360, 860);
    // Esc: cancel the open panel, the plane picker, or the sketch tool in progress
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, &MainWindow::escape);
}

MainWindow::~MainWindow()
{
    // ended without a crash: this window's autosave is not needed (a crash never gets here)
    if (sessionLock_) {
        QFile::remove(recoveryPath());
    }
}

QAction* MainWindow::registryAction(const char* id)
{
    auto it = actions_.find(id);
    return it == actions_.end() ? nullptr : it->second;
}

void MainWindow::buildActions()
{
    // labels and tooltips come from the core in English; translated here (app/registry_strings.h
    // lists them for the translation tools). The standard CAD name stays as it is.
    auto cmd = [](const std::string& s) { return QCoreApplication::translate("Command", s.c_str()); };
    for (const auto& c : jig::CommandRegistry::instance().all()) {
        auto* a = new QAction(cmd(c.label), this);
        a->setObjectName(q(c.id));
        QString tip = "<b>" + cmd(c.label) + "</b>";
        if (c.standardName != c.label) {
            tip += " (" + q(c.standardName) + ")";
        }
        tip += "<br>" + cmd(c.tooltip).toHtmlEscaped();
        if (!c.shortcut.empty()) {
            QKeySequence ks(q(c.shortcut));
            tip += "<br>" + tr("Key: %1").arg(ks.toString(QKeySequence::NativeText));
            a->setShortcut(ks);
        }
        if (!c.available) {
            tip += "<br><i>" + tr("Coming in %1.").arg(q(c.comingIn)) + "</i>";
            if (c.scripted) {
                tip += " " + tr("Already works in scripts.");
            }
            a->setEnabled(false);
        }
        a->setToolTip(tip);
        a->setStatusTip(q(c.tooltip));
        actions_[q(c.id)] = a;
        if (c.available) {
            const QString id = q(c.id);
            connect(a, &QAction::triggered, this, [this, id] { runCommand(id); });
        }
        addAction(a);
    }
    // Native platform keys where the platform has a convention (Cmd on macOS).
    registryAction("file.new")->setShortcut(QKeySequence::New);
    registryAction("file.open")->setShortcut(QKeySequence::Open);
    registryAction("file.save")->setShortcut(QKeySequence::Save);
    registryAction("file.save_as")->setShortcut(QKeySequence::SaveAs);
    registryAction("edit.undo")->setShortcut(QKeySequence::Undo);
    registryAction("edit.redo")->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence::Redo});
    registryAction("edit.palette")->setShortcuts({QKeySequence("Ctrl+K"), QKeySequence("S")});
}

void MainWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(tr("&File"));
    for (const char* id : {"file.new", "file.open", "file.save", "file.save_as", "file.save_version", "file.versions"}) {
        file->addAction(registryAction(id));
    }
    file->addSeparator();
    auto* samples = file->addMenu(tr("Open a sample"));
    samples->addAction(tr("Phone stand"), this, [this] { openSample("phone_stand"); });
    samples->addAction(tr("Knob (revolve, pattern, text)"), this, [this] { openSample("knob"); });
    samples->addAction(tr("Pen cup (parameters, shell)"), this, [this] { openSample("pen_cup"); });
    samples->addAction(tr("Hinged box (parts, hinge joint)"), this, [this] { openSample("hinged_box"); });
    file->addSeparator();
    for (const char* id : {"file.export_stl", "file.export_step", "file.export_3mf"}) {
        file->addAction(registryAction(id));
    }
    file->addSeparator();
    auto* quit = file->addAction(tr("Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(registryAction("edit.undo"));
    edit->addAction(registryAction("edit.redo"));
    edit->addSeparator();
    edit->addAction(registryAction("edit.palette"));
    edit->addSeparator();
    auto* settingsAction = edit->addAction(tr("Settings…"), this, [this] { openSettings(); });
    settingsAction->setShortcut(QKeySequence::Preferences);
    settingsAction->setMenuRole(QAction::PreferencesRole);  // macOS: in the application menu

    auto* view = menuBar()->addMenu(tr("&View"));
    view->addAction(registryAction("view.fit"));
    view->addAction(registryAction("view.home"));
    auto* proj = registryAction("view.projection");
    proj->setCheckable(true);
    proj->setChecked(true);
    view->addAction(proj);
    view->addAction(registryAction("view.save_named"));
    view->addAction(tr("Top"), view_, &Viewport::viewTop);
    view->addAction(tr("Front"), view_, &Viewport::viewFront);
    view->addAction(tr("Right"), view_, &Viewport::viewRight);
    view->addSeparator();
    auto* nav = view->addMenu(tr("Mouse and trackpad"));
    const std::pair<NavPreset, QString> presets[] = {
        {NavPreset::Default, tr("Default (middle-drag pans, Shift+middle orbits)")},
        {NavPreset::Blender, tr("Blender-like (middle-drag orbits)")},
        {NavPreset::SolidWorks, tr("SolidWorks-like (middle-drag orbits, Ctrl pans)")},
        {NavPreset::Trackpad, tr("Trackpad (two fingers orbit, Shift pans, pinch zooms)")}};
    navGroup_ = new QActionGroup(this);
    for (auto& [p, label] : presets) {
        auto* a = nav->addAction(label);
        a->setCheckable(true);
        a->setData(int(p));
        a->setChecked(view_->navPreset() == p);
        navGroup_->addAction(a);
        connect(a, &QAction::triggered, this,
                [this, p = p] { setNavigation(p, view_->zoomInverted(), view_->orbitSpeed()); });
    }
    auto* themes = view->addMenu(tr("Theme"));
    auto* themeGroup = new QActionGroup(this);
    for (auto [t, label] : {std::pair {Theme::Light, tr("Light")}, std::pair {Theme::Dark, tr("Dark")},
                            std::pair {Theme::HighContrast, tr("High contrast")}}) {
        auto* a = themes->addAction(label);
        a->setCheckable(true);
        a->setChecked(theme_ == t);
        themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, t = t] { setTheme(t); });
    }

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Welcome…"), this, &MainWindow::showWelcome);
    help->addAction(registryAction("help.tutorial"));
    help->addAction(QWhatsThis::createAction(this));
    help->addSeparator();
    help->addAction(tr("About Jigsmith"), this, [this] {
        QMessageBox::about(
            this, tr("About Jigsmith"),
            tr("<h3>Jigsmith %1</h3><p>Free, open-source parametric CAD for hobbyists. No account, no "
               "cloud, no telemetry.</p>"
               "<p>Licensed under the GNU General Public License v3 or later.</p>"
               "<p><b>Jigsmith uses Open CASCADE Technology</b> %2 (<a href='https://dev.opencascade.org'>"
               "dev.opencascade.org</a>), licensed under the LGPL 2.1 with an additional exception.</p>"
               "<p>Sketch solving uses <b>planegcs</b> from FreeCAD (LGPL 2.1 or later). The user interface "
               "uses <b>Qt</b> (LGPL 3). Full notices: THIRD_PARTY_LICENSES.md.</p>")
                .arg(JIG_VERSION_STRING, JIG_OCCT_VERSION_STRING));
    });
    help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::buildToolbar()
{
    // Row 1: the workflow loop, always visible (Easy Contract principle 1).
    auto* flow = addToolBar(tr("Workflow"));
    flow->setObjectName("WorkflowBar");
    flow->setMovable(false);
    workflow_ = new QLabel(this);
    workflow_->setTextFormat(Qt::RichText);
    workflow_->setAccessibleName(tr("Workflow step"));
    hint_ = new QLabel(this);
    hint_->setWordWrap(false);
    flow->addWidget(workflow_);
    flow->addSeparator();
    flow->addWidget(hint_);
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    flow->addWidget(spacer);
    auto* search = new QPushButton(tr("Find a tool…  (%1)")
                                       .arg(QKeySequence("Ctrl+K").toString(QKeySequence::NativeText)),
                                   this);
    search->setToolTip(registryAction("edit.palette")->toolTip());
    connect(search, &QPushButton::clicked, this, &MainWindow::openPalette);
    flow->addWidget(search);
    addToolBarBreak();

    // Row 2: panels with the essential tools, text labels (icons alone confuse beginners).
    auto* tools = addToolBar(tr("Tools"));
    toolsBar_ = tools;
    tools->setObjectName("ToolsBar");
    tools->setMovable(false);
    // icon with its name under it: icons alone confuse beginners (brief section 6)
    tools->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tools->setIconSize(QSize(24, 24));
    for (const char* panel : {"CREATE", "MODIFY", "CONSTRUCT", "INSPECT", "INSERT"}) {
        auto* title = new QLabel(QString("<small><b>%1</b></small>").arg(QCoreApplication::translate("Command", panel)), this);
        title->setContentsMargins(10, 0, 4, 0);
        tools->addWidget(title);
        for (const auto& c : jig::CommandRegistry::instance().all()) {
            if (c.panel == panel && c.essential) {
                tools->addAction(registryAction(c.id.c_str()));
            }
        }
        tools->addSeparator();
    }
    auto* all = new QToolButton(this);
    all->setText(tr("All tools…"));
    all->setToolTip(tr("Search every tool, including ones that are not on the toolbar."));
    connect(all, &QToolButton::clicked, this, &MainWindow::openPalette);
    tools->addWidget(all);

    // The contextual sketch toolbar replaces the tools while a sketch is open.
    sketchBar_ = addToolBar(tr("Sketch"));
    sketchBar_->setObjectName("SketchBar");
    sketchBar_->setMovable(false);
    sketchBar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    sketchBar_->setIconSize(QSize(24, 24));
    centerRect_ = new QAction(tr("Center rectangle"), this);
    centerRect_->setObjectName("sketch.rectangle_center");
    centerRect_->setToolTip(tr("<b>Center rectangle</b><br>Draw a rectangle outward from its centre: good for parts "
                               "centred on the origin."));
    connect(centerRect_, &QAction::triggered, this, [this] {
        if (sketch_) {
            sketch_->setTool(SketchEditor::Tool::CenterRectangle);
        }
    });
    auto title = [&](const QString& t) {
        auto* l = new QLabel(QString("<small><b>%1</b></small>").arg(t), this);
        l->setContentsMargins(10, 0, 4, 0);
        sketchBar_->addWidget(l);
    };
    title(tr("SKETCH"));
    for (const char* id : {"sketch.line", "sketch.rectangle"}) {
        sketchBar_->addAction(registryAction(id));
    }
    sketchBar_->addAction(centerRect_);
    for (const char* id : {"sketch.circle", "sketch.arc", "sketch.arc3", "sketch.slot", "sketch.polygon", "sketch.ellipse",
                           "sketch.spline", "sketch.text",
                           "sketch.point", "sketch.dimension", "sketch.construction", "sketch.trim", "sketch.offset",
                           "sketch.extend", "sketch.break", "sketch.fillet", "sketch.mirror", "sketch.move",
                           "sketch.pattern_rect", "sketch.pattern_circ", "sketch.project"}) {
        sketchBar_->addAction(registryAction(id));
    }
    // other ways to draw the same shape: on a drop-down beside its button
    const std::pair<const char*, std::vector<const char*>> variants[] = {
        {"sketch.rectangle", {"sketch.rect3"}},
        {"sketch.circle", {"sketch.circle2", "sketch.circle3", "sketch.circle_tangent"}},
        {"sketch.arc", {"sketch.arc_tangent"}}};
    for (const auto& [base, more] : variants) {
        if (auto* b = qobject_cast<QToolButton*>(sketchBar_->widgetForAction(registryAction(base)))) {
            auto* m = new QMenu(b);
            for (const char* id : more) {
                m->addAction(registryAction(id));
            }
            b->setMenu(m);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        }
    }
    sketchBar_->addSeparator();
    title(tr("CONSTRAIN"));
    for (const char* id : {"constraint.coincident", "constraint.collinear", "constraint.concentric", "constraint.horizontal",
                           "constraint.parallel",
                           "constraint.perpendicular", "constraint.tangent", "constraint.equal", "constraint.midpoint",
                           "constraint.symmetric", "constraint.fix"}) {
        sketchBar_->addAction(registryAction(id));
    }
    sketchBar_->addSeparator();
    sketchBar_->addAction(registryAction("modify.parameters"));  // change sizes without leaving the sketch
    sketchBar_->addAction(registryAction("sketch.finish"));
    sketchBar_->hide();
}

void MainWindow::buildDocks()
{
    auto* bdock = new QDockWidget(tr("Browser"), this);
    bdock->setObjectName("BrowserDock");
    browser_ = new QTreeWidget(bdock);
    browser_->setHeaderHidden(true);
    browser_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(browser_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int) {
        if (const QString sk = it->data(0, Qt::UserRole + 3).toString(); !sk.isEmpty()) {
            const bool on = it->checkState(0) == Qt::Checked;
            QTimer::singleShot(0, this, [this, id = sk.toStdString(), on] { setSketchVisible(id, on); });
            return;
        }
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) {
            const std::string body = id.toStdString();
            const bool on = it->checkState(0) == Qt::Checked;
            QTimer::singleShot(0, this, [this, body, on] { setBodyVisible(body, on); });  // not inside the signal
        }
    });
    // choosing a sketch or body shows the step that made it in the timeline
    connect(browser_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it, QTreeWidgetItem*) {
        if (!it || !doc_) {
            return;
        }
        const int row = doc_->featureIndex(it->data(0, Qt::UserRole + 1).toString().toStdString());
        if (row >= 0 && row < timeline_->count()) {
            timeline_->setCurrentRow(row);
            timeline_->scrollToItem(timeline_->item(row));
        }
    });
    connect(browser_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) {
        const QString v = it ? it->data(0, Qt::UserRole + 2).toString() : QString();
        if (!v.isEmpty()) {
            restoreNamedView(v);
        }
    });
    connect(browser_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* it = browser_->itemAt(pos);
        if (const QString v = it ? it->data(0, Qt::UserRole + 2).toString() : QString(); !v.isEmpty()) {
            QMenu m(this);
            m.addAction(tr("Go to this view"), this, [this, v] { restoreNamedView(v); });
            m.addAction(tr("Replace with the current view"), this, [this, v] { saveNamedView(v); });
            m.addAction(tr("Delete this view"), this, [this, v] {
                doc_->setNamedView(v.toStdString(), nullptr);
                refreshAll();
            });
            m.exec(browser_->viewport()->mapToGlobal(pos));
            return;
        }
        const QString id = it ? it->data(0, Qt::UserRole).toString() : QString();
        if (id.isEmpty()) {
            return;
        }
        const std::string body = id.toStdString();
        QMenu m(this);
        m.addAction(bodyVisible(body) ? tr("Hide") : tr("Show"), this,
                    [this, body] { setBodyVisible(body, !bodyVisible(body)); });
        m.addAction(tr("Isolate (show only this)"), this, [this, body] { isolateBody(body); });
        m.addAction(tr("Show all bodies"), this, [this] { showAllBodies(); });
        if (doc_->feature(body)) {
            m.addAction(tr("Rename…"), this, [this, body] { renameFeature(body); });
        }
        m.addSeparator();
        m.addAction(tr("Delete body"), this, [this, body] { removeBody(body); });
        m.exec(browser_->viewport()->mapToGlobal(pos));
    });
    browser_->setAccessibleName(tr("Design browser"));
    browser_->setWhatsThis(tr("Everything in your design: settings, origin planes, sketches, bodies and "
                              "named parameters."));
    bdock->setWidget(browser_);
    addDockWidget(Qt::LeftDockWidgetArea, bdock);

    auto* ddock = new QDockWidget(tr("Properties"), this);
    propsDock_ = ddock;
    ddock->setObjectName("PropertiesDock");
    sideStack_ = new QStackedWidget(ddock);
    details_ = new QTextBrowser(ddock);
    details_->setOpenExternalLinks(false);
    details_->setAccessibleName(tr("Properties and help"));
    details_->setWhatsThis(tr("Details about what you selected, and explanations and fixes for any step "
                              "marked with a problem."));
    sideStack_->addWidget(details_);
    // tool panels scroll when they are taller than the window, rather than squeezing their
    // explanations to one line
    panelScroll_ = new QScrollArea(sideStack_);
    panelScroll_->setWidgetResizable(true);
    panelScroll_->setFrameShape(QFrame::NoFrame);
    panelScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panelHost_ = new QWidget(panelScroll_);
    new QVBoxLayout(panelHost_);
    panelHost_->layout()->setContentsMargins(0, 0, 0, 0);
    panelScroll_->setWidget(panelHost_);
    sideStack_->addWidget(panelScroll_);
    // sketch palette: state in words and shapes, plus display options
    sketchPanel_ = new QWidget(sideStack_);
    {
        auto* lay = new QVBoxLayout(sketchPanel_);
        sketchInfo_ = new QLabel(sketchPanel_);
        sketchInfo_->setWordWrap(true);
        sketchInfo_->setTextFormat(Qt::RichText);
        lay->addWidget(sketchInfo_);
        auto opt = [&](const QString& text, bool SketchEditor::Options::*field) {
            auto* cb = new QCheckBox(text, sketchPanel_);
            cb->setChecked(true);
            connect(cb, &QCheckBox::toggled, this, [this, field](bool on) {
                if (sketch_) {
                    auto o = sketch_->options;
                    o.*field = on;
                    sketch_->setOptions(o);
                    view_->update();
                }
            });
            lay->addWidget(cb);
        };
        opt(tr("Snap to grid"), &SketchEditor::Options::snapGrid);
        opt(tr("Show constraints and dimensions"), &SketchEditor::Options::showConstraints);
        opt(tr("Shade closed areas (profiles)"), &SketchEditor::Options::showProfiles);
        opt(tr("Show gaps (open line ends)"), &SketchEditor::Options::showGaps);
        sliceBox_ = new QCheckBox(tr("Slice: hide the model in front of the sketch"), sketchPanel_);
        sliceBox_->setToolTip(tr("Cuts the view (not the model) at the sketch plane, to draw inside a body."));
        connect(sliceBox_, &QCheckBox::toggled, this, [this](bool on) { setSketchSlice(on); });
        lay->addWidget(sliceBox_);
        auto* look = new QPushButton(tr("Look at the sketch"), sketchPanel_);
        connect(look, &QPushButton::clicked, this, [this] {
            if (sketch_) {
                sketch_->lookAt();
            }
        });
        lay->addWidget(look);
        auto* legend = new QLabel(tr("<p><b>Reading the sketch</b><br>○ hollow point: can still move<br>■ filled "
                                     "square: fixed in place<br>dashed: construction (ignored by Extrude)<br>red ring: "
                                     "an open end that stops the outline closing</p><p>Hold <b>Ctrl</b> while drawing "
                                     "to stop snapping and automatic constraints.</p>"),
                                  sketchPanel_);
        legend->setWordWrap(true);
        lay->addWidget(legend);
        lay->addStretch();
        auto* finish = new QPushButton(tr("Finish sketch"), sketchPanel_);
        connect(finish, &QPushButton::clicked, this, [this] { exitSketch(); });
        lay->addWidget(finish);
    }
    sideStack_->addWidget(sketchPanel_);
    ddock->setWidget(sideStack_);
    addDockWidget(Qt::RightDockWidgetArea, ddock);

    auto* tdock = new QDockWidget(tr("Timeline"), this);
    tdock->setObjectName("TimelineDock");
    auto* tw = new QWidget(tdock);
    auto* tl = new QVBoxLayout(tw);
    tl->setContentsMargins(4, 2, 4, 2);
    timeline_ = new QListWidget(tw);
    timeline_->setFlow(QListView::LeftToRight);
    timeline_->setWrapping(false);
    timeline_->setFixedHeight(64);
    timeline_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timeline_->setAccessibleName(tr("Timeline of steps"));
    timeline_->setWhatsThis(tr("Every step you took, in order. Click a step to see its details and any "
                               "problem. Drag the rollback marker below to see the design at an earlier "
                               "step; new steps are inserted there."));
    connect(timeline_, &QListWidget::currentRowChanged, this, &MainWindow::showFeatureDetails);
    timeline_->setIconSize(QSize(20, 20));
    timeline_->setContextMenuPolicy(Qt::CustomContextMenu);
    // drag a step to a new place in the history (checked: it must not break anything)
    timeline_->setDragDropMode(QAbstractItemView::InternalMove);
    timeline_->setDefaultDropAction(Qt::MoveAction);
    connect(timeline_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int start, int, const QModelIndex&, int row) {
                if (!doc_ || start < 0 || start >= int(doc_->model().features.size())) {
                    return;
                }
                const std::string id = doc_->model().features[size_t(start)].id;
                const int to = row > start ? row - 1 : row;  // Qt counts the destination before removal
                QTimer::singleShot(0, this, [this, id, to] { moveStep(id, to); });  // after the view settles
            });
    connect(timeline_, &QListWidget::customContextMenuRequested, this, &MainWindow::timelineMenu);
    connect(timeline_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { editFeature(timeline_->row(it)); });
    auto* rl = new QHBoxLayout();
    auto* rlabel = new QLabel(tr("Rollback marker:"), tw);
    rollback_ = new QSlider(Qt::Horizontal, tw);
    rollback_->setAccessibleName(tr("Rollback marker"));
    rollback_->setToolTip(tr("Drag left to see the design at an earlier step."));
    rlabel->setBuddy(rollback_);
    rl->addWidget(rlabel);
    rl->addWidget(rollback_, 1);
    tl->addWidget(timeline_);
    tl->addLayout(rl);
    tdock->setWidget(tw);
    addDockWidget(Qt::BottomDockWidgetArea, tdock);
    auto commitRollback = [this] {
        if (updatingRollback_ || !doc_) {
            return;
        }
        const int n = int(doc_->model().features.size());
        const int v = rollback_->value();
        const int want = v >= n ? -1 : v;
        if (want == doc_->model().rollback) {
            return;
        }
        doc_->transact(tr("Move rollback marker").toStdString(), [want](jig::Model& m) -> jig::Result<bool> {
            m.rollback = want;
            return true;
        });
        refreshAll();
    };
    connect(rollback_, &QSlider::sliderReleased, this, commitRollback);
    connect(rollback_, &QSlider::valueChanged, this, [this, commitRollback](int) {
        if (!rollback_->isSliderDown()) {
            commitRollback();
        }
    });
}

// ---------------------------------------------------------------- refresh

void MainWindow::setDocument(std::unique_ptr<jig::Document> doc, const QString& path)
{
    if (panel_) {
        panel_->cancel();
    }
    cancelPicker();
    if (sketch_) {
        exitSketch();
    }
    doc_ = std::move(doc);
    path_ = path;
    savedRevision_ = doc_->revision();
    autosavedRevision_ = savedRevision_;
    refreshAll();
    view_->homeView();
    homeWhenMeshed_ = !doc_->state().bodies.empty();  // frame them when their meshes are ready
}

bool MainWindow::sketchVisible(const std::string& id) const
{
    if (sketch_ && sketch_->featureId() == id) {
        return true;  // being edited: the sketch editor draws it
    }
    const bool used = !doc_->dependents(id).empty();
    return used ? doc_->shownSketches().count(id) > 0 : !doc_->hiddenBodies().count(id);
}

void MainWindow::setSketchVisible(const std::string& id, bool on)
{
    if (sketchVisible(id) == on) {
        return;
    }
    if (!doc_->dependents(id).empty()) {
        auto shown = doc_->shownSketches();
        on ? (void)shown.insert(id) : (void)shown.erase(id);
        doc_->setShownSketches(std::move(shown));
    }
    else {
        auto hidden = doc_->hiddenBodies();  // the same "hidden" list as bodies (ids never clash)
        on ? (void)hidden.erase(id) : (void)hidden.insert(id);
        doc_->setHiddenBodies(std::move(hidden));
    }
    refreshAll();
}

void MainWindow::refreshDatums()
{
    SceneOverlay ov;
    std::vector<std::pair<QString, QVector3D>> labels;
    float s = 30;
    for (auto& [id, b] : doc_->state().bodies) {
        auto mp = jig::massProperties(b->shape());
        s = std::max({s, float(mp.xmax - mp.xmin) * 0.6f, float(mp.ymax - mp.ymin) * 0.6f, float(mp.zmax - mp.zmin) * 0.6f});
    }
    // finished sketches: their curves in the view, hidden by the model where it is in front
    // (construction ones dashed: drawn in short pieces with gaps, not only lighter)
    SceneOverlay sketchLines;
    const double dash = std::max(0.5, double(s) / 40);
    for (auto& [id, sk] : doc_->state().sketches) {
        if ((sketch_ && sketch_->featureId() == id) || !sketchVisible(id)) {
            continue;
        }
        const gp_Ax3& f = sk->frame;
        auto to3d = [&](const QPointF& p) {
            const gp_XYZ w = f.Location().XYZ() + f.XDirection().XYZ() * p.x() + f.YDirection().XYZ() * p.y();
            return QVector3D(float(w.X()), float(w.Y()), float(w.Z()));
        };
        const QColor ink(110, 170, 255, 235);
        for (auto& [eid, e] : sk->solved.entities()) {
            if (e.type == jig::EntityType::Point) {
                continue;
            }
            const auto pts = SketchEditor::polyline(sk->solved, e);
            double along = 0;
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                if (!e.construction) {
                    sketchLines.lines.push_back({to3d(pts[i]), ink});
                    sketchLines.lines.push_back({to3d(pts[i + 1]), ink});
                    continue;
                }
                const QPointF a = pts[i], b = pts[i + 1];
                const double L = QLineF(a, b).length();
                for (double t = 0; t < L; t += dash / 2) {
                    const double t1 = std::min(L, t + dash / 2);
                    if (int(std::floor((along + t) / (dash / 2))) % 2 == 0) {
                        sketchLines.lines.push_back({to3d(a + (b - a) * (t / L)), ink});
                        sketchLines.lines.push_back({to3d(a + (b - a) * (t1 / L)), ink});
                    }
                }
                along += L;
            }
        }
    }
    view_->setSketchLines(std::move(sketchLines));
    for (auto& [id, f] : doc_->state().planes) {
        const auto c = planeCorners(f, s);
        QColor fill = kDatum, edge = kDatum;
        fill.setAlpha(34);
        edge.setAlpha(210);
        for (int k : {0, 1, 2, 0, 2, 3}) {
            ov.tris.push_back({c[size_t(k)], fill});
        }
        for (int k = 0; k < 4; ++k) {
            ov.lines.push_back({c[size_t(k)], edge});
            ov.lines.push_back({c[size_t((k + 1) % 4)], edge});
        }
        const jig::Feature* feat = doc_->feature(id);
        labels.emplace_back(feat ? q(feat->name) : q(id), c[3]);
    }
    // construction axes: dashed amber lines (dashed, so not told apart by colour alone)
    for (auto& [id, a] : doc_->state().axes) {
        QColor edge = kDatum;
        edge.setAlpha(230);
        const int dashes = 24;
        for (int k = 0; k < dashes; k += 2) {
            for (int e = 0; e < 2; ++e) {
                const double t = -s + 2 * s * (k + e) / dashes;
                const gp_XYZ p = a.Location().XYZ() + a.Direction().XYZ() * t;
                ov.lines.push_back({QVector3D(float(p.X()), float(p.Y()), float(p.Z())), edge});
            }
        }
        const gp_XYZ end = a.Location().XYZ() + a.Direction().XYZ() * s;
        const jig::Feature* feat = doc_->feature(id);
        labels.emplace_back(feat ? q(feat->name) : q(id), QVector3D(float(end.X()), float(end.Y()), float(end.Z())));
    }
    // construction points: a small cross of three short lines, with the name beside it
    for (auto& [id, pt] : doc_->state().points) {
        QColor edge = kDatum;
        edge.setAlpha(240);
        const float r = float(s) * 0.03f;
        const QVector3D c(float(pt.X()), float(pt.Y()), float(pt.Z()));
        for (const QVector3D d : {QVector3D(r, 0, 0), QVector3D(0, r, 0), QVector3D(0, 0, r)}) {
            ov.lines.push_back({c - d, edge});
            ov.lines.push_back({c + d, edge});
        }
        const jig::Feature* feat = doc_->feature(id);
        labels.emplace_back(feat ? q(feat->name) : q(id), c);
    }
    view_->setDatums(std::move(ov), std::move(labels));
    // pictures to trace: decoded once per step (and again only when the picture changes)
    std::vector<Viewport::CanvasPicture> pictures;
    for (auto& [id, cv] : doc_->state().canvases) {
        const jig::Feature* feat = doc_->feature(id);
        if (!feat) {
            continue;
        }
        const std::string image = feat->params.value("image", std::string {});
        const size_t key = std::hash<std::string> {}(image);
        auto& cached = canvasImages_[id];
        if (cached.first != key || cached.second.isNull()) {
            cached = {key, QImage::fromData(QByteArray::fromBase64(QByteArray::fromStdString(image)))};
        }
        Viewport::CanvasPicture pic;
        pic.image = cached.second;
        pic.opacity = cv.opacity;
        const gp_XYZ o = cv.frame.Location().XYZ(), x = cv.frame.XDirection().XYZ() * (cv.width / 2),
                     y = cv.frame.YDirection().XYZ() * (cv.height / 2);
        const gp_XYZ corners[4] = {o - x - y, o + x - y, o + x + y, o - x + y};
        for (int k = 0; k < 4; ++k) {
            pic.corners[size_t(k)] = QVector3D(float(corners[k].X()), float(corners[k].Y()), float(corners[k].Z()));
        }
        pictures.push_back(std::move(pic));
    }
    view_->setCanvases(std::move(pictures));
    std::map<std::string, Viewport::BodyLook> looks;
    for (auto& [id, a] : doc_->appearances()) {
        looks[id] = {QColor(q(a.color)), q(a.finish)};
    }
    view_->setAppearances(std::move(looks));
}

void MainWindow::refreshAll()
{
    if (!doc_) {
        return;
    }
    std::vector<Viewport::BodyInput> bodies;
    for (auto& [id, b] : doc_->state().bodies) {
        if (!doc_->hiddenBodies().count(id)) {
            bodies.push_back({id, b});
        }
    }
    view_->setBodies(std::move(bodies));
    view_->setUnits(doc_->units());
    refreshDatums();
    view_->setEmptyHint(doc_->model().features.empty() && !sketch_ && !picker_
                            ? tr("Start here: every part begins as a flat sketch.\n"
                                 "Click Sketch on the toolbar, then click a plane.\n"
                                 "Or open the phone stand sample (File ▸ Open a sample).")
                            : QString());
    refreshBrowser();
    refreshTimeline();
    refreshWorkflow();
    auto* u = registryAction("edit.undo");
    auto* r = registryAction("edit.redo");
    u->setEnabled(doc_->canUndo());
    r->setEnabled(doc_->canRedo());
    u->setText(doc_->canUndo() ? tr("Undo %1").arg(q(doc_->undoLabel())) : tr("Undo"));
    r->setText(doc_->canRedo() ? tr("Redo %1").arg(q(doc_->redoLabel())) : tr("Redo"));
    const bool dirty = doc_->revision() != savedRevision_;
    const QString name = path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).completeBaseName();
    setWindowTitle(QString("%1[*] — Jigsmith").arg(name));  // [*]: the platform's "unsaved" marker
    setWindowModified(dirty);
    statusInfo_->setText(tr("%1 · Z up · rebuilt %2 step(s) in %3 ms")
                             .arg(q(jig::unitSymbol(doc_->units())))
                             .arg(doc_->recomputedLastTime())
                             .arg(doc_->lastRecomputeMs(), 0, 'f', 0));
}

void MainWindow::refreshBrowser()
{
    browser_->clear();
    auto* settings = new QTreeWidgetItem(browser_, {tr("Document settings")});
    static const char* unitNames[] = {"millimetres", "centimetres", "metres", "inches", "feet"};
    new QTreeWidgetItem(settings, {tr("Units: %1 (change in Settings)").arg(tr(unitNames[int(doc_->units())]))});
    new QTreeWidgetItem(settings, {tr("Up axis: Z")});
    auto* origin = new QTreeWidgetItem(browser_, {tr("Origin")});
    for (const char* p : {"XY plane (floor)", "XZ plane (front)", "YZ plane (side)"}) {
        new QTreeWidgetItem(origin, {tr(p)});
    }
    browser_->blockSignals(true);  // ticking the rows below is not a person ticking them
    auto* sketches = new QTreeWidgetItem(browser_, {tr("Sketches")});
    auto* bodies = new QTreeWidgetItem(browser_, {tr("Bodies")});
    const auto& feats = doc_->model().features;
    for (size_t i = 0; i < feats.size(); ++i) {
        if (feats[i].kind != jig::FeatureKind::Sketch) {
            continue;
        }
        const auto& st = doc_->status(int(i));
        QString extra;
        auto it = doc_->state().sketches.find(feats[i].id);
        if (it != doc_->state().sketches.end()) {
            extra = it->second->report.dof == 0 ? tr("fully defined")
                                                : tr("%1 free").arg(it->second->report.dof);
        }
        const bool shown = sketchVisible(feats[i].id);
        // the tick box shows visibility; a status mark only when something needs attention
        const QString glyph = st.level == jig::StatusLevel::Ok ? QString() : statusGlyph(st.level) + " ";
        auto* sk = new QTreeWidgetItem(sketches, {QString("%1%2  (%3)%4").arg(glyph, q(feats[i].name),
                                                                               extra.isEmpty() ? statusWord(st.level) : extra,
                                                                               shown ? QString() : tr("  hidden"))});
        sk->setData(0, Qt::UserRole + 1, q(feats[i].id));  // the step, for "find in timeline"
        sk->setData(0, Qt::UserRole + 3, q(feats[i].id));  // its tick shows or hides it
        sk->setFlags(sk->flags() | Qt::ItemIsUserCheckable);
        sk->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked);
        sk->setToolTip(0, tr("Tick to show the sketch in the view. A sketch a later step uses hides itself."));
    }
    browser_->blockSignals(true);
    for (auto& [id, b] : doc_->state().bodies) {
        auto mp = jig::massProperties(b->shape());
        const jig::Feature* f = doc_->feature(id);
        const bool shown = !doc_->hiddenBodies().count(id);
        // the checkbox shows visibility; "hidden" is also written out (not colour alone)
        auto* it = new QTreeWidgetItem(bodies, {tr("%1  (%2)%3")
                                                    .arg(f ? q(f->name) : q(id),
                                                         q(jig::formatVolume(mp.volume, doc_->units())),
                                                         shown ? QString() : tr("  hidden"))});
        it->setData(0, Qt::UserRole, q(id));
        it->setData(0, Qt::UserRole + 1, q(id));  // the step that made the body
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked);
        it->setToolTip(0, tr("Tick to show, untick to hide. Right-click to isolate or rename."));
    }
    browser_->blockSignals(false);
    if (!doc_->namedViews().empty()) {
        auto* views = new QTreeWidgetItem(browser_, {tr("Named views")});
        for (auto& [name, cam] : doc_->namedViews()) {
            auto* v = new QTreeWidgetItem(views, {q(name)});
            v->setData(0, Qt::UserRole + 2, q(name));
            v->setToolTip(0, tr("Double-click (or Enter) to look at the design from here. Right-click to replace or delete."));
        }
    }
    auto* params = new QTreeWidgetItem(browser_, {tr("Parameters")});
    for (auto& p : doc_->model().parameters) {
        auto v = doc_->evalParam(p.name);
        new QTreeWidgetItem(params, {QString("%1 = %2  →  %3")
                                         .arg(q(p.name), q(p.expr),
                                              v ? q(jig::formatQuantity(v.value())) : tr("problem"))});
    }
    browser_->expandAll();
    settings->setExpanded(false);
    origin->setExpanded(false);
}

void MainWindow::setSketchSlice(bool on)
{
    if (sliceBox_ && sliceBox_->isChecked() != on) {
        sliceBox_->setChecked(on);  // comes back here through toggled
        return;
    }
    if (!on || !sketch_) {
        view_->setSection(std::nullopt);
        return;
    }
    // keep what is behind the plane (the viewer looks along -normal); a hair in front of the
    // plane so faces the sketch lies on stay visible
    const gp_Ax3& f = sketch_->frame();
    const QVector3D n(float(f.Direction().X()), float(f.Direction().Y()), float(f.Direction().Z()));
    const QVector3D o(float(f.Location().X()), float(f.Location().Y()), float(f.Location().Z()));
    view_->setSection(QVector4D(n, QVector3D::dotProduct(n, o) + 1e-3f));
}

void MainWindow::saveNamedView(const QString& name)
{
    doc_->setNamedView(name.toStdString(), view_->camera());  // saved with the design
    refreshAll();
    statusBar()->showMessage(tr("View “%1” saved with the design. Double-click it in the Browser to come back.").arg(name),
                             6000);
}

bool MainWindow::restoreNamedView(const QString& name)
{
    auto it = doc_->namedViews().find(name.toStdString());
    if (it == doc_->namedViews().end()) {
        return false;
    }
    view_->setCamera(it->second);
    registryAction("view.projection")->setChecked(view_->perspective());
    return true;
}

void MainWindow::setBodyVisible(const std::string& body, bool on)
{
    auto hidden = doc_->hiddenBodies();  // saved with the design
    if (on) {
        hidden.erase(body);
    }
    else {
        hidden.insert(body);
    }
    doc_->setHiddenBodies(std::move(hidden));
    refreshAll();
}

void MainWindow::setSelectFilter(const QString& what)
{
    const int i = selectFilter_->findData(what);
    if (i >= 0 && i != selectFilter_->currentIndex()) {
        selectFilter_->setCurrentIndex(i);  // comes back here through the signal
        return;
    }
    if (!panel_ && !sketch_) {
        view_->setPickMode(what == "edges" ? PickMode::Edges : PickMode::Faces);
    }
    details_->setHtml(tr("<p>Nothing selected.</p>"));
}

QString MainWindow::selectFilter() const
{
    return selectFilter_ ? selectFilter_->currentData().toString() : QString("faces");
}

QString MainWindow::detailsText() const
{
    return details_->toPlainText();
}

MarkingMenu* MainWindow::openMarkingMenu(const QPoint& viewPos)
{
    // the commands people reach for most, for where they are: drawing or modelling
    static const char* model[] = {"sketch.create", "solid.extrude", "solid.fillet", "solid.presspull",
                                  "solid.hole",    "inspect.measure", "edit.undo", "edit.palette"};
    static const char* drawing[] = {"sketch.line",  "sketch.rectangle", "sketch.circle",       "sketch.dimension",
                                    "sketch.trim", "sketch.construction", "edit.undo", "sketch.finish"};
    std::vector<QAction*> acts;
    for (const char* id : sketch_ ? drawing : model) {
        acts.push_back(registryAction(id));
    }
    auto* m = new MarkingMenu(acts, this);
    m->popupAt(view_->mapToGlobal(viewPos));
    return m;
}

QMenu* MainWindow::openSelectOther(const QPoint& viewPos)
{
    const auto faces = view_->facesUnder(viewPos);
    auto* m = new QMenu(this);
    m->setAttribute(Qt::WA_DeleteOnClose);
    m->setAccessibleName(tr("Select other"));
    m->addSection(faces.empty() ? tr("Nothing under the cursor") : tr("Faces here, nearest first"));
    int n = 0;
    for (const auto& f : faces) {
        auto it = doc_->state().bodies.find(f.body);
        if (it == doc_->state().bodies.end()) {
            continue;
        }
        const jig::Feature* bf = doc_->feature(f.body);
        const QString label = tr("%1. %2 (%3)")
                                  .arg(++n)
                                  .arg(q(it->second->faceName(f.face)), bf ? q(bf->name) : q(f.body));
        m->addAction(label, this, [this, f] { view_->selectFace(f.body, f.face); });
    }
    m->popup(view_->mapToGlobal(viewPos));
    return m;
}

void MainWindow::isolateBody(const std::string& body)
{
    std::set<std::string> hidden;
    for (auto& [id, b] : doc_->state().bodies) {
        if (id != body) {
            hidden.insert(id);
        }
    }
    doc_->setHiddenBodies(std::move(hidden));
    refreshAll();
    statusBar()->showMessage(tr("Only this body is shown. Right-click a body and choose Show all to bring the rest back."),
                             6000);
}

void MainWindow::showAllBodies()
{
    doc_->setHiddenBodies({});
    refreshAll();
}

void MainWindow::refreshTimeline()
{
    const int keep = timeline_->currentRow();
    timeline_->blockSignals(true);
    timeline_->clear();
    const auto& feats = doc_->model().features;
    for (size_t i = 0; i < feats.size(); ++i) {
        const auto& st = doc_->status(int(i));
        auto* it = new QListWidgetItem(QString("%1 %2\n%3").arg(statusGlyph(st.level), kindWord(feats[i].kind),
                                                                 q(feats[i].name)),
                                       timeline_);
        it->setTextAlignment(Qt::AlignCenter);
        // groups: the first step of a run carries the group's name; folding hides the rest
        const std::string& g = feats[i].group;
        if (!g.empty()) {
            const bool first = i == 0 || feats[i - 1].group != g;
            const bool folded = doc_->foldedGroups().count(g) > 0;
            if (first) {
                int n = 0;
                for (size_t k = i; k < feats.size() && feats[k].group == g; ++k) {
                    ++n;
                }
                it->setText((folded ? QString("▸ %1 (%2 steps)").arg(q(g)).arg(n) : QString("▾ %1").arg(q(g))) + "\n" +
                            it->text().section('\n', 1));
            }
            else if (folded) {
                it->setHidden(true);
            }
        }
        it->setIcon(toolIcon(QString("feature.") + jig::toString(feats[i].kind), palette().color(QPalette::WindowText),
                             accentFor(palette())));
        QString tip = statusWord(st.level);
        if (!feats[i].group.empty()) {
            tip = tr("Group: %1").arg(q(feats[i].group)) + "\n" + tip;
        }
        if (!st.message.empty()) {
            tip += ": " + coreText(st.message);
        }
        it->setToolTip(tip);
        it->setSizeHint(QSize(150, 52));
        if (st.level == jig::StatusLevel::RolledBack || st.level == jig::StatusLevel::Suppressed) {
            it->setForeground(palette().color(QPalette::PlaceholderText));
        }
        if (st.level == jig::StatusLevel::Error) {
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
        }
    }
    timeline_->blockSignals(false);
    if (keep >= 0 && keep < timeline_->count()) {
        timeline_->setCurrentRow(keep);
    }
    updatingRollback_ = true;
    rollback_->setRange(0, int(feats.size()));
    rollback_->setValue(doc_->activeCount());
    rollback_->setEnabled(!feats.empty());
    updatingRollback_ = false;
}

void MainWindow::refreshWorkflow()
{
    // Which step of Sketch → Feature → Refine the design is in, and what to do next.
    int step = 0;
    QString hint = tr("Next: create a sketch on a plane.");
    const auto& feats = doc_->model().features;
    const int active = doc_->activeCount();
    if (active > 0) {
        const auto& last = feats[size_t(active - 1)];
        if (last.kind == jig::FeatureKind::Sketch) {
            step = 1;
            hint = tr("Next: turn the sketch into 3D with Extrude (E).");
        }
        else {
            step = 2;
            hint = tr("Next: refine it with Fillet (F), Hole (H) or Shell, or export it for printing.");
        }
    }
    for (int i = 0; i < int(feats.size()); ++i) {
        if (doc_->status(i).level == jig::StatusLevel::Error) {
            hint = tr("A step needs attention: click the ✕ step in the timeline to see how to fix it.");
        }
    }
    const QString names[3] = {tr("Sketch"), tr("Feature"), tr("Refine")};
    QString html;
    for (int i = 0; i < 3; ++i) {
        if (i) {
            html += " &nbsp;→&nbsp; ";
        }
        html += i == step ? QString("<b>▶ %1. %2</b>").arg(i + 1).arg(names[i])
                          : QString("%1. %2").arg(i + 1).arg(names[i]);
    }
    workflow_->setText(html);
    hint_->setText(hint);
}

void MainWindow::showFeatureDetails(int index)
{
    if (!doc_ || index < 0 || index >= int(doc_->model().features.size())) {
        return;
    }
    const auto& f = doc_->model().features[size_t(index)];
    const auto& st = doc_->status(index);
    QString html = QString("<h3>%1 %2</h3><p><b>%3:</b> %4</p>")
                       .arg(statusGlyph(st.level), q(f.name).toHtmlEscaped(), kindWord(f.kind),
                            statusWord(st.level));
    if (!st.message.empty()) {
        html += "<p>" + coreText(st.message).toHtmlEscaped() + "</p>";
    }
    if (!st.suggestions.empty()) {
        html += tr("<p><b>Ways to fix this:</b></p><ul>");
        for (auto& s : st.suggestions) {
            html += "<li>" + coreText(s).toHtmlEscaped() + "</li>";
        }
        html += "</ul>";
    }
    html += tr("<p><i>Double-click the step to change it. Right-click for rename, suppress, roll back and "
               "delete.</i></p>");
    details_->setHtml(html);
}

// ---------------------------------------------------------------- commands

void MainWindow::openPalette()
{
    CommandPalette p(this);
    p.move(mapToGlobal(QPoint((width() - p.width()) / 2, 90)));
    if (p.exec() == QDialog::Accepted && !p.chosen.isEmpty()) {
        runCommand(p.chosen);
    }
}

void MainWindow::runCommand(const QString& id)
{
    if (id == "file.new") {
        newDesign();
    }
    else if (id == "file.open") {
        if (!maybeKeepChanges()) {
            return;
        }
        const QString p = QFileDialog::getOpenFileName(this, tr("Open design"), {}, tr("Jigsmith designs (*.jig)"));
        if (!p.isEmpty()) {
            openFile(p);
        }
    }
    else if (id == "file.save") {
        save(false);
    }
    else if (id == "help.tutorial") {
        startTutorial();
    }
    else if (id == "file.save_version") {
        saveVersionInteractive();
    }
    else if (id == "file.versions") {
        showVersions();
    }
    else if (id == "file.save_as") {
        save(true);
    }
    else if (id == "file.export_stl") {
        exportAs("stl");
    }
    else if (id == "file.export_step") {
        exportAs("step");
    }
    else if (id == "file.export_3mf") {
        exportAs("3mf");
    }
    else if (id == "edit.undo" || id == "edit.redo") {
        if (panel_) {
            panel_->cancel();
        }
        id == "edit.undo" ? doc_->undo() : doc_->redo();
        if (sketch_ && !sketch_->refreshFromDocument()) {
            exitSketch();
        }
        refreshAll();
    }
    else if (id == "edit.palette") {
        openPalette();
    }
    else if (id == "view.fit") {
        view_->fitAll();
    }
    else if (id == "view.save_named") {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save view"), tr("Name for this view:"), QLineEdit::Normal,
                                                   tr("View %1").arg(doc_->namedViews().size() + 1), &ok)
                                 .trimmed();
        if (ok && !name.isEmpty()) {
            saveNamedView(name);
        }
    }
    else if (id == "view.home") {
        view_->homeView();
    }
    else if (id == "view.projection") {
        view_->setPerspective(!view_->perspective());
        registryAction("view.projection")->setChecked(view_->perspective());
    }
    else if (id == "sketch.create") {
        startCreateSketch();
    }
    else if (id == "sketch.finish") {
        exitSketch();
    }
    else if (id == "solid.extrude") {
        openExtrude();
    }
    else if (id == "solid.fillet") {
        openFillet();
    }
    else if (id == "solid.chamfer") {
        openChamfer();
    }
    else if (id == "solid.shell") {
        openShell();
    }
    else if (id == "solid.loft") {
        openLoft({});
    }
    else if (id == "solid.box" || id == "solid.cylinder" || id == "solid.sphere" || id == "solid.torus") {
        openPrimitive({}, id.mid(6).toStdString());
    }
    else if (id == "solid.thread") {
        openThread({});
    }
    else if (id == "solid.scale") {
        openScale({});
    }
    else if (id == "solid.draft") {
        openDraft({});
    }
    else if (id == "solid.emboss") {
        openEmboss({});
    }
    else if (id == "solid.rib") {
        openRib({});
    }
    else if (id == "solid.coil") {
        openCoil({});
    }
    else if (id == "solid.pipe") {
        openPipe({});
    }
    else if (id == "solid.sweep") {
        openSweep({});
    }
    else if (id == "solid.revolve") {
        openRevolve();
    }
    else if (id == "solid.hole") {
        openHole();
    }
    else if (id == "solid.pattern_circ" || id == "solid.pattern_rect") {
        openPattern({}, id == "solid.pattern_circ");
    }
    else if (id == "solid.mirror") {
        openMirror();
    }
    else if (id == "inspect.measure") {
        openMeasure();
    }
    else if (id == "solid.combine" || id == "solid.split" || id == "solid.move") {
        openBodyTool(id.toStdString());
    }
    else if (id == "construct.offset_plane" || id == "construct.angle_plane" || id == "construct.midplane" ||
             id == "construct.tangent_plane" || id == "construct.points_plane" || id == "construct.path_plane") {
        openPlane({}, id == "construct.angle_plane"     ? "angle"
                      : id == "construct.midplane"      ? "midplane"
                      : id == "construct.tangent_plane" ? "tangent"
                      : id == "construct.points_plane"  ? "points"
                      : id == "construct.path_plane"    ? "path"
                                                        : "offset");
    }
    else if (id == "construct.axis") {
        openAxis();
    }
    else if (id == "construct.point") {
        openPoint();
    }
    else if (id == "solid.presspull") {
        openPressPull();
    }
    else if (id == "inspect.properties") {
        openProperties();
    }
    else if (id == "insert.dxf") {
        importDxf();
    }
    else if (id == "insert.svg") {
        importSvg();
    }
    else if (id == "view.appearance") {
        openAppearance();
    }
    else if (id == "assemble.joint") {
        openJoint({});
    }
    else if (id == "inspect.interference") {
        const QString text = showInterference();
        if (text.contains('\n') || jig::interferences(doc_->state()).size() == 1) {
            QMessageBox::information(this, tr("Interference"), text);
        }
    }
    else if (id == "insert.fastener") {
        openFastener({});
    }
    else if (id == "insert.canvas") {
        const QString path = QFileDialog::getOpenFileName(this, tr("Picture to trace"), {},
                                                          tr("Pictures (*.png *.jpg *.jpeg)"));
        if (!path.isEmpty()) {
            insertCanvasFile(path);
        }
    }
    else if (id == "insert.mesh") {
        const QString path = QFileDialog::getOpenFileName(this, tr("Insert mesh"), {},
                                                          tr("Meshes (*.stl *.obj *.3mf)"));
        if (!path.isEmpty()) {
            insertMeshFile(path);
        }
    }
    else if (id == "insert.step") {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import STEP"), {}, tr("STEP files (*.step *.stp)"));
        if (!path.isEmpty()) {
            importStepFile(path);
        }
    }
    else if (id == "inspect.section") {
        if (doc_->state().bodies.empty()) {
            statusBar()->showMessage(tr("There is nothing to slice yet: make a 3D body first."), 5000);
        }
        else {
            if (sketch_) {
                exitSketch();
            }
            cancelPicker();
            showPanel(new SectionPanel(*doc_, view_, panelHost_));
        }
    }
    else if (id == "modify.parameters") {
        openParameters();
    }
    else if (id.startsWith("sketch.") || id.startsWith("constraint.")) {
        if (!sketch_) {
            statusBar()->showMessage(tr("Open a sketch first: click Sketch, then a plane."), 5000);
            return;
        }
        using T = SketchEditor::Tool;
        using CT = jig::ConstraintType;
        static const std::map<QString, T> tools {
            {"sketch.line", T::Line},       {"sketch.rectangle", T::Rectangle}, {"sketch.circle", T::Circle},
            {"sketch.arc", T::Arc},         {"sketch.arc3", T::Arc3},         {"sketch.slot", T::Slot},           {"sketch.polygon", T::Polygon},
            {"sketch.ellipse", T::Ellipse}, {"sketch.spline", T::Spline},
            {"sketch.arc_tangent", T::TangentArc}, {"sketch.circle2", T::Circle2}, {"sketch.circle3", T::Circle3},
            {"sketch.circle_tangent", T::CircleTangent},
            {"sketch.rect3", T::Rect3},
            {"sketch.text", T::Text},       {"sketch.trim", T::Trim},
            {"sketch.offset", T::Offset},   {"sketch.fillet", T::Round},
            {"sketch.project", T::Project}, {"sketch.extend", T::Extend},     {"sketch.break", T::Break},
            {"sketch.mirror", T::Mirror},   {"sketch.move", T::Move},         {"sketch.pattern_rect", T::PatternRect},
            {"sketch.pattern_circ", T::PatternCirc},
            {"sketch.point", T::Point},     {"sketch.dimension", T::Dimension}};
        static const std::map<QString, CT> cons {
            {"constraint.coincident", CT::Coincident}, {"constraint.horizontal", CT::Horizontal},
            {"constraint.parallel", CT::Parallel},     {"constraint.perpendicular", CT::Perpendicular},
            {"constraint.tangent", CT::Tangent},       {"constraint.equal", CT::Equal},
            {"constraint.midpoint", CT::Midpoint},     {"constraint.symmetric", CT::Symmetric},
            {"constraint.fix", CT::Fix}};
        if (auto t = tools.find(id); t != tools.end()) {
            sketch_->setTool(t->second);
        }
        else if (auto c = cons.find(id); c != cons.end()) {
            sketch_->applyConstraint(c->second);
        }
        else if (id == "sketch.construction") {
            sketch_->toggleConstruction();
        }
        else if (id == "constraint.collinear") {
            sketch_->applyCollinear();
        }
        else if (id == "constraint.concentric") {
            sketch_->applyConcentric();
        }
        view_->setFocus();
    }
    else {
        statusBar()->showMessage(tr("That tool is not available yet."), 4000);
    }
}

// ---------------------------------------------------------------- files

void MainWindow::newDesign()
{
    if (!maybeKeepChanges()) {
        return;
    }
    setDocument(blankDocument(), {});
}

bool MainWindow::openFile(const QString& path)
{
    auto r = jig::loadDocument(path.toStdString());
    if (!r) {
        QMessageBox::warning(this, tr("Could not open"), coreText(r.error().message));
        return false;
    }
    setDocument(std::move(r.value()), path);
    return true;
}

bool MainWindow::openScript(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Could not open"), tr("The script %1 could not be read.").arg(path));
        return false;
    }
    auto doc = blankDocument();
    jig::ScriptRunner run(*doc);
    run.strict = false;
    auto r = run.runText(f.readAll().toStdString());
    if (!r) {
        QMessageBox::warning(this, tr("Script problem"), coreText(r.error().message));
    }
    setDocument(std::move(doc), {});
    savedRevision_ = 0;  // a design made by a script is new: unsaved until saved (and autosaved meanwhile)
    autosavedRevision_ = 0;
    return true;
}

bool MainWindow::openSample(const QString& name)
{
    QFile f(":/samples/" + name + ".jigs");
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    auto doc = std::make_unique<jig::Document>();
    jig::ScriptRunner run(*doc);
    run.strict = false;
    auto r = run.runText(f.readAll().toStdString());
    if (!r) {
        QMessageBox::warning(this, tr("Sample problem"), coreText(r.error().message));
    }
    // A sample opens as a fresh, unsaved design with clean undo history.
    jig::Model m = doc->model();
    doc->resetModel(std::move(m));
    setDocument(std::move(doc), {});
    return true;
}

bool MainWindow::saveNamedVersion(const QString& label)
{
    if (path_.isEmpty() && !save(true)) {
        return false;  // a version lives in the design's file, so it needs one
    }
    auto r = jig::saveVersion(*doc_, path_.toStdString(), label.toStdString());
    if (!r) {
        QMessageBox::warning(this, tr("Could not save a version"), coreText(r.error().message));
        return false;
    }
    savedRevision_ = doc_->revision();
    refreshAll();
    statusBar()->showMessage(tr("Saved version %1: %2").arg(r.value()).arg(label), 5000);
    return true;
}

void MainWindow::saveVersionInteractive()
{
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Save version"),
                                                tr("A short note, e.g. “before rounding the edges”:"),
                                                QLineEdit::Normal, {}, &ok)
                              .trimmed();
    if (ok) {
        saveNamedVersion(label.isEmpty() ? tr("Version") : label);
    }
}

bool MainWindow::restoreVersion(int number)
{
    if (path_.isEmpty()) {
        return false;
    }
    auto v = jig::loadVersion(path_.toStdString(), number);
    if (!v) {
        QMessageBox::warning(this, tr("Could not restore"), coreText(v.error().message));
        return false;
    }
    if (sketch_) {
        exitSketch();
    }
    const jig::Model old = v.value()->model();
    // one undoable change: the whole design becomes the version
    doc_->transact(tr("Restore version %1").arg(number).toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        m = old;
        return true;
    });
    refreshAll();
    statusBar()->showMessage(tr("Restored version %1. Undo brings back what you had.").arg(number), 6000);
    return true;
}

void MainWindow::showVersions()
{
    if (path_.isEmpty()) {
        QMessageBox::information(this, tr("Versions"), tr("This design has not been saved yet, so it has no versions. "
                                                          "Use File ▸ Save version… to keep a snapshot."));
        return;
    }
    auto list = jig::listVersions(path_.toStdString());
    if (!list) {
        QMessageBox::warning(this, tr("Versions"), coreText(list.error().message));
        return;
    }
    QDialog d(this);
    d.setWindowTitle(tr("Versions of %1").arg(QFileInfo(path_).fileName()));
    d.resize(520, 320);
    auto* lay = new QVBoxLayout(&d);
    auto* items = new QListWidget(&d);
    for (auto it = list->rbegin(); it != list->rend(); ++it) {  // newest first
        QString when = q(it->saved);
        const QDateTime t = QDateTime::fromString(when, Qt::ISODate);
        if (t.isValid()) {
            when = QLocale().toString(t.toLocalTime(), QLocale::ShortFormat);
        }
        auto* row = new QListWidgetItem(tr("%1 · %2 · %3 · %n step(s)", "", it->steps)
                                            .arg(it->number)
                                            .arg(q(it->label), when),
                                        items);
        row->setData(Qt::UserRole, it->number);
    }
    if (list->empty()) {
        new QListWidgetItem(tr("No versions yet. Use File ▸ Save version… to keep one."), items);
    }
    auto* note = new QLabel(tr("Restoring replaces the design with the version. It is one change: Undo brings back "
                               "what you had."),
                            &d);
    note->setWordWrap(true);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    auto* restore = box->addButton(tr("Restore into this design"), QDialogButtonBox::ActionRole);
    restore->setEnabled(false);
    connect(items, &QListWidget::currentRowChanged, &d,
            [&] { restore->setEnabled(items->currentItem() && items->currentItem()->data(Qt::UserRole).isValid()); });
    connect(restore, &QPushButton::clicked, &d, [&] {
        restoreVersion(items->currentItem()->data(Qt::UserRole).toInt());
        d.accept();
    });
    connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    lay->addWidget(items);
    lay->addWidget(note);
    lay->addWidget(box);
    d.exec();
}

bool MainWindow::save(bool askPath)
{
    QString p = path_;
    if (askPath || p.isEmpty()) {
        p = QFileDialog::getSaveFileName(this, tr("Save design"), p.isEmpty() ? tr("Untitled.jig") : p,
                                         tr("Jigsmith designs (*.jig)"));
        if (p.isEmpty()) {
            return false;
        }
        if (!p.endsWith(".jig", Qt::CaseInsensitive)) {
            p += ".jig";
        }
    }
    return saveTo(p);
}

bool MainWindow::saveTo(const QString& p)
{
    auto r = jig::saveDocument(*doc_, p.toStdString());
    if (!r) {
        QMessageBox::warning(this, tr("Could not save"), coreText(r.error().message));
        return false;
    }
    path_ = p;
    savedRevision_ = doc_->revision();
    refreshAll();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(p)), 4000);
    return true;
}

void MainWindow::exportAs(const QString& format)
{
    if (doc_->state().bodies.empty()) {
        QMessageBox::information(this, tr("Nothing to export"), tr("There is no 3D body yet."));
        return;
    }
    const QString filter = format == "stl"   ? tr("STL mesh for slicers (*.stl)")
                         : format == "3mf" ? tr("3MF print file (*.3mf)")
                                           : tr("STEP solid (*.step *.stp)");
    QString p = QFileDialog::getSaveFileName(this, tr("Export"), {}, filter);
    if (p.isEmpty()) {
        return;
    }
    if (QFileInfo(p).suffix().isEmpty()) {  // (a dot in a folder name is not an extension)
        p += "." + format;
    }
    jig::Result<bool> r = true;
    if (format == "3mf") {
        std::vector<std::pair<std::string, TopoDS_Shape>> bodies;
        std::vector<std::string> colors;
        for (auto& [id, b] : doc_->state().bodies) {
            const jig::Feature* f = doc_->feature(id);
            bodies.emplace_back(f ? f->name : id, b->shape());
            auto ap = doc_->appearances().find(id);
            colors.push_back(ap == doc_->appearances().end() ? std::string {} : ap->second.color);
        }
        r = jig::export3mf(bodies, p.toStdString(), 0.02, colors);
    }
    else {
        auto shape = doc_->combinedShape();
        r = format == "stl" ? jig::exportStl(shape, p.toStdString()) : jig::exportStep(shape, p.toStdString());
    }
    if (!r) {
        QMessageBox::warning(this, tr("Could not export"), coreText(r.error().message));
        return;
    }
    exported_ = true;
    statusBar()->showMessage(tr("Exported %1 (millimetres)").arg(QDir::toNativeSeparators(p)), 5000);
}

bool MainWindow::maybeKeepChanges()
{
    if (!doc_ || doc_->revision() == savedRevision_) {
        return true;
    }
    auto b = QMessageBox::question(this, tr("Keep your changes?"),
                                   tr("This design has changes that are not saved yet."),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (b == QMessageBox::Save) {
        return save(false);
    }
    return b == QMessageBox::Discard;
}

// ---------------------------------------------------------------- autosave & recovery

QString MainWindow::recoveryDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
    QDir().mkpath(dir);
    return dir;
}

QString MainWindow::recoveryPath() const
{
    return recoveryDir() + "/autosave-" + sessionId_ + ".jig";
}

void MainWindow::holdSessionLock()
{
    if (sessionLock_) {
        return;
    }
    sessionLock_ = std::make_unique<QLockFile>(recoveryDir() + "/autosave-" + sessionId_ + ".lock");
    sessionLock_->setStaleLockTime(0);  // only a lock whose program has died is stale
    sessionLock_->tryLock(0);
}

void MainWindow::autosave()
{
    if (!doc_ || doc_->recomputing() || doc_->revision() == autosavedRevision_ ||
        doc_->revision() == savedRevision_) {
        return;
    }
    holdSessionLock();
    if (jig::saveDocument(*doc_, recoveryPath().toStdString())) {
        autosavedRevision_ = doc_->revision();
    }
}

QStringList MainWindow::leftoverSessions() const
{
    QStringList out;
    const QDir dir(recoveryDir());
    for (const QFileInfo& fi : dir.entryInfoList({"autosave-*.jig"}, QDir::Files, QDir::Time)) {
        if (fi.absoluteFilePath() == QFileInfo(recoveryPath()).absoluteFilePath()) {
            continue;  // this window's own
        }
        // still locked by a running Jigsmith (another window): not left over
        QLockFile probe(fi.absolutePath() + "/" + fi.completeBaseName() + ".lock");
        probe.setStaleLockTime(0);
        if (probe.tryLock(0)) {
            probe.unlock();
            out << fi.absoluteFilePath();
        }
    }
    // the single autosave of versions before 2026-11-01
    if (QFile::exists(dir.filePath("session.lock")) && QFile::exists(dir.filePath("autosave.jig"))) {
        out << dir.filePath("autosave.jig");
    }
    return out;
}

void MainWindow::checkRecovery()
{
    holdSessionLock();
    const QStringList left = leftoverSessions();
    if (left.isEmpty()) {
        return;
    }
    const auto when = QFileInfo(left.front()).lastModified();
    auto b = QMessageBox::question(this, tr("Recover your work?"),
                                   tr("Jigsmith did not close normally last time. A copy of your design "
                                      "from %1 was kept. Restore it?")
                                       .arg(QLocale().toString(when, QLocale::ShortFormat)));
    if (b == QMessageBox::Yes) {
        openFile(left.front());
        path_.clear();             // recovered work is unsaved until the user saves it
        savedRevision_ = 0;
        refreshAll();
    }
    // asked once: the copies of sessions that crashed are not offered again
    for (const QString& f : left) {
        QFile::remove(f);
        QFile::remove(QFileInfo(f).absolutePath() + "/" + QFileInfo(f).completeBaseName() + ".lock");
    }
    QFile::remove(QDir(recoveryDir()).filePath("session.lock"));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeKeepChanges()) {
        e->ignore();
        return;
    }
    QSettings().setValue("geometry", saveGeometry());
    QFile::remove(recoveryPath());  // closed cleanly: this window's autosave is not needed
    sessionLock_.reset();           // (unlocks and removes the lock file)
    e->accept();
}

// ---------------------------------------------------------------- welcome & theme

void MainWindow::showWelcome()
{
    QDialog d(this);
    d.setWindowTitle(tr("Welcome to Jigsmith"));
    auto* lay = new QVBoxLayout(&d);
    auto* title = new QLabel(tr("<h2>Welcome to Jigsmith</h2><p>Design real parts for 3D printing, laser "
                                "cutting and the workshop. Everything stays on your computer.</p>"),
                             &d);
    title->setWordWrap(true);
    lay->addWidget(title);
    auto* tut = new QPushButton(tr("Your first part (10 min)\nA guided tour: sketch, 3D, round, print"), &d);
    connect(tut, &QPushButton::clicked, &d, [&] {
        d.accept();
        startTutorial();
    });
    auto* sample = new QPushButton(tr("Open a sample\nA finished phone stand to look around"), &d);
    auto* blank = new QPushButton(tr("Start blank\nAn empty design with the origin ready"), &d);
    for (auto* b : {tut, sample, blank}) {
        b->setMinimumHeight(56);
        lay->addWidget(b);
    }
    connect(sample, &QPushButton::clicked, &d, [&] {
        d.accept();
        openSample("phone_stand");
    });
    connect(blank, &QPushButton::clicked, &d, &QDialog::accept);
    sample->setDefault(true);
    d.exec();
    QSettings().setValue("welcomeShown", true);
}

QAction* MainWindow::commandAction(const QString& id)
{
    return actions_.count(id) ? actions_.at(id) : nullptr;
}

void MainWindow::startTutorial()
{
    if (tutorial_) {
        tutorial_->deleteLater();
    }
    if (!doc_->model().features.empty()) {
        if (!maybeKeepChanges()) {
            return;
        }
        setDocument(blankDocument(), {});
    }
    exported_ = false;
    tutorial_ = new TutorialCard(this, view_);
    tutorial_->move(12, 12);
    tutorial_->show();
    tutorial_->raise();
    connect(tutorial_, &TutorialCard::closed, this, [this] { tutorial_->deleteLater(); });
}

std::unique_ptr<jig::Document> MainWindow::blankDocument() const
{
    auto doc = std::make_unique<jig::Document>();
    const auto u = jig::lengthUnitFromString(QSettings().value("units/new", "mm").toString().toStdString());
    if (u && *u != jig::LengthUnit::Millimetre) {
        jig::Model m = doc->model();
        m.units = *u;
        doc->resetModel(std::move(m));
    }
    return doc;
}

void MainWindow::setDesignUnits(jig::LengthUnit u)
{
    if (!doc_ || doc_->units() == u) {
        return;
    }
    doc_->transact(tr("Change units to %1").arg(q(jig::unitSymbol(u))).toStdString(), [u](jig::Model& m) -> jig::Result<bool> {
        m.units = u;
        return true;
    });
    if (sketch_) {
        sketch_->refreshFromDocument();
    }
    refreshAll();
}

void MainWindow::openSettings()
{
    QDialog d(this);
    d.setWindowTitle(tr("Settings"));
    auto* lay = new QVBoxLayout(&d);
    auto* form = new QFormLayout();
    auto* theme = new QComboBox(&d);
    theme->addItem(tr("Dark"), int(Theme::Dark));
    theme->addItem(tr("Light"), int(Theme::Light));
    theme->addItem(tr("High contrast"), int(Theme::HighContrast));
    theme->setCurrentIndex(theme->findData(int(theme_)));
    const std::pair<QString, QString> units[] = {{tr("Millimetres (mm)"), "mm"}, {tr("Centimetres (cm)"), "cm"},
                                                 {tr("Inches (in)"), "in"},      {tr("Feet (ft)"), "ft"},
                                                 {tr("Metres (m)"), "m"}};
    auto* design = new QComboBox(&d);
    auto* fresh = new QComboBox(&d);
    for (auto& [label, sym] : units) {
        design->addItem(label, sym);
        fresh->addItem(label, sym);
    }
    design->setCurrentIndex(design->findData(q(jig::unitSymbol(doc_->units()))));
    fresh->setCurrentIndex(std::max(0, fresh->findData(QSettings().value("units/new", "mm").toString())));
    auto* navCombo = new QComboBox(&d);
    navCombo->addItem(tr("Default"), int(NavPreset::Default));
    navCombo->addItem(tr("Blender-like"), int(NavPreset::Blender));
    navCombo->addItem(tr("SolidWorks-like"), int(NavPreset::SolidWorks));
    navCombo->addItem(tr("Trackpad"), int(NavPreset::Trackpad));
    navCombo->setCurrentIndex(navCombo->findData(int(view_->navPreset())));
    auto* navHelp = new QLabel(&d);
    navHelp->setWordWrap(true);
    auto describe = [navCombo, navHelp] {
        static const QString always =
            tr(" Everywhere: right-drag orbits, Shift+right-drag pans, right-click opens the marking menu, the wheel "
               "zooms where the cursor is.");
        switch (NavPreset(navCombo->currentData().toInt())) {
            case NavPreset::Default: navHelp->setText(tr("Middle-drag pans; Shift+middle-drag orbits.") + always); break;
            case NavPreset::Blender: navHelp->setText(tr("Middle-drag orbits; Shift+middle-drag pans.") + always); break;
            case NavPreset::SolidWorks: navHelp->setText(tr("Middle-drag orbits; Ctrl+middle-drag pans.") + always); break;
            case NavPreset::Trackpad:
                navHelp->setText(tr("Two-finger scroll orbits, Shift+scroll pans, pinch zooms; Alt+drag also orbits.") +
                                 always);
                break;
        }
    };
    describe();
    connect(navCombo, &QComboBox::currentIndexChanged, &d, describe);
    auto* invert = new QCheckBox(tr("Zoom the other way round (wheel and pinch)"), &d);
    invert->setChecked(view_->zoomInverted());
    auto* speed = new QComboBox(&d);
    for (auto [label, f] : {std::pair {tr("Slow"), 0.5f}, std::pair {tr("Normal"), 1.0f}, std::pair {tr("Fast"), 2.0f}}) {
        speed->addItem(label, f);
    }
    speed->setCurrentIndex(std::max(0, speed->findData(view_->orbitSpeed())));
    form->addRow(tr("Theme"), theme);
    form->addRow(tr("Units of this design"), design);
    form->addRow(tr("Units for new designs"), fresh);
    auto* language = new QComboBox(&d);
    language->addItem(tr("The computer's language"), QString());
    language->addItem("English", "en");
    for (const QString& code : availableLanguages()) {
        language->addItem(languageName(code), code);
    }
    language->setCurrentIndex(std::max(0, language->findData(QSettings().value("language").toString())));
    form->addRow(tr("Language (after a restart)"), language);
    form->addRow(tr("Mouse and trackpad"), navCombo);
    form->addRow(QString(), navHelp);
    form->addRow(QString(), invert);
    form->addRow(tr("Orbit speed"), speed);
    auto* note = new QLabel(tr("Changing units never resizes anything: it changes how sizes are shown, and what a "
                               "plain number typed into a size field means. You can always type a unit, e.g. "
                               "<i>2 in</i> or <i>50 mm</i>, in any design."),
                            &d);
    note->setWordWrap(true);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    lay->addLayout(form);
    lay->addWidget(note);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(theme, &QComboBox::currentIndexChanged, this, [this, theme] { setTheme(Theme(theme->currentData().toInt())); });
    const Theme before = theme_;
    if (d.exec() != QDialog::Accepted) {
        setTheme(before);  // the theme previews live; Cancel puts it back
        return;
    }
    QSettings().setValue("units/new", fresh->currentData().toString());
    QSettings().setValue("language", language->currentData().toString());
    setNavigation(NavPreset(navCombo->currentData().toInt()), invert->isChecked(), speed->currentData().toFloat());
    if (auto u = jig::lengthUnitFromString(design->currentData().toString().toStdString())) {
        setDesignUnits(*u);
    }
}

void MainWindow::setNavigation(NavPreset preset, bool zoomInverted, float orbitSpeed)
{
    view_->setNavPreset(preset);
    view_->setZoomInverted(zoomInverted);
    view_->setOrbitSpeed(orbitSpeed);
    QSettings st;
    st.setValue("navigation", int(preset));
    st.setValue("navigation/zoom_inverted", zoomInverted);
    st.setValue("navigation/orbit_speed", orbitSpeed);
    if (navGroup_) {
        for (QAction* a : navGroup_->actions()) {
            a->setChecked(a->data().toInt() == int(preset));
        }
    }
}

void MainWindow::setTheme(Theme t)
{
    theme_ = t;
    QSettings().setValue("appearance/theme", int(t));
    qApp->setStyle(QStyleFactory::create("Fusion"));  // Qt's built-in cross-platform widget style
    QPalette p;
    ViewColors vc;
    if (t == Theme::Light) {
        p = qApp->style()->standardPalette();
        vc = {QColor(244, 245, 247), QColor(225, 228, 233), QColor(176, 186, 199), QColor(40, 44, 52),
              QColor(255, 190, 80), QColor(66, 140, 255), QColor(214, 218, 224), QColor(188, 194, 204),
              QColor(50, 54, 62)};
    }
    else {
        const bool hc = t == Theme::HighContrast;
        const QColor win = hc ? QColor(0, 0, 0) : QColor(37, 40, 46);
        const QColor base = hc ? QColor(0, 0, 0) : QColor(28, 30, 35);
        const QColor text = hc ? QColor(255, 255, 255) : QColor(225, 228, 233);
        p.setColor(QPalette::Window, win);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, win.lighter(115));
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, win.lighter(120));
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::ToolTipBase, base);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Highlight, hc ? QColor(255, 255, 0) : QColor(66, 140, 255));
        p.setColor(QPalette::HighlightedText, QColor(0, 0, 0));
        p.setColor(QPalette::PlaceholderText, hc ? QColor(200, 200, 200) : QColor(135, 140, 150));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, hc ? QColor(170, 170, 170) : QColor(120, 125, 135));
        p.setColor(QPalette::Disabled, QPalette::WindowText, hc ? QColor(170, 170, 170) : QColor(120, 125, 135));
        vc = hc ? ViewColors {QColor(0, 0, 0), QColor(0, 0, 0), QColor(200, 200, 200), QColor(255, 255, 255),
                              QColor(255, 255, 0), QColor(0, 200, 255), QColor(70, 70, 70), QColor(140, 140, 140),
                              QColor(255, 255, 255)}
                : ViewColors {QColor(30, 33, 38), QColor(20, 22, 26), QColor(150, 160, 175), QColor(16, 18, 22),
                              QColor(255, 190, 80), QColor(80, 150, 255), QColor(52, 56, 64), QColor(78, 84, 95),
                              QColor(220, 224, 230)};
    }
    qApp->setPalette(p);
    view_->setColors(vc);
    applyIcons();
    if (doc_) {
        refreshTimeline();
    }
}

// ---------------------------------------------------------------- icons

void MainWindow::applyIcons()
{
    const QColor ink = palette().color(QPalette::WindowText), accent = accentFor(palette());
    for (auto& [id, a] : actions_) {
        a->setIcon(toolIcon(id, ink, accent));
    }
    if (centerRect_) {
        centerRect_->setIcon(toolIcon("sketch.rectangle", ink, accent));
    }
}

// ---------------------------------------------------------------- sketching

void MainWindow::startCreateSketch()
{
    if (panel_) {
        panel_->cancel();
    }
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    picker_ = std::make_unique<PlanePicker>(this, view_);
    view_->setInteractor(picker_.get());
    view_->setPickMode(PickMode::None);
    view_->setEmptyHint({});
    statusBar()->showMessage(tr("Click a plane or a flat face to sketch on. Esc cancels."));
}

void MainWindow::cancelPicker()
{
    if (!picker_) {
        return;
    }
    view_->setInteractor(nullptr);
    view_->setPickMode(PickMode::Faces);
    picker_.reset();
    statusBar()->clearMessage();
}

bool MainWindow::createSketchOn(const std::string& plane, const std::string& faceBody, int faceIndex)
{
    std::string id;
    auto r = doc_->transact(tr("New sketch").toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        jig::Feature f;
        f.id = jig::Document::newId(m, "s");
        f.name = tr("Sketch %1").arg(q(f.id.substr(1))).toStdString();
        f.kind = jig::FeatureKind::Sketch;
        if (faceIndex >= 0) {
            auto it = doc_->state().bodies.find(faceBody);
            if (it == doc_->state().bodies.end()) {
                return jig::makeError("sketch.face", "That body no longer exists.");
            }
            f.params["face"] = it->second->faceRef(faceBody, faceIndex).toJson();
        }
        else {
            f.params["plane"] = plane;
        }
        f.params["geometry"] = jig::Sketch().toJson();
        id = f.id;
        jig::Document::insertAtMarker(m, std::move(f));
        return true;
    });
    QTimer::singleShot(0, this, [this] { cancelPicker(); });
    if (!r) {
        QMessageBox::warning(this, tr("Sketch"), coreText(r.error().message));
        return false;
    }
    cancelPicker();
    enterSketch(id);
    return true;
}

void MainWindow::enterSketch(const std::string& id)
{
    if (panel_) {
        panel_->cancel();
    }
    cancelPicker();
    if (sketch_) {
        exitSketch();
    }
    sketch_ = new SketchEditor(view_, *doc_, id, this);
    view_->setInteractor(sketch_);
    view_->setSketchGrid(sketch_->frame());  // the grid lies in this sketch's plane
    view_->setPickMode(PickMode::None);
    connect(sketch_, &SketchEditor::committed, this, [this] { refreshAll(); });
    connect(sketch_, &SketchEditor::stateChanged, this, &MainWindow::updateSketchInfo);
    connect(sketch_, &SketchEditor::finishRequested, this, &MainWindow::exitSketch);
    toolsBar_->hide();
    sketchBar_->show();
    sideStack_->setCurrentWidget(sketchPanel_);
    propsDock_->setWindowTitle(tr("Sketch"));
    sketch_->lookAt();
    sketch_->setTool(SketchEditor::Tool::Line);  // the tool beginners need first
    refreshAll();
    updateSketchInfo();
    view_->setFocus();
}

void MainWindow::exitSketch()
{
    if (!sketch_) {
        return;
    }
    view_->setInteractor(nullptr);
    view_->setOverlay({});
    view_->setPickMode(PickMode::Faces);
    view_->setSketchGrid(std::nullopt);  // back to the floor grid
    if (sliceBox_ && sliceBox_->isChecked()) {
        sliceBox_->setChecked(false);  // takes the slice away
    }
    sketch_->deleteLater();
    sketch_ = nullptr;
    sketchBar_->hide();
    toolsBar_->show();
    sideStack_->setCurrentWidget(details_);
    propsDock_->setWindowTitle(tr("Properties"));
    refreshAll();
    view_->homeView();
}

void MainWindow::updateSketchInfo()
{
    if (!sketch_) {
        return;
    }
    const auto& rep = sketch_->report();
    const jig::Feature* f = doc_->feature(sketch_->featureId());
    QString state = rep.dof == 0 && rep.state != jig::SolveState::Failed
                        ? tr("■ <b>Fully defined</b>: nothing can move.")
                        : tr("○ <b>%n degree(s) of freedom</b> left: hollow points can still move.", "", rep.dof);
    if (rep.state == jig::SolveState::OverConstrained) {
        state = "⚠ " + q(rep.message).toHtmlEscaped();
    }
    QString html = QString("<h3>%1</h3><p>%2</p><p>%3</p>")
                       .arg(f ? q(f->name).toHtmlEscaped() : QString(), state, sketch_->toolHint().toHtmlEscaped());
    if (!sketch_->message().isEmpty()) {
        html += "<p style='color:#c53030'><b>" + sketch_->message().toHtmlEscaped() + "</b></p>";
    }
    sketchInfo_->setText(html);
    view_->update();
}

// ---------------------------------------------------------------- feature panels

void MainWindow::showPanel(ToolPanel* p)
{
    if (panel_) {
        panel_->cancel();
    }
    nameInputsFromLabels(p);  // screen readers say each field's label
    panel_ = p;
    panelHost_->layout()->addWidget(p);
    // wide enough for the panel and a scroll bar beside it, so nothing is cut off at the right
    panelScroll_->setMinimumWidth(p->minimumSizeHint().width() + panelScroll_->verticalScrollBar()->sizeHint().width() + 4);
    sideStack_->setCurrentWidget(panelScroll_);
    propsDock_->setWindowTitle(tr("Change"));
    propsDock_->show();
    connect(p, &ToolPanel::finished, this, [this](bool) { closePanel(); });
}

void MainWindow::closePanel()
{
    if (!panel_) {
        return;
    }
    panel_->hide();
    // back to the selection filter once the panel is gone (its destructor resets the view)
    connect(panel_, &QObject::destroyed, this, [this, view = QPointer<Viewport>(view_)] {
        if (view && !panel_ && !sketch_ && selectFilter_) {
            view->setPickMode(selectFilter() == "edges" ? PickMode::Edges : PickMode::Faces);
        }
    });
    panel_->deleteLater();
    panel_ = nullptr;
    sideStack_->setCurrentWidget(details_);
    propsDock_->setWindowTitle(tr("Properties"));
    view_->setModelTint(std::nullopt);
    refreshAll();
}

void MainWindow::openExtrude(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new ExtrudePanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openFillet(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->stateBefore(editId.empty() ? doc_->activeCount() : doc_->featureIndex(editId)).bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body to round yet: sketch and extrude first."), 5000);
        return;
    }
    showPanel(new FilletPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openChamfer(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->stateBefore(editId.empty() ? doc_->activeCount() : doc_->featureIndex(editId)).bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body to bevel yet: sketch and extrude first."), 5000);
        return;
    }
    showPanel(new FilletPanel(*doc_, view_, editId, panelHost_, true));
}

void MainWindow::openShell(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->stateBefore(editId.empty() ? doc_->activeCount() : doc_->featureIndex(editId)).bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body to hollow yet: sketch and extrude first."), 5000);
        return;
    }
    showPanel(new ShellPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openRevolve(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new RevolvePanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openHole(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->stateBefore(editId.empty() ? doc_->activeCount() : doc_->featureIndex(editId)).bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body to drill yet: sketch and extrude first."), 5000);
        return;
    }
    showPanel(new HolePanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openPattern(const std::string& editId, bool circular)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new PatternPanel(*doc_, view_, editId, panelHost_, circular));
}

void MainWindow::openMirror(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->stateBefore(editId.empty() ? doc_->activeCount() : doc_->featureIndex(editId)).bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body to mirror yet: sketch and extrude first."), 5000);
        return;
    }
    showPanel(new MirrorPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openBodyTool(const std::string& tool, const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->stateBefore(editId.empty() ? doc_->activeCount() : doc_->featureIndex(editId)).bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body yet: sketch and extrude first."), 5000);
        return;
    }
    if (tool == "solid.combine") {
        showPanel(new CombinePanel(*doc_, view_, editId, panelHost_));
    }
    else if (tool == "solid.split") {
        showPanel(new SplitPanel(*doc_, view_, editId, panelHost_));
    }
    else {
        showPanel(new MovePanel(*doc_, view_, editId, panelHost_));
    }
}

void MainWindow::openSweep(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new SweepPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openPipe(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new PipePanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openCoil(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new CoilPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openPrimitive(const std::string& editId, const std::string& shape)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new PrimitivePanel(*doc_, view_, editId, shape, panelHost_));
}

void MainWindow::openRib(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new RibPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openEmboss(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new EmbossPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openDraft(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new DraftPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openScale(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new ScalePanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openThread(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new ThreadPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openLoft(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new LoftPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openPlane(const std::string& editId, const std::string& kind)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new PlanePanel(*doc_, view_, editId, panelHost_, kind));
}

void MainWindow::openPoint(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new PointPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openAxis(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new AxisPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openPressPull(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    auto* pp = new PressPullPanel(*doc_, view_, editId, panelHost_);
    showPanel(pp);
    if (editId.empty()) {
        // a clicked face that is not flat: open the step that made it, when there is one
        connect(view_, &Viewport::faceClicked, pp, [this, pp](const QString& b, const QString& n, Qt::KeyboardModifiers) {
            if (featurePanel() == pp && !pp->previewOk()) {
                const QString what = pressPullFace(b.toStdString(), n.toStdString());
                if (!what.isEmpty()) {
                    statusBar()->showMessage(what, 6000);
                }
            }
        });
    }
}

QString MainWindow::pressPullFace(const std::string& body, const std::string& face)
{
    auto it = doc_->state().bodies.find(body);
    if (it == doc_->state().bodies.end()) {
        return {};
    }
    auto hits = it->second->findFaces(face);
    if (hits.empty() || jig::measureItem(it->second->face(hits[0])).kind == "flat face") {
        return {};
    }
    // face names start with the id of the step that made them: "<step>/…"
    const std::string owner = face.substr(0, face.find('/'));
    const int idx = doc_->featureIndex(owner);
    if (idx < 0) {
        return {};
    }
    const auto kind = doc_->model().features[size_t(idx)].kind;
    if (kind != jig::FeatureKind::Fillet && kind != jig::FeatureKind::Chamfer && kind != jig::FeatureKind::Hole) {
        return {};
    }
    editFeature(idx);
    return tr("That face was made by %1: change its size here.").arg(q(doc_->model().features[size_t(idx)].name));
}

bool MainWindow::importStepFile(const QString& path)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto id = jig::insertStep(*doc_, path.toStdString());
    QApplication::restoreOverrideCursor();
    if (!id) {
        QMessageBox::warning(this, tr("Could not import"), coreText(id.error().message));
        return false;
    }
    refreshAll();
    view_->fitAll();
    int n = 0;
    for (auto& [bid, b] : doc_->state().bodies) {
        n += bid == id.value() || bid.rfind(id.value() + ".", 0) == 0;
    }
    statusBar()->showMessage(tr("Imported %n solid(s) from %1. It is kept inside this design.", "", n)
                                 .arg(QFileInfo(path).fileName()),
                             8000);
    return true;
}

bool MainWindow::setBodyAppearance(const std::string& body, const QString& color, const QString& finish)
{
    auto r = doc_->setAppearance(body, {color.toStdString(), finish.toStdString()});
    if (!r) {
        statusBar()->showMessage(coreText(r.error().message), 6000);
        return false;
    }
    refreshAll();
    return true;
}

void MainWindow::openAppearance()
{
    if (doc_->state().bodies.empty()) {
        statusBar()->showMessage(tr("There is no body to colour yet: make a 3D body first."), 5000);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Appearance"));
    auto* form = new QFormLayout(&dlg);
    auto* body = new QComboBox(&dlg);
    for (auto& [id, b] : doc_->state().bodies) {
        const jig::Feature* f = doc_->feature(id);
        body->addItem(f ? q(f->name) : q(id), q(id));
    }
    if (const auto sel = view_->selection(); sel) {
        body->setCurrentIndex(std::max(0, body->findData(q(sel->body))));
    }
    auto* finish = new QComboBox(&dlg);
    finish->addItem(tr("Matte (no shine)"), "matte");
    finish->addItem(tr("Satin (a little shine)"), "satin");
    finish->addItem(tr("Glossy (shiny plastic)"), "glossy");
    finish->addItem(tr("Metal"), "metal");
    auto* swatches = new QWidget(&dlg);
    auto* grid = new QGridLayout(swatches);
    grid->setContentsMargins(0, 0, 0, 0);
    // named colours (the name is in each button's text for screen readers and tooltips)
    const std::vector<std::pair<QString, QString>> colours = {
        {tr("White"), "#f2f2f0"},  {tr("Light grey"), "#b9bec6"}, {tr("Dark grey"), "#4a4f57"}, {tr("Black"), "#1c1d20"},
        {tr("Red"), "#d0453a"},    {tr("Orange"), "#e98a2e"},     {tr("Yellow"), "#efc941"},    {tr("Green"), "#4aa865"},
        {tr("Teal"), "#2f9e9a"},   {tr("Blue"), "#3a7bd5"},       {tr("Purple"), "#8a5cc7"},    {tr("Wood"), "#b07a4a"}};
    QString chosen;
    auto apply = [&] {
        const std::string id = body->currentData().toString().toStdString();
        setBodyAppearance(id, chosen, finish->currentData().toString());
    };
    for (size_t i = 0; i < colours.size(); ++i) {
        auto* b = new QPushButton(&dlg);
        b->setToolTip(colours[i].first);
        b->setAccessibleName(colours[i].first);
        b->setFixedSize(28, 28);
        b->setStyleSheet(QString("background:%1; border:1px solid #888;").arg(colours[i].second));
        grid->addWidget(b, int(i / 6), int(i % 6));
        connect(b, &QPushButton::clicked, &dlg, [&, c = colours[i].second] {
            chosen = c;
            apply();
        });
    }
    auto* other = new QPushButton(tr("Other colour…"), &dlg);
    auto* reset = new QPushButton(tr("Back to the theme colour"), &dlg);
    connect(other, &QPushButton::clicked, &dlg, [&] {
        const QColor c = QColorDialog::getColor(chosen.isEmpty() ? QColor("#3a7bd5") : QColor(chosen), &dlg, tr("Colour"));
        if (c.isValid()) {
            chosen = c.name();
            apply();
        }
    });
    connect(reset, &QPushButton::clicked, &dlg, [&] {
        chosen.clear();
        apply();
    });
    auto load = [&] {
        auto it = doc_->appearances().find(body->currentData().toString().toStdString());
        chosen = it == doc_->appearances().end() ? QString() : q(it->second.color);
        finish->setCurrentIndex(std::max(0, finish->findData(it == doc_->appearances().end() ? QString("satin") : q(it->second.finish))));
    };
    load();
    connect(body, &QComboBox::currentIndexChanged, &dlg, [&] { load(); });
    connect(finish, &QComboBox::currentIndexChanged, &dlg, [&] {
        if (!chosen.isEmpty()) {
            apply();
        }
    });
    form->addRow(tr("Body"), body);
    form->addRow(tr("Colour"), swatches);
    form->addRow(QString(), other);
    form->addRow(tr("Finish"), finish);
    form->addRow(QString(), reset);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(close, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(close);
    dlg.exec();
}

void MainWindow::openFastener(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new FastenerPanel(*doc_, view_, editId, panelHost_));
}

void MainWindow::openJoint(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new JointPanel(*doc_, view_, editId, panelHost_));
}

QString MainWindow::showInterference()
{
    const auto hits = jig::interferences(doc_->state());
    QString text;
    if (hits.empty()) {
        text = doc_->state().bodies.size() < 2 ? tr("There is only one part, so nothing can overlap.")
                                               : tr("No parts overlap.");
    }
    else {
        QStringList lines;
        for (const auto& h : hits) {
            auto name = [this](const std::string& id) {
                const jig::Feature* f = doc_->feature(id);
                return f ? q(f->name) : q(id);
            };
            lines << tr("%1 and %2 overlap by %3.").arg(name(h.a), name(h.b), q(jig::formatVolume(h.volume, doc_->units())));
        }
        text = lines.join("\n");
    }
    statusBar()->showMessage(text.section('\n', 0, 0), 10000);
    return text;
}

bool MainWindow::insertCanvasFile(const QString& path)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    auto id = jig::insertCanvas(*doc_, path.toStdString(), "XY", "0 mm", "0 mm", doc_->defaultLength(100, 4));
    if (!id) {
        QMessageBox::warning(this, tr("Could not place the picture"), coreText(id.error().message));
        return false;
    }
    refreshAll();
    view_->viewTop();
    // straight on to its settings: plane, place, size, see-through
    openCanvas(id.value());
    return true;
}

void MainWindow::openCanvas(const std::string& editId)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new CanvasPanel(*doc_, view_, editId, panelHost_));
}

bool MainWindow::insertMeshFile(const QString& path, double unitMm)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    // STL and OBJ files carry no unit: ask, with millimetres first
    if (unitMm <= 0) {
        if (path.endsWith(".3mf", Qt::CaseInsensitive)) {
            unitMm = 1;
        }
        else {
            QStringList units {tr("Millimetres (most 3D-printing files)"), tr("Centimetres"), tr("Inches"),
                               tr("Metres")};
            bool ok = false;
            const QString u = QInputDialog::getItem(this, tr("Insert mesh"),
                                                    tr("What unit is this file in? STL and OBJ files do not say."),
                                                    units, 0, false, &ok);
            if (!ok) {
                return false;
            }
            const double sizes[4] = {1, 10, 25.4, 1000};
            unitMm = sizes[std::max<qsizetype>(0, units.indexOf(u))];
        }
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto id = jig::insertMesh(*doc_, path.toStdString(), unitMm);
    QApplication::restoreOverrideCursor();
    if (!id) {
        QString text = coreText(id.error().message);
        for (const auto& s : id.error().suggestions) {
            text += "\n" + coreText(s);
        }
        QMessageBox::warning(this, tr("Could not insert the mesh"), text);
        return false;
    }
    refreshAll();
    view_->fitAll();
    statusBar()->showMessage(tr("Inserted %1 as a solid. It is kept inside this design.").arg(QFileInfo(path).fileName()),
                             8000);
    return true;
}

void MainWindow::importDxf()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import DXF"), {}, tr("DXF drawings (*.dxf)"));
    if (path.isEmpty()) {
        return;
    }
    auto dwg = jig::readDxf(path.toStdString());
    if (!dwg) {
        QMessageBox::warning(this, tr("Could not import"), coreText(dwg.error().message));
        return;
    }
    std::optional<jig::LengthUnit> units;
    if (!dwg->unitsKnown) {
        // the file does not say: ask, offering the design's unit first
        QStringList choices {tr("Millimetres"), tr("Inches"), tr("Centimetres"), tr("Feet"), tr("Metres")};
        const jig::LengthUnit order[] = {jig::LengthUnit::Millimetre, jig::LengthUnit::Inch, jig::LengthUnit::Centimetre,
                                         jig::LengthUnit::Foot, jig::LengthUnit::Metre};
        int current = 0;
        for (int i = 0; i < 5; ++i) {
            current = order[i] == doc_->units() ? i : current;
        }
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, tr("Units of this drawing"),
                                                   tr("The file does not say which unit it uses. One unit in the "
                                                      "drawing is:"),
                                                   choices, current, false, &ok);
        if (!ok) {
            return;
        }
        units = order[choices.indexOf(pick)];
    }
    importDxfFile(path, units);
}

bool MainWindow::importDxfFile(const QString& path, std::optional<jig::LengthUnit> units)
{
    auto dwg = jig::readDxf(path.toStdString());
    if (!dwg) {
        QMessageBox::warning(this, tr("Could not import"), coreText(dwg.error().message));
        return false;
    }
    const double scale = units ? jig::millimetresPer(*units) : dwg->unitScale;
    if (!sketch_) {
        if (!createSketchOn("XY")) {
            return false;
        }
    }
    const std::string sid = sketch_->featureId();
    int added = 0;
    auto r = doc_->transact(tr("Import DXF").toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        jig::Feature* f = jig::Document::findFeature(m, sid);
        if (!f) {
            return jig::makeError("dxf.sketch", "The sketch to import into is gone.");
        }
        auto sk = jig::Sketch::fromJson(f->params.value("geometry", nlohmann::json::object()));
        if (!sk) {
            return sk.error();
        }
        added = jig::addDxfToSketch(sk.value(), dwg.value(), scale);
        f->params["geometry"] = sk->toJson();
        return true;
    });
    if (!r) {
        QMessageBox::warning(this, tr("Could not import"), coreText(r.error().message));
        return false;
    }
    sketch_->refreshFromDocument();
    sketch_->lookAt();
    refreshAll();
    QString msg = tr("Imported %n curve(s) from %1", "", added).arg(QFileInfo(path).fileName());
    QStringList skipped;
    for (auto& [type, n] : dwg->skipped) {
        skipped << QString("%1 × %2").arg(n).arg(q(type));
    }
    if (!skipped.isEmpty()) {
        msg += tr(" (not imported: %1)").arg(skipped.join(", "));
    }
    statusBar()->showMessage(msg, 8000);
    return true;
}

void MainWindow::importSvg()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import SVG"), {}, tr("SVG artwork (*.svg)"));
    if (!path.isEmpty()) {
        importSvgFile(path);
    }
}

bool MainWindow::importSvgFile(const QString& path)
{
    auto art = jig::readSvg(path.toStdString());
    if (!art) {
        QMessageBox::warning(this, tr("Could not import"), coreText(art.error().message));
        return false;
    }
    if (!sketch_ && !createSketchOn("XY")) {
        return false;
    }
    const std::string sid = sketch_->featureId();
    auto r = doc_->transact(tr("Import SVG").toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        jig::Feature* f = jig::Document::findFeature(m, sid);
        if (!f) {
            return jig::makeError("svg.sketch", "The sketch to import into is gone.");
        }
        auto sk = jig::Sketch::fromJson(f->params.value("geometry", nlohmann::json::object()));
        if (!sk) {
            return sk.error();
        }
        sk->addArtwork(0, 0, art->contours, art->width, {}, art->width, QFileInfo(path).completeBaseName().toStdString());
        f->params["geometry"] = sk->toJson();
        return true;
    });
    if (!r) {
        QMessageBox::warning(this, tr("Could not import"), coreText(r.error().message));
        return false;
    }
    sketch_->refreshFromDocument();
    sketch_->lookAt();
    refreshAll();
    QString msg = tr("Imported %1 at %2 wide. Double-click its centre point to change the size; drag it to move it.")
                      .arg(QFileInfo(path).fileName(), q(doc_->fmtLength(art->width)));
    if (!art->unitsKnown) {
        msg += " " + tr("(The file gives no physical size: 96 pixels per inch was assumed.)");
    }
    statusBar()->showMessage(msg, 10000);
    return true;
}

void MainWindow::openMeasure()
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    if (doc_->state().bodies.empty()) {
        statusBar()->showMessage(tr("There is nothing to measure yet: make a 3D body first."), 5000);
        return;
    }
    showPanel(new MeasurePanel(*doc_, view_, panelHost_));
}

void MainWindow::openProperties()
{
    if (doc_->state().bodies.empty()) {
        statusBar()->showMessage(tr("There is no 3D body yet."), 5000);
        return;
    }
    PropertiesDialog d(*doc_, this);
    d.exec();
}

void MainWindow::openParameters()
{
    ParametersDialog d(*doc_, this);
    connect(&d, &ParametersDialog::variantUsed, this, [this] { refreshAll(); });
    d.exec();
    // a variant used or saved changes the design even when the dialog is cancelled afterwards
    if (sketch_) {
        sketch_->refreshFromDocument();
    }
    refreshAll();
}

void MainWindow::escape()
{
    if (panel_) {
        panel_->cancel();
    }
    else if (picker_) {
        cancelPicker();
        refreshAll();
    }
    else if (sketch_) {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        sketch_->key(&ev);
    }
}

// ---------------------------------------------------------------- timeline editing

void MainWindow::editFeature(int index)
{
    if (!doc_ || index < 0 || index >= int(doc_->model().features.size())) {
        return;
    }
    const jig::Feature& f = doc_->model().features[size_t(index)];
    switch (f.kind) {
        case jig::FeatureKind::Sketch:
            enterSketch(f.id);
            break;
        case jig::FeatureKind::Extrude:
            openExtrude(f.id);
            break;
        case jig::FeatureKind::Fillet:
            openFillet(f.id);
            break;
        case jig::FeatureKind::Chamfer:
            openChamfer(f.id);
            break;
        case jig::FeatureKind::Shell:
            openShell(f.id);
            break;
        case jig::FeatureKind::Revolve:
            openRevolve(f.id);
            break;
        case jig::FeatureKind::Hole:
            openHole(f.id);
            break;
        case jig::FeatureKind::Pattern:
            openPattern(f.id);
            break;
        case jig::FeatureKind::Mirror:
            openMirror(f.id);
            break;
        case jig::FeatureKind::Plane:
            openPlane(f.id);
            break;
        case jig::FeatureKind::Axis:
            openAxis(f.id);
            break;
        case jig::FeatureKind::Point:
            openPoint(f.id);
            break;
        case jig::FeatureKind::Remove:
            statusBar()->showMessage(tr("This step takes bodies out of the design. To bring them back, delete the step "
                                        "(or suppress it from its menu)."),
                                     8000);
            break;
        case jig::FeatureKind::Import:
            statusBar()->showMessage(tr("An imported solid has no settings to change. To use a newer copy of the "
                                        "file, delete this step and import it again."),
                                     8000);
            break;
        case jig::FeatureKind::PressPull:
            openPressPull(f.id);
            break;
        case jig::FeatureKind::Combine:
            openBodyTool("solid.combine", f.id);
            break;
        case jig::FeatureKind::Split:
            openBodyTool("solid.split", f.id);
            break;
        case jig::FeatureKind::Loft:
            openLoft(f.id);
            break;
        case jig::FeatureKind::Pipe:
            openPipe(f.id);
            break;
        case jig::FeatureKind::Coil:
            openCoil(f.id);
            break;
        case jig::FeatureKind::Primitive:
            openPrimitive(f.id, {});
            break;
        case jig::FeatureKind::Rib:
            openRib(f.id);
            break;
        case jig::FeatureKind::Emboss:
            openEmboss(f.id);
            break;
        case jig::FeatureKind::Draft:
            openDraft(f.id);
            break;
        case jig::FeatureKind::Scale:
            openScale(f.id);
            break;
        case jig::FeatureKind::Thread:
            openThread(f.id);
            break;
        case jig::FeatureKind::Canvas:
            openCanvas(f.id);
            break;
        case jig::FeatureKind::Joint:
            openJoint(f.id);
            break;
        case jig::FeatureKind::Fastener:
            openFastener(f.id);
            break;
        case jig::FeatureKind::Sweep:
            openSweep(f.id);
            break;
        case jig::FeatureKind::Move:
            openBodyTool("solid.move", f.id);
            break;
    }
}

void MainWindow::timelineMenu(const QPoint& pos)
{
    const int row = timeline_->row(timeline_->itemAt(pos));
    if (row < 0) {
        return;
    }
    const jig::Feature f = doc_->model().features[size_t(row)];
    QMenu m(this);
    m.addAction(tr("Edit…"), this, [this, row] { editFeature(row); });
    m.addAction(tr("Rename…"), this, [this, id = f.id] { renameFeature(id); });
    m.addAction(f.suppressed ? tr("Unsuppress (use this step again)") : tr("Suppress (skip this step)"), this,
                [this, id = f.id, on = !f.suppressed] {
                    doc_->transact((on ? tr("Suppress") : tr("Unsuppress")).toStdString(),
                                   [&](jig::Model& mm) -> jig::Result<bool> {
                                       if (auto* x = jig::Document::findFeature(mm, id)) {
                                           x->suppressed = on;
                                       }
                                       return true;
                                   });
                    refreshAll();
                });
    const int count = int(doc_->model().features.size());
    auto* earlier = m.addAction(tr("Move earlier"), this, [this, id = f.id, row] { moveStep(id, row - 1); });
    earlier->setEnabled(row > 0);
    auto* later = m.addAction(tr("Move later"), this, [this, id = f.id, row] { moveStep(id, row + 1); });
    later->setEnabled(row + 1 < count);
    m.addAction(tr("Roll back to before this step"), this, [this, row] {
        doc_->transact(tr("Move rollback marker").toStdString(), [row](jig::Model& mm) -> jig::Result<bool> {
            mm.rollback = row;
            return true;
        });
        refreshAll();
    });
    m.addSeparator();
    m.addAction(tr("Put in a group…"), this, [this, id = f.id, row] {
        const auto& feats = doc_->model().features;
        const QString suggest = row > 0 ? q(feats[size_t(row - 1)].group) : QString();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Group"), tr("Group name:"), QLineEdit::Normal,
                                                   suggest.isEmpty() ? tr("Group") : suggest, &ok)
                                 .trimmed();
        if (ok && !name.isEmpty()) {
            setStepGroup(id, name.toStdString());
        }
    });
    if (!f.group.empty()) {
        m.addAction(tr("Take out of the group"), this, [this, id = f.id] { setStepGroup(id, {}); });
        const bool folded = doc_->foldedGroups().count(f.group) > 0;
        m.addAction(folded ? tr("Unfold the group") : tr("Fold the group"), this,
                    [this, g = f.group, folded] { setGroupFolded(g, !folded); });
    }
    m.addSeparator();
    m.addAction(toolIcon("sketch.trim", palette().color(QPalette::WindowText), accentFor(palette())), tr("Delete…"),
                this, [this, id = f.id] { deleteFeature(id); });
    m.exec(timeline_->viewport()->mapToGlobal(pos));
}

void MainWindow::setStepGroup(const std::string& id, const std::string& group)
{
    doc_->transact(group.empty() ? tr("Ungroup").toStdString() : tr("Group").toStdString(),
                   [&](jig::Model& mm) -> jig::Result<bool> {
                       if (auto* x = jig::Document::findFeature(mm, id)) {
                           x->group = group;
                       }
                       return true;
                   });
    refreshAll();
}

void MainWindow::setGroupFolded(const std::string& group, bool folded)
{
    auto groups = doc_->foldedGroups();  // saved with the design
    if (folded) {
        groups.insert(group);
    }
    else {
        groups.erase(group);
    }
    doc_->setFoldedGroups(std::move(groups));
    refreshAll();  // the title shows the unsaved change
}

bool MainWindow::moveStep(const std::string& id, int to)
{
    auto r = doc_->moveFeature(id, to);
    if (!r) {
        QMessageBox::information(this, tr("Cannot move this step there"), coreText(r.error().message) +
                                     (r.error().suggestions.empty() ? QString() : "\n\n" + q(r.error().suggestions.front())));
    }
    refreshAll();  // also puts a refused drag back where it was
    return r.ok();
}

void MainWindow::renameFeature(const std::string& id)
{
    const jig::Feature* f = doc_->feature(id);
    if (!f) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename step"), tr("Name:"), QLineEdit::Normal, q(f->name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    doc_->transact(tr("Rename").toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        if (auto* x = jig::Document::findFeature(m, id)) {
            x->name = name.toStdString();
        }
        return true;
    });
    refreshAll();
}

bool MainWindow::removeBody(const std::string& body)
{
    if (!doc_->state().bodies.count(body)) {
        return false;
    }
    if (sketch_) {
        exitSketch();
    }
    const jig::Feature* maker = doc_->feature(body);
    const std::string label = maker ? maker->name : body;
    std::string id;
    auto r = doc_->transact(tr("Delete body %1").arg(q(label)).toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        jig::Feature f;
        f.id = jig::Document::newId(m, "rm");
        f.name = tr("Remove %1").arg(q(label)).toStdString();
        f.kind = jig::FeatureKind::Remove;
        f.params = {{"bodies", nlohmann::json::array({body})}};
        id = f.id;
        jig::Document::insertAtMarker(m, std::move(f));
        return true;
    });
    if (!r) {
        statusBar()->showMessage(coreText(r.error().message), 6000);
        return false;
    }
    refreshAll();
    statusBar()->showMessage(tr("Body removed as a step in the timeline. Undo, or delete that step, to bring it back."),
                             6000);
    return true;
}

void MainWindow::deleteFeature(const std::string& id)
{
    const jig::Feature* f = doc_->feature(id);
    if (!f) {
        return;
    }
    const auto deps = doc_->dependents(id);
    if (!deps.empty()) {
        QStringList names;
        for (auto& d : deps) {
            if (const jig::Feature* x = doc_->feature(d)) {
                names << q(x->name);
            }
        }
        auto b = QMessageBox::question(this, tr("Delete step"),
                                       tr("“%1” is used by later steps: %2.\n\nThey will show a problem until you fix "
                                          "them. You can undo this. Delete anyway?")
                                           .arg(q(f->name), names.join(", ")));
        if (b != QMessageBox::Yes) {
            return;
        }
    }
    if (sketch_ && sketch_->featureId() == id) {
        exitSketch();
    }
    doc_->transact(tr("Delete %1").arg(q(f->name)).toStdString(), [&](jig::Model& m) -> jig::Result<bool> {
        const int i = [&] {
            for (size_t k = 0; k < m.features.size(); ++k) {
                if (m.features[k].id == id) {
                    return int(k);
                }
            }
            return -1;
        }();
        if (i < 0) {
            return jig::makeError("delete.gone", "That step no longer exists.");
        }
        m.features.erase(m.features.begin() + i);
        if (m.rollback > i) {
            --m.rollback;
        }
        return true;
    });
    refreshAll();
}

}  // namespace jigapp

#include "MainWindow.moc"
