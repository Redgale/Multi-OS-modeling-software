// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>

#include <QMainWindow>

#include "jig/document.hpp"

class QAction;
class QDockWidget;
class QStackedWidget;
class QToolBar;
class QLabel;
class QListWidget;
class QSlider;
class QTextBrowser;
class QTimer;
class QTreeWidget;

namespace jigapp
{

class Viewport;
class SketchEditor;
class FeaturePanel;
class PlanePicker;

enum class Theme
{
    Light,
    Dark,
    HighContrast
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    bool openFile(const QString& path);
    bool openSample(const QString& name);  // e.g. "phone_stand"
    void newDesign();
    void showWelcome();
    void setTheme(Theme t);
    Viewport* viewport() const
    {
        return view_;
    }
    // Offers to restore an autosave left by a session that did not end cleanly.
    void checkRecovery();

    // ---- modelling workflow (also used by UI tests)
    jig::Document& document()
    {
        return *doc_;
    }
    SketchEditor* sketchEditor() const
    {
        return sketch_;
    }
    FeaturePanel* featurePanel() const
    {
        return panel_;
    }
    void startCreateSketch();
    // plane: "XY", "XZ" or "YZ"; or a planar face of a body
    bool createSketchOn(const std::string& plane, const std::string& faceBody = {}, int faceIndex = -1);
    void enterSketch(const std::string& id);
    void exitSketch();
    void openExtrude(const std::string& editId = {});
    void openFillet(const std::string& editId = {});
    void openChamfer(const std::string& editId = {});
    void openShell(const std::string& editId = {});
    void openParameters();
    void editFeature(int index);
    void runCommand(const QString& id);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildDocks();
    void refreshAll();  // after any model change
    void refreshBrowser();
    void refreshTimeline();
    void refreshWorkflow();
    void showFeatureDetails(int index);
    void openPalette();
    void applyIcons();
    void showPanel(FeaturePanel* p);
    void closePanel();
    void cancelPicker();
    void escape();
    void timelineMenu(const QPoint& pos);
    void deleteFeature(const std::string& id);
    void renameFeature(const std::string& id);
    void updateSketchInfo();
    bool save(bool askPath);
    void exportAs(const QString& format);
    bool maybeKeepChanges();
    void autosave();
    QString recoveryPath() const;
    void setDocument(std::unique_ptr<jig::Document> doc, const QString& path);
    QAction* registryAction(const char* id);

    std::unique_ptr<jig::Document> doc_;
    QString path_;
    uint64_t savedRevision_ = 0, autosavedRevision_ = 0;

    Viewport* view_ = nullptr;
    QTreeWidget* browser_ = nullptr;
    QListWidget* timeline_ = nullptr;
    QSlider* rollback_ = nullptr;
    QTextBrowser* details_ = nullptr;
    QLabel* workflow_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* statusSel_ = nullptr;
    QLabel* statusInfo_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    std::map<QString, QAction*> actions_;
    SketchEditor* sketch_ = nullptr;
    FeaturePanel* panel_ = nullptr;
    std::unique_ptr<PlanePicker> picker_;
    QToolBar* toolsBar_ = nullptr;
    QToolBar* sketchBar_ = nullptr;
    QDockWidget* propsDock_ = nullptr;
    QStackedWidget* sideStack_ = nullptr;
    QWidget* panelHost_ = nullptr;
    QWidget* sketchPanel_ = nullptr;
    QLabel* sketchInfo_ = nullptr;
    QAction* centerRect_ = nullptr;
    Theme theme_ = Theme::Light;
    bool updatingRollback_ = false;
};

}  // namespace jigapp
