// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.hpp"

#include <QAction>
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

#include "Viewport.hpp"
#include "jig/commands.hpp"
#include "jig/io.hpp"
#include "jig/version.hpp"

namespace jigapp
{
namespace
{

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
            auto* it = new QListWidgetItem(line, list_);
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

MainWindow::MainWindow()
{
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
        details_->setHtml(tr("<h3>Face</h3><p><b>Name:</b> %1<br><b>Surface:</b> %2<br><b>Area:</b> %3 mm²<br>"
                             "<b>Body:</b> %4</p><p>Faces keep their name when earlier steps change, so "
                             "fillets and sketches stay attached.</p>")
                              .arg(q(it->second->faceName(face)).toHtmlEscaped(), q(fp.type))
                              .arg(fp.size, 0, 'f', 1)
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
    theme_ = Theme(st.value("theme", int(Theme::Light)).toInt());
    view_->setNavPreset(NavPreset(st.value("navigation", int(NavPreset::Default)).toInt()));
    setTheme(theme_);
    restoreGeometry(st.value("geometry").toByteArray());

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(60 * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer_->start();

    setDocument(std::make_unique<jig::Document>(), {});
    resize(1360, 860);
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
    for (const char* id : {"file.new", "file.open", "file.save", "file.save_as"}) {
        file->addAction(registryAction(id));
    }
    file->addSeparator();
    auto* samples = file->addMenu(tr("Open a sample"));
    samples->addAction(tr("Phone stand"), this, [this] { openSample("phone_stand"); });
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
    tools->setObjectName("ToolsBar");
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
    ddock->setObjectName("PropertiesDock");
    details_ = new QTextBrowser(ddock);
    details_->setOpenExternalLinks(false);
    details_->setAccessibleName(tr("Properties and help"));
    details_->setWhatsThis(tr("Details about what you selected, and explanations and fixes for any step "
                              "marked with a problem."));
    ddock->setWidget(details_);
    addDockWidget(Qt::RightDockWidgetArea, ddock);

    auto* tdock = new QDockWidget(tr("Timeline"), this);
    tdock->setObjectName("TimelineDock");
    auto* tw = new QWidget(tdock);
    auto* tl = new QVBoxLayout(tw);
    tl->setContentsMargins(4, 2, 4, 2);
    timeline_ = new QListWidget(tw);
    timeline_->setFlow(QListView::LeftToRight);
    timeline_->setWrapping(false);
    timeline_->setFixedHeight(56);
    timeline_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timeline_->setAccessibleName(tr("Timeline of steps"));
    timeline_->setWhatsThis(tr("Every step you took, in order. Click a step to see its details and any "
                               "problem. Drag the rollback marker below to see the design at an earlier "
                               "step; new steps are inserted there."));
    connect(timeline_, &QListWidget::currentRowChanged, this, &MainWindow::showFeatureDetails);
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
    doc_ = std::move(doc);
    path_ = path;
    savedRevision_ = doc_->revision();
    autosavedRevision_ = savedRevision_;
    refreshAll();
    view_->homeView();
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
    view_->setEmptyHint(doc_->model().features.empty()
                            ? tr("Start here: every part begins as a flat sketch on a plane.\n"
                                 "Interactive sketching arrives in milestone M1. For now, open the phone "
                                 "stand sample (File ▸ Open a sample) to explore a finished part.")
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
    setWindowTitle(QString("%1%2 — Jigsmith").arg(name, dirty ? " •" : ""));
    setWindowModified(dirty);
    statusInfo_->setText(tr("mm · Z up · rebuilt %1 step(s) in %2 ms")
                             .arg(doc_->recomputedLastTime())
                             .arg(doc_->lastRecomputeMs(), 0, 'f', 0));
}

void MainWindow::refreshBrowser()
{
    browser_->clear();
    auto* settings = new QTreeWidgetItem(browser_, {tr("Document settings")});
    new QTreeWidgetItem(settings, {tr("Units: millimetres")});
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
        new QTreeWidgetItem(bodies, {tr("%1  (%2 cm³)").arg(f ? q(f->name) : q(id)).arg(mp.volume / 1000, 0, 'f', 1)});
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
        QString tip = statusWord(st.level);
        if (!st.message.empty()) {
            tip += ": " + q(st.message);
        }
        it->setToolTip(tip);
        it->setSizeHint(QSize(124, 46));
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
    html += tr("<p><i>Editing a step's values arrives in milestone M2. Parameters, undo and the rollback "
               "marker work now.</i></p>");
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
    else if (id == "file.save_as") {
        save(true);
    }
    else if (id == "file.export_stl") {
        exportAs("stl");
    }
    else if (id == "file.export_step") {
        exportAs("step");
    }
    else if (id == "edit.undo") {
        doc_->undo();
        refreshAll();
    }
    else if (id == "edit.redo") {
        doc_->redo();
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
    setDocument(std::make_unique<jig::Document>(), {});
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
    const QString filter = format == "stl" ? tr("STL mesh for slicers (*.stl)") : tr("STEP solid (*.step *.stp)");
    QString p = QFileDialog::getSaveFileName(this, tr("Export"), {}, filter);
    if (p.isEmpty()) {
        return;
    }
    if (!p.contains('.')) {
        p += "." + format;
    }
    auto shape = doc_->combinedShape();
    auto r = format == "stl" ? jig::exportStl(shape, p.toStdString()) : jig::exportStep(shape, p.toStdString());
    if (!r) {
        QMessageBox::warning(this, tr("Could not export"), q(r.error().message));
        return;
    }
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
    auto* tut = new QPushButton(tr("Your first part (15 min)\nComing in milestone M7"), &d);
    tut->setEnabled(false);
    tut->setToolTip(tr("A guided tour that builds a phone stand with you. Not built yet."));
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

void MainWindow::setTheme(Theme t)
{
    theme_ = t;
    QSettings().setValue("theme", int(t));
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
}

}  // namespace jigapp
