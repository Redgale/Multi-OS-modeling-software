// SPDX-License-Identifier: GPL-3.0-or-later
#include "FeaturePanels.hpp"
#include "Translations.hpp"

#include "jig/screws.hpp"

#include <cctype>
#include <functional>
#include <map>
#include <regex>
#include <set>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTextDocumentFragment>
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

// Construction axes made before step `limit`, added to an axis list; `data` turns an
// axis id into the list's item data.
void addConstructionAxes(QComboBox* c, const jig::Document& doc, const jig::ModelState& before, int limit,
                         const std::function<QString(const std::string&)>& data)
{
    for (int i = 0; i < limit && i < int(doc.model().features.size()); ++i) {
        const auto& f = doc.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Axis && before.axes.count(f.id)) {
            c->addItem(q(f.name), data(f.id));
        }
    }
}

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
        result_->setText("✕ " + coreText(r.error().message));
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

void FeaturePanel::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && ok_->isEnabled()) {
        timer_->stop();
        accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        cancel();
        return;
    }
    ToolPanel::keyPressEvent(e);
}

void nameInputsFromLabels(QWidget* root)
{
    auto plain = [](const QLabel* l) { return QTextDocumentFragment::fromHtml(l->text()).toPlainText().trimmed(); };
    auto isInput = [](QWidget* w) {
        return qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w) ||
               qobject_cast<QAbstractItemView*>(w) || qobject_cast<ExprField*>(w);
    };
    auto name = [&](QWidget* w, const QString& text) {
        if (text.isEmpty()) {
            return;
        }
        if (w->accessibleName().isEmpty()) {
            w->setAccessibleName(text);
        }
        for (auto* e : w->findChildren<QLineEdit*>()) {  // the edit inside a size field
            if (e->accessibleName().isEmpty()) {
                e->setAccessibleName(text);
            }
        }
    };
    for (auto* l : root->findChildren<QLabel*>()) {
        if (l->buddy()) {
            name(l->buddy(), plain(l));
        }
    }
    // a label followed by an input in the same layout
    std::function<void(QLayout*)> walk = [&](QLayout* lay) {
        QLabel* pending = nullptr;
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem* it = lay->itemAt(i);
            if (it->layout()) {
                walk(it->layout());
                pending = nullptr;
                continue;
            }
            QWidget* w = it->widget();
            if (!w) {
                continue;
            }
            if (auto* l = qobject_cast<QLabel*>(w)) {
                pending = l;
                continue;
            }
            if (pending && isInput(w)) {
                name(w, plain(pending));
            }
            pending = nullptr;
        }
    };
    if (root->layout()) {
        walk(root->layout());
    }
    for (auto* child : root->findChildren<QWidget*>()) {
        if (child->layout()) {
            walk(child->layout());
        }
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
            t += "<br>⚠ " + coreText(o.status.message).toHtmlEscaped();
        }
        status_->setText(t);
    }
    else {
        QString t = "<b>✕ " + coreText(o.status.message).toHtmlEscaped() + "</b>";
        status_->setText(t);
        // sure repairs as buttons; the rest as advice
        for (const auto& fx : o.status.fixes) {
            auto it = paramFields_.find(fx.param);
            if (it == paramFields_.end()) {
                continue;
            }
            auto* b = new QPushButton(fx.param == "op" && fx.value == "new" ? tr("Make it a new body")
                                                                            : tr("Use %1").arg(q(fx.value)),
                                      fixes_);
            b->setToolTip(tr("Changes this step's setting and shows the result."));
            QWidget* field = it->second;
            const QString value = q(fx.value);
            connect(b, &QPushButton::clicked, this, [field, value] {
                if (auto* e = qobject_cast<ExprField*>(field)) {
                    e->setText(value);
                }
                else if (auto* c = qobject_cast<QComboBox*>(field)) {
                    c->setCurrentIndex(std::max(0, c->findData(value)));
                }
                else if (auto* cb = qobject_cast<QCheckBox*>(field)) {
                    cb->setChecked(value == "yes");
                }
            });
            fixes_->layout()->addWidget(b);
        }
        for (auto& sgg : o.status.suggestions) {
            auto* l = new QLabel("• " + coreText(sgg), fixes_);
            l->setWordWrap(true);
            fixes_->layout()->addWidget(l);
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
        status_->setText("<b>✕ " + coreText(r.error().message).toHtmlEscaped() + "</b>");
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

// The extrude arrow: drawn from the profile's centre along the extrude direction; drag its
// round tip to set the distance.
class ExtrudeHandle : public Interactor
{
public:
    explicit ExtrudeHandle(ExtrudePanel* p)
        : p_(p)
    {}
    bool mousePress(QMouseEvent* e) override
    {
        auto tip = p_->handleTip();
        dragging_ = tip && QLineF(*tip, e->position()).length() <= 12;
        return dragging_;
    }
    bool mouseMove(QMouseEvent* e) override
    {
        if (dragging_) {
            p_->dragHandleTo(e->position());
            return true;
        }
        auto tip = p_->handleTip();
        const bool over = tip && QLineF(*tip, e->position()).length() <= 12;
        if (over != hover_ && p_->view_) {
            hover_ = over;
            p_->view_->setCursor(over ? Qt::SizeAllCursor : Qt::ArrowCursor);
            p_->view_->update();
        }
        return false;
    }
    bool mouseRelease(QMouseEvent*) override
    {
        const bool was = dragging_;
        dragging_ = false;
        return was;
    }
    void paint(QPainter& g) override
    {
        auto tip = p_->handleTip();
        if (!tip || !p_->view_) {
            return;
        }
        const QPointF base = p_->view_->project(*p_->handleBase_);
        const QColor c(255, 170, 40);
        g.setRenderHint(QPainter::Antialiasing);
        g.setPen(QPen(c, 2.5));
        g.drawLine(base, *tip);
        // the grip: a ring that fills when the cursor is on it (shape changes, not only colour)
        g.setBrush(hover_ || dragging_ ? QBrush(c) : QBrush(Qt::NoBrush));
        g.drawEllipse(*tip, 7, 7);
        g.setPen(p_->view_->colors().text);
        g.drawText(*tip + QPointF(12, -8), p_->distance_->text());
    }

private:
    ExtrudePanel* p_;
    bool dragging_ = false, hover_ = false;
};

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
    extent_->addItem(tr("Up to a face or plane"), "to");
    to_ = new QComboBox(this);
    to_->addItem(tr("Floor (XY)"), "\"XY\"");
    to_->addItem(tr("Front (XZ)"), "\"XZ\"");
    to_->addItem(tr("Side (YZ)"), "\"YZ\"");
    toOffset_ = new ExprField(doc, jig::Dim::len(), this);
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
    addRow(tr("Up to (click a flat face)"), to_);
    addRow(tr("Beyond it by"), toOffset_);
    addRow(tr("Distance"), distance_);
    addRow(tr("Direction"), direction_);
    addRow(tr("Other side distance"), distance2_);
    addRow(tr("Taper (narrows as it goes; negative widens)"), taper_);
    form_->addWidget(thinOn_);
    addRow(tr("Wall thickness"), thin_);
    addRow(tr("Wall position"), thinSide_);
    addRow(tr("Operation"), op_);
    for (auto [param, field] : std::initializer_list<std::pair<const char*, QWidget*>> {
        {"distance", distance_}, {"taper", taper_}, {"thin", thin_}, {"op", op_}}) {
        bindParam(param, field);
    }

    // sketches available before the insertion point
    const auto before = stateBefore();
    const int limit = insertIndex();
    for (int i = 0; i < limit; ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Sketch && before->sketches.count(f.id)) {
            sketch_->addItem(q(f.name), q(f.id));
        }
        if (f.kind == jig::FeatureKind::Plane && before->planes.count(f.id)) {
            to_->addItem(q(f.name), q(nlohmann::json(f.id).dump()));
        }
    }
    toOffset_->setText("0 mm");
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    if (edit) {
        const auto& p = edit->params;
        if (p.contains("to")) {
            const QString key = q(p["to"].dump());
            if (to_->findData(key) < 0) {
                to_->addItem(p["to"].is_object() ? tr("Face %1").arg(q(p["to"].value("face", nlohmann::json::object())
                                                                         .value("name", std::string {})))
                                                 : key,
                             key);
            }
            to_->setCurrentIndex(to_->findData(key));
        }
        toOffset_->setText(q(p.value("to_offset", std::string("0 mm"))));
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
    for (auto* c : {extent_, direction_, op_, to_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] { schedulePreview(); });
    }
    connect(toOffset_, &ExprField::changed, this, [this] { schedulePreview(); });
    connect(extent_, &QComboBox::currentIndexChanged, this, [this] {
        distance_->setEnabled(extent_->currentData().toString() == "distance");
        updateVisibility();
    });
    if (view_) {
        connect(view_.data(), &Viewport::faceClicked, this, [this](const QString& b, const QString& n, Qt::KeyboardModifiers) {
            if (extent_->currentData().toString() == "to") {
                useFace(b.toStdString(), n.toStdString());
            }
        });
    }
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
    if (view_) {
        handle_ = std::make_unique<ExtrudeHandle>(this);
        view_->setInteractor(handle_.get());
    }
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

ExtrudePanel::~ExtrudePanel()
{
    if (view_ && view_->interactor() == handle_.get()) {
        view_->setInteractor(nullptr);
    }
}

std::optional<QPointF> ExtrudePanel::handleTip() const
{
    if (!view_ || !handleBase_ || extent_->currentData().toString() != "distance" || !distance_->valid()) {
        return std::nullopt;
    }
    return view_->project(*handleBase_ + handleDir_ * float(distance_->value()));
}

void ExtrudePanel::dragHandleTo(const QPointF& screen)
{
    if (!view_ || !handleBase_) {
        return;
    }
    // how many pixels one millimetre along the arrow covers on screen
    const QPointF s0 = view_->project(*handleBase_), s1 = view_->project(*handleBase_ + handleDir_ * 10.0f);
    const QPointF axis = s1 - s0;
    const double len2 = QPointF::dotProduct(axis, axis);
    if (len2 < 1e-6) {
        return;  // looking straight down the arrow: use the field instead
    }
    double d = QPointF::dotProduct(screen - s0, axis) / len2 * 10.0;
    const bool inch = doc_.units() == jig::LengthUnit::Inch || doc_.units() == jig::LengthUnit::Foot;
    const double step = inch ? 25.4 / 32 : 0.5;  // snaps: 1/32 in or 0.5 mm
    d = std::round(d / step) * step;
    if (std::abs(d) < step) {
        d = d < 0 ? -step : step;
    }
    distance_->setText(QString::fromStdString(doc_.fmtLength(d)));
}

void ExtrudePanel::previewed(const jig::FeatureOutcome&)
{
    // the arrow starts at the centre of the first chosen area, on the sketch plane
    handleBase_.reset();
    const auto before = stateBefore();
    auto it = before->sketches.find(sketch_->currentData().toString().toStdString());
    if (it != before->sketches.end() && !it->second->profiles.regions.empty()) {
        const jig::Profile* prof = &it->second->profiles.regions.front();
        for (int i = 0; i < profiles_->count(); ++i) {
            if (profiles_->item(i)->checkState() == Qt::Checked) {
                for (auto& r : it->second->profiles.regions) {
                    if (q(r.key) == profiles_->item(i)->data(Qt::UserRole).toString()) {
                        prof = &r;
                    }
                }
                break;
            }
        }
        GProp_GProps g;
        BRepGProp::SurfaceProperties(prof->face, g);
        const gp_Pnt c = g.CentreOfMass();
        gp_Dir n = it->second->frame.Direction();
        if (direction_->currentData().toString() == "reverse") {
            n.Reverse();
        }
        handleBase_ = QVector3D(float(c.X()), float(c.Y()), float(c.Z()));
        handleDir_ = QVector3D(float(n.X()), float(n.Y()), float(n.Z()));
    }
    if (view_) {
        view_->update();
    }
}

void ExtrudePanel::useFace(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return;
    }
    auto hits = it->second->findFaces(name);
    if (hits.empty()) {
        return;
    }
    if (jig::measureItem(it->second->face(hits[0])).kind != "flat face") {
        status_->setText("<b>✕ " + tr("Click a flat face to extrude up to.") + "</b>");
        return;
    }
    extent_->setCurrentIndex(extent_->findData("to"));
    const QString key = q(nlohmann::json {{"face", it->second->faceRef(body, hits[0]).toJson()}}.dump());
    int i = to_->findData(key);
    if (i < 0) {
        to_->addItem(tr("Face %1").arg(q(name)), key);
        i = to_->count() - 1;
    }
    to_->setCurrentIndex(i);
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
    const bool upTo = extent_->currentData().toString() == "to";
    show(to_, upTo);
    show(toOffset_, upTo);
    show(distance_, !upTo);
    show(direction_, !upTo);
    show(distance2_, !upTo && direction_->currentData().toString() == "two");
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
        // open curves can still be extruded as a wall
        const bool open = !it->second->solved.openEndpoints().empty();
        if (open) {
            auto* item = new QListWidgetItem(tr("Open curves, as a wall (needs a wall thickness)"), profiles_);
            item->setData(Qt::UserRole, "open");
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(it->second->profiles.regions.empty() ? Qt::Checked : Qt::Unchecked);
            if (it->second->profiles.regions.empty()) {
                thinOn_->setChecked(true);
            }
        }
        else if (it->second->profiles.regions.empty()) {
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
    bool openChecked = false;
    for (int i = 0; i < profiles_->count(); ++i) {
        openChecked |= profiles_->item(i)->data(Qt::UserRole).toString() == "open" &&
                       profiles_->item(i)->checkState() == Qt::Checked;
    }
    if (openChecked) {
        p["profiles"] = "open";  // a wall along the open curves (the closed areas are not used)
        if (!thinOn_->isChecked()) {
            p["thin"] = thin_->text().isEmpty() ? std::string("2 mm") : thin_->text().toStdString();
        }
    }
    p["extent"] = extent_->currentData().toString().toStdString();
    if (p["extent"] == "to") {
        const std::string d = to_->currentData().toString().toStdString();
        p["to"] = nlohmann::json::parse(d.empty() ? "\"XY\"" : d, nullptr, false);
        if (toOffset_->valid() && std::abs(toOffset_->value()) > 1e-12) {
            p["to_offset"] = toOffset_->text().toStdString();
        }
    }
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
    bindParam(chamfer ? "distance" : "radius", radius_);
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
    bindParam("thickness", thickness_);
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
    bindParam("op", op_);
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
        else if (ax.contains("axis")) {
            select(axis_, "ax:" + ax.value("axis", std::string {}));
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
    addConstructionAxes(axis_, doc_, *stateBefore(), insertIndex(), [](const std::string& id) { return q("ax:" + id); });
    axis_->setCurrentIndex(firstConstruction >= 0 ? firstConstruction : axis_->findData("Z"));
    axis_->blockSignals(false);
}

jig::Feature RevolvePanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "r", tr("Revolve"));
    f.kind = jig::FeatureKind::Revolve;
    const std::string ax = current(axis_);
    nlohmann::json axis = ax.rfind("line:", 0) == 0 ? nlohmann::json {{"line", std::stoi(ax.substr(5))}}
                        : ax.rfind("ax:", 0) == 0   ? nlohmann::json {{"axis", ax.substr(3)}}
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
    // screw sizes: the design's own system first
    const bool usFirst = doc.units() == jig::LengthUnit::Inch || doc.units() == jig::LengthUnit::Foot;
    size_ = new QComboBox(this);
    size_->addItem(tr("Custom (type the sizes)"), QString());
    for (bool inch : {usFirst, !usFirst}) {
        for (auto& sz : jig::screwSizes()) {
            if (sz.inch == inch) {
                size_->addItem(tr("%1 screw").arg(q(sz.name)), q(sz.name));
            }
        }
    }
    fit_ = combo(this, {{tr("Close (snug, accurate)"), "close"},
                        {tr("Normal"), "normal"},
                        {tr("Loose (easy to line up)"), "loose"}});
    bottom_ = combo(this, {{tr("Flat bottom"), "flat"}, {tr("Drill point (118°)"), "118 deg"}});
    diameter_ = new ExprField(doc, jig::Dim::len(), this);
    depth_ = new ExprField(doc, jig::Dim::len(), this);
    cboreDiameter_ = new ExprField(doc, jig::Dim::len(), this);
    cboreDepth_ = new ExprField(doc, jig::Dim::len(), this);
    csinkDiameter_ = new ExprField(doc, jig::Dim::len(), this);
    csinkAngle_ = new ExprField(doc, jig::Dim::ang(), this);
    addRow(tr("Sketch with hole points"), sketch_);
    form_->addWidget(points_);
    addRow(tr("Type"), type_);
    addRow(tr("Screw size"), size_);
    addRow(tr("Fit"), fit_);
    addRow(tr("Diameter"), diameter_);
    bindParam("diameter", diameter_);
    addRow(tr("Extent"), extent_);
    addRow(tr("Depth"), depth_);
    addRow(tr("Bottom"), bottom_);
    addRow(tr("Counterbore diameter"), cboreDiameter_);
    bindParam("cbore_diameter", cboreDiameter_);
    addRow(tr("Counterbore depth"), cboreDepth_);
    addRow(tr("Countersink diameter"), csinkDiameter_);
    addRow(tr("Countersink angle"), csinkAngle_);
    auto* tip = new QLabel(tr("Pick a screw size to fill in the clearance hole (and the counterbore or countersink "
                              "for its head). 3D-printed holes often come out small: use Loose, or Custom and add "
                              "0.2–0.4 mm."),
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
    select(size_, value("size", "").toStdString());
    select(fit_, value("fit", "normal").toStdString());
    select(bottom_, value("tip", "flat").toStdString());
    diameter_->setText(value("diameter", doc_.defaultLength(3.4, 0.13).c_str()));
    depth_->setText(value("depth", doc_.defaultLength(10, 0.375).c_str()));
    cboreDiameter_->setText(value("cbore_diameter", doc_.defaultLength(6.5, 0.25).c_str()));
    cboreDepth_->setText(value("cbore_depth", doc_.defaultLength(3.5, 0.14).c_str()));
    csinkDiameter_->setText(value("csink_diameter", doc_.defaultLength(6.5, 0.25).c_str()));
    csinkAngle_->setText(value("csink_angle", "90 deg"));
    fillFromSize();
    for (auto* c : {sketch_, type_, extent_, size_, fit_, bottom_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            fillFromSize();
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
    show(bottom_, current(extent_) == "distance");
    show(fit_, !current(size_).empty());
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
                {"depth", text(depth_, "10 mm")}};
    if (current(extent_) == "distance" && current(bottom_) != "flat") {
        f.params["tip"] = current(bottom_);
    }
    if (!current(size_).empty()) {  // the table decides the sizes; they are not stored twice
        f.params["size"] = current(size_);
        f.params["fit"] = current(fit_);
        return f;
    }
    f.params["diameter"] = text(diameter_, "3.4 mm");
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

void HolePanel::fillFromSize()
{
    const std::string size = current(size_);
    const bool custom = size.empty();
    for (ExprField* e : {diameter_, cboreDiameter_, cboreDepth_, csinkDiameter_, csinkAngle_}) {
        e->setEnabled(custom);
    }
    if (custom) {
        return;
    }
    auto s = jig::screwSize(size);
    auto fit = jig::holeFitFromString(current(fit_));
    if (!s || !fit) {
        return;
    }
    auto d = jig::clearanceDiameter(size, *fit);
    auto shown = [this](double mm) { return QString::fromStdString(doc_.fmtLength(mm)); };
    diameter_->setText(d ? shown(d.value()) : tr("no %1 fit in the table").arg(fit_->currentText()));
    cboreDiameter_->setText(shown(s->cboreDiameter));
    cboreDepth_->setText(shown(s->cboreDepth));
    csinkDiameter_->setText(shown(s->csinkDiameter));
    csinkAngle_->setText(QString("%1 deg").arg(s->csinkAngleDeg));
}

void HolePanel::rememberValues()
{
    remembered()["hole.type"] = q(current(type_));
    remembered()["hole.size"] = q(current(size_));
    remembered()["hole.fit"] = q(current(fit_));
    remembered()["hole.tip"] = q(current(bottom_));
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
                             tr("Repeat an earlier Extrude, Revolve or Hole, or a whole body, around an axis (ring) "
                                "or in rows and columns (grid). Each copy of a step adds or removes material the "
                                "same way.")));
    source_ = new QComboBox(this);
    kind_ = combo(this, {{tr("Ring (around an axis)"), "circular"}, {tr("Grid (rows and columns)"), "rectangular"}});
    bodyOp_ = combo(this, {{tr("Separate bodies"), "new"}, {tr("Join into one body"), "join"}});
    addRow(tr("What to repeat"), source_);
    addRow(tr("Copies of a body"), bodyOp_);
    addRow(tr("Kind"), kind_);

    circular_ = new QWidget(this);
    auto* cl = new QVBoxLayout(circular_);
    cl->setContentsMargins(0, 0, 0, 0);
    axis_ = combo(circular_, kAxes);
    addConstructionAxes(axis_, doc_, *stateBefore(), insertIndex(), [](const std::string& id) { return q(id); });
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
    for (auto* c : {dir1_, dir2_}) {
        addConstructionAxes(c, doc_, *stateBefore(), insertIndex(), [](const std::string& id) { return q(id); });
    }
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
    for (auto& [id, b] : before->bodies) {  // whole bodies first, then the steps
        const jig::Feature* bf = doc_.feature(id);
        source_->addItem(tr("Body: %1").arg(bf ? q(bf->name) : q(id)), q("body:" + id));
    }
    for (int i = 0; i < limit; ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (before->tools.count(f.id)) {
            source_->addItem(q(f.name), q(f.id));
        }
    }
    const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_);
    auto value = [&](const char* key, const char* def) { return q(edit ? edit->params.value(key, std::string(def)) : def); };
    if (edit) {
        if (edit->params.contains("body")) {
            select(source_, "body:" + edit->params.value("body", std::string {}));
            select(bodyOp_, edit->params.value("op", std::string("new")));
        }
        else {
            select(source_, edit->params.value("feature", std::string {}));
        }
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
    for (auto* c : {source_, kind_, axis_, dir1_, dir2_, bodyOp_}) {
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
        status_->setText("<b>✕ " + tr("There is no body or step to repeat yet.") + "</b>");
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
    const bool body = current(source_).rfind("body:", 0) == 0;
    bodyOp_->setVisible(body);
    if (const int i = form_->indexOf(bodyOp_); i > 0) {
        if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
            item->widget()->setVisible(body);
        }
    }
}

jig::Feature PatternPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "p", tr("Pattern"));
    f.kind = jig::FeatureKind::Pattern;
    const std::string src = current(source_);
    if (src.rfind("body:", 0) == 0) {
        f.params = {{"body", src.substr(5)}, {"op", current(bodyOp_)}, {"kind", current(kind_)}};
    }
    else {
        f.params = {{"feature", src}, {"kind", current(kind_)}};
    }
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
                             tr("Copy a body, or one step (an extrude, a hole), across a plane. Model one half up "
                                "to the plane, then mirror and join for a symmetric part. Click a flat face to "
                                "mirror across it.")));
    body_ = new QComboBox(this);
    plane_ = combo(this, {{tr("Side plane (YZ): flips left and right"), "YZ"},
                          {tr("Front plane (XZ): flips front and back"), "XZ"},
                          {tr("Floor plane (XY): flips up and down"), "XY"}});
    op_ = combo(this, {{tr("Join to the original"), "join"}, {tr("New body"), "new"}});
    addRow(tr("What to mirror"), body_);
    addRow(tr("Mirror plane"), plane_);
    addRow(tr("Result"), op_);
    const auto before = stateBefore();
    for (auto& [id, b] : before->bodies) {
        const jig::Feature* bf = doc_.feature(id);
        body_->addItem(bf ? q(bf->name) : q(id), q(id));
    }
    for (int i = 0; i < insertIndex(); ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (before->tools.count(f.id)) {
            body_->addItem(tr("Step: %1").arg(q(f.name)), q("step:" + f.id));
        }
        if (f.kind == jig::FeatureKind::Plane && before->planes.count(f.id)) {
            plane_->addItem(q(f.name), q(f.id));
        }
    }
    if (const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_)) {
        const auto& p = edit->params;
        select(body_, p.contains("feature") ? "step:" + p.value("feature", std::string {}) : p.value("body", std::string {}));
        const auto pl = p.value("plane", nlohmann::json("YZ"));
        if (pl.is_string()) {
            select(plane_, pl.get<std::string>());
        }
        else {
            plane_->addItem(tr("Face %1").arg(q(pl.value("face", nlohmann::json::object()).value("name", std::string {}))),
                            q(pl.dump()));
            plane_->setCurrentIndex(plane_->count() - 1);
        }
        select(op_, p.value("op", std::string("join")));
    }
    auto sync = [this] {
        const bool step = current(body_).rfind("step:", 0) == 0;  // a step does what it did: no join/new choice
        op_->setVisible(!step);
        if (const int i = form_->indexOf(op_); i > 0) {
            if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
                item->widget()->setVisible(!step);
            }
        }
    };
    sync();
    for (auto* c : {body_, plane_, op_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this, sync] {
            sync();
            schedulePreview();
        });
    }
    if (view_) {
        connect(view_.data(), &Viewport::faceClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useFace(b.toStdString(), n.toStdString()); });
    }
    if (body_->count() == 0) {
        status_->setText("<b>✕ " + tr("There is no body to mirror yet.") + "</b>");
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

MirrorPanel::~MirrorPanel() = default;

void MirrorPanel::useFace(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return;
    }
    auto hits = it->second->findFaces(name);
    if (hits.empty()) {
        return;
    }
    if (jig::measureItem(it->second->face(hits[0])).kind != "flat face") {
        status_->setText("<b>✕ " + tr("That face is curved. Click a flat face to mirror across.") + "</b>");
        return;
    }
    const QString key = q(nlohmann::json {{"face", it->second->faceRef(body, hits[0]).toJson()}}.dump());
    int i = plane_->findData(key);
    if (i < 0) {
        plane_->addItem(tr("Face %1").arg(q(name)), key);
        i = plane_->count() - 1;
    }
    plane_->setCurrentIndex(i);
}

jig::Feature MirrorPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "m", tr("Mirror"));
    f.kind = jig::FeatureKind::Mirror;
    const std::string pl = current(plane_);
    const nlohmann::json plane = !pl.empty() && pl[0] == '{' ? nlohmann::json::parse(pl, nullptr, false) : nlohmann::json(pl);
    const std::string what = current(body_);
    if (what.rfind("step:", 0) == 0) {
        f.params = {{"feature", what.substr(5)}, {"plane", plane}};
    }
    else {
        f.params = {{"body", what}, {"plane", plane}, {"op", current(op_)}};
    }
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
    addConstructionAxes(axis_, doc_, *stateBefore(), insertIndex(),
                        [](const std::string& id) { return q(nlohmann::json(id).dump()); });
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

// ---------------------------------------------------------------- Press pull

PressPullPanel::PressPullPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Press pull"),
                             tr("Click a flat face, then type how far to pull it out. A negative distance pushes it "
                                "in. Clicking a rounded edge or a hole opens the step that made it instead.")));
    face_ = new QLabel(tr("No face picked yet: click one in the view."), this);
    face_->setWordWrap(true);
    distance_ = new ExprField(doc, jig::Dim::len(), this);
    addRow(tr("Face"), face_);
    addRow(tr("Distance (out +, in −)"), distance_);
    if (const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_)) {
        faceRef_ = edit->params.value("face", nlohmann::json());
        face_->setText(tr("Face %1").arg(q(faceRef_.value("name", std::string {}))));
        distance_->setText(q(edit->params.value("distance", std::string("5 mm"))));
    }
    else {
        distance_->setText(q(doc_.defaultLength(5, 0.25)));
    }
    connect(distance_, &ExprField::changed, this, [this] { schedulePreview(); });
    if (view_) {
        connect(view_.data(), &Viewport::faceClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useFace(b.toStdString(), n.toStdString()); });
    }
    if (faceRef_.is_null()) {
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

PressPullPanel::~PressPullPanel()
{
    if (view_) {
        view_->setOverlay({});
    }
}

bool PressPullPanel::useFace(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return false;
    }
    auto hits = it->second->findFaces(name);
    if (hits.empty() || jig::measureItem(it->second->face(hits[0])).kind != "flat face") {
        status_->setText("<b>✕ " + tr("Press pull moves flat faces. To change a rounded edge or a hole, edit the step "
                                      "that made it (double-click it in the timeline).") +
                         "</b>");
        return false;
    }
    faceRef_ = it->second->faceRef(body, hits[0]).toJson();
    face_->setText(tr("Face %1").arg(q(name)));
    ok_->setEnabled(true);
    schedulePreview();
    return true;
}

jig::Feature PressPullPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "pp", tr("Press pull"));
    f.kind = jig::FeatureKind::PressPull;
    f.params = {{"face", faceRef_}, {"distance", text(distance_, "5 mm")}};
    return f;
}

QString PressPullPanel::title() const
{
    return tr("press pull");
}

// ---------------------------------------------------------------- Axis

AxisPanel::AxisPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent)
    : FeaturePanel(doc, view, std::move(editId), parent)
{
    form_->addWidget(heading(this, tr("Construction axis"),
                             tr("A straight reference line to revolve, pattern or turn around. Click a straight "
                                "edge, a round edge or a round face (a hole, a cylinder) in the view.")));
    kind_ = combo(this, {{tr("Along an edge, or through a round edge or face"), "through"},
                         {tr("Where two planes meet"), "planes"}});
    through_ = new QComboBox(this);
    base_ = new QComboBox(this);
    base2_ = new QComboBox(this);
    addRow(tr("Kind"), kind_);
    addRow(tr("Through"), through_);
    addRow(tr("Plane"), base_);
    addRow(tr("And plane"), base2_);
    for (auto* c : {base_, base2_}) {
        c->addItem(tr("Floor (XY)"), "\"XY\"");
        c->addItem(tr("Front (XZ)"), "\"XZ\"");
        c->addItem(tr("Side (YZ)"), "\"YZ\"");
    }
    const auto before = stateBefore();
    for (int i = 0; i < insertIndex(); ++i) {
        const auto& f = doc_.model().features[size_t(i)];
        if (f.kind == jig::FeatureKind::Plane && before->planes.count(f.id)) {
            for (auto* c : {base_, base2_}) {
                c->addItem(q(f.name), q(nlohmann::json(f.id).dump()));
            }
        }
    }
    base2_->setCurrentIndex(1);
    if (const jig::Feature* edit = editId_.empty() ? nullptr : doc_.feature(editId_)) {
        const auto& p = edit->params;
        auto put = [](QComboBox* c, const nlohmann::json& v) {
            const QString key = q(v.dump());
            int i = c->findData(key);
            if (i < 0) {
                QString label = q(v.is_string() ? v.get<std::string>() : std::string("(picked)"));
                if (v.is_object() && v.contains("face")) {
                    label = tr("Face %1").arg(q(v["face"].value("name", std::string {})));
                }
                else if (v.is_object() && v.contains("edge")) {
                    label = tr("Edge %1").arg(q(v["edge"].value("name", std::string {})));
                }
                c->addItem(label, key);
                i = c->count() - 1;
            }
            c->setCurrentIndex(i);
        };
        select(kind_, p.contains("through") ? "through" : "planes");
        if (p.contains("through")) {
            put(through_, p["through"]);
        }
        if (p.contains("base")) {
            put(base_, p["base"]);
        }
        if (p.contains("base2")) {
            put(base2_, p["base2"]);
        }
    }
    for (auto* c : {kind_, through_, base_, base2_}) {
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateVisibility();
            schedulePreview();
        });
    }
    if (view_) {
        view_->setPickMode(PickMode::Any);
        connect(view_.data(), &Viewport::faceClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useFace(b.toStdString(), n.toStdString()); });
        connect(view_.data(), &Viewport::edgeClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { useEdge(b.toStdString(), n.toStdString()); });
    }
    updateVisibility();
    if (through_->count() == 0 && current(kind_) == "through") {
        status_->setText(tr("Click an edge or a round face in the view."));
        ok_->setEnabled(false);
    }
    else {
        schedulePreview();
    }
}

AxisPanel::~AxisPanel()
{
    if (view_) {
        view_->setPickMode(PickMode::Faces);
        view_->setOverlay({});
    }
}

void AxisPanel::useFace(const std::string& body, const std::string& name)
{
    const auto before = stateBefore();
    auto it = before->bodies.find(body);
    if (it == before->bodies.end()) {
        return;
    }
    auto hits = it->second->findFaces(name);
    if (hits.empty()) {
        return;
    }
    const jig::MeasureItem m = jig::measureItem(it->second->face(hits[0]));
    nlohmann::json ref {{"face", it->second->faceRef(body, hits[0]).toJson()}};
    QComboBox* target = through_;
    if (m.kind == "flat face") {  // flat faces: the axis where two of them meet
        select(kind_, "planes");
        target = nextIsSecond_ ? base2_ : base_;
        nextIsSecond_ = !nextIsSecond_;
    }
    else if (!m.axis) {
        status_->setText("<b>✕ " + tr("That face has no centre line. Click a hole, a cylinder or a cone.") + "</b>");
        return;
    }
    else {
        select(kind_, "through");
    }
    const QString key = q(ref.dump());
    int i = target->findData(key);
    if (i < 0) {
        target->addItem(tr("Face %1").arg(q(name)), key);
        i = target->count() - 1;
    }
    target->setCurrentIndex(i);
    ok_->setEnabled(true);
    schedulePreview();
}

void AxisPanel::useEdge(const std::string& body, const std::string& name)
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
    if (!jig::measureItem(it->second->edge(hits[0])).axis) {
        status_->setText("<b>✕ " + tr("Click a straight edge, or a circle or arc.") + "</b>");
        return;
    }
    select(kind_, "through");
    nlohmann::json ref {{"edge", it->second->edgeRef(body, hits[0]).toJson()}};
    const QString key = q(ref.dump());
    int i = through_->findData(key);
    if (i < 0) {
        through_->addItem(tr("Edge %1").arg(q(name)), key);
        i = through_->count() - 1;
    }
    through_->setCurrentIndex(i);
    ok_->setEnabled(true);
    schedulePreview();
}

void AxisPanel::updateVisibility()
{
    const bool through = current(kind_) == "through";
    auto show = [this](QWidget* w, bool on) {
        w->setVisible(on);
        const int i = form_->indexOf(w);
        if (i > 0) {
            if (auto* item = form_->itemAt(i - 1); item && item->widget()) {
                item->widget()->setVisible(on);
            }
        }
    };
    show(through_, through);
    show(base_, !through);
    show(base2_, !through);
}

jig::Feature AxisPanel::draft() const
{
    jig::Feature f = startDraft(doc_, editId_, "ax", tr("Axis"));
    f.kind = jig::FeatureKind::Axis;
    auto json = [](const QComboBox* c) {
        const std::string d = c->currentData().toString().toStdString();
        return d.empty() ? nlohmann::json() : nlohmann::json::parse(d, nullptr, false);
    };
    if (current(kind_) == "through") {
        f.params = {{"through", json(through_)}};
    }
    else {
        f.params = {{"base", json(base_)}, {"base2", json(base2_)}};
    }
    return f;
}

void AxisPanel::previewed(const jig::FeatureOutcome& o)
{
    if (!view_) {
        return;
    }
    SceneOverlay ov;
    const jig::Feature f = draft();
    if (o.after && o.after->axes.count(f.id)) {
        const gp_Ax1& a = o.after->axes.at(f.id);
        float s = 30;
        for (auto& [id, b] : o.after->bodies) {
            auto mp = jig::massProperties(b->shape());
            s = std::max({s, float(mp.xmax - mp.xmin) * 0.7f, float(mp.ymax - mp.ymin) * 0.7f, float(mp.zmax - mp.zmin) * 0.7f});
        }
        const gp_XYZ p0 = a.Location().XYZ() - a.Direction().XYZ() * s, p1 = a.Location().XYZ() + a.Direction().XYZ() * s;
        const QColor c(222, 150, 40, 255);
        ov.lines.push_back({QVector3D(float(p0.X()), float(p0.Y()), float(p0.Z())), c});
        ov.lines.push_back({QVector3D(float(p1.X()), float(p1.Y()), float(p1.Z())), c});
    }
    view_->setOverlay(std::move(ov));
}

QString AxisPanel::title() const
{
    return tr("construction axis");
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
    addConstructionAxes(axis_, doc_, *stateBefore(), insertIndex(), [](const std::string& id) { return q(id); });
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
            text = ok ? "= " + q(jig::formatQuantity(v.value())) : "✕ " + coreText(v.error().message);
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
