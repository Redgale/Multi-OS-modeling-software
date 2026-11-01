// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QLockFile;

#include <map>
#include <memory>

#include <set>

#include <QMainWindow>
#include <QPointer>

#include "Tutorial.hpp"
#include "jig/document.hpp"

class QAction;
class QCheckBox;
class QDockWidget;
class QStackedWidget;
class QToolBar;
class QLabel;
class QListWidget;
class QSlider;
class QTextBrowser;
class QTimer;
class QTreeWidget;
class QComboBox;
class QMenu;
class QActionGroup;

namespace jigapp
{
class MarkingMenu;
enum class NavPreset;

class Viewport;
class SketchEditor;
class FeaturePanel;
class ToolPanel;
class TutorialCard;
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
    bool openSample(const QString& name);
    bool openScript(const QString& path);  // a .jigs script, run into a new unsaved design  // e.g. "phone_stand"
    void newDesign();
    void showWelcome();
    void setTheme(Theme t);
    void openSettings();
    void setDesignUnits(jig::LengthUnit u);  // undoable; display and typed input only
    std::unique_ptr<jig::Document> blankDocument() const;  // in the units chosen for new designs
    Viewport* viewport() const
    {
        return view_;
    }
    // Offers to restore an autosave left by a session that did not end cleanly.
    void checkRecovery();
    // Each window autosaves to its own file, held by a lock file while it runs; these are the
    // autosaves whose program is no longer running (a crash), newest first.
    QStringList leftoverSessions() const;
    static QString recoveryDir();
    void autosave();  // (every minute; public for tests)

    // ---- modelling workflow (also used by UI tests)
    jig::Document& document()
    {
        return *doc_;
    }
    SketchEditor* sketchEditor() const
    {
        return sketch_;
    }
    ToolPanel* featurePanel() const
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
    void openRevolve(const std::string& editId = {});
    void openHole(const std::string& editId = {});
    void openPattern(const std::string& editId = {}, bool circular = true);
    void openMirror(const std::string& editId = {});
    void openMeasure();
    void importDxf();  // Insert > DXF: asks for the file (and its units if the file does not say)
    // Brings a DXF file into the open sketch (or a new floor sketch). `units` overrides
    // what the file says; returns false and explains when it cannot.
    bool importDxfFile(const QString& path, std::optional<jig::LengthUnit> units = std::nullopt);
    void importSvg();  // Insert > SVG
    // Insert > STEP: an Import step with the file's solids (false, with a message, if not)
    bool importStepFile(const QString& path);
    void saveVersionInteractive();
    bool saveNamedVersion(const QString& label);  // saves first when the design has no file yet
    void showVersions();
    bool restoreVersion(int number);              // undoable
    bool saveTo(const QString& path);             // save without asking (also used by tests)
    void startTutorial();
    TutorialCard* tutorial() const
    {
        return tutorial_;
    }
    bool exportedSinceStart() const
    {
        return exported_;
    }
    QAction* commandAction(const QString& id);  // the toolbar/menu action of a command
    bool importSvgFile(const QString& path);
    void openBodyTool(const std::string& tool, const std::string& editId = {});  // combine, split, move
    void openLoft(const std::string& editId);
    void openSweep(const std::string& editId);
    void openPipe(const std::string& editId);
    void openCoil(const std::string& editId);
    void openRib(const std::string& editId);
    void openPrimitive(const std::string& editId, const std::string& shape);
    void openPlane(const std::string& editId = {}, const std::string& kind = "offset");
    void openAxis(const std::string& editId = {});
    void openPoint(const std::string& editId = {});
    // Named views: the camera kept with the design under a name
    void saveNamedView(const QString& name);
    // Finished sketches are drawn in the view; one a later step uses hides itself (like a
    // profile that has been extruded) until shown again from the Browser.
    bool sketchVisible(const std::string& id) const;
    void setSketchVisible(const std::string& id, bool on);
    // Delete a body: a Remove step in the timeline (false if there is no such body)
    bool removeBody(const std::string& body);
    // Slice: while sketching, hide the part of the model in front of the sketch plane
    void setSketchSlice(bool on);
    bool restoreNamedView(const QString& name);
    // Browser: which bodies are drawn (a view setting, not part of the design or its undo)
    void setBodyVisible(const std::string& body, bool on);
    // Timeline groups: put a step in a group ("" takes it out); fold a group to one item.
    void setStepGroup(const std::string& id, const std::string& group);
    void setGroupFolded(const std::string& group, bool folded);
    QListWidget* timeline() const
    {
        return timeline_;
    }
    // What a plain click picks when no tool is open: "faces", "edges" or "bodies".
    void setSelectFilter(const QString& what);
    QString selectFilter() const;
    QString detailsText() const;
    // Mouse and trackpad settings (saved, and shown in the View menu and Settings).
    void setNavigation(NavPreset preset, bool zoomInverted, float orbitSpeed);
    // The marking menu for the view's current context, opened at a view position.
    MarkingMenu* openMarkingMenu(const QPoint& viewPos);
    // Select Other: a menu of every face under a view position, nearest first.
    QMenu* openSelectOther(const QPoint& viewPos);
    void isolateBody(const std::string& body);
    void showAllBodies();
    void refreshAll();  // after any model change (also from tests that change the document directly)
    bool bodyVisible(const std::string& body) const
    {
        return !doc_->hiddenBodies().count(body);
    }
    QTreeWidget* browser() const
    {
        return browser_;
    }
    void openPressPull(const std::string& editId = {});
    // Press pull on a face: a flat face opens the press pull panel; a face made by a fillet,
    // chamfer or hole opens that step instead. Returns what happened, for the status bar.
    QString pressPullFace(const std::string& body, const std::string& face);
    void openProperties();
    void openParameters();
    void editFeature(int index);
    bool moveStep(const std::string& id, int to);  // timeline reorder (checked; explains a refusal)
    void runCommand(const QString& id);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildDocks();
    void refreshDatums();
    void refreshBrowser();
    void refreshTimeline();
    void refreshWorkflow();
    void showFeatureDetails(int index);
    void openPalette();
    void applyIcons();
    void showPanel(ToolPanel* p);
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
    QString recoveryPath() const;  // this window's autosave
    void holdSessionLock();
    void setDocument(std::unique_ptr<jig::Document> doc, const QString& path);
    QAction* registryAction(const char* id);

    std::unique_ptr<jig::Document> doc_;
    QString path_;
    uint64_t savedRevision_ = 0, autosavedRevision_ = 0;

    Viewport* view_ = nullptr;
    QTreeWidget* browser_ = nullptr;
    QPointer<QComboBox> selectFilter_;  // (a QPointer: panels may be destroyed after it on exit)
    QActionGroup* navGroup_ = nullptr;
    QListWidget* timeline_ = nullptr;
    QSlider* rollback_ = nullptr;
    QTextBrowser* details_ = nullptr;
    QLabel* workflow_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* statusSel_ = nullptr;
    QLabel* statusInfo_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    QString sessionId_;                       // names this window's autosave and lock files
    std::unique_ptr<QLockFile> sessionLock_;  // held while the window runs
    std::map<QString, QAction*> actions_;
    SketchEditor* sketch_ = nullptr;
    ToolPanel* panel_ = nullptr;
    QPointer<TutorialCard> tutorial_;
    bool exported_ = false;
    std::unique_ptr<PlanePicker> picker_;
    QToolBar* toolsBar_ = nullptr;
    QToolBar* sketchBar_ = nullptr;
    QDockWidget* propsDock_ = nullptr;
    QStackedWidget* sideStack_ = nullptr;
    QWidget* panelHost_ = nullptr;
    QWidget* sketchPanel_ = nullptr;
    QCheckBox* sliceBox_ = nullptr;
    bool homeWhenMeshed_ = false;  // frame a just-opened design once its meshes are ready
    QLabel* sketchInfo_ = nullptr;
    QAction* centerRect_ = nullptr;
    Theme theme_ = Theme::Dark;
    bool updatingRollback_ = false;
};

}  // namespace jigapp
