// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/expr.hpp"

#include <array>
#include <cctype>
#include <locale>
#include <cmath>
#include <numbers>
#include <sstream>

namespace jig
{
namespace
{

constexpr double kDegToRad = std::numbers::pi / 180.0;

struct UnitDef
{
    std::string_view name;
    double factor;  // to base unit
    Dim dim;
};

// Lengths convert to mm, angles to rad.
constexpr std::array<UnitDef, 12> kUnits {{
    {"mm", 1.0, {1, 0}},
    {"cm", 10.0, {1, 0}},
    {"m", 1000.0, {1, 0}},
    {"um", 0.001, {1, 0}},
    {"in", 25.4, {1, 0}},
    {"ft", 304.8, {1, 0}},
    {"yd", 914.4, {1, 0}},
    {"thou", 0.0254, {1, 0}},  // a thousandth of an inch
    {"mil", 0.0254, {1, 0}},
    {"deg", kDegToRad, {0, 1}},
    {"rad", 1.0, {0, 1}},
    {"turn", 2.0 * std::numbers::pi, {0, 1}},
}};

constexpr std::array<std::string_view, 12> kFunctions {
    "sqrt", "abs", "sin", "cos", "tan", "asin", "acos", "atan", "min", "max", "round", "floor"};

const UnitDef* findUnit(std::string_view name)
{
    for (const auto& u : kUnits) {
        if (u.name == name) {
            return &u;
        }
    }
    return nullptr;
}

bool isFunction(std::string_view name)
{
    for (auto f : kFunctions) {
        if (f == name) {
            return true;
        }
    }
    return name == "ceil";
}

std::string dimName(Dim d)
{
    if (d == Dim::none()) {
        return "a plain number";
    }
    if (d == Dim::len()) {
        return "a length";
    }
    if (d == Dim::ang()) {
        return "an angle";
    }
    if (d == Dim {2, 0}) {
        return "an area";
    }
    if (d == Dim {3, 0}) {
        return "a volume";
    }
    std::ostringstream os;
    os << "mm^" << d.length;
    if (d.angle) {
        os << " rad^" << d.angle;
    }
    return os.str();
}

struct Value
{
    double v = 0;
    Dim dim;
    bool bare = true;  // made only of unitless literals: may adopt a unit from context
};

class Parser
{
public:
    Parser(std::string_view text, const ParamLookup& lookup)
        : s_(text)
        , lookup_(lookup)
    {}

    Result<Value> parse()
    {
        auto v = parseExpr();
        if (!v) {
            return v;
        }
        skipWs();
        if (pos_ < s_.size()) {
            return fail("I don't understand '" + std::string(s_.substr(pos_)) + "'.");
        }
        return v;
    }

    std::vector<std::string> names;  // identifiers seen (for referencedNames)
    bool collectOnly = false;

private:
    std::string_view s_;
    size_t pos_ = 0;
    const ParamLookup& lookup_;

    Error fail(std::string msg)
    {
        return makeError("expr.syntax",
                         std::move(msg),
                         {"Check the expression, for example: 20 mm, width / 2, 2 in, 45 deg."});
    }

    void skipWs()
    {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }
    bool eat(char c)
    {
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool peekIdentStart()
    {
        skipWs();
        return pos_ < s_.size()
            && (std::isalpha(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_');
    }
    std::string_view ident()
    {
        size_t b = pos_;
        while (pos_ < s_.size()
               && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
            ++pos_;
        }
        return s_.substr(b, pos_ - b);
    }

    // Promote a bare number to the dimension `want` using the friendly unit for it
    // (mm for length, degrees for angle).
    static Value promote(Value v, Dim want)
    {
        if (v.bare && v.dim == Dim::none() && want != Dim::none()) {
            if (want == Dim::ang()) {
                v.v *= kDegToRad;
            }
            v.dim = want;
        }
        return v;
    }

    Result<Value> parseExpr()
    {
        auto lhs = parseTerm();
        if (!lhs) {
            return lhs;
        }
        Value a = lhs.value();
        for (;;) {
            char op = 0;
            if (eat('+')) {
                op = '+';
            }
            else if (eat('-')) {
                op = '-';
            }
            else {
                break;
            }
            auto rhs = parseTerm();
            if (!rhs) {
                return rhs;
            }
            Value b = rhs.value();
            a = promote(a, b.dim);
            b = promote(b, a.dim);
            if (!(a.dim == b.dim)) {
                return makeError("expr.units",
                                 "You can't add " + dimName(a.dim) + " and " + dimName(b.dim) + ".",
                                 {"Give both sides the same kind of unit, e.g. 10 mm + 2 mm."});
            }
            a.v = op == '+' ? a.v + b.v : a.v - b.v;
            a.bare = a.bare && b.bare;
        }
        return a;
    }

    Result<Value> parseTerm()
    {
        auto lhs = parseUnary();
        if (!lhs) {
            return lhs;
        }
        Value a = lhs.value();
        for (;;) {
            char op = 0;
            if (eat('*')) {
                op = '*';
            }
            else if (eat('/')) {
                op = '/';
            }
            else {
                break;
            }
            auto rhs = parseUnary();
            if (!rhs) {
                return rhs;
            }
            Value b = rhs.value();
            if (op == '*') {
                a.v *= b.v;
                a.dim = {a.dim.length + b.dim.length, a.dim.angle + b.dim.angle};
            }
            else {
                if (b.v == 0.0 && !collectOnly) {
                    return makeError("expr.div0", "This expression divides by zero.");
                }
                a.v = collectOnly ? 0 : a.v / b.v;
                a.dim = {a.dim.length - b.dim.length, a.dim.angle - b.dim.angle};
            }
            a.bare = a.bare && b.bare;
        }
        return a;
    }

    Result<Value> parseUnary()
    {
        if (eat('-')) {
            auto v = parseUnary();
            if (v) {
                v.value().v = -v.value().v;
            }
            return v;
        }
        if (eat('+')) {
            return parseUnary();
        }
        return parsePower();
    }

    Result<Value> parsePower()
    {
        auto base = parsePrimary();
        if (!base) {
            return base;
        }
        if (eat('^')) {
            auto ex = parseUnary();
            if (!ex) {
                return ex;
            }
            if (!(ex->dim == Dim::none())) {
                return makeError("expr.units", "A power must be a plain number.");
            }
            double e = ex->v;
            Value b = base.value();
            if (!(b.dim == Dim::none()) && std::abs(e - std::round(e)) > 1e-12) {
                return makeError("expr.units", "Only whole powers of a unit are allowed.");
            }
            int ie = static_cast<int>(std::lround(e));
            b.v = std::pow(b.v, e);
            b.dim = {b.dim.length * ie, b.dim.angle * ie};
            b.bare = b.bare && ex->bare;
            return b;
        }
        return base;
    }

    // Optional unit suffix right after a number or parenthesised group: "5 cm", "(a+b) in".
    Result<Value> applySuffixUnit(Value v)
    {
        size_t save = pos_;
        skipWs();
        if (pos_ < s_.size() && static_cast<unsigned char>(s_[pos_]) == 0xC2 && pos_ + 1 < s_.size()
            && static_cast<unsigned char>(s_[pos_ + 1]) == 0xB0) {  // UTF-8 degree sign
            pos_ += 2;
            if (!(v.dim == Dim::none())) {
                return makeError("expr.units", "A unit can only follow a plain number.");
            }
            return Value {v.v * kDegToRad, Dim::ang(), false};
        }
        if (peekIdentStart()) {
            size_t identPos = pos_;
            auto id = ident();
            if (const UnitDef* u = findUnit(id)) {
                if (!(v.dim == Dim::none())) {
                    return makeError("expr.units", "A unit can only follow a plain number.");
                }
                return Value {v.v * u->factor, u->dim, false};
            }
            pos_ = identPos;
        }
        pos_ = save;
        return v;
    }

    Result<Value> parsePrimary()
    {
        skipWs();
        if (pos_ >= s_.size()) {
            return fail("The expression ends too early.");
        }
        char c = s_[pos_];
        if (c == '(') {
            ++pos_;
            auto v = parseExpr();
            if (!v) {
                return v;
            }
            if (!eat(')')) {
                return fail("A closing ')' is missing.");
            }
            return applySuffixUnit(v.value());
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t b = pos_;
            while (pos_ < s_.size()
                   && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.')) {
                ++pos_;
            }
            // exponent: 1e3, 2.5e-2 (only when followed by a digit or sign+digit)
            if (pos_ + 1 < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
                size_t q = pos_ + 1;
                if (q < s_.size() && (s_[q] == '+' || s_[q] == '-')) {
                    ++q;
                }
                if (q < s_.size() && std::isdigit(static_cast<unsigned char>(s_[q]))) {
                    pos_ = q;
                    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                        ++pos_;
                    }
                }
            }
            std::string num(s_.substr(b, pos_ - b));
            // Locale-independent parse ('.' decimal point). std::from_chars for double is
            // missing from Apple's libc++ (Xcode 16), so use a classic-locale stream.
            double d = 0;
            std::istringstream in(num);
            in.imbue(std::locale::classic());
            in >> d;
            if (in.fail() || in.peek() != std::char_traits<char>::eof()) {
                return fail("'" + num + "' is not a number.");
            }
            return applySuffixUnit(Value {d, Dim::none(), true});
        }
        if (peekIdentStart()) {
            std::string name(ident());
            if (eat('(')) {
                return parseCall(name);
            }
            if (name == "pi") {
                return Value {std::numbers::pi, Dim::none(), true};
            }
            if (lookup_) {
                if (auto q = lookup_(name)) {
                    names.push_back(name);
                    return Value {q->value, q->dim, false};
                }
            }
            if (const UnitDef* u = findUnit(name)) {
                return Value {u->factor, u->dim, false};
            }
            names.push_back(name);
            if (collectOnly) {
                return Value {1.0, Dim::none(), false};
            }
            return makeError("expr.unknown_name",
                             "There is no parameter called '" + name + "'.",
                             {"Create it in the Parameters table, or check the spelling."});
        }
        return fail("Unexpected '" + std::string(1, c) + "'.");
    }

    Result<Value> parseCall(const std::string& name)
    {
        std::vector<Value> args;
        if (!eat(')')) {
            for (;;) {
                auto a = parseExpr();
                if (!a) {
                    return a;
                }
                args.push_back(a.value());
                if (eat(')')) {
                    break;
                }
                if (!eat(',') && !eat(';')) {
                    return fail("Expected ',' or ')' in " + name + "(...).");
                }
            }
        }
        auto need = [&](size_t n) -> std::optional<Error> {
            if (args.size() != n) {
                return makeError("expr.args",
                                 name + "() takes " + std::to_string(n) + " value"
                                     + (n == 1 ? "" : "s") + ".");
            }
            return std::nullopt;
        };
        bool bare = true;
        for (auto& a : args) {
            bare = bare && a.bare;
        }
        if (name == "sqrt") {
            if (auto e = need(1)) {
                return *e;
            }
            Value a = args[0];
            if (a.dim.length % 2 || a.dim.angle % 2) {
                return makeError("expr.units", "sqrt() of " + dimName(a.dim) + " has no unit.");
            }
            if (a.v < 0 && !collectOnly) {
                return makeError("expr.domain", "sqrt() of a negative value.");
            }
            return Value {std::sqrt(std::max(a.v, 0.0)), {a.dim.length / 2, a.dim.angle / 2}, bare};
        }
        if (name == "abs" || name == "round" || name == "floor" || name == "ceil") {
            if (auto e = need(1)) {
                return *e;
            }
            Value a = args[0];
            a.v = name == "abs" ? std::abs(a.v)
                : name == "round" ? std::round(a.v)
                : name == "floor" ? std::floor(a.v)
                                  : std::ceil(a.v);
            return a;
        }
        if (name == "min" || name == "max") {
            if (args.empty()) {
                return makeError("expr.args", name + "() needs at least one value.");
            }
            Value r = args[0];
            for (size_t i = 1; i < args.size(); ++i) {
                Value b = promote(args[i], r.dim);
                r = promote(r, b.dim);
                if (!(b.dim == r.dim)) {
                    return makeError("expr.units", name + "() values must have the same unit.");
                }
                r.v = name == "min" ? std::min(r.v, b.v) : std::max(r.v, b.v);
                r.bare = r.bare && b.bare;
            }
            return r;
        }
        if (name == "sin" || name == "cos" || name == "tan") {
            if (auto e = need(1)) {
                return *e;
            }
            Value a = promote(args[0], Dim::ang());
            if (!(a.dim == Dim::ang())) {
                return makeError("expr.units", name + "() needs an angle.");
            }
            double r = name == "sin" ? std::sin(a.v) : name == "cos" ? std::cos(a.v) : std::tan(a.v);
            return Value {r, Dim::none(), true};
        }
        if (name == "asin" || name == "acos" || name == "atan") {
            if (auto e = need(1)) {
                return *e;
            }
            if (!(args[0].dim == Dim::none())) {
                return makeError("expr.units", name + "() needs a plain number.");
            }
            double x = args[0].v;
            double r = name == "asin" ? std::asin(x) : name == "acos" ? std::acos(x) : std::atan(x);
            return Value {r, Dim::ang(), false};
        }
        return makeError("expr.unknown_function", "There is no function called '" + name + "'.");
    }
};

}  // namespace

Result<Quantity> evaluate(std::string_view text, const ParamLookup& lookup, Dim defaultUnit)
{
    Parser p(text, lookup);
    auto r = p.parse();
    if (!r) {
        return r.error();
    }
    Value v = r.value();
    if (!(defaultUnit == Dim::none())) {
        if (v.dim == Dim::none() && v.bare) {
            v.v *= defaultUnit == Dim::ang() ? kDegToRad : 1.0;
            v.dim = defaultUnit;
        }
        if (!(v.dim == defaultUnit)) {
            return makeError("expr.units",
                             "This field needs " + dimName(defaultUnit) + ", but the expression gives "
                                 + dimName(v.dim) + ".",
                             {defaultUnit == Dim::ang() ? "Try a value like 45 deg."
                                                        : "Try a value like 20 mm or 2 in."});
        }
    }
    if (!std::isfinite(v.v)) {
        return makeError("expr.domain", "The expression does not give a finite number.");
    }
    // the geometry kernel's tolerances are made for parts, not continents: past 10 km its
    // results are meaningless, so say so instead of failing somewhere deep inside it
    if (v.dim == Dim::len() && std::abs(v.v) > 1e7) {
        return makeError("expr.range", "That length is more than 10 km, which is larger than Jigsmith can model.",
                         {"Check the units and the number of zeros."});
    }
    return Quantity {v.v, v.dim};
}

std::vector<std::string> referencedNames(std::string_view text)
{
    ParamLookup none;
    Parser p(text, none);
    p.collectOnly = true;
    (void)p.parse();
    return p.names;
}

std::string formatQuantity(const Quantity& q)
{
    double v = q.value;
    std::string unit;
    if (q.dim == Dim::len()) {
        unit = " mm";
    }
    else if (q.dim == Dim::ang()) {
        v /= kDegToRad;
        unit = " deg";
    }
    else if (q.dim == Dim {2, 0}) {
        unit = " mm^2";
    }
    else if (q.dim == Dim {3, 0}) {
        unit = " mm^3";
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return std::string(buf) + unit;
}

std::string unitSymbol(LengthUnit u)
{
    switch (u) {
        case LengthUnit::Centimetre:
            return "cm";
        case LengthUnit::Metre:
            return "m";
        case LengthUnit::Inch:
            return "in";
        case LengthUnit::Foot:
            return "ft";
        case LengthUnit::Millimetre:
            break;
    }
    return "mm";
}

std::optional<LengthUnit> lengthUnitFromString(std::string_view s)
{
    if (s == "mm" || s == "millimetre" || s == "millimeter") {
        return LengthUnit::Millimetre;
    }
    if (s == "cm" || s == "centimetre" || s == "centimeter") {
        return LengthUnit::Centimetre;
    }
    if (s == "m" || s == "metre" || s == "meter") {
        return LengthUnit::Metre;
    }
    if (s == "in" || s == "inch" || s == "inches") {
        return LengthUnit::Inch;
    }
    if (s == "ft" || s == "foot" || s == "feet") {
        return LengthUnit::Foot;
    }
    return std::nullopt;
}

double millimetresPer(LengthUnit u)
{
    switch (u) {
        case LengthUnit::Centimetre:
            return 10;
        case LengthUnit::Metre:
            return 1000;
        case LengthUnit::Inch:
            return 25.4;
        case LengthUnit::Foot:
            return 304.8;
        case LengthUnit::Millimetre:
            break;
    }
    return 1;
}

namespace
{
std::string trimmed(double v, int decimals)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.setf(std::ios::fixed);
    os.precision(decimals);
    os << v;
    std::string s = os.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s == "-0" ? "0" : s;
}

int decimalsFor(LengthUnit u)
{
    return u == LengthUnit::Inch || u == LengthUnit::Foot || u == LengthUnit::Metre ? 4 : 3;
}

bool imperial(LengthUnit u)
{
    return u == LengthUnit::Inch || u == LengthUnit::Foot;
}
}  // namespace

std::string formatLength(double mm, LengthUnit u, bool withSymbol)
{
    const std::string v = trimmed(mm / millimetresPer(u), decimalsFor(u));
    return withSymbol ? v + " " + unitSymbol(u) : v;
}

std::string formatArea(double mm2, LengthUnit u)
{
    const double f = millimetresPer(u);
    return trimmed(mm2 / (f * f), decimalsFor(u)) + " " + unitSymbol(u) + "²";
}

std::string formatVolume(double mm3, LengthUnit u)
{
    if (!imperial(u)) {
        return trimmed(mm3 / 1000, 3) + " cm³";  // metric hobby convention, and 1 cm³ = 1 ml
    }
    const double f = millimetresPer(u);
    return trimmed(mm3 / (f * f * f), 4) + " " + unitSymbol(u) + "³";
}

std::string withLengthUnit(std::string_view expr, LengthUnit u, const ParamLookup& lookup)
{
    std::string e(expr);
    while (!e.empty() && std::isspace(static_cast<unsigned char>(e.back()))) {
        e.pop_back();
    }
    if (e.empty()) {
        return e;
    }
    auto q = evaluate(e, lookup);
    if (!q || !(q->dim == Dim::none())) {
        return e;
    }
    // plain numbers get a plain suffix; anything else is wrapped
    bool plain = true;
    for (char c : e) {
        plain = plain && (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ' ' || c == '-' || c == '+');
    }
    return (plain ? e : "(" + e + ")") + " " + unitSymbol(u);
}

bool isValidParameterName(std::string_view name)
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
        return false;
    }
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return !findUnit(name) && !isFunction(name) && name != "pi";
}

}  // namespace jig
