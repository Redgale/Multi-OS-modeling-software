// SPDX-License-Identifier: GPL-3.0-or-later
// jigsmith-cli: run any Jigsmith feature without a window (architecture rule 1).
//
//   jigsmith-cli --run script.jigs [--report out.json] [--save out.jig]
//   jigsmith-cli --open design.jig --report -
//   jigsmith-cli --commands          list the command registry
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "jig/commands.hpp"
#include "jig/files.hpp"
#include "jig/io.hpp"
#include "jig/version.hpp"

namespace
{
void usage()
{
    std::cout << "Jigsmith " << JIG_VERSION_STRING << " (OCCT " << JIG_OCCT_VERSION_STRING << ")\n"
              << "Usage:\n"
              << "  jigsmith-cli --run <script.jigs> [--report <file|->] [--save <file.jig>]\n"
              << "  jigsmith-cli --open <design.jig> [--report <file|->]\n"
              << "  jigsmith-cli --commands\n"
              << "Jigsmith uses Open CASCADE Technology (https://dev.opencascade.org), LGPL-2.1.\n";
}

void printError(const jig::Error& e)
{
    std::cerr << "error: " << e.message << "\n";
    for (auto& s : e.suggestions) {
        std::cerr << "  try: " << s << "\n";
    }
    if (!e.detail.empty()) {
        std::cerr << "  (detail: " << e.detail << ")\n";
    }
}

// The arguments as UTF-8 (Jigsmith's paths are UTF-8). Windows hands argv over in the system
// code page, so there they are read again from the wide command line.
std::vector<std::string> utf8Arguments(int argc, char** argv)
{
    std::vector<std::string> out;
#ifdef _WIN32
    int n = 0;
    if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 0; i < n; ++i) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(size_t(len > 0 ? len - 1 : 0), '\0');
            if (len > 1) {
                WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
            }
            out.push_back(std::move(s));
        }
        LocalFree(w);
        return out;
    }
#endif
    for (int i = 0; i < argc; ++i) {
        out.emplace_back(argv[i]);
    }
    return out;
}
}  // namespace

int main(int argc, char** argv)
{
    std::string run, open, report, save;
    bool listCommands = false;
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    const int count = int(args.size());
    for (int i = 1; i < count; ++i) {
        std::string a = args[size_t(i)];
        auto next = [&]() -> std::string { return i + 1 < count ? args[size_t(++i)] : std::string {}; };
        if (a == "--run") {
            run = next();
        }
        else if (a == "--open") {
            open = next();
        }
        else if (a == "--report") {
            report = next();
        }
        else if (a == "--save") {
            save = next();
        }
        else if (a == "--commands") {
            listCommands = true;
        }
        else if (a == "--version") {
            std::cout << JIG_VERSION_STRING << "\n";
            return 0;
        }
        else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    if (listCommands) {
        for (auto& c : jig::CommandRegistry::instance().all()) {
            std::printf("%-26s %-28s %-6s %-9s %s\n", c.id.c_str(), c.standardName.c_str(), c.shortcut.c_str(),
                        c.available ? "ui" : (c.scripted ? "script" : "planned"),
                        c.available || c.scripted ? "" : ("coming in " + c.comingIn).c_str());
        }
        return 0;
    }
    std::unique_ptr<jig::Document> doc;
    if (!open.empty()) {
        auto r = jig::loadDocument(open);
        if (!r) {
            printError(r.error());
            return 1;
        }
        doc = std::move(r.value());
    }
    else {
        doc = std::make_unique<jig::Document>();
    }
    int rc = 0;
    if (!run.empty()) {
        jig::ScriptRunner runner(*doc);
        auto r = runner.runFile(run);
        for (auto& line : runner.output()) {
            std::cout << line << "\n";
        }
        if (!r) {
            printError(r.error());
            rc = 1;
        }
    }
    else if (open.empty()) {
        usage();
        return 2;
    }
    if (!report.empty()) {
        std::string text = jig::reportJson(*doc).dump(2) + "\n";
        if (report == "-") {
            std::cout << text;
        }
        else {
            std::ofstream(jig::pathFromUtf8(report)) << text;
        }
    }
    if (!save.empty()) {
        auto r = jig::saveDocument(*doc, save);
        if (!r) {
            printError(r.error());
            rc = 1;
        }
    }
    return rc;
}
