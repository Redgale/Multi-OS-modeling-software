// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.hpp"

#include <array>

#include <QAction>
#include <QLocale>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QPainter>
#include <QInputDialog>
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
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWhatsThis>

#include "FeaturePanels.hpp"
#include "InspectPanels.hpp"
#include "Tutorial.hpp"
#include "Icons.hpp"
#include "SketchEditor.hpp"
#include "Viewport.hpp"
#include "jig/commands.hpp"
#include "jig/io.hpp"
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
        case jig::FeatureKind::Combine:
            return MainWindow::tr("Combine");
        case jig::FeatureKind::Split:
            return MainWindow::tr("Split");
        case jig::FeatureKind::Move:
            return MainWindow::tr("Move");
    }
    return {};
}

// ---------------------------------------------------------------- command palette

class CommandPalette : public QDialog
{
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
            QString line = q(c.label);
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
        const QString msg = QObject::tr("Click a plane, a construction plane or a flat face to sketch on.  Esc cancels.");
        const QRectF box(10, 10, p.fontMetrics().horizontalAdvance(msg) + 20, 26);
        QColor bg = v_->colors().background;
        bg.setAlpha(230);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(box, 5, 5);
        p.setPen(v_->colors().text);
        p.drawText(box, Qt::AlignCenter, msg);
        const float s = size();
        const std::pair<QString, QVector3D> labels[] = {{QObject::tr("XY · floor"), {s * 0.75f, -s * 0.75f, 0}},
                                                         {QObject::tr("XZ · front"), {s * 0.75f, 0, s * 0.85f}},
                                                         {QObject::tr("YZ · side"), {0, s * 0.75f, s * 0.85f}}};
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

MainWindow::MainWindow()
{
    initJigResources();
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
    connect(view_, &Viewport::selectionChanged, this, [this](const QString& body, int face) {
        if (face < 0 || !doc_) {
            details_->setHtml(tr("<p>Nothing selected.</p>"));
            return;
        }
        auto it = doc_->state().bodies.find(body.toStdString());
        if (it == doc_->state().bodies.end()) {
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
    connect(view_, &Viewport::meshesUpdated, this, [this] {
        static bool first = true;
        if (first && doc_ && !doc_->state().bodies.empty()) {
            view_->homeView();
            first = false;
        }
    });

    QSettings st;
    // Dark is the default. The key is new: the old "theme" key was written with Light on
    // every first start, so it does not tell us what anyone actually chose.
    theme_ = Theme(st.value("appearance/theme", int(Theme::Dark)).toInt());
    view_->setNavPreset(NavPreset(st.value("navigation", int(NavPreset::Default)).toInt()));
    setTheme(theme_);
    restoreGeometry(st.value("geometry").toByteArray());

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(60 * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer_->start();

    setDocument(blankDocument(), {});
    resize(1360, 860);
    // Esc: cancel the open panel, the plane picker, or the sketch tool in progress
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, &MainWindow::escape);
}

MainWindow::~MainWindow() = default;

QAction* MainWindow::registryAction(const char* id)
{
    auto it = actions_.find(id);
    return it == actions_.end() ? nullptr : it->second;
}

void MainWindow::buildActions()
{
    for (const auto& c : jig::CommandRegistry::instance().all()) {
        auto* a = new QAction(q(c.label), this);
        a->setObjectName(q(c.id));
        QString tip = "<b>" + q(c.label) + "</b>";
        if (c.standardName != c.label) {
            tip += " (" + q(c.standardName) + ")";
        }
        tip += "<br>" + q(c.tooltip).toHtmlEscaped();
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
    auto* navGroup = new QActionGroup(this);
    for (auto& [p, label] : presets) {
        auto* a = nav->addAction(label);
        a->setCheckable(true);
        a->setChecked(view_->navPreset() == p);
        navGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, p = p] {
            view_->setNavPreset(p);
            QSettings().setValue("navigation", int(p));
        });
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
        auto* title = new QLabel(QString("<small><b>%1</b></small>").arg(tr(panel)), this);
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
        {"sketch.circle", {"sketch.circle2", "sketch.circle3"}},
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
    sketchBar_->addAction(registryAction("sketch.finish"));
    sketchBar_->hide();
}

void MainWindow::buildDocks()
{
    auto* bdock = new QDockWidget(tr("Browser"), this);
    bdock->setObjectName("BrowserDock");
    browser_ = new QTreeWidget(bdock);
    browser_->setHeaderHidden(true);
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
    panelHost_ = new QWidget(sideStack_);
    new QVBoxLayout(panelHost_);
    panelHost_->layout()->setContentsMargins(0, 0, 0, 0);
    sideStack_->addWidget(panelHost_);
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
    view_->setDatums(std::move(ov), std::move(labels));
}

void MainWindow::refreshAll()
{
    if (!doc_) {
        return;
    }
    std::vector<Viewport::BodyInput> bodies;
    for (auto& [id, b] : doc_->state().bodies) {
        bodies.push_back({id, b});
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
        new QTreeWidgetItem(sketches, {QString("%1 %2  (%3)").arg(statusGlyph(st.level), q(feats[i].name),
                                                                  extra.isEmpty() ? statusWord(st.level) : extra)});
    }
    for (auto& [id, b] : doc_->state().bodies) {
        auto mp = jig::massProperties(b->shape());
        const jig::Feature* f = doc_->feature(id);
        new QTreeWidgetItem(bodies, {tr("%1  (%2)").arg(f ? q(f->name) : q(id), q(jig::formatVolume(mp.volume, doc_->units())))});
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
        it->setIcon(toolIcon(QString("feature.") + jig::toString(feats[i].kind), palette().color(QPalette::WindowText),
                             accentFor(palette())));
        QString tip = statusWord(st.level);
        if (!st.message.empty()) {
            tip += ": " + q(st.message);
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
        html += "<p>" + q(st.message).toHtmlEscaped() + "</p>";
    }
    if (!st.suggestions.empty()) {
        html += tr("<p><b>Ways to fix this:</b></p><ul>");
        for (auto& s : st.suggestions) {
            html += "<li>" + q(s).toHtmlEscaped() + "</li>";
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
    else if (id == "construct.offset_plane" || id == "construct.angle_plane" || id == "construct.midplane") {
        openPlane({}, id == "construct.angle_plane" ? "angle" : id == "construct.midplane" ? "midplane" : "offset");
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
        QMessageBox::warning(this, tr("Could not open"), q(r.error().message));
        return false;
    }
    setDocument(std::move(r.value()), path);
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
        QMessageBox::warning(this, tr("Sample problem"), q(r.error().message));
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
        QMessageBox::warning(this, tr("Could not save a version"), q(r.error().message));
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
        QMessageBox::warning(this, tr("Could not restore"), q(v.error().message));
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
        QMessageBox::warning(this, tr("Versions"), q(list.error().message));
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
        QMessageBox::warning(this, tr("Could not save"), q(r.error().message));
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
    if (!p.contains('.')) {
        p += "." + format;
    }
    jig::Result<bool> r = true;
    if (format == "3mf") {
        std::vector<std::pair<std::string, TopoDS_Shape>> bodies;
        for (auto& [id, b] : doc_->state().bodies) {
            const jig::Feature* f = doc_->feature(id);
            bodies.emplace_back(f ? f->name : id, b->shape());
        }
        r = jig::export3mf(bodies, p.toStdString());
    }
    else {
        auto shape = doc_->combinedShape();
        r = format == "stl" ? jig::exportStl(shape, p.toStdString()) : jig::exportStep(shape, p.toStdString());
    }
    if (!r) {
        QMessageBox::warning(this, tr("Could not export"), q(r.error().message));
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

QString MainWindow::recoveryPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
    QDir().mkpath(dir);
    return dir + "/autosave.jig";
}

void MainWindow::autosave()
{
    if (!doc_ || doc_->revision() == autosavedRevision_ || doc_->revision() == savedRevision_) {
        return;
    }
    if (jig::saveDocument(*doc_, recoveryPath().toStdString())) {
        autosavedRevision_ = doc_->revision();
    }
}

void MainWindow::checkRecovery()
{
    const QString lock = QFileInfo(recoveryPath()).absolutePath() + "/session.lock";
    const bool crashed = QFile::exists(lock);
    QFile lf(lock);
    if (lf.open(QIODevice::WriteOnly)) {
        lf.write(QByteArray::number(QCoreApplication::applicationPid()));
    }
    if (!crashed || !QFile::exists(recoveryPath())) {
        return;
    }
    const auto when = QFileInfo(recoveryPath()).lastModified();
    auto b = QMessageBox::question(this, tr("Recover your work?"),
                                   tr("Jigsmith did not close normally last time. A copy of your design "
                                      "from %1 was kept. Restore it?")
                                       .arg(QLocale().toString(when, QLocale::ShortFormat)));
    if (b == QMessageBox::Yes) {
        openFile(recoveryPath());
        path_.clear();             // recovered work is unsaved until the user saves it
        savedRevision_ = 0;
        refreshAll();
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeKeepChanges()) {
        e->ignore();
        return;
    }
    QSettings().setValue("geometry", saveGeometry());
    const QString dir = QFileInfo(recoveryPath()).absolutePath();
    QFile::remove(dir + "/session.lock");
    QFile::remove(recoveryPath());
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
    form->addRow(tr("Theme"), theme);
    form->addRow(tr("Units of this design"), design);
    form->addRow(tr("Units for new designs"), fresh);
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
    if (auto u = jig::lengthUnitFromString(design->currentData().toString().toStdString())) {
        setDesignUnits(*u);
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
        QMessageBox::warning(this, tr("Sketch"), q(r.error().message));
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
    panel_ = p;
    panelHost_->layout()->addWidget(p);
    sideStack_->setCurrentWidget(panelHost_);
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

void MainWindow::openPlane(const std::string& editId, const std::string& kind)
{
    if (sketch_) {
        exitSketch();
    }
    cancelPicker();
    showPanel(new PlanePanel(*doc_, view_, editId, panelHost_, kind));
}

void MainWindow::importDxf()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import DXF"), {}, tr("DXF drawings (*.dxf)"));
    if (path.isEmpty()) {
        return;
    }
    auto dwg = jig::readDxf(path.toStdString());
    if (!dwg) {
        QMessageBox::warning(this, tr("Could not import"), q(dwg.error().message));
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
        QMessageBox::warning(this, tr("Could not import"), q(dwg.error().message));
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
        QMessageBox::warning(this, tr("Could not import"), q(r.error().message));
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
        QMessageBox::warning(this, tr("Could not import"), q(art.error().message));
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
        QMessageBox::warning(this, tr("Could not import"), q(r.error().message));
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
    if (d.exec() == QDialog::Accepted) {
        if (sketch_) {
            sketch_->refreshFromDocument();
        }
        refreshAll();
    }
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
        case jig::FeatureKind::Combine:
            openBodyTool("solid.combine", f.id);
            break;
        case jig::FeatureKind::Split:
            openBodyTool("solid.split", f.id);
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
    m.addAction(toolIcon("sketch.trim", palette().color(QPalette::WindowText), accentFor(palette())), tr("Delete…"),
                this, [this, id = f.id] { deleteFeature(id); });
    m.exec(timeline_->viewport()->mapToGlobal(pos));
}

bool MainWindow::moveStep(const std::string& id, int to)
{
    auto r = doc_->moveFeature(id, to);
    if (!r) {
        QMessageBox::information(this, tr("Cannot move this step there"), q(r.error().message) +
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
