// SPDX-License-Identifier: GPL-3.0-or-later
#include "InspectPanels.hpp"

#include <cmath>
#include <numbers>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Viewport.hpp"
#include "jig/kernel.hpp"

namespace jigapp
{
namespace
{

QString num(double v)
{
    // up to 3 decimals, no trailing zeros
    QString s = QString::number(v, 'f', 3);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
        s.chop(1);
    }
    return s == "-0" ? "0" : s;
}

QString kindWord(const std::string& kind)
{
    static const std::map<std::string, QString> words {
        {"flat face", QObject::tr("flat face")},
        {"round face (cylinder)", QObject::tr("round face (cylinder)")},
        {"cone", QObject::tr("cone")},
        {"ball face (sphere)", QObject::tr("ball face (sphere)")},
        {"rounded face (torus)", QObject::tr("rounded face (torus)")},
        {"curved face", QObject::tr("curved face")},
        {"straight edge", QObject::tr("straight edge")},
        {"circle", QObject::tr("circle")},
        {"arc", QObject::tr("arc")},
        {"curved edge", QObject::tr("curved edge")}};
    auto it = words.find(kind);
    return it == words.end() ? QString::fromStdString(kind) : it->second;
}

}  // namespace

// ---------------------------------------------------------------- Measure

MeasurePanel::MeasurePanel(const jig::Document& doc, Viewport* view, QWidget* parent)
    : ToolPanel(parent)
    , doc_(doc)
    , view_(view)
{
    auto* lay = new QVBoxLayout(this);
    auto* head = new QLabel("<h3>" + tr("Measure") + "</h3><p>" +
                                tr("Click a face or an edge to read its size. Click a second one for the distance "
                                   "and angle between them. A third click starts again.") + "</p>",
                            this);
    head->setWordWrap(true);
    first_ = new QLabel(this);
    second_ = new QLabel(this);
    result_ = new QLabel(this);
    for (auto* l : {first_, second_, result_}) {
        l->setWordWrap(true);
        l->setTextFormat(Qt::RichText);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    auto* clearButton = new QPushButton(tr("Clear"), this);
    auto* close = new QPushButton(tr("Close"), this);
    close->setToolTip(tr("Close Measure (Esc)."));
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(clearButton);
    buttons->addStretch();
    buttons->addWidget(close);
    lay->addWidget(head);
    lay->addWidget(first_);
    lay->addWidget(second_);
    lay->addWidget(result_);
    lay->addStretch();
    lay->addLayout(buttons);
    connect(clearButton, &QPushButton::clicked, this, [this] { clear(); });
    connect(close, &QPushButton::clicked, this, [this] { cancel(); });
    if (view_) {
        view_->setPickMode(PickMode::Any);
        connect(view_.data(), &Viewport::faceClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { pick(b.toStdString(), n.toStdString(), false); });
        connect(view_.data(), &Viewport::edgeClicked, this,
                [this](const QString& b, const QString& n, Qt::KeyboardModifiers) { pick(b.toStdString(), n.toStdString(), true); });
    }
    update();
}

MeasurePanel::~MeasurePanel()
{
    if (view_) {
        view_->setFaceHighlight({}, {});
        view_->setEdgeHighlight({}, {});
        view_->setPickMode(PickMode::Faces);
    }
}

void MeasurePanel::cancel()
{
    emit finished(false);
}

void MeasurePanel::pick(const std::string& body, const std::string& name, bool edge)
{
    if (picks_.size() >= 2) {
        picks_.clear();
    }
    picks_.push_back({body, name, edge});
    update();
}

void MeasurePanel::clear()
{
    picks_.clear();
    update();
}

QString MeasurePanel::resultText() const
{
    return first_->text() + "\n" + second_->text() + "\n" + result_->text();
}

void MeasurePanel::update()
{
    // resolve the picks on the current geometry
    std::vector<TopoDS_Shape> shapes;
    std::vector<jig::MeasureItem> items;
    std::string hlBody;
    std::vector<int> hlFaces, hlEdges;
    for (const Pick& p : picks_) {
        auto it = doc_.state().bodies.find(p.body);
        if (it == doc_.state().bodies.end()) {
            continue;
        }
        const jig::NamedShape& b = *it->second;
        auto hits = p.edge ? b.findEdges(p.name) : b.findFaces(p.name);
        if (hits.empty()) {
            continue;
        }
        shapes.push_back(p.edge ? b.edge(hits[0]) : TopoDS_Shape(b.face(hits[0])));
        items.push_back(jig::measureItem(shapes.back()));
        hlBody = p.body;
        (p.edge ? hlEdges : hlFaces).push_back(hits[0]);
    }
    if (view_) {
        view_->setFaceHighlight(hlBody, hlFaces);
        view_->setEdgeHighlight(hlBody, hlEdges);
    }
    auto describe = [](const jig::MeasureItem& m) {
        const bool face = m.kind.find("face") != std::string::npos || m.kind == "cone";
        QString t = "<b>" + kindWord(m.kind) + "</b><br>";
        t += face ? tr("Area: %1 mm²").arg(num(m.size)) : tr("Length: %1 mm").arg(num(m.size));
        if (m.radius) {
            t += "<br>" + tr("Diameter: %1 mm  (radius %2 mm)").arg(num(2 * *m.radius), num(*m.radius));
        }
        return t;
    };
    first_->setText(items.size() > 0 ? tr("First: ") + describe(items[0]) : tr("First: click a face or edge."));
    second_->setText(items.size() > 1 ? tr("Second: ") + describe(items[1])
                     : items.size() == 1 ? tr("Second: click another face or edge to measure between them.")
                                         : QString());
    if (shapes.size() == 2) {
        auto r = jig::measureBetween(shapes[0], shapes[1]);
        if (!r) {
            result_->setText("✕ " + QString::fromStdString(r.error().message));
            return;
        }
        QString t = "<b>" + tr("Distance: %1 mm").arg(num(r->distance)) + "</b>";
        if (r->distance < 1e-9) {
            t += " " + tr("(they touch)");
        }
        if (r->centres) {
            t += "<br><b>" + tr("Centre to centre: %1 mm").arg(num(*r->centres)) + "</b>";
        }
        if (r->angle) {
            t += "<br>" + tr("Angle: %1°").arg(num(*r->angle * 180 / std::numbers::pi));
        }
        const gp_Vec d(r->p1, r->p2);
        t += "<br>" + tr("Along X, Y, Z: %1, %2, %3 mm").arg(num(std::abs(d.X())), num(std::abs(d.Y())), num(std::abs(d.Z())));
        result_->setText(t);
    }
    else {
        result_->clear();
    }
}

// ---------------------------------------------------------------- Properties

PropertiesDialog::PropertiesDialog(const jig::Document& doc, QWidget* parent)
    : QDialog(parent)
    , doc_(doc)
{
    setWindowTitle(tr("Properties"));
    resize(640, 360);
    auto* lay = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    material_ = new QComboBox(this);
    // typical densities in g/cm³
    for (auto [name, rho] : std::initializer_list<std::pair<QString, double>> {
             {tr("PLA"), 1.24}, {tr("PETG"), 1.27}, {tr("ABS"), 1.04}, {tr("ASA"), 1.07}, {tr("TPU"), 1.21},
             {tr("Nylon (PA12)"), 1.01}, {tr("Resin (standard)"), 1.15}, {tr("Wood (pine)"), 0.5},
             {tr("Aluminium"), 2.70}, {tr("Steel"), 7.85}, {tr("Brass"), 8.5}, {tr("Other…"), 1.0}}) {
        material_->addItem(name, rho);
    }
    density_ = new QDoubleSpinBox(this);
    density_->setRange(0.01, 30);
    density_->setDecimals(3);
    density_->setSuffix(" g/cm³");
    density_->setValue(1.24);
    row->addWidget(new QLabel(tr("Material:"), this));
    row->addWidget(material_);
    row->addWidget(new QLabel(tr("Density:"), this));
    row->addWidget(density_);
    row->addStretch();
    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* note = new QLabel(tr("Weights assume a solid part (100 % infill). A print with less infill weighs less. "
                               "Filament length is for 1.75 mm filament."),
                            this);
    note->setWordWrap(true);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    lay->addLayout(row);
    lay->addWidget(table_);
    lay->addWidget(note);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(material_, &QComboBox::currentIndexChanged, this, [this] {
        density_->setValue(material_->currentData().toDouble());
    });
    connect(density_, &QDoubleSpinBox::valueChanged, this, [this] { fill(); });
    fill();
}

void PropertiesDialog::fill()
{
    const auto& bodies = doc_.state().bodies;
    const QStringList rows {tr("Volume"), tr("Weight"), tr("Filament (1.75 mm)"), tr("Surface area"), tr("Size (X × Y × Z)"),
                            tr("Centre of mass"), tr("Valid solid")};
    table_->clear();
    table_->setRowCount(int(rows.size()));
    table_->setColumnCount(int(bodies.size()));
    table_->setVerticalHeaderLabels(rows);
    QStringList cols;
    int c = 0;
    const double rho = density_->value();  // g/cm³ = 0.001 g/mm³
    for (auto& [id, b] : bodies) {
        const jig::Feature* f = doc_.feature(id);
        cols << (f ? QString::fromStdString(f->name) : QString::fromStdString(id));
        const jig::MassProperties m = jig::massProperties(b->shape());
        const double filament = m.volume / (std::numbers::pi * 0.875 * 0.875) / 1000;  // metres
        const QStringList vals {
            tr("%1 cm³  (%2 mm³)").arg(num(m.volume / 1000), num(m.volume)),
            tr("%1 g").arg(num(m.volume * rho / 1000)),
            tr("%1 m").arg(num(filament)),
            tr("%1 cm²").arg(num(m.area / 100)),
            tr("%1 × %2 × %3 mm").arg(num(m.xmax - m.xmin), num(m.ymax - m.ymin), num(m.zmax - m.zmin)),
            tr("%1, %2, %3 mm").arg(num(m.cx), num(m.cy), num(m.cz)),
            m.valid ? tr("✓ yes") : tr("✕ no: the shape has errors")};
        for (int r = 0; r < vals.size(); ++r) {
            table_->setItem(r, c, new QTableWidgetItem(vals[r]));
        }
        ++c;
    }
    table_->setHorizontalHeaderLabels(cols);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

}  // namespace jigapp
