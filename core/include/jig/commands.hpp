// SPDX-License-Identifier: GPL-3.0-or-later
// Data-described commands (architecture rule 7). One registry feeds the command
// palette, the toolbar, the keymap, tooltips and (later) Python scripting. Every
// v0.1 tool is listed, including ones not built yet: those report
// `available == false` and a "coming in ..." note, so the UI can show them
// disabled instead of pretending they work (brief section 4, release discipline).
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "jig/document.hpp"
#include "jig/result.hpp"

namespace jig
{

struct CommandInfo
{
    std::string id;            // "solid.extrude"
    std::string label;         // plain-language label shown on buttons: "Extrude"
    std::string standardName;  // the common CAD name, always shown in search results
    std::string panel;         // "CREATE", "MODIFY", "CONSTRUCT", "INSPECT", "INSERT", "SELECT",
                               // "SKETCH CREATE", "SKETCH MODIFY", "CONSTRAINTS", "FILE", "EDIT", "VIEW"
    std::string tooltip;       // one line: what it does and when to use it
    std::string shortcut;      // default key, e.g. "E", "Ctrl+Z" (Ctrl means Cmd on macOS)
    std::vector<std::string> aliases;  // plain-language search terms
    bool essential = false;    // part of the ~25 tools shown by default
    bool available = false;    // usable from the desktop UI in this build
    bool scripted = false;     // works headlessly (scripts / CLI) in this build
    std::string comingIn;      // milestone/release note when not available, e.g. "M3 (v0.1)"
};

class CommandRegistry
{
public:
    static const CommandRegistry& instance();

    const std::vector<CommandInfo>& all() const
    {
        return commands_;
    }
    const CommandInfo* find(std::string_view id) const;

    struct Match
    {
        const CommandInfo* info;
        int score;  // higher is better
    };
    // Searches labels, standard names and aliases ("round the edges" finds Fillet).
    std::vector<Match> search(std::string_view query, bool includeUnavailable = true) const;

private:
    CommandRegistry();
    std::vector<CommandInfo> commands_;
};

// ---------------------------------------------------------------- scripting

// Runs Jigsmith command scripts (one command per line, `#` comments). This is the
// headless entry point behind `jigsmith-cli --run`, used by every geometry test.
// Syntax: `verb key=value key="quoted value"`. See docs/SCRIPTING.md.
class ScriptRunner
{
public:
    explicit ScriptRunner(Document& doc)
        : doc_(doc)
    {}

    Result<bool> runLine(std::string_view line);
    // Stops at the first failing line; the error names the line number.
    Result<bool> runText(std::string_view text);
    Result<bool> runFile(const std::string& path);

    // Lines printed by commands such as `report` and `status`.
    const std::vector<std::string>& output() const
    {
        return output_;
    }
    // Most recent feature id created by a command.
    const std::string& lastFeature() const
    {
        return last_;
    }
    // When true (default), a feature that ends in an Error makes the line fail.
    bool strict = true;

private:
    Document& doc_;
    std::vector<std::string> output_;
    std::string last_;
};

// JSON summary of the document: per-body mass properties and per-feature status.
nlohmann::json reportJson(const Document& doc);

}  // namespace jig
