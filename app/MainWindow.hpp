// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>

#include <QMainWindow>

#include "jig/document.hpp"

class QAction;
class QLabel;
class QListWidget;
class QSlider;
class QTextBrowser;
class QTimer;
class QTreeWidget;

namespace jigapp
{

class Viewport;

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
    void runCommand(const QString& id);
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
    Theme theme_ = Theme::Light;
    bool updatingRollback_ = false;
};

}  // namespace jigapp
