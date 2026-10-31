// SPDX-License-Identifier: GPL-3.0-or-later
#include "FeaturePanels.hpp"

#include <cctype>
#include <map>
#include <regex>
#include <set>

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

// ---------------------------------------------------------------- shared helpers

namespace
{

QLabel* heading(QWidget* parent, const QString& title, const QString& text)
{
    auto* l = new QLabel("<h3>" + title + "</h3><p>" + text + "</p>", parent);
    l->setWordWrap(true);
    return l;
}

QComboBox* combo(QWidget* parent, const std::vector<std::pair<QString, QString>>& items)
{
    auto* c = new QComboBox(parent);
    for (auto& [label, data] : items) {
        c->addItem(label, data);
    }
    return c;
}

void select(QComboBox* c, const std::string& data)
{
    const int i = c->findData(q(data));
    if (i >= 0) {
        c->setCurrentIndex(i);
    }
}

std::string current(const QComboBox* c)
{
    return c->currentData().toString().toStdString();
}

std::string text(const ExprField* f, const char* fallback)
{
    return f->text().isEmpty() ? std::string(fallback) : f->text().toStdString();
}

// Sketches before the insertion point.
void fillSketches(QComboBox* c, const jig::Document& doc, int limit, const jig::ModelState& before)
{
    for (int i = 0; i < limit; ++i) {
        const auto& f = doc.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Sketch && before.sketches.count(f.id)) {
            c->addItem(q(f.name), q(f.id));
        }
    }
}

void fillProfileList(QListWidget* list, const jig::SketchState* sk, const nlohmann::json& chosen,
                     jig::LengthUnit units = jig::LengthUnit::Millimetre)
{
    list->blockSignals(true);
    list->clear();
    if (sk) {
        std::set<std::string> keys;
        if (chosen.is_array()) {
            for (auto& k : chosen) {
                keys.insert(k.get<std::string>());
            }
        }
        int n = 1;
        for (auto& r : sk->profiles.regions) {
            auto* item = new QListWidgetItem(QObject::tr("Area %1 · %2").arg(n++).arg(q(jig::formatArea(r.area, units))), list);
            item->setData(Qt::UserRole, q(r.key));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(keys.empty() || keys.count(r.key) ? Qt::Checked : Qt::Unchecked);
        }
        if (sk->profiles.regions.empty()) {
            new QListWidgetItem(QObject::tr("(no closed areas: the outline has a gap)"), list);
        }
    }
    list->blockSignals(false);
}

nlohmann::json chosenProfiles(const QListWidget* list)
{
    nlohmann::json keys = nlohmann::json::array();
    bool all = true;
    for (int i = 0; i < list->count(); ++i) {
        auto* it = list->item(i);
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
    return all ? nlohmann::json("all") : keys;
}

// A new feature's id and name, or the edited feature.
jig::Feature startDraft(const jig::Document& doc, const std::string& editId, const std::string& prefix,
                        const QString& word)
{
    if (!editId.empty()) {
        return *doc.feature(editId);
    }
    jig::Feature f;
    jig::Model tmp = doc.model();
    f.id = jig::Document::newId(tmp, prefix);
    f.name = QString("%1 %2").arg(word, q(f.id.substr(prefix.size()))).toStdString();
    return f;
}

const std::vector<std::pair<QString, QString>> kAxes {
    {QObject::tr("Z axis (up)"), "Z"}, {QObject::tr("X axis"), "X"}, {QObject::tr("Y axis"), "Y"}};

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
    edit_->setPlaceholderText(dim == jig::Dim::ang() ? tr("e.g. 45 deg") : tr("e.g. 10 mm, 2 in, width/2 (a plain number is in %1)").arg(q(jig::unitSymbol(doc.units()))));
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
    // a plain number typed into a size field is in the design's unit: store it explicitly
    const QString raw = edit_->text().trimmed();
    return dim_ == jig::Dim::len() ? q(doc_.lengthInput(raw.toStdString())) : raw;
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
        result_->setText("= " + q(dim_ == jig::Dim::len() ? doc_.fmtLength(r->value) : jig::formatQuantity(r.value())));
        result_->setStyleSheet({});
    }
    else {
        result_->setText("✕ " + q(r.error().message));
        result_->setStyleSheet("color: #c53030;");
    }
}

// ---------------------------------------------------------------- FeaturePanel

FeaturePanel::FeaturePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : ToolPanel(parent)
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
    previewed(o);
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
                                   // prefix = the draft id without its number ("sh1" -> "sh")
                                   std::string prefix = nf.id;
                                   while (!prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix.back()))) {
                                       prefix.pop_back();
                                   }
                                   const std::string id = jig::Document::newId(m, prefix);
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
    direction_->addItem(tr("Two sides (two distances)"), "two");
    distance2_ = new ExprField(doc, jig::Dim::len(), this);
    taper_ = new ExprField(doc, jig::Dim::ang(), this);
    thinOn_ = new QCheckBox(tr("Thin wall instead of solid"), this);
    thin_ = new ExprField(doc, jig::Dim::len(), this);
    thinSide_ = new QComboBox(this);
    thinSide_->addItem(tr("Centred on the outline"), "center");
    thinSide_->addItem(tr("Inside the outline"), "inside");
    thinSide_->addItem(tr("Outside the outline"), "outside");
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
    addRow(tr("Other side distance"), distance2_);
    addRow(tr("Taper (narrows as it goes; negative widens)"), taper_);
    form_->addWidget(thinOn_);
    addRow(tr("Wall thickness"), thin_);
    addRow(tr("Wall position"), thinSide_);
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
        distance2_->setText(q(p.value("distance2", doc_.defaultLength(10, 0.5))));
        taper_->setText(q(p.value("taper", std::string("0 deg"))));
        thinOn_->setChecked(p.contains("thin"));
        thin_->setText(q(p.value("thin", doc_.defaultLength(2, 0.08))));
        thinSide_->setCurrentIndex(std::max(0, thinSide_->findData(q(p.value("thin_side", std::string("center"))))));
        op_->setCurrentIndex(std::max(0, op_->findData(q(p.value("op", std::string("join"))))));
    }
    else {
        sketch_->setCurrentIndex(sketch_->count() - 1);
        distance_->setText(remembered().count("extrude.distance") ? remembered()["extrude.distance"] : q(doc_.defaultLength(10, 0.5)));
        distance2_->setText(q(doc_.defaultLength(10, 0.5)));
        taper_->setText("0 deg");
        thin_->setText(q(doc_.defaultLength(2, 0.08)));
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
    for (auto* f : {distance2_, taper_, thin_}) {
        connect(f, &ExprField::changed, this, [this] { schedulePreview(); });
    }
    connect(thinSide_, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    connect(direction_, &QComboBox::currentIndexChanged, this, [this] { updateVisibility(); });
    connect(thinOn_, &QCheckBox::toggled, this, [this] {
        updateVisibility();
        schedulePreview();
    });
    updateVisibility();
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

void ExtrudePanel::updateVisibility()
{
    auto show = [this](QWidget* w, bool on) {
        w->setVisible(on);
        const int i = form_->indexOf(w);
        if (i > 0) {
            if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
                item->widget()->setVisible(on);
            }
        }
    };
    show(distance2_, direction_->currentData().toString() == "two");
    show(thin_, thinOn_->isChecked());
    show(thinSide_, thinOn_->isChecked());
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
            auto* item = new QListWidgetItem(tr("Area %1 · %2").arg(n++).arg(q(jig::formatArea(r.area, doc_.units()))), profiles_);
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
    if (p["direction"] == "two") {
        p["distance2"] = distance2_->text().isEmpty() ? std::string("10 mm") : distance2_->text().toStdString();
    }
    if (taper_->valid() && std::abs(taper_->value()) > 1e-12) {
        p["taper"] = taper_->text().toStdString();
    }
    if (thinOn_->isChecked()) {
        p["thin"] = thin_->text().isEmpty() ? std::string("2 mm") : thin_->text().toStdString();
        p["thin_side"] = thinSide_->currentData().toString().toStdString();
    }
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

FilletPanel::FilletPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent, bool chamfer)
    : FeaturePanel(doc, view, std::move(editId), parent)
    , chamfer_(chamfer)
{
    auto* head = new QLabel(chamfer ? "<h3>" + tr("Chamfer") + "</h3><p>" +
                                          tr("Bevel edges with a flat cut. Click edges in the 3D view to add or remove "
                                             "them. A small chamfer on the bottom edge stops 3D prints flaring.") + "</p>"
                                    : "<h3>" + tr("Fillet") + "</h3><p>" +
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
    type_ = chamfer ? combo(this, {{tr("Equal distance"), "equal"}, {tr("Two distances"), "two"}, {tr("Distance and angle"), "angle"}})
                    : combo(this, {{tr("Constant radius"), "equal"}, {tr("Variable: one radius at each end"), "variable"}});
    second_ = new ExprField(doc, jig::Dim::len(), this);
    angle_ = new ExprField(doc, jig::Dim::ang(), this);
    flip_ = new QCheckBox(chamfer ? tr("Swap the two sides") : tr("Swap the start and end"), this);
    addRow(tr("Type"), type_);
    addRow(chamfer ? tr("Distance") : tr("Radius"), radius_);
    addRow(chamfer ? tr("Second distance") : tr("Radius at the other end"), second_);
    addRow(tr("Angle from the first face"), angle_);
    form_->addWidget(flip_);
    connect(clear, &QPushButton::clicked, this, [this] {
        edges_.clear();
        refreshEdges();
    });
    connect(radius_, &ExprField::changed, this, [this] { schedulePreview(); });
    for (auto* e : {second_, angle_}) {
        connect(e, &ExprField::changed, this, [this] { schedulePreview(); });
    }
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        schedulePreview();
    });
    connect(flip_, &QCheckBox::toggled, this, [this] { schedulePreview(); });
    second_->setText(q(doc_.defaultLength(chamfer ? 2 : 4, chamfer ? 0.0625 : 0.125)));
    angle_->setText("45 deg");
    connect(view_.data(), &Viewport::edgeClicked, this, [this](const QString& body, const QString& name, Qt::KeyboardModifiers) {
        toggleEdgeByName(body.toStdString(), name.toStdString());
    });

    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    if (edit) {
        radius_->setText(q(edit->params.value(chamfer ? "distance" : "radius", std::string("1 mm"))));
        select(type_, edit->params.value("type", std::string("equal")));
        if (edit->params.contains(chamfer ? "distance2" : "radius2")) {
            second_->setText(q(edit->params.value(chamfer ? "distance2" : "radius2", std::string {})));
        }
        if (edit->params.contains("angle")) {
            angle_->setText(q(edit->params.value("angle", std::string("45 deg"))));
        }
        flip_->setChecked(edit->params.value("flip", false));
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
        const QString key = chamfer ? "chamfer.distance" : "fillet.radius";
        radius_->setText(remembered().count(key) ? remembered()[key]
                                                 : (chamfer ? q(doc_.defaultLength(1, 0.03125)) : q(doc_.defaultLength(2, 0.0625))));
    }
    view_->setPickMode(PickMode::Edges);
    updateVisibility();
    refreshEdges();
}

void FilletPanel::updateVisibility()
{
    const std::string t = current(type_);
    auto show = [this](QWidget* w, bool on) {
        w->setVisible(on);
        const int i = form_->indexOf(w);
        if (i > 0) {
            if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
                item->widget()->setVisible(on);
            }
        }
    };
    show(second_, t == "two" || t == "variable");
    show(angle_, t == "angle");
    flip_->setVisible(t != "equal");
}

FilletPanel::~FilletPanel()
{
    if (view_) {
        view_->setPickMode(PickMode::Faces);
        view_->setEdgeHighlight({}, {});
    }
}

void FilletPanel::toggleEdgeByName(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto bit = before->bodies.find(body);
    if (bit == before->bodies.end()) {
        return;
    }
    auto hits = bit->second->findEdges(name);
    if (hits.size() == 1) {
        toggleEdge(body, hits[0]);
    }
    // else: the click was on a surface this step itself made; nothing to toggle
}

void FilletPanel::toggleEdge(const std::string& body, int e)
{
    const auto before = stateBefore();
    auto bit = before->bodies.find(body);
    if (bit == before->bodies.end() || e < 0 || e >= bit->second->edgeCount()) {
        return;
    }
    if (!body_.empty() && body_ != body) {
        status_->setText("<b>✕ " + tr("Pick edges of one body at a time.") + "</b>");
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
        status_->setText(chamfer_ ? tr("Pick edges to bevel.") : tr("Pick edges to round."));
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
        f.id = jig::Document::newId(tmp, chamfer_ ? "c" : "f");
        f.name = (chamfer_ ? tr("Chamfer %1") : tr("Fillet %1")).arg(q(f.id.substr(1))).toStdString();
    }
    else {
        f = *doc_.feature(editId_);
    }
    f.kind = chamfer_ ? jig::FeatureKind::Chamfer : jig::FeatureKind::Fillet;
    nlohmann::json refs = nlohmann::json::array();
    const auto before = stateBefore();
    auto bit = before->bodies.find(body_);
    if (bit != before->bodies.end()) {
        for (int e : edges_) {
            refs.push_back(bit->second->edgeRef(body_, e).toJson());
        }
    }
    f.params = {{"edges", refs},
                {chamfer_ ? "distance" : "radius", radius_->text().isEmpty() ? std::string("1 mm") : radius_->text().toStdString()}};
    const std::string t = current(type_);
    if (t != "equal") {
        f.params["type"] = t;
        if (flip_->isChecked()) {
            f.params["flip"] = true;
        }
    }
    if (t == "two" || t == "variable") {
        f.params[chamfer_ ? "distance2" : "radius2"] = second_->text().toStdString();
    }
    if (t == "angle") {
        f.params["angle"] = angle_->text().toStdString();
    }
    return f;
}

QString FilletPanel::title() const
{
    return chamfer_ ? tr("chamfer") : tr("fillet");
}

void FilletPanel::rememberValues()
{
    remembered()[chamfer_ ? "chamfer.distance" : "fillet.radius"] = radius_->text();
}

// ---------------------------------------------------------------- Shell

ShellPanel::ShellPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    auto* head = new QLabel("<h3>" + tr("Shell") + "</h3><p>" +
                                tr("Hollow the body, leaving walls. Click the faces to leave open (for a cup, the "
                                   "top). With no face picked, the inside is a closed hollow.") + "</p>",
                            this);
    head->setWordWrap(true);
    form_->addWidget(head);
    count_ = new QLabel(this);
    auto* clear = new QPushButton(tr("Clear open faces"), this);
    thickness_ = new ExprField(doc, jig::Dim::len(), this);
    addRow(tr("Open faces"), count_);
    form_->addWidget(clear);
    addRow(tr("Wall thickness"), thickness_);
    auto* tip = new QLabel(tr("For 3D prints, use at least two nozzle widths (0.8 mm for a 0.4 mm nozzle)."), this);
    tip->setWordWrap(true);
    form_->addWidget(tip);
    connect(clear, &QPushButton::clicked, this, [this] {
        faces_.clear();
        refreshFaces();
    });
    connect(thickness_, &ExprField::changed, this, [this] { schedulePreview(); });
    connect(view_.data(), &Viewport::faceClicked, this, [this](const QString& body, const QString& name, Qt::KeyboardModifiers) {
        toggleFaceByName(body.toStdString(), name.toStdString());
    });
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    const auto before = stateBefore();
    if (edit) {
        thickness_->setText(q(edit->params.value("thickness", std::string("2 mm"))));
        body_ = edit->params.value("body", std::string {});
        for (auto& jr : edit->params.value("faces", nlohmann::json::array())) {
            jig::ShapeRef ref = jig::ShapeRef::fromJson(jr);
            auto bit = before->bodies.find(ref.body);
            if (bit != before->bodies.end()) {
                auto res = jig::resolve(*bit->second, ref);
                if (res.index >= 0) {
                    faces_.insert(res.index);
                    body_ = ref.body;
                }
            }
        }
    }
    else {
        thickness_->setText(remembered().count("shell.thickness") ? remembered()["shell.thickness"] : q(doc_.defaultLength(2, 0.08)));
    }
    if (body_.empty() && !before->bodies.empty()) {
        body_ = before->bodies.begin()->first;
    }
    view_->setPickMode(PickMode::Faces);
    refreshFaces();
}

ShellPanel::~ShellPanel()
{
    if (view_) {
        view_->setFaceHighlight({}, {});
    }
}

void ShellPanel::toggleFaceByName(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto bit = before->bodies.find(body);
    if (bit == before->bodies.end()) {
        return;
    }
    auto hits = bit->second->findFaces(name);
    if (hits.size() == 1) {
        toggleFace(body, hits[0]);
    }
}

void ShellPanel::toggleFace(const std::string& body, int face)
{
    if (!faces_.empty() && body != body_) {
        status_->setText("<b>✕ " + tr("Pick faces of one body at a time.") + "</b>");
        return;
    }
    body_ = body;
    if (!faces_.erase(face)) {
        faces_.insert(face);
    }
    refreshFaces();
}

void ShellPanel::refreshFaces()
{
    count_->setText(faces_.empty() ? tr("None: a closed hollow. Click faces to leave them open.")
                                   : tr("%n face(s) left open", "", int(faces_.size())));
    schedulePreview();
}

jig::Feature ShellPanel::draft() const
{
    jig::Feature f;
    if (editId_.empty()) {
        jig::Model tmp = doc_.model();
        f.id = jig::Document::newId(tmp, "sh");
        f.name = tr("Shell %1").arg(q(f.id.substr(2))).toStdString();
    }
    else {
        f = *doc_.feature(editId_);
    }
    f.kind = jig::FeatureKind::Shell;
    nlohmann::json refs = nlohmann::json::array();
    const auto before = stateBefore();
    auto bit = before->bodies.find(body_);
    if (bit != before->bodies.end()) {
        for (int fi : faces_) {
            refs.push_back(bit->second->faceRef(body_, fi).toJson());
        }
    }
    f.params = {{"faces", refs},
                {"thickness", thickness_->text().isEmpty() ? std::string("2 mm") : thickness_->text().toStdString()},
                {"body", body_}};
    return f;
}

QString ShellPanel::title() const
{
    return tr("shell");
}

void ShellPanel::rememberValues()
{
    remembered()["shell.thickness"] = thickness_->text();
}


// ---------------------------------------------------------------- Revolve

RevolvePanel::RevolvePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Revolve"),
                             tr("Spin closed areas of a sketch around an axis, like a lathe. Draw half of the "
                                "outline next to the axis; it must not cross it.")));
    sketch_ = new QComboBox(this);
    profiles_ = new QListWidget(this);
    profiles_->setMaximumHeight(110);
    axis_ = new QComboBox(this);
    angle_ = new ExprField(doc, jig::Dim::ang(), this);
    op_ = combo(this, {{tr("Join: add to the body"), "join"},
                       {tr("Cut: remove material"), "cut"},
                       {tr("New body"), "new"},
                       {tr("Intersect: keep the overlap"), "intersect"}});
    addRow(tr("Sketch"), sketch_);
    addRow(tr("Closed areas (profiles)"), profiles_);
    addRow(tr("Axis"), axis_);
    addRow(tr("Angle"), angle_);
    addRow(tr("Operation"), op_);
    const auto before = stateBefore();
    fillSketches(sketch_, doc_, insertIndex(), *before);
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    if (edit) {
        select(sketch_, edit->params.value("sketch", std::string {}));
        angle_->setText(q(edit->params.value("angle", std::string("360 deg"))));
        select(op_, edit->params.value("op", std::string("join")));
    }
    else {
        sketch_->setCurrentIndex(sketch_->count() - 1);
        angle_->setText("360 deg");
        select(op_, before->bodies.empty() ? "new" : "join");
    }
    fillFromSketch();
    if (edit) {
        const auto ax = edit->params.value("axis", nlohmann::json::object());
        if (ax.contains("line")) {
            select(axis_, "line:" + std::to_string(ax["line"].get<int>()));
        }
        else {
            select(axis_, ax.value("origin", std::string("Z")));
        }
    }
    connect(sketch_, &QComboBox::currentIndexChanged, this, [this] {
        fillFromSketch();
        schedulePreview();
    });
    connect(profiles_, &QListWidget::itemChanged, this, [this] { schedulePreview(); });
    for (auto* c : {axis_, op_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    }
    connect(angle_, &ExprField::changed, this, [this] { schedulePreview(); });
    if (sketch_->count() == 0) {
        status_->setText("<b>✕ " + tr("There is no sketch to revolve yet. Create a sketch first.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

void RevolvePanel::fillFromSketch()
{
    const auto before = stateBefore();
    auto it = before->sketches.find(current(sketch_));
    const jig::SketchState* sk = it == before->sketches.end() ? nullptr : it->second.get();
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    fillProfileList(profiles_, sk, edit ? edit->params.value("profiles", nlohmann::json("all")) : nlohmann::json("all"),
                    doc_.units());
    axis_->blockSignals(true);
    axis_->clear();
    // Lines of the sketch first (construction lines are the usual choice), then the origin axes.
    int firstConstruction = -1;
    if (sk) {
        int n = 1;
        for (auto& [id, e] : sk->solved.entities()) {
            if (e.type != jig::EntityType::Line) {
                continue;
            }
            if (e.construction && firstConstruction < 0) {
                firstConstruction = axis_->count();
            }
            axis_->addItem(e.construction ? tr("Sketch line %1 (construction)").arg(n) : tr("Sketch line %1").arg(n),
                           q("line:" + std::to_string(id)));
            ++n;
        }
    }
    for (auto& [label, d] : kAxes) {
        axis_->addItem(label, d);
    }
    axis_->setCurrentIndex(firstConstruction >= 0 ? firstConstruction : axis_->findData("Z"));
    axis_->blockSignals(false);
}

jig::Feature RevolvePanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "r", tr("Revolve"));
    f.kind = jig::FeatureKind::Revolve;
    const std::string ax = current(axis_);
    nlohmann::json axis = ax.rfind("line:", 0) == 0 ? nlohmann::json {{"line", std::stoi(ax.substr(5))}}
                                                    : nlohmann::json {{"origin", ax.empty() ? "Z" : ax}};
    f.params = {{"sketch", current(sketch_)},
                {"profiles", chosenProfiles(profiles_)},
                {"axis", axis},
                {"angle", text(angle_, "360 deg")},
                {"op", current(op_)}};
    return f;
}

QString RevolvePanel::title() const
{
    return tr("revolve");
}

// ---------------------------------------------------------------- Hole

HolePanel::HolePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Hole"),
                             tr("Drill a hole at every point of a sketch. Sketch on the face to drill, place a "
                                "Point at each hole centre, then pick the sketch here.")));
    sketch_ = new QComboBox(this);
    points_ = new QLabel(this);
    type_ = combo(this, {{tr("Simple"), "simple"},
                         {tr("Counterbore (screw head sits below the surface)"), "counterbore"},
                         {tr("Countersink (for flat-head screws)"), "countersink"}});
    extent_ = combo(this, {{tr("Depth"), "distance"}, {tr("Through everything"), "all"}});
    diameter_ = new ExprField(doc, jig::Dim::len(), this);
    depth_ = new ExprField(doc, jig::Dim::len(), this);
    cboreDiameter_ = new ExprField(doc, jig::Dim::len(), this);
    cboreDepth_ = new ExprField(doc, jig::Dim::len(), this);
    csinkDiameter_ = new ExprField(doc, jig::Dim::len(), this);
    csinkAngle_ = new ExprField(doc, jig::Dim::ang(), this);
    addRow(tr("Sketch with hole points"), sketch_);
    form_->addWidget(points_);
    addRow(tr("Type"), type_);
    addRow(tr("Diameter"), diameter_);
    addRow(tr("Extent"), extent_);
    addRow(tr("Depth"), depth_);
    addRow(tr("Counterbore diameter"), cboreDiameter_);
    addRow(tr("Counterbore depth"), cboreDepth_);
    addRow(tr("Countersink diameter"), csinkDiameter_);
    addRow(tr("Countersink angle"), csinkAngle_);
    auto* tip = new QLabel(doc.units() == jig::LengthUnit::Inch || doc.units() == jig::LengthUnit::Foot
                               ? tr("Clearance for a #4 screw: 0.13 in; for a #6: 0.15 in. Add about 0.01 in for 3D prints.")
                               : tr("Clearance for an M3 screw: 3.4 mm. Add 0.2–0.4 mm for 3D prints."),
                           this);
    tip->setWordWrap(true);
    form_->addWidget(tip);
    const auto before = stateBefore();
    fillSketches(sketch_, doc_, insertIndex(), *before);
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    auto value = [&](const char* key, const char* def) {
        if (edit) {
            return q(edit->params.value(key, std::string(def)));
        }
        auto it = remembered().find(QString("hole.") + key);
        return it != remembered().end() ? it->second : QString(def);
    };
    if (edit) {
        select(sketch_, edit->params.value("sketch", std::string {}));
    }
    else {
        sketch_->setCurrentIndex(sketch_->count() - 1);
    }
    select(type_, value("type", "simple").toStdString());
    select(extent_, value("extent", "distance").toStdString());
    diameter_->setText(value("diameter", doc_.defaultLength(3.4, 0.13).c_str()));
    depth_->setText(value("depth", doc_.defaultLength(10, 0.375).c_str()));
    cboreDiameter_->setText(value("cbore_diameter", doc_.defaultLength(6.5, 0.25).c_str()));
    cboreDepth_->setText(value("cbore_depth", doc_.defaultLength(3.5, 0.14).c_str()));
    csinkDiameter_->setText(value("csink_diameter", doc_.defaultLength(6.5, 0.25).c_str()));
    csinkAngle_->setText(value("csink_angle", "90 deg"));
    for (auto* c : {sketch_, type_, extent_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            schedulePreview();
        });
    }
    for (auto* e : {diameter_, depth_, cboreDiameter_, cboreDepth_, csinkDiameter_, csinkAngle_}) {
        connect(e, &ExprField::changed, this, [this] { schedulePreview(); });
    }
    updateVisibility();
    if (sketch_->count() == 0) {
        status_->setText("<b>✕ " + tr("Make a sketch with a Point at each hole centre first.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

void HolePanel::updateVisibility()
{
    const std::string t = current(type_);
    auto show = [this](QWidget* w, bool on) {
        w->setVisible(on);
        // the bold label added by addRow sits right before the field
        const int i = form_->indexOf(w);
        if (i > 0) {
            if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
                item->widget()->setVisible(on);
            }
        }
    };
    show(depth_, current(extent_) == "distance");
    show(cboreDiameter_, t == "counterbore");
    show(cboreDepth_, t == "counterbore");
    show(csinkDiameter_, t == "countersink");
    show(csinkAngle_, t == "countersink");
    // how many hole points the sketch has
    const auto before = stateBefore();
    auto it = before->sketches.find(current(sketch_));
    int n = 0;
    if (it != before->sketches.end()) {
        std::set<int> used;
        for (auto& [id, e] : it->second->solved.entities()) {
            if (e.type != jig::EntityType::Point) {
                used.insert({e.p1, e.p2, e.center});
            }
        }
        for (auto& [id, e] : it->second->solved.entities()) {
            n += e.type == jig::EntityType::Point && !e.internal && !e.construction && !used.count(id);
        }
    }
    points_->setText(n == 1 ? tr("1 hole point") : tr("%1 hole points").arg(n));
}

jig::Feature HolePanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "h", tr("Hole"));
    f.kind = jig::FeatureKind::Hole;
    f.params = {{"sketch", current(sketch_)},
                {"type", current(type_)},
                {"extent", current(extent_)},
                {"diameter", text(diameter_, "3.4 mm")},
                {"depth", text(depth_, "10 mm")}};
    if (current(type_) == "counterbore") {
        f.params["cbore_diameter"] = text(cboreDiameter_, "6.5 mm");
        f.params["cbore_depth"] = text(cboreDepth_, "3.5 mm");
    }
    if (current(type_) == "countersink") {
        f.params["csink_diameter"] = text(csinkDiameter_, "6.5 mm");
        f.params["csink_angle"] = text(csinkAngle_, "90 deg");
    }
    return f;
}

QString HolePanel::title() const
{
    return tr("hole");
}

void HolePanel::rememberValues()
{
    remembered()["hole.type"] = q(current(type_));
    remembered()["hole.extent"] = q(current(extent_));
    remembered()["hole.diameter"] = diameter_->text();
    remembered()["hole.depth"] = depth_->text();
    remembered()["hole.cbore_diameter"] = cboreDiameter_->text();
    remembered()["hole.cbore_depth"] = cboreDepth_->text();
    remembered()["hole.csink_diameter"] = csinkDiameter_->text();
    remembered()["hole.csink_angle"] = csinkAngle_->text();
}

// ---------------------------------------------------------------- Pattern

PatternPanel::PatternPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent, bool circular)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Pattern"),
                             tr("Repeat an earlier Extrude, Revolve or Hole around an axis (ring) or in rows and "
                                "columns (grid). Each copy adds or removes material the same way.")));
    source_ = new QComboBox(this);
    kind_ = combo(this, {{tr("Ring (around an axis)"), "circular"}, {tr("Grid (rows and columns)"), "rectangular"}});
    addRow(tr("Step to repeat"), source_);
    addRow(tr("Kind"), kind_);

    circular_ = new QWidget(this);
    auto* cl = new QVBoxLayout(circular_);
    cl->setContentsMargins(0, 0, 0, 0);
    axis_ = combo(circular_, kAxes);
    count_ = new ExprField(doc, jig::Dim::none(), circular_);
    angle_ = new ExprField(doc, jig::Dim::ang(), circular_);
    for (auto [label, w] : {std::pair<QString, QWidget*> {tr("Axis"), axis_},
                            {tr("Number of copies (including the original)"), count_},
                            {tr("Total angle"), angle_}}) {
        cl->addWidget(new QLabel("<b>" + label + "</b>", circular_));
        cl->addWidget(w);
    }
    grid_ = new QWidget(this);
    auto* gl = new QVBoxLayout(grid_);
    gl->setContentsMargins(0, 0, 0, 0);
    dir1_ = combo(grid_, {{tr("X"), "X"}, {tr("Y"), "Y"}, {tr("Z"), "Z"}});
    dir2_ = combo(grid_, {{tr("Y"), "Y"}, {tr("X"), "X"}, {tr("Z"), "Z"}});
    count1_ = new ExprField(doc, jig::Dim::none(), grid_);
    spacing1_ = new ExprField(doc, jig::Dim::len(), grid_);
    count2_ = new ExprField(doc, jig::Dim::none(), grid_);
    spacing2_ = new ExprField(doc, jig::Dim::len(), grid_);
    for (auto [label, w] : {std::pair<QString, QWidget*> {tr("First direction"), dir1_},
                            {tr("Copies along it"), count1_},
                            {tr("Spacing"), spacing1_},
                            {tr("Second direction"), dir2_},
                            {tr("Copies along it (1 = a single row)"), count2_},
                            {tr("Spacing"), spacing2_}}) {
        gl->addWidget(new QLabel("<b>" + label + "</b>", grid_));
        gl->addWidget(w);
    }
    form_->addWidget(circular_);
    form_->addWidget(grid_);

    const auto before = stateBefore();
    const int limit = insertIndex();
    for (int i = 0; i < limit; ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (before->tools.count(f.id)) {
            source_->addItem(q(f.name), q(f.id));
        }
    }
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    auto value = [&](const char* key, const char* def) { return q(edit ? edit->params.value(key, std::string(def)) : def); };
    if (edit) {
        select(source_, edit->params.value("feature", std::string {}));
        select(kind_, edit->params.value("kind", std::string("circular")));
    }
    else {
        source_->setCurrentIndex(source_->count() - 1);
        select(kind_, circular ? "circular" : "rectangular");
    }
    select(axis_, value("axis", "Z").toStdString());
    count_->setText(value("count", "6"));
    angle_->setText(value("angle", "360 deg"));
    select(dir1_, value("dir1", "X").toStdString());
    select(dir2_, value("dir2", "Y").toStdString());
    count1_->setText(value("count1", "3"));
    spacing1_->setText(value("spacing1", doc_.defaultLength(10, 0.5).c_str()));
    count2_->setText(value("count2", "1"));
    spacing2_->setText(value("spacing2", doc_.defaultLength(10, 0.5).c_str()));
    for (auto* c : {source_, kind_, axis_, dir1_, dir2_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            schedulePreview();
        });
    }
    for (auto* e : {count_, angle_, count1_, spacing1_, count2_, spacing2_}) {
        connect(e, &ExprField::changed, this, [this] { schedulePreview(); });
    }
    updateVisibility();
    if (source_->count() == 0) {
        status_->setText("<b>✕ " + tr("There is no Extrude, Revolve or Hole to repeat yet.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

void PatternPanel::updateVisibility()
{
    const bool circ = current(kind_) == "circular";
    circular_->setVisible(circ);
    grid_->setVisible(!circ);
}

jig::Feature PatternPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "p", tr("Pattern"));
    f.kind = jig::FeatureKind::Pattern;
    f.params = {{"feature", current(source_)}, {"kind", current(kind_)}};
    if (current(kind_) == "circular") {
        f.params["axis"] = current(axis_);
        f.params["count"] = text(count_, "6");
        f.params["angle"] = text(angle_, "360 deg");
    }
    else {
        f.params["dir1"] = current(dir1_);
        f.params["count1"] = text(count1_, "3");
        f.params["spacing1"] = text(spacing1_, "10 mm");
        f.params["dir2"] = current(dir2_);
        f.params["count2"] = text(count2_, "1");
        f.params["spacing2"] = text(spacing2_, "10 mm");
    }
    return f;
}

QString PatternPanel::title() const
{
    return tr("pattern");
}

// ---------------------------------------------------------------- Mirror

MirrorPanel::MirrorPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Mirror"),
                             tr("Copy a body across an origin plane. Model one half up to the plane, then mirror "
                                "and join for a symmetric part.")));
    body_ = new QComboBox(this);
    plane_ = combo(this, {{tr("Side plane (YZ): flips left and right"), "YZ"},
                          {tr("Front plane (XZ): flips front and back"), "XZ"},
                          {tr("Floor plane (XY): flips up and down"), "XY"}});
    op_ = combo(this, {{tr("Join to the original"), "join"}, {tr("New body"), "new"}});
    addRow(tr("Body"), body_);
    addRow(tr("Mirror plane"), plane_);
    addRow(tr("Result"), op_);
    const auto before = stateBefore();
    for (auto& [id, b] : before->bodies) {
        const jig::Feature* bf = doc_.feature(id);
        body_->addItem(bf ? q(bf->name) : q(id), q(id));
    }
    if (const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_)) {
        select(body_, edit->params.value("body", std::string {}));
        select(plane_, edit->params.value("plane", std::string("YZ")));
        select(op_, edit->params.value("op", std::string("join")));
    }
    for (auto* c : {body_, plane_, op_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    }
    if (body_->count() == 0) {
        status_->setText("<b>✕ " + tr("There is no body to mirror yet.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

jig::Feature MirrorPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "m", tr("Mirror"));
    f.kind = jig::FeatureKind::Mirror;
    f.params = {{"body", current(body_)}, {"plane", current(plane_)}, {"op", current(op_)}};
    return f;
}

QString MirrorPanel::title() const
{
    return tr("mirror");
}

// ---------------------------------------------------------------- Plane

PlanePanel::PlanePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent, const std::string& kind)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Construction plane"),
                             tr("A flat reference to sketch on. Pick a plane from the list or click a flat face "
                                "in the view; for a tilted plane, click a straight edge to turn around.")));
    kind_ = combo(this, {{tr("Offset: parallel, at a distance"), "offset"},
                         {tr("At an angle: turned around an axis or edge"), "angle"},
                         {tr("Midplane: halfway between two faces"), "midplane"}});
    base_ = new QComboBox(this);
    base2_ = new QComboBox(this);
    axis_ = combo(this, {{tr("X axis"), "\"X\""}, {tr("Y axis"), "\"Y\""}, {tr("Z axis"), "\"Z\""}});
    distance_ = new ExprField(doc, jig::Dim::len(), this);
    angle_ = new ExprField(doc, jig::Dim::ang(), this);
    addRow(tr("Kind"), kind_);
    addRow(tr("From"), base_);
    addRow(tr("And"), base2_);
    addRow(tr("Distance"), distance_);
    addRow(tr("Turn around"), axis_);
    addRow(tr("Angle"), angle_);
    for (auto* c : {base_, base2_}) {
        c->addItem(tr("Floor (XY)"), "\"XY\"");
        c->addItem(tr("Front (XZ)"), "\"XZ\"");
        c->addItem(tr("Side (YZ)"), "\"YZ\"");
    }
    const auto before = stateBefore();
    const int limit = insertIndex();
    for (int i = 0; i < limit; ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Plane && before->planes.count(f.id)) {
            for (auto* c : {base_, base2_}) {
                c->addItem(q(f.name), q(nlohmann::json(f.id).dump()));
            }
        }
    }
    base2_->setCurrentIndex(0);
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    auto selectJson = [&](QComboBox* c, const nlohmann::json& v, const QString& label) {
        const QString key = q(v.dump());
        int i = c->findData(key);
        if (i < 0) {
            c->addItem(label, key);
            i = c->count() - 1;
        }
        c->setCurrentIndex(i);
    };
    auto labelOf = [&](const nlohmann::json& v) {
        if (v.is_object() && v.contains("face")) {
            return tr("Face %1").arg(q(v["face"].value("name", std::string("(picked)"))));
        }
        if (v.is_object() && v.contains("edge")) {
            return tr("Edge %1").arg(q(v["edge"].value("name", std::string("(picked)"))));
        }
        return q(v.is_string() ? v.get<std::string>() : v.dump());
    };
    if (edit) {
        const auto& p = edit->params;
        select(kind_, p.value("kind", std::string("offset")));
        if (p.contains("base")) {
            selectJson(base_, p["base"], labelOf(p["base"]));
        }
        if (p.contains("base2")) {
            selectJson(base2_, p["base2"], labelOf(p["base2"]));
        }
        if (p.contains("axis")) {
            selectJson(axis_, p["axis"], labelOf(p["axis"]));
        }
        distance_->setText(q(p.value("distance", std::string("10 mm"))));
        angle_->setText(q(p.value("angle", std::string("45 deg"))));
    }
    else {
        select(kind_, kind);
        distance_->setText(q(doc_.defaultLength(10, 0.5)));
        angle_->setText("45 deg");
        if (kind == "midplane") {
            base2_->setCurrentIndex(base2_->findData("\"XY\""));
        }
    }
    for (auto* c : {kind_, base_, base2_, axis_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            schedulePreview();
        });
    }
    for (auto* e : {distance_, angle_}) {
        connect(e, &ExprField::changed, this, [this] { schedulePreview(); });
    }
    if (view_) {
        view_->setPickMode(PickMode::Any);
        connect(view_.data(), &Viewport::faceClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useFace(b.toStdString(), n.toStdString()); });
        connect(view_.data(), &Viewport::edgeClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useEdge(b.toStdString(), n.toStdString()); });
    }
    updateVisibility();
    schedulePreview();
}

PlanePanel::~PlanePanel()
{
    if (view_) {
        view_->setPickMode(PickMode::Faces);
        view_->setOverlay({});
    }
}

void PlanePanel::useFace(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return;  // a face of the preview itself, not of the model before this step
    }
    auto hits = it->second->findFaces(name);
    if (hits.empty()) {
        return;
    }
    if (jig::measureItem(it->second->face(hits[0])).kind != "flat face") {
        status_->setText("<b>✕ " + tr("That face is curved. Click a flat face.") + "</b>");
        return;
    }
    nlohmann::json ref {{"face", it->second->faceRef(body, hits[0]).toJson()}};
    QComboBox* target = current(kind_) == "midplane" && nextIsSecond_ ? base2_ : base_;
    if (current(kind_) == "midplane") {
        nextIsSecond_ = !nextIsSecond_;
    }
    const QString key = q(ref.dump());
    int i = target->findData(key);
    if (i < 0) {
        target->addItem(tr("Face %1").arg(q(name)), key);
        i = target->count() - 1;
    }
    target->setCurrentIndex(i);
}

void PlanePanel::useEdge(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return;
    }
    auto hits = it->second->findEdges(name);
    if (hits.empty()) {
        return;
    }
    if (jig::measureItem(it->second->edge(hits[0])).kind != "straight edge") {
        status_->setText("<b>✕ " + tr("A plane can only turn around a straight edge.") + "</b>");
        return;
    }
    select(kind_, "angle");
    nlohmann::json ref {{"edge", it->second->edgeRef(body, hits[0]).toJson()}};
    const QString key = q(ref.dump());
    int i = axis_->findData(key);
    if (i < 0) {
        axis_->addItem(tr("Edge %1").arg(q(name)), key);
        i = axis_->count() - 1;
    }
    axis_->setCurrentIndex(i);
}

void PlanePanel::updateVisibility()
{
    const std::string k = current(kind_);
    auto show = [this](QWidget* w, bool on) {
        w->setVisible(on);
        const int i = form_->indexOf(w);
        if (i > 0) {
            if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
                item->widget()->setVisible(on);
            }
        }
    };
    show(base2_, k == "midplane");
    show(distance_, k == "offset");
    show(axis_, k == "angle");
    show(angle_, k == "angle");
}

jig::Feature PlanePanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "pl", tr("Plane"));
    f.kind = jig::FeatureKind::Plane;
    auto json = [](const QComboBox* c) {
        const std::string d = c->currentData().toString().toStdString();
        return d.empty() ? nlohmann::json("XY") : nlohmann::json::parse(d, nullptr, false);
    };
    const std::string k = current(kind_);
    f.params = {{"kind", k}, {"base", json(base_)}};
    if (k == "offset") {
        f.params["distance"] = text(distance_, "10 mm");
    }
    else if (k == "angle") {
        f.params["axis"] = json(axis_);
        f.params["angle"] = text(angle_, "45 deg");
    }
    else {
        f.params["base2"] = json(base2_);
    }
    return f;
}

void PlanePanel::previewed(const jig::FeatureOutcome& o)
{
    if (!view_) {
        return;
    }
    SceneOverlay ov;
    const jig::Feature f = draft();
    if (o.after && o.after->planes.count(f.id)) {
        const gp_Ax3& fr = o.after->planes.at(f.id);
        float s = 30;
        for (auto& [id, b] : o.after->bodies) {
            auto mp = jig::massProperties(b->shape());
            s = std::max({s, float(mp.xmax - mp.xmin) * 0.6f, float(mp.ymax - mp.ymin) * 0.6f, float(mp.zmax - mp.zmin) * 0.6f});
        }
        auto at = [&](double u, double v) {
            const gp_XYZ p = fr.Location().XYZ() + fr.XDirection().XYZ() * u + fr.YDirection().XYZ() * v;
            return QVector3D(float(p.X()), float(p.Y()), float(p.Z()));
        };
        const QVector3D c[4] = {at(-s, -s), at(s, -s), at(s, s), at(-s, s)};
        QColor fill(222, 150, 40, 80), edge(222, 150, 40, 255);
        for (int k : {0, 1, 2, 0, 2, 3}) {
            ov.tris.push_back({c[k], fill});
        }
        for (int k = 0; k < 4; ++k) {
            ov.lines.push_back({c[k], edge});
            ov.lines.push_back({c[(k + 1) % 4], edge});
        }
    }
    view_->setOverlay(std::move(ov));
}

QString PlanePanel::title() const
{
    return tr("construction plane");
}

// ---------------------------------------------------------------- body list helpers

namespace
{
QString bodyLabel(const jig::Document& doc, const std::string& id)
{
    const jig::Feature* f = doc.feature(id);
    return f ? q(f->name) : q(id);
}

void fillBodyList(QListWidget* list, const jig::Document& doc, const jig::ModelState& st, const std::set<std::string>& checked,
                  const std::string& exclude = {})
{
    list->blockSignals(true);
    list->clear();
    for (auto& [id, b] : st.bodies) {
        if (id == exclude) {
            continue;
        }
        auto* it = new QListWidgetItem(bodyLabel(doc, id), list);
        it->setData(Qt::UserRole, q(id));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(checked.count(id) ? Qt::Checked : Qt::Unchecked);
    }
    list->blockSignals(false);
}

nlohmann::json checkedBodies(const QListWidget* list)
{
    nlohmann::json out = nlohmann::json::array();
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() == Qt::Checked) {
            out.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
        }
    }
    return out;
}

std::set<std::string> toSet(const nlohmann::json& v)
{
    std::set<std::string> out;
    if (v.is_array()) {
        for (auto& x : v) {
            out.insert(x.get<std::string>());
        }
    }
    return out;
}
}  // namespace

// ---------------------------------------------------------------- Combine

CombinePanel::CombinePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Combine"),
                             tr("Join bodies together, cut one out of another, or keep only where they overlap.")));
    target_ = new QComboBox(this);
    tools_ = new QListWidget(this);
    tools_->setMaximumHeight(120);
    op_ = combo(this, {{tr("Join: add them together"), "join"},
                       {tr("Cut: remove the others from it"), "cut"},
                       {tr("Intersect: keep only the overlap"), "intersect"}});
    keep_ = new QCheckBox(tr("Keep the other bodies too"), this);
    addRow(tr("Body to change"), target_);
    addRow(tr("With"), tools_);
    addRow(tr("Operation"), op_);
    form_->addWidget(keep_);
    const auto before = stateBefore();
    for (auto& [id, b] : before->bodies) {
        target_->addItem(bodyLabel(doc_, id), q(id));
    }
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    if (edit) {
        select(target_, edit->params.value("target", std::string {}));
        select(op_, edit->params.value("op", std::string("join")));
        keep_->setChecked(edit->params.value("keep_tools", false));
    }
    fillTools();
    if (edit) {
        fillBodyList(tools_, doc_, *before, toSet(edit->params.value("tools", nlohmann::json::array())), current(target_));
    }
    connect(target_, &QComboBox::currentIndexChanged, this, [this] {
        fillTools();
        schedulePreview();
    });
    connect(op_, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    connect(tools_, &QListWidget::itemChanged, this, [this] { schedulePreview(); });
    connect(keep_, &QCheckBox::toggled, this, [this] { schedulePreview(); });
    if (before->bodies.size() < 2) {
        status_->setText("<b>✕ " + tr("Combine needs at least two bodies.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

void CombinePanel::fillTools()
{
    // by default every other body is ticked
    std::set<std::string> all;
    const auto before = stateBefore();  // keep it alive while iterating
    for (auto& [id, b] : before->bodies) {
        all.insert(id);
    }
    fillBodyList(tools_, doc_, *before, all, current(target_));
}

jig::Feature CombinePanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "cb", tr("Combine"));
    f.kind = jig::FeatureKind::Combine;
    f.params = {{"target", current(target_)}, {"tools", checkedBodies(tools_)}, {"op", current(op_)},
                {"keep_tools", keep_->isChecked()}};
    return f;
}

QString CombinePanel::title() const
{
    return tr("combine");
}

// ---------------------------------------------------------------- Split

SplitPanel::SplitPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Split body"),
                             tr("Cut a body in two with a plane, for example to print a big part in pieces. Pick "
                                "a plane or click a flat face.")));
    body_ = new QComboBox(this);
    plane_ = new QComboBox(this);
    plane_->addItem(tr("Side (YZ)"), "\"YZ\"");
    plane_->addItem(tr("Front (XZ)"), "\"XZ\"");
    plane_->addItem(tr("Floor (XY)"), "\"XY\"");
    addRow(tr("Body"), body_);
    addRow(tr("Split with"), plane_);
    const auto before = stateBefore();
    for (auto& [id, b] : before->bodies) {
        body_->addItem(bodyLabel(doc_, id), q(id));
    }
    for (int i = 0; i < insertIndex(); ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Plane && before->planes.count(f.id)) {
            plane_->addItem(q(f.name), q(nlohmann::json(f.id).dump()));
        }
    }
    if (const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_)) {
        select(body_, edit->params.value("body", std::string {}));
        const QString key = q(edit->params.value("plane", nlohmann::json("YZ")).dump());
        if (plane_->findData(key) < 0) {
            plane_->addItem(tr("Picked face"), key);
        }
        plane_->setCurrentIndex(plane_->findData(key));
    }
    for (auto* c : {body_, plane_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    }
    if (view_) {
        view_->setPickMode(PickMode::Faces);
        connect(view_.data(), &Viewport::faceClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useFace(b.toStdString(), n.toStdString()); });
    }
    if (body_->count() == 0) {
        status_->setText("<b>✕ " + tr("There is no body to split yet.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

SplitPanel::~SplitPanel() = default;

void SplitPanel::useFace(const std::string& body, const std::string& name)
{
    auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return;
    }
    auto hits = it->second->findFaces(name);
    if (hits.empty() || jig::measureItem(it->second->face(hits[0])).kind != "flat face") {
        status_->setText("<b>✕ " + tr("Click a flat face to split along it.") + "</b>");
        return;
    }
    const QString key = q(nlohmann::json {{"face", it->second->faceRef(body, hits[0]).toJson()}}.dump());
    if (plane_->findData(key) < 0) {
        plane_->addItem(tr("Face %1").arg(q(name)), key);
    }
    plane_->setCurrentIndex(plane_->findData(key));
}

jig::Feature SplitPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "sp", tr("Split"));
    f.kind = jig::FeatureKind::Split;
    const std::string d = plane_->currentData().toString().toStdString();
    f.params = {{"body", current(body_)}, {"plane", d.empty() ? nlohmann::json("YZ") : nlohmann::json::parse(d, nullptr, false)}};
    return f;
}

QString SplitPanel::title() const
{
    return tr("split");
}

// ---------------------------------------------------------------- Move

MovePanel::MovePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Move / copy"),
                             tr("Shift bodies along X, Y and Z and turn them around an axis through the origin. "
                                "Tick Copy to keep the original where it is.")));
    bodies_ = new QListWidget(this);
    bodies_->setMaximumHeight(120);
    const char* names[3] = {"X", "Y", "Z"};
    addRow(tr("Bodies"), bodies_);
    for (int k = 0; k < 3; ++k) {
        d_[k] = new ExprField(doc, jig::Dim::len(), this);
        addRow(tr("Move along %1").arg(names[k]), d_[k]);
    }
    axis_ = combo(this, {{tr("Z axis (up)"), "Z"}, {tr("X axis"), "X"}, {tr("Y axis"), "Y"}});
    angle_ = new ExprField(doc, jig::Dim::ang(), this);
    copy_ = new QCheckBox(tr("Copy (keep the original)"), this);
    addRow(tr("Turn around"), axis_);
    addRow(tr("Angle"), angle_);
    form_->addWidget(copy_);
    const auto before = stateBefore();
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    std::set<std::string> chosen;
    if (edit) {
        chosen = toSet(edit->params.value("bodies", nlohmann::json::array()));
    }
    else if (before->bodies.size() == 1) {
        chosen.insert(before->bodies.begin()->first);
    }
    fillBodyList(bodies_, doc_, *before, chosen);
    const char* keys[3] = {"dx", "dy", "dz"};
    for (int k = 0; k < 3; ++k) {
        d_[k]->setText(q(edit ? edit->params.value(keys[k], std::string("0 mm")) : doc_.defaultLength(0, 0)));
        connect(d_[k], &ExprField::changed, this, [this] { schedulePreview(); });
    }
    angle_->setText(q(edit ? edit->params.value("angle", std::string("0 deg")) : std::string("0 deg")));
    if (edit) {
        select(axis_, edit->params.value("axis", std::string("Z")));
        copy_->setChecked(edit->params.value("copy", false));
    }
    connect(angle_, &ExprField::changed, this, [this] { schedulePreview(); });
    connect(axis_, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    connect(bodies_, &QListWidget::itemChanged, this, [this] { schedulePreview(); });
    connect(copy_, &QCheckBox::toggled, this, [this] { schedulePreview(); });
    if (before->bodies.empty()) {
        status_->setText("<b>✕ " + tr("There is no body to move yet.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

jig::Feature MovePanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "mv", tr("Move"));
    f.kind = jig::FeatureKind::Move;
    f.params = {{"bodies", checkedBodies(bodies_)},
                {"dx", text(d_[0], "0 mm")},
                {"dy", text(d_[1], "0 mm")},
                {"dz", text(d_[2], "0 mm")},
                {"axis", current(axis_)},
                {"angle", text(angle_, "0 deg")},
                {"copy", copy_->isChecked()}};
    return f;
}

QString MovePanel::title() const
{
    return tr("move");
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
        addRow({"param" + std::to_string(n), doc_.defaultLength(10, 0.5), jig::ParamKind::Length, {}});
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
