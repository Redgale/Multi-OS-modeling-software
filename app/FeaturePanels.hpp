// SPDX-License-Identifier: GPL-3.0-or-later
// Feature panels (milestone M2): non-modal panels in the side dock with a live
// preview in the 3D view and OK / Cancel. Nothing changes until OK; Cancel and Esc
// leave the document exactly as it was (Easy Contract principle 2). Values are
// remembered per command for the session.
#pragma once

#include <set>
#include <string>

#include <QDialog>
#include <QPointer>
#include <QWidget>

#include "jig/document.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;

namespace jigapp
{

class Viewport;

// A numeric field that accepts units, expressions and parameter names, and shows
// the evaluated value (or a plain-language error) right under it.
class ExprField : public QWidget
{
    Q_OBJECT
public:
    ExprField(jig::Document& doc, jig::Dim dim, QWidget* parent = nullptr);
    QString text() const;
    void setText(const QString& t);
    bool valid() const;
    double value() const;  // base units (mm, rad)
    QLineEdit* edit() const
    {
        return edit_;
    }

signals:
    void changed();

private:
    void evaluate();
    jig::Document& doc_;
    jig::Dim dim_;
    QLineEdit* edit_;
    QLabel* result_;
    bool valid_ = false;
    double value_ = 0;
};

class FeaturePanel : public QWidget
{
    Q_OBJECT
public:
    FeaturePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent);
    ~FeaturePanel() override;
    const std::string& editId() const
    {
        return editId_;
    }
    // OK/Cancel from outside (keyboard shortcuts, UI tests)
    bool accept();
    void cancel();
    QString statusText() const;
    bool previewOk() const
    {
        return previewOk_;
    }

signals:
    void finished(bool committed);

protected:
    virtual jig::Feature draft() const = 0;
    virtual QString title() const = 0;
    virtual void rememberValues() {}
    void schedulePreview();
    void addRow(const QString& label, QWidget* w);
    int insertIndex() const;
    std::shared_ptr<const jig::ModelState> stateBefore() const;

    jig::Document& doc_;
    QPointer<Viewport> view_;  // may be gone first when the window closes
    std::string editId_;
    QVBoxLayout* form_;
    QLabel* status_;
    QWidget* fixes_;
    QPushButton* ok_;
    QTimer* timer_;
    bool previewOk_ = false;
    bool done_ = false;

private:
    void runPreview();
};

class ExtrudePanel : public FeaturePanel
{
    Q_OBJECT
public:
    ExtrudePanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ExprField* distance() const
    {
        return distance_;
    }
    QComboBox* operation() const
    {
        return op_;
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void rememberValues() override;

private:
    void fillProfiles();
    QString autoOperation() const;
    QComboBox *sketch_, *extent_, *direction_, *op_;
    QListWidget* profiles_;
    ExprField* distance_;
};

class FilletPanel : public FeaturePanel
{
    Q_OBJECT
public:
    FilletPanel(jig::Document& doc, Viewport* view, std::string editId, QWidget* parent = nullptr);
    ~FilletPanel() override;
    // Add/remove an edge of the body as it was before this step (and its tangent chain).
    void toggleEdge(const std::string& body, int edge);
    // A click in the view: the edge index is on the shape currently displayed (which may
    // be the preview); it is mapped to the body before this step by persistent name.
    void toggleShownEdge(const std::string& body, int shownEdge);
    ExprField* radius() const
    {
        return radius_;
    }
    size_t edgeCount() const
    {
        return edges_.size();
    }

protected:
    jig::Feature draft() const override;
    QString title() const override;
    void rememberValues() override;

private:
    void refreshEdges();
    std::string body_;
    std::set<int> edges_;
    ExprField* radius_;
    QCheckBox* chain_;
    QLabel* count_;
};

// Modify ▸ Parameters: the named values (width, wall, clearance) that drive the design.
class ParametersDialog : public QDialog
{
    Q_OBJECT
public:
    ParametersDialog(jig::Document& doc, QWidget* parent = nullptr);
    // Applies the table as one undoable change. Returns false (and explains) if a
    // name is invalid or a value does not work.
    bool apply();

private:
    void addRow(const jig::Parameter& p);
    void revalidate();
    jig::Document& doc_;
    QTableWidget* table_;
    QLabel* note_;
};

}  // namespace jigapp
