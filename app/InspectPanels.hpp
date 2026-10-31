// SPDX-License-Identifier: GPL-3.0-or-later
// Inspect tools (milestone M6): Measure in the side dock and the Properties dialog.
// Neither changes the design.
#pragma once

#include <string>
#include <vector>

#include <QDialog>
#include <QPointer>

#include "FeaturePanels.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace jigapp
{

class Viewport;

// Click one face or edge for its size, a second one for the distance and angle between
// them (and centre-to-centre spacing for holes and round edges).
class MeasurePanel : public ToolPanel
{
    Q_OBJECT
public:
    MeasurePanel(const jig::Document& doc, Viewport* view, QWidget* parent = nullptr);
    ~MeasurePanel() override;
    void cancel() override;
    // A click in the view, by persistent name (also used by UI tests).
    void pick(const std::string& body, const std::string& name, bool edge);
    void clear();
    QString resultText() const;

private:
    struct Pick
    {
        std::string body, name;
        bool edge = false;
    };
    void update();
    const jig::Document& doc_;
    QPointer<Viewport> view_;
    std::vector<Pick> picks_;
    QLabel *first_, *second_, *result_;
};

// Section view: hide one side of a plane to look inside walls and holes. View only:
// the design does not change, and closing the panel shows the whole part again.
class SectionPanel : public ToolPanel
{
    Q_OBJECT
public:
    SectionPanel(jig::Document& doc, Viewport* view, QWidget* parent = nullptr);
    ~SectionPanel() override;
    void cancel() override;
    QComboBox* plane() const
    {
        return plane_;
    }
    ExprField* position() const
    {
        return position_;
    }

private:
    void apply();
    const jig::Document& doc_;
    QPointer<Viewport> view_;
    QComboBox* plane_;
    ExprField* position_;
    QCheckBox* flip_;
    QLabel* note_;
};

// Volume, weight for a chosen material, filament length, centre of mass and size.
class PropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    PropertiesDialog(const jig::Document& doc, QWidget* parent = nullptr);
    QTableWidget* table() const
    {
        return table_;
    }
    QComboBox* material() const
    {
        return material_;
    }

private:
    void fill();
    const jig::Document& doc_;
    QComboBox* material_;
    QDoubleSpinBox* density_;
    QTableWidget* table_;
};

}  // namespace jigapp
