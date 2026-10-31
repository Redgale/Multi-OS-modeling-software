// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "jig/commands.hpp"

namespace jigtest
{

inline std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::string sample(const char* name)
{
    return std::string(JIG_SOURCE_DIR) + "/samples/" + name;
}

inline std::filesystem::path tempDir()
{
    auto d = std::filesystem::temp_directory_path() / "jigsmith-tests";
    std::filesystem::create_directories(d);
    return d;
}

// Builds a document from a script and fails the test on any script error.
inline std::unique_ptr<jig::Document> build(const std::string& script)
{
    auto doc = std::make_unique<jig::Document>();
    jig::ScriptRunner run(*doc);
    auto r = run.runText(script);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message + " " + r.error().detail);
    return doc;
}

inline std::unique_ptr<jig::Document> buildSample(const char* name)
{
    return build(readFile(sample(name)));
}

inline void run(jig::Document& doc, const std::string& line)
{
    jig::ScriptRunner r(doc);
    auto res = r.runText(line);
    ASSERT_TRUE(res.ok()) << line << ": " << (res.ok() ? "" : res.error().message);
}

inline jig::MassProperties props(const jig::Document& doc)
{
    return jig::massProperties(doc.combinedShape());
}

// Every active feature is OK (no warnings, no errors).
inline void expectAllOk(const jig::Document& doc)
{
    for (int i = 0; i < int(doc.model().features.size()); ++i) {
        const auto& st = doc.status(i);
        EXPECT_EQ(int(st.level), int(jig::StatusLevel::Ok))
            << doc.model().features[i].id << ": " << st.message;
    }
}

}  // namespace jigtest
