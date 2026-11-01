// SPDX-License-Identifier: GPL-3.0-or-later
// Jigsmith desktop application.
//
//   jigsmith [design.jig]
//   jigsmith --sample phone_stand --screenshot out.png [--size 1360x860] [--theme dark]
//            (renders the window off-screen-friendly and exits: used for README images and CI)
#include <cstdio>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QImage>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>

#include "MainWindow.hpp"
#include "Translations.hpp"
#include "Viewport.hpp"
#include "jig/version.hpp"

int main(int argc, char** argv)
{
    QElapsedTimer startup;
    startup.start();
    jigapp::configureSurfaceFormat();

    QApplication app(argc, argv);
    QApplication::setApplicationName("Jigsmith");
    QApplication::setOrganizationName("Jigsmith");
    QApplication::setApplicationVersion(JIG_VERSION_STRING);

    QCommandLineParser cli;
    cli.setApplicationDescription("Free, open-source parametric CAD for hobbyists.");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption shot("screenshot", "Render the window to <png> and quit.", "png");
    QCommandLineOption sample("sample", "Open a built-in sample (phone_stand, knob, pen_cup).", "name");
    QCommandLineOption size("size", "Window size for screenshots, e.g. 1360x860.", "WxH", "1360x860");
    QCommandLineOption theme("theme", "light, dark or contrast.", "name");
    QCommandLineOption select("select-face", "Screenshot helper: select a face by index.", "n");
    QCommandLineOption lang("lang", "Interface language code (e.g. qps for the test pseudo-language).", "code");
    cli.addOptions({shot, sample, size, theme, select, lang});
    cli.addPositionalArgument("design", "A .jig file to open, or a .jigs script to run into a new design.");
    cli.process(app);
    jigapp::installLanguage(cli.isSet(lang) ? cli.value(lang) : QSettings().value("language").toString());

    jigapp::MainWindow w;
    if (cli.isSet(theme)) {
        const QString t = cli.value(theme);
        w.setTheme(t == "dark" ? jigapp::Theme::Dark : t == "contrast" ? jigapp::Theme::HighContrast
                                                                        : jigapp::Theme::Light);
    }
    const bool screenshot = cli.isSet(shot);
    if (screenshot) {
        const auto parts = cli.value(size).split('x');
        if (parts.size() == 2) {
            w.resize(parts[0].toInt(), parts[1].toInt());
        }
    }
    w.show();

    if (cli.isSet(sample)) {
        w.openSample(cli.value(sample));
    }
    else if (!cli.positionalArguments().isEmpty()) {
        const QString file = cli.positionalArguments().first();
        file.endsWith(".jigs", Qt::CaseInsensitive) ? w.openScript(file) : w.openFile(file);
    }

    if (screenshot) {
        const QString out = cli.value(shot);
        // Wait until meshes are uploaded, then grab the whole window including the GL view.
        auto* t = new QTimer(&w);
        int ticks = 0;
        QObject::connect(t, &QTimer::timeout, &w, [&, t]() mutable {
            ++ticks;
            if ((!w.viewport()->meshesReady() || ticks < 8) && ticks < 400) {
                return;
            }
            t->stop();
            if (ticks == 8 || ticks == 9) {
                w.viewport()->homeView();
            }
            if (cli.isSet(select)) {
                w.viewport()->selectFaceForScreenshot(cli.value(select).toInt());
            }
            QTimer::singleShot(150, &w, [&] {
                QImage img = w.grab().toImage();
                const bool ok = img.save(out);
                const bool drawn = !w.viewport()->graphicsFailed();
                fprintf(stderr, "screenshot %s %s (%dx%d), started in %lld ms%s\n", ok ? "saved" : "FAILED",
                        qPrintable(out), img.width(), img.height(), static_cast<long long>(startup.elapsed()),
                        drawn ? "" : "; the 3D view could not draw (no OpenGL 3.3)");
                QApplication::exit(!ok ? 1 : drawn ? 0 : 3);  // 3: saved, but the model was not drawn
            });
        });
        t->start(25);
        return app.exec();
    }

    QTimer::singleShot(0, &w, [&] {
        w.checkRecovery();
        if (!QSettings().value("welcomeShown", false).toBool() && cli.positionalArguments().isEmpty()) {
            w.showWelcome();
        }
    });
    return app.exec();
}
