// aot_natives.cpp -- see aot_natives.h.

#include "aot_natives.h"

#include "../bridge/native_abi.h"
#include "../bridge/prelude.h"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace godot {
namespace aot {

const char *const kNativesModuleName = "tg-godot";
const char *const kNativesModuleFile = "tg-godot.tur";

namespace {

bool line_starts_with(const std::string &s, size_t at, size_t end,
                      const char *lit) {
    const size_t n = std::strlen(lit);
    if (end - at < n) return false;
    return std::memcmp(s.data() + at, lit, n) == 0;
}

// First non-space byte of [at, end), or `end`.
size_t first_non_space(const std::string &s, size_t at, size_t end) {
    while (at < end && (s[at] == ' ' || s[at] == '\t')) at++;
    return at;
}

// A Turmeric symbol byte, matching the reader's notion closely enough to lift
// a definition's name out of the source. Same character class the syntax
// highlighter and the completion scanner use.
bool is_sym_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '+' || c == '*' || c == '/' ||
           c == '?' || c == '!' || c == '<' || c == '>' || c == '=' ||
           c == '&' || c == '%' || c == ':' || c == '.';
}

// Names bound by top-level `(defX NAME ...)` forms in `src`, in source order.
// Depth-tracked so a nested `defn` inside a `let` is not mistaken for a
// top-level binding, and string-literal aware so a `"(defn "` inside a
// docstring is not either.
void collect_top_level_defs(const std::string &src,
                            std::vector<std::string> *out) {
    const size_t n = src.size();
    size_t i = 0;
    while (i < n) {
        while (i < n && std::isspace((unsigned char)src[i])) i++;
        if (i < n && src[i] == ';') {
            while (i < n && src[i] != '\n') i++;
            continue;
        }
        if (i >= n) break;
        if (src[i] != '(') { i++; continue; }

        size_t j = i + 1;
        while (j < n && std::isspace((unsigned char)src[j])) j++;
        const size_t head_start = j;
        while (j < n && is_sym_char(src[j])) j++;
        const std::string head = src.substr(head_start, j - head_start);

        if (head == "defn" || head == "def" || head == "defmacro" ||
            head == "defstruct" || head == "defopaque" || head == "deftype" ||
            head == "defalias") {
            while (j < n && std::isspace((unsigned char)src[j])) j++;
            const size_t name_start = j;
            while (j < n && is_sym_char(src[j])) j++;
            if (j > name_start) {
                out->push_back(src.substr(name_start, j - name_start));
            }
        }

        // Skip to the matching close paren.
        int depth = 1;
        size_t k = i + 1;
        bool in_str = false;
        while (k < n && depth > 0) {
            const char c = src[k];
            if (in_str) {
                if (c == '\\' && k + 1 < n) { k += 2; continue; }
                if (c == '"') in_str = false;
            } else if (c == '"') {
                in_str = true;
            } else if (c == ';') {
                while (k < n && src[k] != '\n') k++;
                continue;
            } else if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth--;
            }
            k++;
        }
        i = k;
    }
}

} // namespace

std::string strip_aot_skip_regions(const std::string &prelude) {
    std::string out;
    out.reserve(prelude.size());
    bool skipping = false;
    size_t at = 0;
    const size_t n = prelude.size();
    while (at < n) {
        size_t eol = prelude.find('\n', at);
        const size_t body_end = (eol == std::string::npos) ? n : eol;
        const size_t start = first_non_space(prelude, at, body_end);

        bool is_begin = line_starts_with(prelude, start, body_end, ";;#aot-skip-begin");
        bool is_end   = line_starts_with(prelude, start, body_end, ";;#aot-skip-end");

        if (is_begin) skipping = true;

        if (skipping) {
            // Blank the line's bytes, keep its newline: the prelude's own line
            // numbers survive, so a diagnostic inside it still points at the
            // right line of bridge/prelude.cpp.
            out.append(body_end - at, ' ');
        } else {
            out.append(prelude, at, body_end - at);
        }

        if (is_end) skipping = false;

        if (eol == std::string::npos) break;
        out.push_back('\n');
        at = eol + 1;
    }
    return out;
}

std::string staged_natives_module() {
    const std::string prelude =
        strip_aot_skip_regions(std::string(TG_PRELUDE_SOURCE));

    std::vector<std::string> names;
    names.reserve(TG_NATIVE_ABI_COUNT + 160);
    for (size_t i = 0; i < TG_NATIVE_ABI_COUNT; i++) {
        names.emplace_back(TG_NATIVE_ABI[i].tur_name);
    }
    collect_top_level_defs(prelude, &names);

    std::ostringstream o;
    o << ";; AUTO-GENERATED by aot/aot_natives.cpp -- do not edit.\n"
         ";;\n"
         ";; Declarations for the godot-* natives the GDExtension exports\n"
         ";; (bridge/native_abi.h) plus the baked-in prelude, so a staged\n"
         ";; script compiles against the same names the interpreter binds.\n"
         ";; Symbols resolve at dlopen time against the running extension.\n"
         "(defmodule " << kNativesModuleName << "\n";

    o << "  (export";
    {
        std::unordered_set<std::string> seen;
        int on_line = 0;
        for (const auto &nm : names) {
            if (!seen.insert(nm).second) continue;
            if (on_line == 0) o << "\n   ";
            o << " " << nm;
            if (++on_line == 6) on_line = 0;
        }
    }
    o << ")\n\n";

    for (size_t i = 0; i < TG_NATIVE_ABI_COUNT; i++) {
        const TgNativeAbi &r = TG_NATIVE_ABI[i];
        o << "  (extern-c " << r.tur_name << " [" << r.params << "] : "
          << r.ret << ")\n";
    }

    o << "\n" << prelude << "\n)\n";
    return o.str();
}

namespace {

// One top-level form of a source file.
struct TopForm {
    size_t      start = 0;   // byte offset of '('
    size_t      end   = 0;   // one past the matching ')'
    std::string head;        // the head symbol ("defn", "godot-export", ...)
    std::string name;        // the second symbol, when the head defines one
};

// Walk `src` at nesting depth 0, recording each parenthesised form. Comment-
// and string-literal aware, so a `(` inside a docstring never opens a form.
std::vector<TopForm> scan_top_forms(const std::string &src) {
    std::vector<TopForm> out;
    const size_t n = src.size();
    size_t i = 0;
    while (i < n) {
        while (i < n && std::isspace((unsigned char)src[i])) i++;
        if (i < n && src[i] == ';') {
            while (i < n && src[i] != '\n') i++;
            continue;
        }
        if (i >= n) break;
        if (src[i] != '(') { i++; continue; }

        TopForm f;
        f.start = i;

        size_t j = i + 1;
        while (j < n && std::isspace((unsigned char)src[j])) j++;
        size_t hs = j;
        while (j < n && is_sym_char(src[j])) j++;
        f.head = src.substr(hs, j - hs);

        while (j < n && std::isspace((unsigned char)src[j])) j++;
        size_t ns = j;
        while (j < n && is_sym_char(src[j])) j++;
        f.name = src.substr(ns, j - ns);

        int depth = 1;
        size_t k = i + 1;
        bool in_str = false;
        while (k < n && depth > 0) {
            const char c = src[k];
            if (in_str) {
                if (c == '\\' && k + 1 < n) { k += 2; continue; }
                if (c == '"') in_str = false;
            } else if (c == '"') {
                in_str = true;
            } else if (c == ';') {
                while (k < n && src[k] != '\n') k++;
                continue;
            } else if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth--;
            }
            k++;
        }
        f.end = k;
        out.push_back(f);
        i = k;
    }
    return out;
}

// Does this head introduce a binding (as opposed to running something)?
//
// Anything spelled `def*` counts, which covers the whole family without this
// having to track it, plus the four module-level forms. The two exceptions are
// the prelude's per-decl `godot-export` / `godot-signal` macro shells: they are
// spelled like definitions but expand to a bare native call, so inside a
// defmodule they land on TUR-E0711 exactly as the raw call would.
bool head_is_definition(const std::string &head) {
    if (head == "defgodot-export" || head == "defgodot-signal") return false;
    if (head.rfind("def", 0) == 0) return true;
    return head == "import" || head == "export" ||
           head == "load"   || head == "extern-c";
}

// Overwrite [start, end) with spaces, keeping newlines so line numbers hold.
void blank_range(std::string *s, size_t start, size_t end) {
    for (size_t i = start; i < end && i < s->size(); i++) {
        if ((*s)[i] != '\n' && (*s)[i] != '\r') (*s)[i] = ' ';
    }
}

// Byte offset of the start of the line holding `pos`.
size_t line_start_of(const std::string &s, size_t pos) {
    size_t at = s.rfind('\n', pos == 0 ? 0 : pos - 1);
    return (at == std::string::npos) ? 0 : at + 1;
}

} // namespace

std::string prepare_staged_source(const std::string &src,
                                  const std::string &module_name) {
    const std::string import_form =
        std::string("(import ") + kNativesModuleName + ") ";

    std::vector<TopForm> forms = scan_top_forms(src);
    if (forms.empty()) {
        // Comments only. Nothing to compile and nothing to import into; hand
        // the source through so the build produces an empty (but valid) module.
        return src;
    }

    // Case 1 -- the script already is a module. Splice the import in after the
    // module name; `defmodule` stays the file's first form, which it must.
    if (forms[0].head == "defmodule") {
        size_t at = forms[0].start + 1;
        const size_t n = src.size();
        while (at < n && std::isspace((unsigned char)src[at])) at++;
        while (at < n && is_sym_char(src[at])) at++;   // past "defmodule"
        while (at < n && std::isspace((unsigned char)src[at])) at++;
        while (at < n && is_sym_char(src[at])) at++;   // past the module name
        return src.substr(0, at) + " " + import_form + src.substr(at);
    }

    // Case 2 -- bare top-level forms. Wrap, and blank every top-level call.
    std::string body = src;
    std::vector<std::string> exported;
    for (const TopForm &f : forms) {
        if (head_is_definition(f.head)) {
            if (f.head == "defn" && !f.name.empty()) exported.push_back(f.name);
        } else {
            blank_range(&body, f.start, f.end);
        }
    }

    std::ostringstream open;
    open << "(defmodule " << module_name << " " << import_form;
    if (!exported.empty()) {
        open << "(export";
        for (const auto &e : exported) open << " " << e;
        open << ") ";
    }

    const size_t ins = line_start_of(body, forms[0].start);
    std::string out = body.substr(0, ins) + open.str() + body.substr(ins);
    if (!out.empty() && out.back() != '\n') out.push_back('\n');
    out += ")\n";
    return out;
}

std::string annotate_build_log(const std::string &log) {
    struct Row { const char *name; const char *why; };
    static const Row kInterpOnly[] = {
#define TG_ROW_IO(NAME, WHY) { NAME, WHY },
        TG_INTERP_ONLY_NATIVES(TG_ROW_IO)
#undef TG_ROW_IO
    };

    static const char kNeedle[] = "unknown function or operator '";
    std::vector<const Row *> hits;
    size_t at = 0;
    while ((at = log.find(kNeedle, at)) != std::string::npos) {
        const size_t ns = at + sizeof(kNeedle) - 1;
        const size_t ne = log.find('\'', ns);
        at = (ne == std::string::npos) ? log.size() : ne + 1;
        if (ne == std::string::npos) break;
        const std::string name = log.substr(ns, ne - ns);
        for (const Row &r : kInterpOnly) {
            if (name == r.name) {
                bool dup = false;
                for (const Row *h : hits) dup = dup || (h == &r);
                if (!dup) hits.push_back(&r);
                break;
            }
        }
    }
    if (hits.empty()) return log;

    std::ostringstream o;
    o << log;
    if (!log.empty() && log.back() != '\n') o << "\n";
    o << "\nturmeric-godot AOT: the name(s) above are registered for the\n"
         "interpreter but have no compiled entry point, which is why the same\n"
         "script runs in the editor and fails to AOT-build:\n";
    for (const Row *r : hits) {
        o << "  " << r->name << " -- " << r->why << "\n";
    }
    o << "Drop `#mode aot` from this script to keep it on the interpreter path.\n";
    return o.str();
}

} // namespace aot
} // namespace godot
