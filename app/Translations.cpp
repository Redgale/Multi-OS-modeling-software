// SPDX-License-Identifier: GPL-3.0-or-later
#include "Translations.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace jigapp
{

QString coreText(const std::string& s)
{
    return QCoreApplication::translate("Core", s.c_str());
}

QStringList availableLanguages()
{
    QStringList out;
    for (const QString& f : QDir(":/i18n").entryList({"jigsmith_*.qm"})) {
        out << f.mid(9, f.size() - 9 - 3);  // jigsmith_<code>.qm
    }
    return out;
}

QString languageName(const QString& code)
{
    if (code == "qps") {
        return QCoreApplication::translate("Settings", "Pseudo-translation (for testing)");
    }
    const QLocale l(code);
    return l.nativeLanguageName().isEmpty() ? code : l.nativeLanguageName();
}

QString installLanguage(const QString& code)
{
    static QTranslator* app = nullptr;
    static QTranslator* qt = nullptr;
    for (QTranslator** t : {&app, &qt}) {
        if (*t) {
            QCoreApplication::removeTranslator(*t);
            delete *t;
            *t = nullptr;
        }
    }
    QString use = code;
    if (use.isEmpty()) {  // the computer's language, when there is a translation for it
        const QLocale sys = QLocale::system();
        for (const QString& c : availableLanguages()) {
            if (c == sys.name() || c == sys.name().section('_', 0, 0)) {
                use = c;
            }
        }
    }
    if (use.isEmpty() || use == "en") {
        return "en";
    }
    app = new QTranslator;
    if (!app->load(":/i18n/jigsmith_" + use + ".qm")) {
        delete app;
        app = nullptr;
        return "en";
    }
    QCoreApplication::installTranslator(app);
    qt = new QTranslator;  // standard buttons and dialogs from Qt itself
    if (qt->load(QLocale(use), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qt);
    }
    else {
        delete qt;
        qt = nullptr;
    }
    return use;
}

}  // namespace jigapp
