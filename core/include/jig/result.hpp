// SPDX-License-Identifier: GPL-3.0-or-later
// Plain-language errors. Every failure that can reach a user is an Error with a
// message written for a hobbyist, plus optional suggested repairs (Easy Contract
// principle 11). Raw kernel exception text only ever goes into `detail`.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace jig
{

// A one-click repair: set a step's parameter to a value ("size" = the step's main size,
// renamed to the real parameter by the step that reports it).
struct Fix
{
    std::string label;  // "Use 1.8 mm"
    std::string param;  // "radius", "thickness", "op", …
    std::string value;  // "1.8 mm", "new", …
};

struct Error
{
    std::string code;                      // stable machine id, e.g. "fillet.radius_too_large"
    std::string message;                   // one plain-language sentence
    std::vector<std::string> suggestions;  // short actionable repairs
    std::string detail;                    // technical detail for logs / "more info"
    std::vector<Fix> fixes;                // one-click repairs, when there is a sure one
};

template<typename T>
class Result
{
public:
    Result(T value)
        : v_(std::move(value))
    {}
    Result(Error err)
        : v_(std::move(err))
    {}

    bool ok() const
    {
        return std::holds_alternative<T>(v_);
    }
    explicit operator bool() const
    {
        return ok();
    }
    T& value()
    {
        return std::get<T>(v_);
    }
    const T& value() const
    {
        return std::get<T>(v_);
    }
    T* operator->()
    {
        return &value();
    }
    const T* operator->() const
    {
        return &value();
    }
    T& operator*()
    {
        return value();
    }
    const Error& error() const
    {
        return std::get<Error>(v_);
    }

private:
    std::variant<T, Error> v_;
};

inline Error makeError(std::string code,
                       std::string message,
                       std::vector<std::string> suggestions = {},
                       std::string detail = {})
{
    return Error {std::move(code), std::move(message), std::move(suggestions), std::move(detail), {}};
}

}  // namespace jig
