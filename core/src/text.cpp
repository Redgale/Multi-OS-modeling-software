// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch text: glyph outlines from FreeType turned into flat faces (brief 5.4f, D-014).
// No OCCT visualisation toolkits and no window system: the core stays headless.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <tuple>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_BezierCurve.hxx>
#include <ShapeFix_Face.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#ifdef JIG_HAVE_HARFBUZZ
#include <hb.h>
#endif

#include "jig/kernel.hpp"

extern const unsigned char jig_font_inter_regular[];
extern const std::size_t jig_font_inter_regular_size;

namespace jig
{
namespace
{

// One closed glyph contour in font units: a list of segments (2 points = line,
// 3 = quadratic, 4 = cubic Bezier), plus a dense polyline for containment tests.
struct Contour
{
    std::vector<std::vector<gp_XY>> segments;
    std::vector<gp_XY> poly;
    double area = 0;  // signed, from the polyline
};

struct Decomposer
{
    std::vector<Contour> contours;
    gp_XY last;

    void add(std::vector<gp_XY> seg)
    {
        auto& c = contours.back();
        // flatten for the containment test
        const int n = seg.size() == 2 ? 1 : 8;
        for (int k = 1; k <= n; ++k) {
            const double t = double(k) / n;
            gp_XY p;
            if (seg.size() == 2) {
                p = seg[0] * (1 - t) + seg[1] * t;
            }
            else if (seg.size() == 3) {
                p = seg[0] * ((1 - t) * (1 - t)) + seg[1] * (2 * t * (1 - t)) + seg[2] * (t * t);
            }
            else {
                const double u = 1 - t;
                p = seg[0] * (u * u * u) + seg[1] * (3 * u * u * t) + seg[2] * (3 * u * t * t) + seg[3] * (t * t * t);
            }
            c.poly.push_back(p);
        }
        last = seg.back();
        c.segments.push_back(std::move(seg));
    }
};

gp_XY xy(const FT_Vector* v)
{
    return gp_XY(double(v->x), double(v->y));
}

int moveTo(const FT_Vector* to, void* user)
{
    auto* d = static_cast<Decomposer*>(user);
    d->contours.emplace_back();
    d->last = xy(to);
    d->contours.back().poly.push_back(d->last);
    return 0;
}

int lineTo(const FT_Vector* to, void* user)
{
    auto* d = static_cast<Decomposer*>(user);
    if ((xy(to) - d->last).Modulus() > 1e-9) {
        d->add({d->last, xy(to)});
    }
    return 0;
}

int conicTo(const FT_Vector* c, const FT_Vector* to, void* user)
{
    auto* d = static_cast<Decomposer*>(user);
    d->add({d->last, xy(c), xy(to)});
    return 0;
}

int cubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
{
    auto* d = static_cast<Decomposer*>(user);
    d->add({d->last, xy(c1), xy(c2), xy(to)});
    return 0;
}

bool inside(const std::vector<gp_XY>& poly, const gp_XY& p)
{
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const gp_XY& a = poly[i];
        const gp_XY& b = poly[j];
        if ((a.Y() > p.Y()) != (b.Y() > p.Y()) && p.X() < (b.X() - a.X()) * (p.Y() - a.Y()) / (b.Y() - a.Y()) + a.X()) {
            in = !in;
        }
    }
    return in;
}

std::vector<char32_t> utf8(const std::string& s)
{
    std::vector<char32_t> out;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + size_t(n) > s.size()) {
            out.push_back(U'�');
            ++i;
            continue;
        }
        char32_t cp = n == 1 ? c : c & (0x7F >> n);
        for (int k = 1; k < n; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + size_t(k)]) & 0x3F);
        }
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

// FreeType is not thread-safe per library/face; one lock guards it.
std::mutex& ftLock()
{
    static std::mutex m;
    return m;
}

FT_Library library()
{
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        FT_Init_FreeType(&l);
        return l;
    }();
    return lib;
}

}  // namespace

std::vector<TopoDS_Face> facesFromOutlines(const std::vector<OutlineContour>& contours,
                                           const std::function<gp_Pnt(const gp_XY&)>& to3d, const gp_Pln& plane)
{
    // a dense polyline and signed area per contour, for the nesting test
    struct Info
    {
        std::vector<gp_XY> poly;
        double area = 0;
    };
    std::vector<Info> info;
    for (auto& c : contours) {
        Info in;
        for (auto& seg : c) {
            if (seg.empty()) {
                continue;
            }
            if (in.poly.empty()) {
                in.poly.push_back(seg.front());
            }
            const int k = seg.size() == 2 ? 1 : 8;
            for (int j = 1; j <= k; ++j) {
                const double t = double(j) / k, u = 1 - t;
                gp_XY p;
                if (seg.size() == 2) {
                    p = seg[0] * u + seg[1] * t;
                }
                else if (seg.size() == 3) {
                    p = seg[0] * (u * u) + seg[1] * (2 * t * u) + seg[2] * (t * t);
                }
                else {
                    p = seg[0] * (u * u * u) + seg[1] * (3 * u * u * t) + seg[2] * (3 * u * t * t) + seg[3] * (t * t * t);
                }
                in.poly.push_back(p);
            }
        }
        for (size_t i = 0, j = in.poly.empty() ? 0 : in.poly.size() - 1; i < in.poly.size(); j = i++) {
            in.area += in.poly[j].X() * in.poly[i].Y() - in.poly[i].X() * in.poly[j].Y();
        }
        in.area /= 2;
        info.push_back(std::move(in));
    }
    const size_t n = contours.size();
    // Outer contours have an even number of contours around them; holes an odd number.
    // This works whichever winding direction the source uses.
    std::vector<int> depth(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i != j && info[i].poly.size() > 2 && info[j].poly.size() > 2 &&
                std::abs(info[j].area) > std::abs(info[i].area) && inside(info[j].poly, info[i].poly.front())) {
                ++depth[i];
            }
        }
    }
    auto wireOf = [&](const OutlineContour& c) {
        BRepBuilderAPI_MakeWire mw;
        for (auto& seg : c) {
            if (seg.size() == 2) {
                const gp_Pnt a = to3d(seg[0]), b = to3d(seg[1]);
                if (a.Distance(b) < 1e-9) {
                    continue;
                }
                mw.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
            }
            else if (seg.size() >= 3) {
                TColgp_Array1OfPnt poles(1, int(seg.size()));
                for (size_t k = 0; k < seg.size(); ++k) {
                    poles.SetValue(int(k) + 1, to3d(seg[k]));
                }
                Handle(Geom_BezierCurve) bez = new Geom_BezierCurve(poles);
                mw.Add(BRepBuilderAPI_MakeEdge(bez).Edge());
            }
        }
        return mw;
    };
    std::vector<TopoDS_Face> out;
    for (size_t i = 0; i < n; ++i) {
        if (depth[i] % 2 != 0 || info[i].poly.size() < 3 || std::abs(info[i].area) < 1e-9) {
            continue;  // a hole (added to its outer contour below) or nothing at all
        }
        auto outer = wireOf(contours[i]);
        if (!outer.IsDone()) {
            continue;
        }
        BRepBuilderAPI_MakeFace mf(plane, outer.Wire(), true);
        for (size_t j = 0; j < n; ++j) {
            if (depth[j] == depth[i] + 1 && info[j].poly.size() > 2 && std::abs(info[j].area) < std::abs(info[i].area) &&
                inside(info[i].poly, info[j].poly.front())) {
                auto hole = wireOf(contours[j]);
                if (hole.IsDone()) {
                    mf.Add(hole.Wire());
                }
            }
        }
        if (!mf.IsDone()) {
            continue;
        }
        ShapeFix_Face fix(mf.Face());
        fix.FixOrientation();
        fix.Perform();
        const TopoDS_Face f = TopoDS::Face(fix.Face());
        if (BRepCheck_Analyzer(f).IsValid()) {
            out.push_back(f);
        }
    }
    return out;
}

// ---------------------------------------------------------------- installed fonts

namespace
{
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::vector<std::filesystem::path> fontFolders()
{
    std::vector<std::filesystem::path> dirs;
    auto env = [](const char* n) -> std::string {
        const char* v = std::getenv(n);
        return v ? v : "";
    };
    const std::string home = env("HOME");
#if defined(_WIN32)
    const std::string win = env("WINDIR").empty() ? "C:\\Windows" : env("WINDIR");
    dirs.push_back(std::filesystem::path(win) / "Fonts");
    if (!env("LOCALAPPDATA").empty()) {
        dirs.push_back(std::filesystem::path(env("LOCALAPPDATA")) / "Microsoft" / "Windows" / "Fonts");
    }
#elif defined(__APPLE__)
    dirs = {"/System/Library/Fonts", "/Library/Fonts"};
    if (!home.empty()) {
        dirs.push_back(std::filesystem::path(home) / "Library" / "Fonts");
    }
#else
    dirs = {"/usr/share/fonts", "/usr/local/share/fonts"};
    if (!home.empty()) {
        dirs.push_back(std::filesystem::path(home) / ".local" / "share" / "fonts");
        dirs.push_back(std::filesystem::path(home) / ".fonts");
    }
#endif
    // extra folders, separated like PATH
    std::string extra = env("JIG_FONT_DIRS");
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    for (size_t from = 0; from < extra.size();) {
        const size_t at = std::min(extra.find(sep, from), extra.size());
        if (at > from) {
            dirs.push_back(extra.substr(from, at - from));
        }
        from = at + 1;
    }
    return dirs;
}
}  // namespace

const std::vector<FontFace>& installedFonts(bool rescan)
{
    static std::vector<FontFace> fonts;
    static bool scanned = false;
    static std::mutex scanLock;
    std::lock_guard<std::mutex> once(scanLock);
    if (scanned && !rescan) {
        return fonts;
    }
    scanned = true;
    fonts.clear();
    {
        std::lock_guard<std::mutex> guard(ftLock());
        FT_Library lib = library();
        for (const auto& dir : lib ? fontFolders() : std::vector<std::filesystem::path> {}) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) {
                continue;
            }
            for (auto it = std::filesystem::recursive_directory_iterator(
                     dir, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                const std::string ext = lower(it->path().extension().string());
                if (ext != ".ttf" && ext != ".otf" && ext != ".ttc" && ext != ".otc") {
                    continue;
                }
                const std::string path = it->path().string();
                FT_Face probe = nullptr;
                if (FT_New_Face(lib, path.c_str(), -1, &probe) != 0) {
                    continue;
                }
                const long n = probe->num_faces;
                FT_Done_Face(probe);
                for (long i = 0; i < n && i < 64; ++i) {
                    FT_Face f = nullptr;
                    if (FT_New_Face(lib, path.c_str(), i, &f) != 0) {
                        continue;
                    }
                    if (f->family_name && FT_IS_SCALABLE(f)) {
                        fonts.push_back({f->family_name, f->style_name ? f->style_name : "Regular", path, int(i)});
                    }
                    FT_Done_Face(f);
                }
            }
        }
        std::sort(fonts.begin(), fonts.end(), [](const FontFace& a, const FontFace& b) {
            return std::tie(a.family, a.style, a.path) < std::tie(b.family, b.style, b.path);
        });
    }
    return fonts;
}

std::optional<FontFace> findFont(const std::string& wanted)
{
    const std::string w = lower(wanted);
    std::optional<FontFace> best;
    int bestScore = -1;
    for (const auto& f : installedFonts()) {
        const std::string fam = lower(f.family);
        if (w.rfind(fam, 0) != 0) {
            continue;  // the family must start the request
        }
        std::string rest = w.substr(fam.size());
        rest.erase(0, rest.find_first_not_of(' '));
        if (!rest.empty() && w.size() > fam.size() && w[fam.size()] != ' ') {
            continue;  // "DejaVu Sans Mono" is not "DejaVu Sans" + "Mono" style... unless no better
        }
        const std::string style = lower(f.style);
        int score = 0;
        if (rest.empty()) {
            score = style == "regular" || style == "book" || style == "normal" || style == "roman" ? 3 : 1;
        }
        else if (style == rest) {
            score = 4;
        }
        else {
            continue;
        }
        score = score * 100 + int(fam.size());  // the longest family that matches wins
        if (score > bestScore) {
            bestScore = score;
            best = f;
        }
    }
    return best;
}

Result<TextOutline> textFaces(const SketchText& t, double x, double y, const gp_Ax3& frame)
{
    if (!t.font.empty() && t.font != "Inter" && !std::filesystem::exists(t.font)) {
        (void)installedFonts();  // the first scan takes the FreeType lock itself
    }
    std::lock_guard<std::mutex> guard(ftLock());
    FT_Library lib = library();
    if (!lib) {
        return makeError("text.font", "The text engine could not start.");
    }
    TextOutline result;
    FT_Face face = nullptr;
    std::string facePath;  // where the face came from, for the shaper (empty: the built-in font)
    int faceIndex = 0;
    if (!t.font.empty() && std::filesystem::exists(t.font)) {
        facePath = t.font;
        if (FT_New_Face(lib, t.font.c_str(), 0, &face) != 0) {
            face = nullptr;
            result.note = fmt("The font file %1 could not be read; using the built-in font (Inter).", {t.font});
        }
    }
    else if (!t.font.empty() && t.font != "Inter") {
        // an installed font by family name (and style)
        const std::optional<FontFace> found = findFont(t.font);  // (fonts were scanned before the lock)
        if (found && FT_New_Face(lib, found->path.c_str(), found->index, &face) != 0) {
            face = nullptr;
        }
        if (face) {
            facePath = found->path;
            faceIndex = found->index;
        }
        if (!face) {
            result.note = fmt("The font '%1' is not available here; using the built-in font (Inter).", {t.font});
        }
    }
    if (!face) {
        facePath.clear();
        faceIndex = 0;
    }
    if (!face && FT_New_Memory_Face(lib, jig_font_inter_regular, FT_Long(jig_font_inter_regular_size), 0, &face) != 0) {
        return makeError("text.font", "The built-in font could not be loaded.");
    }
    struct FaceCloser
    {
        FT_Face f;
        ~FaceCloser()
        {
            FT_Done_Face(f);
        }
    } closer {face};

    // Unscaled outlines (font units), scaled exactly to the requested size: no hinting.
    const double scale = t.heightValue / double(face->units_per_EM);
    FT_Outline_Funcs funcs {};
    funcs.move_to = moveTo;
    funcs.line_to = lineTo;
    funcs.conic_to = conicTo;
    funcs.cubic_to = cubicTo;

    struct Glyph
    {
        std::vector<Contour> contours;
        double penX, penY;
        int line;
    };
    std::vector<Glyph> glyphs;
    double pen = 0, penY = 0;
    int line = 0;
    // the font's own line spacing (or 1.2 em when it gives none)
    const double lineStep = face->height > 0 ? double(face->height) : 1.2 * double(face->units_per_EM);
    std::vector<std::pair<double, double>> lineX;  // per line: leftmost and rightmost outline point
    double ymin = 1e300, ymax = -1e300;
    // "\n" (a backslash and n, as typed in a one-line field or a script) starts a new line too
    std::string text = t.text;
    for (size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n", at)) {
        text.replace(at, 2, "\n");
    }
    // Each line as glyphs with their advances and offsets, in font units, left to right as seen.
    struct Placed
    {
        FT_UInt gi;
        double advance, dx, dy;
    };
    std::vector<std::vector<Placed>> lines;
    std::vector<std::string> lineTexts;
    for (size_t from = 0;;) {
        const size_t nl = text.find('\n', from);
        lineTexts.push_back(text.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        if (nl == std::string::npos) {
            break;
        }
        from = nl + 1;
    }
#ifdef JIG_HAVE_HARFBUZZ
    hb_blob_t* blob = facePath.empty()
                          ? hb_blob_create(reinterpret_cast<const char*>(jig_font_inter_regular),
                                           unsigned(jig_font_inter_regular_size), HB_MEMORY_MODE_READONLY, nullptr,
                                           nullptr)
                          : hb_blob_create_from_file(facePath.c_str());
    hb_face_t* hface = hb_face_create(blob, unsigned(faceIndex));
    hb_font_t* hfont = hb_font_create(hface);
    hb_font_set_scale(hfont, int(face->units_per_EM), int(face->units_per_EM));  // font units
    const bool canShape = hb_face_get_glyph_count(hface) > 0;
    for (const std::string& lt : lineTexts) {
        std::vector<Placed> row;
        if (canShape) {
            hb_buffer_t* buf = hb_buffer_create();
            hb_buffer_add_utf8(buf, lt.c_str(), int(lt.size()), 0, int(lt.size()));
            hb_buffer_guess_segment_properties(buf);  // script, language, direction from the text
            hb_shape(hfont, buf, nullptr, 0);
            unsigned n = 0;
            const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
            const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
            for (unsigned i = 0; i < n; ++i) {
                row.push_back({FT_UInt(info[i].codepoint), double(pos[i].x_advance), double(pos[i].x_offset),
                               double(pos[i].y_offset)});
            }
            hb_buffer_destroy(buf);
        }
        lines.push_back(std::move(row));
    }
    hb_font_destroy(hfont);
    hb_face_destroy(hface);
    hb_blob_destroy(blob);
    result.shaped = canShape;
#else
    const bool canShape = false;
#endif
    if (!canShape) {
        // letters one by one, with the font's pair kerning
        lines.clear();
        const bool kerning = FT_HAS_KERNING(face);
        for (const std::string& lt : lineTexts) {
            std::vector<Placed> row;
            FT_UInt prev = 0;
            for (char32_t cp : utf8(lt)) {
                const FT_UInt gi = FT_Get_Char_Index(face, FT_ULong(cp));
                if (kerning && prev && gi && !row.empty()) {
                    FT_Vector k;
                    if (FT_Get_Kerning(face, prev, gi, FT_KERNING_UNSCALED, &k) == 0) {
                        row.back().advance += double(k.x);
                    }
                }
                prev = gi;
                double adv = 0;
                if (FT_Load_Glyph(face, gi, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0) {
                    adv = double(face->glyph->advance.x);
                }
                row.push_back({gi, adv, 0, 0});
            }
            lines.push_back(std::move(row));
        }
    }
    lineX.push_back({1e300, -1e300});
    for (const auto& row : lines) {
        if (&row != &lines.front()) {
            pen = 0;
            penY -= lineStep;
            ++line;
            lineX.push_back({1e300, -1e300});
        }
        for (const Placed& pl : row) {
            const FT_UInt gi = pl.gi;
            result.glyphs.push_back(unsigned(gi));
            const double gx = pen + pl.dx, gy = penY + pl.dy;
            pen += pl.advance;  // (the glyph sits at gx, gy)
            if (FT_Load_Glyph(face, gi, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
                continue;
            }
            if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
                Decomposer d;
                FT_Outline_Decompose(&face->glyph->outline, &funcs, &d);
                Glyph g {{}, gx, gy, line};
                for (auto& c : d.contours) {
                    if (c.segments.empty()) {
                        continue;
                    }
                    // close the contour if the font left it open
                    if ((c.segments.back().back() - c.segments.front().front()).Modulus() > 1e-9) {
                        c.segments.push_back({c.segments.back().back(), c.segments.front().front()});
                    }
                    double a = 0;
                    for (size_t i = 0, j = c.poly.size() - 1; i < c.poly.size(); j = i++) {
                        a += c.poly[j].X() * c.poly[i].Y() - c.poly[i].X() * c.poly[j].Y();
                    }
                    c.area = a / 2;
                    if (std::abs(c.area) < 1e-6 || c.poly.size() < 3) {
                        continue;
                    }
                    for (auto& p : c.poly) {
                        lineX[size_t(line)].first = std::min(lineX[size_t(line)].first, p.X() + gx);
                        lineX[size_t(line)].second = std::max(lineX[size_t(line)].second, p.X() + gx);
                        ymin = std::min(ymin, p.Y() + gy);
                        ymax = std::max(ymax, p.Y() + gy);
                    }
                    g.contours.push_back(std::move(c));
                }
                if (!g.contours.empty()) {
                    glyphs.push_back(std::move(g));
                }
            }
        }
    }
    if (glyphs.empty()) {
        return result;  // empty or whitespace-only text: no areas
    }

    // Centre each line on its own, then the block on the anchor, in the sketch plane.
    for (Glyph& g : glyphs) {
        const auto [l0, l1] = lineX[size_t(g.line)];
        g.penX -= (l0 + l1) / 2;
    }
    const gp_XY centre(0, (ymin + ymax) / 2);
    const double ca = std::cos(t.angle), sa = std::sin(t.angle);
    // along a curve: the baseline's middle at the point of the curve nearest the anchor; it
    // reads the way that keeps the letters upright (left to right as seen), letters standing
    // on the left of the way it reads; the text's middle height sits on the curve
    const auto& P = t.pathPts;
    const bool bent = P.size() >= 2;
    std::vector<double> along(P.size(), 0.0);  // distance along the curve to each point
    double s0 = 0, dir = 1;
    if (bent) {
        for (size_t k = 1; k < P.size(); ++k) {
            along[k] = along[k - 1] + std::hypot(P[k].first - P[k - 1].first, P[k].second - P[k - 1].second);
        }
        double best = 1e300;
        for (size_t k = 0; k + 1 < P.size(); ++k) {
            const double ax = P[k].first, ay = P[k].second, bx = P[k + 1].first, by = P[k + 1].second;
            const double L2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
            const double w = L2 > 0 ? std::clamp(((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / L2, 0.0, 1.0) : 0;
            const double d = std::hypot(ax + w * (bx - ax) - x, ay + w * (by - ay) - y);
            if (d < best) {
                best = d;
                s0 = along[k] + w * std::sqrt(L2);
                dir = (bx - ax) >= -1e-12 ? 1 : -1;  // read so that the text goes rightwards
            }
        }
    }
    // the point and the reading direction at distance s along the curve (straight on past its ends)
    auto onPath = [&](double s, double& px, double& py, double& tx, double& ty) {
        size_t k = 0;
        while (k + 2 < P.size() && along[k + 1] < s) {
            ++k;
        }
        const double L = along[k + 1] - along[k];
        tx = L > 0 ? (P[k + 1].first - P[k].first) / L : 1;
        ty = L > 0 ? (P[k + 1].second - P[k].second) / L : 0;
        const double w = s - along[k];
        px = P[k].first + tx * w;
        py = P[k].second + ty * w;
    };
    auto to3d = [&](const gp_XY& fontPt, double penX, double pY) {
        const double u = (fontPt.X() + penX - centre.X()) * scale;
        const double v = (fontPt.Y() + pY - centre.Y()) * scale;
        double lx = x + u * ca - v * sa;
        double ly = y + u * sa + v * ca;
        if (bent) {
            double px, py, tx, ty;
            onPath(s0 + dir * u, px, py, tx, ty);
            tx *= dir;
            ty *= dir;
            lx = px - ty * v;  // the left of the reading direction is up for the letters
            ly = py + tx * v;
        }
        return gp_Pnt(frame.Location().XYZ() + frame.XDirection().XYZ() * lx + frame.YDirection().XYZ() * ly);
    };
    const gp_Pln plane(frame);

    try {
        for (const Glyph& g : glyphs) {
            std::vector<OutlineContour> cs;
            for (auto& c : g.contours) {
                cs.push_back(c.segments);
            }
            const double penX = g.penX, pY = g.penY;
            auto faces = facesFromOutlines(cs, [&](const gp_XY& p) { return to3d(p, penX, pY); }, plane);
            result.faces.insert(result.faces.end(), faces.begin(), faces.end());
        }
        return result;
    }
    catch (const Standard_Failure& f) {
        const char* m = f.GetMessageString();
        return makeError("text.outline", "This text could not be turned into shapes.", {"Try another font."},
                         m ? m : "");
    }
    return result;
}

}  // namespace jig
