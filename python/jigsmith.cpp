// SPDX-License-Identifier: GPL-3.0-or-later
// Python bindings for the Jigsmith core. Everything goes through the core's command layer
// (the same one the app and .jigs scripts use), so Python can do what a script can, and
// errors come back as JigsmithError with the core's explanation and suggestions.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "jig/commands.hpp"
#include "jig/document.hpp"
#include "jig/io.hpp"
#include "jig/kernel.hpp"

namespace py = pybind11;
using namespace jig;

namespace
{
PyObject* errorType = nullptr;

[[noreturn]] void raise(const Error& e)
{
    std::string text = e.message;
    for (const auto& s : e.suggestions) {
        text += "\n  try: " + s;
    }
    PyErr_SetString(errorType, text.c_str());
    throw py::error_already_set();
}

template <typename T>
T check(Result<T> r)
{
    if (!r) {
        raise(r.error());
    }
    return std::move(r.value());
}

py::dict propsOf(const TopoDS_Shape& s)
{
    const MassProperties m = massProperties(s);
    py::dict d;
    d["volume"] = m.volume;
    d["area"] = m.area;
    d["centre"] = py::make_tuple(m.cx, m.cy, m.cz);
    d["bbox"] = py::make_tuple(py::make_tuple(m.xmin, m.ymin, m.zmin), py::make_tuple(m.xmax, m.ymax, m.zmax));
    d["faces"] = m.faces;
    d["solids"] = m.solids;
    d["valid"] = m.valid;
    return d;
}

// A design: owns its Document and the script runner that keeps names between calls.
struct Design
{
    std::unique_ptr<Document> doc = std::make_unique<Document>();
    std::unique_ptr<ScriptRunner> runner = std::make_unique<ScriptRunner>(*doc);

    void reset(std::unique_ptr<Document> d)
    {
        doc = std::move(d);
        runner = std::make_unique<ScriptRunner>(*doc);
    }
};
}  // namespace

PYBIND11_MODULE(jigsmith, m)
{
    m.doc() = "Jigsmith: parametric CAD from Python. Commands are those of .jigs scripts (docs/SCRIPTING.md).";
    m.attr("__version__") = appVersion();
    errorType = PyErr_NewException("jigsmith.JigsmithError", PyExc_RuntimeError, nullptr);
    m.attr("JigsmithError") = py::handle(errorType);

    py::class_<Design>(m, "Design", "A Jigsmith design: run commands, set parameters, measure, save and export.")
        .def(py::init<>())
        .def_static(
            "open",
            [](const std::string& path) {
                auto d = std::make_unique<Design>();
                d->reset(check(loadDocument(path)));
                return d;
            },
            py::arg("path"), "Opens a .jig design.")
        .def(
            "run",
            [](Design& d, const std::string& text) { check(d.runner->runText(text)); },
            py::arg("commands"),
            "Runs one or more script commands, e.g. 'box length=20mm width=10mm height=5mm'. "
            "Raises JigsmithError (with the line) when one fails; earlier lines stay done.")
        .def(
            "set_param",
            [](Design& d, const std::string& name, const std::string& expr) {
                check(d.runner->runLine("param " + name + " = " + expr));
            },
            py::arg("name"), py::arg("expression"), "Sets (or adds) a named parameter, e.g. set_param('wall', '2.4 mm').")
        .def(
            "param",
            [](Design& d, const std::string& name) {
                const Quantity q = check(d.doc->evalParam(name));
                return q.value;
            },
            py::arg("name"), "A parameter's value in millimetres (lengths), radians (angles) or plain numbers.")
        .def_property_readonly(
            "params",
            [](Design& d) {
                py::dict out;
                for (const auto& p : d.doc->model().parameters) {
                    out[py::str(p.name)] = p.expr;
                }
                return out;
            },
            "Every parameter's expression by name.")
        .def_property_readonly(
            "steps",
            [](Design& d) {
                py::list out;
                const auto& fs = d.doc->model().features;
                for (size_t i = 0; i < fs.size(); ++i) {
                    const FeatureStatus& st = d.doc->status(int(i));
                    py::dict s;
                    s["id"] = fs[i].id;
                    s["name"] = fs[i].name;
                    s["kind"] = std::string(toString(fs[i].kind));
                    s["status"] = st.level == StatusLevel::Ok ? "ok" : st.level == StatusLevel::Warning ? "warning"
                                                                    : st.level == StatusLevel::Error   ? "error"
                                                                                                         : "skipped";
                    s["message"] = st.message;
                    out.append(s);
                }
                return out;
            },
            "The timeline: each step's id, name, kind, status and message.")
        .def_property_readonly(
            "bodies",
            [](Design& d) {
                std::vector<std::string> ids;
                for (auto& [id, b] : d.doc->state().bodies) {
                    ids.push_back(id);
                }
                return ids;
            },
            "The ids of the bodies (parts) as the design stands.")
        .def(
            "properties",
            [](Design& d, const std::string& body) {
                if (body.empty()) {
                    return propsOf(d.doc->combinedShape());
                }
                auto it = d.doc->state().bodies.find(body);
                if (it == d.doc->state().bodies.end()) {
                    raise(makeError("python.body", "There is no body called '" + body + "'."));
                }
                return propsOf(it->second->shape());
            },
            py::arg("body") = std::string {},
            "Volume (mm³), area (mm²), centre, bounding box, faces, solids and validity of one body, or of all.")
        .def(
            "faces",
            [](Design& d, const std::string& body) {
                auto it = d.doc->state().bodies.find(body);
                if (it == d.doc->state().bodies.end()) {
                    raise(makeError("python.body", "There is no body called '" + body + "'."));
                }
                std::vector<std::string> names;
                for (int i = 0; i < it->second->faceCount(); ++i) {
                    names.push_back(it->second->faceName(i));
                }
                return names;
            },
            py::arg("body"), "The names of a body's faces (for fillet, hole, sketch on a face …).")
        .def(
            "export",
            [](Design& d, const std::string& path) {
                const auto dot = path.rfind('.');
                const std::string ext = dot == std::string::npos ? std::string {} : path.substr(dot + 1);
                check(d.runner->runLine("export " + ext + " \"" + path + "\""));
            },
            py::arg("path"), "Exports to STL, STEP or 3MF, by the file's extension.")
        .def(
            "save", [](Design& d, const std::string& path) { check(saveDocument(*d.doc, path)); }, py::arg("path"),
            "Saves the design as a .jig file.")
        .def("undo", [](Design& d) { return d.doc->undo(); }, "Undoes the last change; False when there is none.")
        .def("redo", [](Design& d) { return d.doc->redo(); }, "Redoes the last undone change.")
        .def("__repr__", [](Design& d) {
            return "<jigsmith.Design: " + std::to_string(d.doc->model().features.size()) + " steps, " +
                   std::to_string(d.doc->state().bodies.size()) + " bodies>";
        });
}
