// SPDX-License-Identifier: GPL-3.0-or-later
#include "FeaturePanels.hpp"

#include <map>
#include <regex>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "Viewport.hpp"

namespace jigapp
{
namespace
{

// Values remembered per command for the session (brief: dialog behaviour).
std::map<QString, QString>& remembered()
{
    static std::map<QString, QString> m;
    return m;
}

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

const QColor kPreviewTint(132, 176, 238);

}  // namespace

// ---------------------------------------------------------------- ExprField

ExprField::ExprField(jig::Document& doc, jig::Dim dim, QWidget* parent)
    : QWidget(parent)
    , doc_(doc)
    , dim_(dim)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(1);
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(dim == jig::Dim::ang() ? tr("e.g. 45 deg") : tr("e.g. 10 mm, 2 in, width/2"));
    result_ = new QLabel(this);
    QFont f = result_->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    result_->setFont(f);
    result_->setWordWrap(true);
    lay->addWidget(edit_);
    lay->addWidget(result_);
    connect(edit_, &QLineEdit::textChanged, this, [this] {
        evaluate();
        emit changed();
    });
}

QString ExprField::text() const
{
    return edit_->text().trimmed();
}

void ExprField::setText(const QString& t)
{
    edit_->setText(t);
    evaluate();
}

bool ExprField::valid() const
{
    return valid_;
}

double ExprField::value() const
{
    return value_;
}

void ExprField::evaluate()
{
    auto r = jig::evaluate(text().toStdString(), doc_.lookup(), dim_);
    valid_ = r.ok();
    if (valid_) {
        value_ = r->value;
        result_->setText("= " + q(jig::formatQuantity(r.value())));
        result_->setStyleSheet({});
    }
    else {
        result_->setText("✕ " + q(r.error().message));
        result_->setStyleSheet("color: #c53030;");
    }
}

// ---------------------------------------------------------------- FeaturePanel

FeaturePanel::FeaturePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : QWidget(parent)
    , doc_(doc)
    , view_(view)
    , editId_(std::move(editId))
{
    auto* outer = new QVBoxLayout(this);
    form_ = new QVBoxLayout();
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::RichText);
    fixes_ = new QWidget(this);
    new QVBoxLayout(fixes_);
    fixes_->layout()->setContentsMargins(0, 0, 0, 0);
    ok_ = new QPushButton(tr("OK"), this);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    ok_->setDefault(true);
    ok_->setToolTip(tr("Apply this step (Enter)."));
    cancelButton->setToolTip(tr("Close without changing anything (Esc)."));
    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(cancelButton);
    buttons->addWidget(ok_);
    outer->addLayout(form_);
    outer->addWidget(status_);
    outer->addWidget(fixes_);
    outer->addStretch();
    outer->addLayout(buttons);
    connect(ok_, &QPushButton::clicked, this, [this] { accept(); });
    connect(cancelButton, &QPushButton::clicked, this, [this] { cancel(); });
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(120);
    connect(timer_, &QTimer::timeout, this, [this] { runPreview(); });
}

FeaturePanel::~FeaturePanel()
{
    if (view_) {
        view_->setModelTint(std::nullopt);
    }
}

void FeaturePanel::addRow(const QString& label, QWidget* w)
{
    auto* l = new QLabel(label, this);
    l->setBuddy(w);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    form_->addWidget(l);
    form_->addWidget(w);
}

int FeaturePanel::insertIndex() const
{
    return editId_.empty() ? doc_.activeCount() : doc_.featureIndex(editId_);
}

std::shared_ptr<const jig::ModelState> FeaturePanel::stateBefore() const
{
    return std::make_shared<jig::ModelState>(doc_.stateBefore(insertIndex()));
}

void FeaturePanel::schedulePreview()
{
    timer_->start();
}

QString FeaturePanel::statusText() const
{
    return status_->text();
}

void FeaturePanel::runPreview()
{
    if (done_) {
        return;
    }
    const jig::Feature f = draft();
    const auto before = stateBefore();
    jig::FeatureOutcome o = jig::applyFeature(doc_, f, before);
    previewOk_ = o.status.level != jig::StatusLevel::Error;
    const auto& shown = o.after ? o.after : before;
    std::vector<Viewport::BodyInput> bodies;
    for (auto& [id, b] : shown->bodies) {
        bodies.push_back({id, b});
    }
    view_->setBodies(std::move(bodies));
    view_->setModelTint(previewOk_ ? std::optional<QColor>(kPreviewTint) : std::nullopt);

    // clear old fix buttons
    while (auto* item = fixes_->layout()->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (previewOk_) {
        QString t = "<b>✓ " + tr("Preview") + "</b>";
        if (!o.status.message.empty()) {
            t += "<br>⚠ " + q(o.status.message).toHtmlEscaped();
        }
        status_->setText(t);
    }
    else {
        QString t = "<b>✕ " + q(o.status.message).toHtmlEscaped() + "</b>";
        status_->setText(t);
        for (auto& sgg : o.status.suggestions) {
            // "Try 1.8 mm or less." becomes a one-click fix
            std::smatch m;
            const std::string s = sgg;
            if (std::regex_search(s, m, std::regex("^Try ([0-9.]+ ?mm)"))) {
                auto* b = new QPushButton(tr("Use %1").arg(q(m[1].str())), fixes_);
                const QString value = q(m[1].str());
                connect(b, &QPushButton::clicked, this, [this, value] {
                    for (auto* f : findChildren<ExprField*>()) {
                        f->setText(value);  // the panel's main size field
                        break;
                    }
                });
                fixes_->layout()->addWidget(b);
            }
            else {
                auto* l = new QLabel("• " + q(s), fixes_);
                l->setWordWrap(true);
                fixes_->layout()->addWidget(l);
            }
        }
    }
    ok_->setEnabled(previewOk_);
}

bool FeaturePanel::accept()
{
    if (done_) {
        return false;
    }
    timer_->stop();
    runPreview();
    if (!previewOk_) {
        return false;
    }
    const jig::Feature f = draft();
    const bool isNew = editId_.empty();
    auto r = doc_.transact((isNew ? tr("Add %1") : tr("Edit %1")).arg(title()).toStdString(),
                           [&](jig::Model& m) -> jig::Result<bool> {
                               if (isNew) {
                                   jig::Feature nf = f;
                                   const std::string id = jig::Document::newId(m, nf.id.substr(0, 1));
                                   nf.id = id;  // same as the draft's id: ids are counter-based
                                   jig::Document::insertAtMarker(m, nf);
                                   return true;
                               }
                               jig::Feature* old = jig::Document::findFeature(m, editId_);
                               if (!old) {
                                   return jig::makeError("panel.gone", "This step no longer exists.");
                               }
                               old->params = f.params;
                               return true;
                           });
    if (!r) {
        status_->setText("<b>✕ " + q(r.error().message).toHtmlEscaped() + "</b>");
        return false;
    }
    rememberValues();
    done_ = true;
    emit finished(true);
    return true;
}

void FeaturePanel::cancel()
{
    if (done_) {
        return;
    }
    done_ = true;
    timer_->stop();
    emit finished(false);
}

// ---------------------------------------------------------------- Extrude

ExtrudePanel::ExtrudePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    auto* head = new QLabel("<h3>" + tr("Extrude") + "</h3><p>" +
                                tr("Push closed areas of a sketch into 3D. Type a distance; negative goes the "
                                   "other way.") + "</p>",
                            this);
    head->setWordWrap(true);
    form_->addWidget(head);
    sketch_ = new QComboBox(this);
    profiles_ = new QListWidget(this);
    profiles_->setMaximumHeight(110);
    extent_ = new QComboBox(this);
    extent_->addItem(tr("Distance"), "distance");
    extent_->addItem(tr("Through everything"), "all");
    distance_ = new ExprField(doc, jig::Dim::len(), this);
    direction_ = new QComboBox(this);
    direction_->addItem(tr("One side"), "one");
    direction_->addItem(tr("Other side"), "reverse");
    direction_->addItem(tr("Both sides (symmetric)"), "symmetric");
    op_ = new QComboBox(this);
    op_->addItem(tr("Auto"), "auto");
    op_->addItem(tr("Join: add to the body"), "join");
    op_->addItem(tr("Cut: remove material"), "cut");
    op_->addItem(tr("New body"), "new");
    op_->addItem(tr("Intersect: keep the overlap"), "intersect");
    addRow(tr("Sketch"), sketch_);
    addRow(tr("Closed areas (profiles)"), profiles_);
    addRow(tr("Extent"), extent_);
    addRow(tr("Distance"), distance_);
    addRow(tr("Direction"), direction_);
    addRow(tr("Operation"), op_);

    // sketches available before the insertion point
    const auto before = stateBefore();
    const int limit = insertIndex();
    for (int i = 0; i < limit; ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Sketch && before->sketches.count(f.id)) {
            sketch_->addItem(q(f.name), q(f.id));
        }
    }
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    if (edit) {
        const auto& p = edit->params;
        sketch_->setCurrentIndex(std::max(0, sketch_->findData(q(p.value("sketch", std::string {})))));
        extent_->setCurrentIndex(std::max(0, extent_->findData(q(p.value("extent", std::string("distance"))))));
        distance_->setText(q(p.value("distance", std::string("10 mm"))));
        direction_->setCurrentIndex(std::max(0, direction_->findData(q(p.value("direction", std::string("one"))))));
        op_->setCurrentIndex(std::max(0, op_->findData(q(p.value("op", std::string("join"))))));
    }
    else {
        sketch_->setCurrentIndex(sketch_->count() - 1);
        distance_->setText(remembered().count("extrude.distance") ? remembered()["extrude.distance"] : "10 mm");
    }
    fillProfiles();
    if (edit && edit->params.contains("profiles") && edit->params["profiles"].is_array()) {
        std::set<std::string> keys;
        for (auto& k : edit->params["profiles"]) {
            keys.insert(k.get<std::string>());
        }
        for (int i = 0; i < profiles_->count(); ++i) {
            auto* it = profiles_->item(i);
            it->setCheckState(keys.count(it->data(Qt::UserRole).toString().toStdString()) ? Qt::Checked : Qt::Unchecked);
        }
    }
    connect(sketch_, &QComboBox::currentIndexChanged, this, [this] {
        fillProfiles();
        schedulePreview();
    });
    connect(profiles_, &QListWidget::itemChanged, this, [this] { schedulePreview(); });
    for (auto* c : {extent_, direction_, op_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    }
    connect(extent_, &QComboBox::currentIndexChanged, this,
            [this] { distance_->setEnabled(extent_->currentData().toString() == "distance"); });
    connect(distance_, &ExprField::changed, this, [this] { schedulePreview(); });
    if (sketch_->count() == 0) {
        status_->setText("<b>✕ " + tr("There is no sketch to extrude yet. Create a sketch first.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
    distance_->edit()->setFocus();
    distance_->edit()->selectAll();
}

void ExtrudePanel::fillProfiles()
{
    profiles_->blockSignals(true);
    profiles_->clear();
    const auto before = stateBefore();
    auto it = before->sketches.find(sketch_->currentData().toString().toStdString());
    if (it != before->sketches.end()) {
        int n = 1;
        for (auto& r : it->second->profiles.regions) {
            auto* item = new QListWidgetItem(tr("Area %1 · %2 mm²").arg(n++).arg(r.area, 0, 'f', 1), profiles_);
            item->setData(Qt::UserRole, q(r.key));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
        if (it->second->profiles.regions.empty()) {
            new QListWidgetItem(tr("(no closed areas: the outline has a gap)"), profiles_);
        }
    }
    profiles_->blockSignals(false);
}

QString ExtrudePanel::autoOperation() const
{
    const auto before = stateBefore();
    if (before->bodies.empty()) {
        return "new";
    }
    auto it = before->sketches.find(sketch_->currentData().toString().toStdString());
    if (it == before->sketches.end() || it->second->profiles.regions.empty() || !distance_->valid()) {
        return "join";
    }
    // Test a point just past the sketch plane, in the extrude direction, at the
    // first selected profile's centre: inside material means the user is cutting.
    const jig::Profile* prof = &it->second->profiles.regions.front();
    for (int i = 0; i < profiles_->count(); ++i) {
        if (profiles_->item(i)->checkState() == Qt::Checked) {
            prof = &it->second->profiles.regions[size_t(i)];
            break;
        }
    }
    GProp_GProps g;
    BRepGProp::SurfaceProperties(prof->face, g);
    const gp_Pnt c = g.CentreOfMass();
    const gp_Dir n = it->second->frame.Direction();
    double sign = distance_->value() < 0 ? -1 : 1;
    if (direction_->currentData().toString() == "reverse") {
        sign = -sign;
    }
    const double step = std::min(0.5, std::abs(distance_->value()) * 0.25) * sign;
    for (auto& [id, b] : before->bodies) {
        if (jig::pointInside(b->shape(), c.X() + n.X() * step, c.Y() + n.Y() * step, c.Z() + n.Z() * step)) {
            return "cut";
        }
    }
    return "join";
}

jig::Feature ExtrudePanel::draft() const
{
    jig::Feature f;
    if (editId_.empty()) {
        jig::Model tmp = doc_.model();
        f.id = jig::Document::newId(tmp, "e");
        f.name = tr("Extrude %1").arg(q(f.id.substr(1))).toStdString();
    }
    else {
        f = *doc_.feature(editId_);
    }
    f.kind = jig::FeatureKind::Extrude;
    nlohmann::json p;
    p["sketch"] = sketch_->currentData().toString().toStdString();
    nlohmann::json keys = nlohmann::json::array();
    bool all = true;
    for (int i = 0; i < profiles_->count(); ++i) {
        auto* it = profiles_->item(i);
        if (it->data(Qt::UserRole).toString().isEmpty()) {
            continue;
        }
        if (it->checkState() == Qt::Checked) {
            keys.push_back(it->data(Qt::UserRole).toString().toStdString());
        }
        else {
            all = false;
        }
    }
    p["profiles"] = all ? nlohmann::json("all") : keys;
    p["extent"] = extent_->currentData().toString().toStdString();
    p["distance"] = distance_->text().isEmpty() ? std::string("10 mm") : distance_->text().toStdString();
    p["direction"] = direction_->currentData().toString().toStdString();
    QString op = op_->currentData().toString();
    if (op == "auto") {
        op = autoOperation();
        const QString word = op == "cut" ? tr("Cut (goes into the body)") : op == "new" ? tr("New body") : tr("Join");
        const_cast<QComboBox*>(op_)->setItemText(0, tr("Auto: %1").arg(word));
    }
    p["op"] = op.toStdString();
    f.params = p;
    return f;
}

QString ExtrudePanel::title() const
{
    return tr("extrude");
}

void ExtrudePanel::rememberValues()
{
    remembered()["extrude.distance"] = distance_->text();
}

// ---------------------------------------------------------------- Fillet

FilletPanel::FilletPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    auto* head = new QLabel("<h3>" + tr("Fillet") + "</h3><p>" +
                                tr("Round edges. Click edges in the 3D view to add or remove them.") + "</p>",
                            this);
    head->setWordWrap(true);
    form_->addWidget(head);
    count_ = new QLabel(this);
    chain_ = new QCheckBox(tr("Also pick edges that flow smoothly into it (tangent chain)"), this);
    chain_->setChecked(true);
    auto* clear = new QPushButton(tr("Clear edges"), this);
    radius_ = new ExprField(doc, jig::Dim::len(), this);
    addRow(tr("Edges"), count_);
    form_->addWidget(chain_);
    form_->addWidget(clear);
    addRow(tr("Radius"), radius_);
    connect(clear, &QPushButton::clicked, this, [this] {
        edges_.clear();
        refreshEdges();
    });
    connect(radius_, &ExprField::changed, this, [this] { schedulePreview(); });
    connect(view_.data(), &Viewport::edgeClicked, this,
            [this](const QString& body, int edge, Qt::KeyboardModifiers) { toggleShownEdge(body.toStdString(), edge); });

    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    if (edit) {
        radius_->setText(q(edit->params.value("radius", std::string("1 mm"))));
        const auto before = stateBefore();
        for (auto& jr : edit->params.value("edges", nlohmann::json::array())) {
            jig::ShapeRef ref = jig::ShapeRef::fromJson(jr);
            auto bit = before->bodies.find(ref.body);
            if (bit == before->bodies.end()) {
                continue;
            }
            auto res = jig::resolve(*bit->second, ref);
            if (res.index >= 0) {
                body_ = ref.body;
                edges_.insert(res.index);
            }
        }
    }
    else {
        radius_->setText(remembered().count("fillet.radius") ? remembered()["fillet.radius"] : "2 mm");
    }
    view_->setPickMode(PickMode::Edges);
    refreshEdges();
}

FilletPanel::~FilletPanel()
{
    if (view_) {
        view_->setPickMode(PickMode::Faces);
        view_->setEdgeHighlight({}, {});
    }
}

void FilletPanel::toggleShownEdge(const std::string& body, int shownEdge)
{
    const auto before = stateBefore();
    auto bit = before->bodies.find(body);
    if (bit == before->bodies.end() || shownEdge < 0) {
        return;
    }
    jig::FeatureOutcome shown = jig::applyFeature(doc_, draft(), before);
    const auto& st = (edges_.empty() || !shown.after || !previewOk_) ? before : shown.after;
    auto sit = st->bodies.find(body);
    if (sit == st->bodies.end() || shownEdge >= sit->second->edgeCount()) {
        return;
    }
    auto hits = bit->second->findEdges(sit->second->edgeName(shownEdge));
    if (hits.size() == 1) {
        toggleEdge(body, hits[0]);
    }
    // else: the click was on a surface the fillet itself made; nothing to toggle
}

void FilletPanel::toggleEdge(const std::string& body, int e)
{
    const auto before = stateBefore();
    auto bit = before->bodies.find(body);
    if (bit == before->bodies.end() || e < 0 || e >= bit->second->edgeCount()) {
        return;
    }
    if (!body_.empty() && body_ != body) {
        status_->setText("<b>✕ " + tr("A fillet rounds edges of one body at a time.") + "</b>");
        return;
    }
    body_ = body;
    std::vector<int> group = chain_->isChecked() ? jig::tangentChain(*bit->second, e) : std::vector<int> {e};
    const bool remove = edges_.count(e) > 0;
    for (int g : group) {
        if (remove) {
            edges_.erase(g);
        }
        else {
            edges_.insert(g);
        }
    }
    if (edges_.empty()) {
        body_.clear();
    }
    refreshEdges();
}

void FilletPanel::refreshEdges()
{
    count_->setText(edges_.empty() ? tr("No edges yet: click edges in the 3D view.")
                                   : tr("%n edge(s) selected", "", int(edges_.size())));
    view_->setEdgeHighlight({}, {});
    if (edges_.empty()) {
        // show the plain model for picking
        const auto before = stateBefore();
        std::vector<Viewport::BodyInput> bodies;
        for (auto& [id, b] : before->bodies) {
            bodies.push_back({id, b});
        }
        view_->setBodies(std::move(bodies));
        view_->setModelTint(std::nullopt);
        status_->setText(tr("Pick edges to round."));
        ok_->setEnabled(false);
        previewOk_ = false;
        return;
    }
    schedulePreview();
}

jig::Feature FilletPanel::draft() const
{
    jig::Feature f;
    if (editId_.empty()) {
        jig::Model tmp = doc_.model();
        f.id = jig::Document::newId(tmp, "f");
        f.name = tr("Fillet %1").arg(q(f.id.substr(1))).toStdString();
    }
    else {
        f = *doc_.feature(editId_);
    }
    f.kind = jig::FeatureKind::Fillet;
    nlohmann::json refs = nlohmann::json::array();
    const auto before = stateBefore();
    auto bit = before->bodies.find(body_);
    if (bit != before->bodies.end()) {
        for (int e : edges_) {
            refs.push_back(bit->second->edgeRef(body_, e).toJson());
        }
    }
    f.params = {{"edges", refs}, {"radius", radius_->text().isEmpty() ? std::string("1 mm") : radius_->text().toStdString()}};
    return f;
}

QString FilletPanel::title() const
{
    return tr("fillet");
}

void FilletPanel::rememberValues()
{
    remembered()["fillet.radius"] = radius_->text();
}

// ---------------------------------------------------------------- Parameters

ParametersDialog::ParametersDialog(jig::Document& doc, QWidget* parent)
    : QDialog(parent)
    , doc_(doc)
{
    setWindowTitle(tr("Parameters"));
    resize(720, 420);
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Name the sizes that matter (width, wall, clearance) and type the names in any "
                                "size field. Values can use units and other names, e.g. <i>width / 2 + 3 mm</i>."),
                             this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Value"), tr("Kind"), tr("Result"), tr("Comment")});
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->setAccessibleName(tr("Parameters table"));
    lay->addWidget(table_);
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    lay->addWidget(note_);
    auto* row = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add parameter"), this);
    auto* del = new QPushButton(tr("Remove selected"), this);
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch();
    lay->addLayout(row);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(bb);
    for (auto& p : doc_.model().parameters) {
        addRow(p);
    }
    connect(add, &QPushButton::clicked, this, [this] {
        int n = table_->rowCount() + 1;
        addRow({"param" + std::to_string(n), "10 mm", jig::ParamKind::Length, {}});
        table_->editItem(table_->item(table_->rowCount() - 1, 0));
    });
    connect(del, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        std::set<int> r;
        for (auto& i : rows) {
            r.insert(i.row());
        }
        if (r.empty() && table_->currentRow() >= 0) {
            r.insert(table_->currentRow());
        }
        for (auto it = r.rbegin(); it != r.rend(); ++it) {
            table_->removeRow(*it);
        }
        revalidate();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (it->column() != 3) {
            revalidate();
        }
    });
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        if (apply()) {
            accept();
        }
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    revalidate();
}

void ParametersDialog::addRow(const jig::Parameter& p)
{
    table_->blockSignals(true);
    const int r = table_->rowCount();
    table_->insertRow(r);
    table_->setItem(r, 0, new QTableWidgetItem(q(p.name)));
    table_->setItem(r, 1, new QTableWidgetItem(q(p.expr)));
    auto* kind = new QComboBox(table_);
    kind->addItem(tr("Length"), int(jig::ParamKind::Length));
    kind->addItem(tr("Angle"), int(jig::ParamKind::Angle));
    kind->addItem(tr("Number"), int(jig::ParamKind::Number));
    kind->setCurrentIndex(kind->findData(int(p.kind)));
    connect(kind, &QComboBox::currentIndexChanged, this, [this] { revalidate(); });
    table_->setCellWidget(r, 2, kind);
    auto* res = new QTableWidgetItem();
    res->setFlags(res->flags() & ~Qt::ItemIsEditable);
    table_->setItem(r, 3, res);
    table_->setItem(r, 4, new QTableWidgetItem(q(p.comment)));
    table_->blockSignals(false);
    revalidate();
}

namespace
{
jig::Model modelFromTable(const jig::Model& base, QTableWidget* t)
{
    jig::Model m = base;
    m.parameters.clear();
    for (int r = 0; r < t->rowCount(); ++r) {
        jig::Parameter p;
        p.name = t->item(r, 0) ? t->item(r, 0)->text().trimmed().toStdString() : std::string {};
        p.expr = t->item(r, 1) ? t->item(r, 1)->text().trimmed().toStdString() : std::string {};
        auto* kind = qobject_cast<QComboBox*>(t->cellWidget(r, 2));
        p.kind = kind ? jig::ParamKind(kind->currentData().toInt()) : jig::ParamKind::Length;
        p.comment = t->item(r, 4) ? t->item(r, 4)->text().toStdString() : std::string {};
        m.parameters.push_back(p);
    }
    return m;
}
}  // namespace

void ParametersDialog::revalidate()
{
    const jig::Model m = modelFromTable(doc_.model(), table_);
    table_->blockSignals(true);
    int bad = 0;
    std::set<std::string> names;
    for (int r = 0; r < table_->rowCount(); ++r) {
        const auto& p = m.parameters[size_t(r)];
        QString text;
        bool ok = true;
        if (!jig::isValidParameterName(p.name)) {
            text = tr("✕ name must start with a letter: letters, digits and _ only");
            ok = false;
        }
        else if (!names.insert(p.name).second) {
            text = tr("✕ this name is used twice");
            ok = false;
        }
        else {
            auto v = jig::Document::evalParamIn(m, p.name);
            ok = v.ok();
            text = ok ? "= " + q(jig::formatQuantity(v.value())) : "✕ " + q(v.error().message);
        }
        bad += !ok;
        if (auto* it = table_->item(r, 3)) {
            it->setText(text);
            it->setForeground(ok ? palette().color(QPalette::Text) : QColor(197, 48, 48));
        }
    }
    table_->blockSignals(false);
    note_->setText(bad ? tr("%n parameter(s) need attention before you can apply.", "", bad)
                       : tr("Everything works. Changes update the whole design when you press OK."));
}

bool ParametersDialog::apply()
{
    const jig::Model m = modelFromTable(doc_.model(), table_);
    std::set<std::string> names;
    for (auto& p : m.parameters) {
        if (!jig::isValidParameterName(p.name) || !names.insert(p.name).second || !jig::Document::evalParamIn(m, p.name)) {
            revalidate();
            QMessageBox::information(this, tr("Parameters"), tr("Some parameters need attention first (see the Result column)."));
            return false;
        }
    }
    auto r = doc_.transact(tr("Change parameters").toStdString(), [&](jig::Model& mm) -> jig::Result<bool> {
        mm.parameters = m.parameters;
        return true;
    });
    return r.ok();
}

}  // namespace jigapp
