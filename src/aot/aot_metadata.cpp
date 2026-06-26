// aot_metadata.cpp -- see aot_metadata.h.

#include "aot_metadata.h"
#include "../turmeric_script.h"

#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace godot {
namespace aot {

namespace {

// Variant types we round-trip via stringify. Anything outside this list
// keeps the type info but loses the default value; the inspector then
// shows the type's zero-initialised value, which matches what GDScript
// does for an unannotated typed export.
bool is_primitive_variant_type(Variant::Type t) {
    return t == Variant::BOOL  || t == Variant::INT ||
           t == Variant::FLOAT || t == Variant::STRING ||
           t == Variant::STRING_NAME;
}

std::string encode_default(const ExportDecl &e) {
    if (!is_primitive_variant_type(e.type)) return std::string();
    String s = e.default_value.stringify();
    CharString cs = s.utf8();
    std::string out(cs.get_data(), (size_t)cs.length());
    // Strip newlines / tabs so the line-based format survives.
    for (char &c : out) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    return out;
}

Variant decode_default(Variant::Type t, const std::string &s) {
    if (!is_primitive_variant_type(t) || s.empty()) {
        return Variant();
    }
    switch (t) {
        case Variant::BOOL: {
            // `Variant::stringify()` on a bool emits "true" / "false".
            return Variant(s == "true" || s == "True" || s == "1");
        }
        case Variant::INT: {
            char *end = nullptr;
            long long v = std::strtoll(s.c_str(), &end, 10);
            if (end == s.c_str()) return Variant((int64_t)0);
            return Variant((int64_t)v);
        }
        case Variant::FLOAT: {
            char *end = nullptr;
            double v = std::strtod(s.c_str(), &end);
            if (end == s.c_str()) return Variant(0.0);
            return Variant(v);
        }
        case Variant::STRING:
            return Variant(String::utf8(s.c_str()));
        case Variant::STRING_NAME:
            return Variant(StringName(String::utf8(s.c_str())));
        default:
            return Variant();
    }
}

// Split a tab-delimited line into `max_parts` fields. Trailing empty
// fields are preserved. Returns the number of fields actually parsed.
size_t split_tabs(const std::string &line,
                  std::string fields[], size_t max_parts) {
    size_t n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= line.size() && n < max_parts; i++) {
        if (i == line.size() || line[i] == '\t') {
            if (n + 1 == max_parts) {
                // Last requested field swallows the rest of the line.
                fields[n++] = line.substr(start);
                return n;
            }
            fields[n++] = line.substr(start, i - start);
            start = i + 1;
        }
    }
    return n;
}

} // namespace

bool write_metadata(const std::string &path,
                    const std::vector<ExportDecl> &exports,
                    const std::vector<SignalDecl> &signals,
                    std::string *out_err) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (out_err) *out_err = "cannot open for write: " + path;
        return false;
    }
    f << "# turmeric-godot AOT metadata; regenerated on each rebuild.\n";
    for (const auto &e : exports) {
        String n = e.name;
        CharString ncs = n.utf8();
        std::string def = encode_default(e);
        f << "EXPORT\t" << ncs.get_data() << "\t" << (int)e.type
          << "\t" << def << "\n";
    }
    for (const auto &s : signals) {
        String n = s.name;
        CharString ncs = n.utf8();
        f << "SIGNAL\t" << ncs.get_data() << "\t" << s.args.size() << "\n";
        for (const auto &a : s.args) {
            String an = a.name;
            CharString acs = an.utf8();
            f << "SIGARG\t" << acs.get_data() << "\t" << (int)a.type << "\n";
        }
    }
    if (!f.good()) {
        if (out_err) *out_err = "write error to: " + path;
        return false;
    }
    return true;
}

bool read_metadata(const std::string &path,
                   std::vector<ExportDecl> *out_exports,
                   std::vector<SignalDecl> *out_signals,
                   std::string *out_err) {
    if (!out_exports || !out_signals) return false;
    std::ifstream f(path);
    if (!f) {
        if (out_err) *out_err = "metadata file not present: " + path;
        return false;
    }
    out_exports->clear();
    out_signals->clear();

    std::string line;
    size_t lineno = 0;
    SignalDecl *open_signal = nullptr;  // SIGARGs attach to the last SIGNAL
    size_t      open_signal_remaining = 0;

    while (std::getline(f, line)) {
        lineno++;
        if (line.empty() || line[0] == '#') continue;

        std::string parts[4];
        size_t n = split_tabs(line, parts, 4);
        if (n == 0) continue;

        if (parts[0] == "EXPORT") {
            if (n < 3) {
                if (out_err) *out_err = "EXPORT short at line "
                                       + std::to_string(lineno);
                return false;
            }
            ExportDecl d;
            d.name = StringName(String::utf8(parts[1].c_str()));
            d.type = (Variant::Type)std::stoi(parts[2]);
            d.default_value = decode_default(d.type,
                                              n >= 4 ? parts[3] : std::string());
            out_exports->push_back(std::move(d));
            open_signal           = nullptr;
            open_signal_remaining = 0;
        } else if (parts[0] == "SIGNAL") {
            if (n < 3) {
                if (out_err) *out_err = "SIGNAL short at line "
                                       + std::to_string(lineno);
                return false;
            }
            SignalDecl s;
            s.name = StringName(String::utf8(parts[1].c_str()));
            int argc = std::stoi(parts[2]);
            if (argc < 0) argc = 0;
            s.args.reserve((size_t)argc);
            out_signals->push_back(std::move(s));
            open_signal           = &out_signals->back();
            open_signal_remaining = (size_t)argc;
        } else if (parts[0] == "SIGARG") {
            if (!open_signal || open_signal_remaining == 0) {
                if (out_err) *out_err = "stray SIGARG at line "
                                       + std::to_string(lineno);
                return false;
            }
            if (n < 3) {
                if (out_err) *out_err = "SIGARG short at line "
                                       + std::to_string(lineno);
                return false;
            }
            SignalArg a;
            a.name = StringName(String::utf8(parts[1].c_str()));
            a.type = (Variant::Type)std::stoi(parts[2]);
            open_signal->args.push_back(std::move(a));
            open_signal_remaining--;
        } else {
            // Unknown tag -- a metadata bump older code doesn't recognise.
            // Treat as fatal so the caller falls back to a fresh eval.
            if (out_err) *out_err = "unknown tag '" + parts[0]
                                  + "' at line " + std::to_string(lineno);
            return false;
        }
    }
    if (open_signal && open_signal_remaining > 0) {
        if (out_err) *out_err = "SIGNAL '" + std::string(((String)open_signal->name).utf8().get_data())
                              + "' missing " + std::to_string(open_signal_remaining)
                              + " SIGARG entries";
        return false;
    }
    return true;
}

} // namespace aot
} // namespace godot
