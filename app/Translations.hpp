// SPDX-License-Identifier: GPL-3.0-or-later
// Interface languages: translations built into the app (translations/*.ts, compiled by Qt
// Linguist when it is installed) plus Qt's own strings for standard dialogs.
#pragma once

#include <QString>
#include <QStringList>

#include <string>

namespace jigapp
{

// Language codes built into this copy, e.g. "qps" (the pseudo-translation for testing).
QStringList availableLanguages();
// A readable name for a code: "Deutsch", or "Pseudo-translation (for testing)".
QString languageName(const QString& code);
// A message from the core (an error explanation, a step status, a suggestion), translated
// when the language has it (app/core_strings.h lists them for the translation tools).
QString coreText(const std::string& s);
// Loads a language for the whole application ("" = the computer's language, falling back
// to English). Returns the code in use.
QString installLanguage(const QString& code);

}  // namespace jigapp
