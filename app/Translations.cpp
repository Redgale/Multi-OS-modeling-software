// SPDX-License-Identifier: GPL-3.0-or-later
#include "Translations.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTranslator>

#include <algorithm>
#include <vector>

#include "core_strings.h"  // kCorePatterns

namespace jigapp
{

namespace
{
// Fills %1, %2… in one pass (a value that itself contains "%2" is left alone).
QString fill(const QString& pattern, const QStringList& values)
{
    QString out;
    for (int i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c == '%' && i + 1 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
            out += values.value(pattern[i + 1].digitValue() - 1);
            ++i;
        }
        else {
            out += c;
        }
    }
    return out;
}

struct Pattern
{
    const char* english;
    QRegularExpression match;
    std::vector<int> order;  // which placeholder each captured group is
};

const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> all = [] {
        std::vector<Pattern> out;
        static const QRegularExpression ph("%([1-9])");
        for (const char* p : kCorePatterns) {
            const QString en = QString::fromUtf8(p);
            QString re = "^";
            std::vector<int> order;
            std::vector<bool> seen(10, false);
            int last = 0;
            for (auto it = ph.globalMatch(en); it.hasNext();) {
                const auto m = it.next();
                re += QRegularExpression::escape(en.mid(last, m.capturedStart() - last));
                const int n = m.captured(1).toInt();
                if (seen[size_t(n)]) {
                    re += QString("\\%1").arg(std::find(order.begin(), order.end(), n) - order.begin() + 1);
                }
                else {
                    seen[size_t(n)] = true;
                    order.push_back(n);
                    re += "((?:(?![.?!] \\S).)+?)";  // a value never runs past the end of a sentence
                }
                last = int(m.capturedEnd());
            }
            re += QRegularExpression::escape(en.mid(last)) + "$";
            out.push_back({p, QRegularExpression(re), order});
        }
        return out;
    }();
    return all;
}

// One sentence: as written, or as a pattern with values.
QString translateOne(const QString& s)
{
    const QByteArray utf8 = s.toUtf8();
    const QString exact = QCoreApplication::translate("Core", utf8.constData());
    if (exact != s) {
        return exact;
    }
    for (const auto& p : patterns()) {
        const auto m = p.match.match(s);
        if (!m.hasMatch()) {
            continue;
        }
        QStringList values(9);
        for (size_t g = 0; g < p.order.size(); ++g) {
            values[p.order[g] - 1] = m.captured(int(g) + 1);
        }
        return fill(QCoreApplication::translate("Core", p.english), values);
    }
    return s;
}
}  // namespace

QString coreText(const std::string& text)
{
    const QString s = QString::fromStdString(text);
    QString whole = translateOne(s);
    if (whole != s) {
        return whole;
    }
    // several sentences put together ("2 of the edges … no longer exist. 1 edge was split, …"):
    // each on its own
    static const QRegularExpression end("(?<=[.?!]) (?=\\S)");
    const QStringList parts = s.split(end);
    if (parts.size() < 2) {
        return s;
    }
    QStringList out;
    for (const QString& p : parts) {
        out << translateOne(p);
    }
    return out.join(' ');
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
