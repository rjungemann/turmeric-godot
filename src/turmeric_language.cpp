#include "turmeric_language.h"
#include "turmeric_script.h"
#include "turmeric_instance.h"
#include "bridge/classdb_proxy.h"
#include "bridge/variant_marshal.h"
#include "bridge/prelude.h"
#include "bridge/generated_facade.h"

#include "aot/aot_mode.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <utility>
#include <vector>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <unordered_set>

extern "C" {
#include "turi/eval.h"
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

// G2 :exports — defined in turmeric_script.cpp / turmeric_instance.cpp; the
// natives below read these to find the right script / instance to operate on.
extern thread_local TurmericScript   *g_reloading_script;

static TurmericLanguage *s_singleton = nullptr;
TurmericLanguage *TurmericLanguage::singleton() { return s_singleton; }

void TurmericLanguage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("validate_source", "script", "path"),
                         &TurmericLanguage::validate_source);
    ClassDB::bind_method(D_METHOD("complete_code_for_test", "code", "path"),
                         &TurmericLanguage::complete_code_for_test);
}

Dictionary TurmericLanguage::validate_source(const String &p_script, const String &p_path) {
    return _validate(p_script, p_path,
                     /*functions*/ true, /*errors*/ true,
                     /*warnings*/ true,  /*safe_lines*/ false);
}

// --- G4.2 Code completion ---
//
// MVP scope: top-level symbol names. Sources in priority order:
//   1. Names defined by the user's source (extracted via top-level
//      `(def | defn | defmacro | defstruct ...)` regex-style scan).
//   2. The hand-written prelude (TG_PRELUDE_SOURCE).
//   3. The generated extension_api.json facade
//      (TG_GENERATED_FACADE_SOURCE).
//
// Filtering: by the last "word" the user is typing -- characters up to
// the cursor that form a valid Turmeric symbol. The editor passes the
// entire buffer; we scan backward from the end of p_code for the
// in-progress symbol.
//
// We do NOT spin up a TuriEnv here because (a) it would re-eval the
// user's source on every keystroke (slow), and (b) libturi has no
// public env-iteration API yet. The source-scan approach gets the
// vast majority of useful completions at near-zero cost. A future
// pass can plug into libturi once an iteration entry point lands.

namespace {

// Same set of leading-symbol chars the highlighter recognizes. Keep in
// sync with turmeric_syntax_highlighter.cpp.
static bool is_sym_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '+' || c == '*' || c == '/' ||
           c == '?' || c == '!' || c == '<' || c == '>' || c == '=' ||
           c == '&' || c == '%' || c == ':' || c == '.';
}

// Extract names defined by top-level (defX NAME ...) forms in src.
// `defs` is what we recognize: defn, def, defmacro, defstruct,
// definstance, deftype.
static void collect_top_level_defs(const char *src, std::vector<std::string> &out) {
    if (!src) return;
    const size_t n = std::strlen(src);
    size_t i = 0;
    while (i < n) {
        // Skip whitespace + comments.
        while (i < n && std::isspace((unsigned char)src[i])) i++;
        if (i < n && src[i] == ';') {
            while (i < n && src[i] != '\n') i++;
            continue;
        }
        if (i >= n) break;
        if (src[i] != '(') { i++; continue; }

        // Inside a paren. Read the head word.
        size_t j = i + 1;
        while (j < n && std::isspace((unsigned char)src[j])) j++;
        const size_t head_start = j;
        while (j < n && is_sym_char(src[j])) j++;
        const size_t head_end = j;

        const std::string head(src + head_start, head_end - head_start);
        if (head == "defn" || head == "def" || head == "defmacro" ||
            head == "defstruct" || head == "definstance" || head == "deftype" ||
            head == "defopaque" || head == "defclass") {
            // Read the next word -- the name.
            while (j < n && std::isspace((unsigned char)src[j])) j++;
            const size_t name_start = j;
            while (j < n && is_sym_char(src[j])) j++;
            const size_t name_end = j;
            if (name_end > name_start) {
                out.emplace_back(src + name_start, name_end - name_start);
            }
        }

        // Skip to matching paren so we don't pick up nested defs as
        // top-level. Track depth + string literals.
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

// Pull the in-progress symbol the user is typing -- characters
// immediately before the end of `code` that form a sym. Returns "" if
// the trailing char isn't a sym char (cursor sitting on whitespace /
// paren / etc.).
static std::string trailing_prefix(const String &code) {
    CharString cs = code.utf8();
    const char *s = cs.get_data();
    const int n = (int)cs.length();
    int end = n;
    int start = end;
    while (start > 0 && is_sym_char(s[start - 1])) start--;
    return std::string(s + start, end - start);
}

} // namespace

Dictionary TurmericLanguage::_complete_code(const String &p_code,
                                            const String &p_path,
                                            Object *p_owner) const {
    (void)p_path; (void)p_owner;

    // Collect candidates.
    std::vector<std::string> names;
    {
        CharString cs = p_code.utf8();
        collect_top_level_defs(cs.get_data(), names);
    }
    collect_top_level_defs(TG_PRELUDE_SOURCE, names);
    collect_top_level_defs(TG_GENERATED_FACADE_SOURCE, names);

    // Dedup while preserving first-seen order (user source wins).
    std::vector<std::string> deduped;
    deduped.reserve(names.size());
    {
        std::unordered_set<std::string> seen;
        for (const auto &n : names) {
            if (seen.insert(n).second) deduped.push_back(n);
        }
    }

    // Filter by trailing prefix.
    const std::string prefix = trailing_prefix(p_code);
    Array options;
    for (const auto &n : deduped) {
        if (!prefix.empty() && n.rfind(prefix, 0) != 0) continue;  // not a prefix match
        Dictionary opt;
        opt["kind"]        = 0;                       // CodeEdit::KIND_FUNCTION
        opt["display"]     = String(n.c_str());
        opt["insert_text"] = String(n.c_str());
        opt["font_color"]  = Color(0.85f, 0.85f, 0.85f);
        opt["icon"]        = Variant();
        opt["default_value"] = Variant();
        opt["location"]    = 0;                       // LOCATION_OTHER
        options.push_back(opt);
    }

    Dictionary result;
    result["result"]    = 0;       // CODE_COMPLETION_RESULT_SUCCESS
    result["force"]     = false;
    result["call_hint"] = String();
    result["options"]   = options;
    return result;
}

Dictionary TurmericLanguage::complete_code_for_test(const String &p_code, const String &p_path) {
    return _complete_code(p_code, p_path, nullptr);
}

// --- Native: (godot/println msg) ---------------------------------------------
// Routes a Turmeric cstr argument through Godot's print pipeline so it shows
// up in the editor Output panel + the running game's stdout.
static TuriValue tg_native_println(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1) {
        std::fprintf(stderr, "[turmeric-godot] (godot/println): expected 1 arg, got %u\n", n);
        return turi_nil();
    }
    const char *msg = (args[0].tag == TURI_CSTR && args[0].as_cstr)
                          ? args[0].as_cstr
                          : "<non-cstr>";
    UtilityFunctions::print(String(msg));
    return turi_nil();
}

// --- G2 :exports natives -----------------------------------------------------

static Variant::Type tg_parse_export_type(const char *t) {
    if (!t) return Variant::NIL;
    if (!std::strcmp(t, "float"))   return Variant::FLOAT;
    if (!std::strcmp(t, "int"))     return Variant::INT;
    if (!std::strcmp(t, "bool"))    return Variant::BOOL;
    if (!std::strcmp(t, "string"))  return Variant::STRING;
    // Aggregate / handle types: the default value comes through as an
    // arena handle (tagged :int) or an Object pointer (plain :int).
    if (!std::strcmp(t, "vec2"))    return Variant::VECTOR2;
    if (!std::strcmp(t, "vec3"))    return Variant::VECTOR3;
    if (!std::strcmp(t, "color"))   return Variant::COLOR;
    if (!std::strcmp(t, "rect2"))   return Variant::RECT2;
    if (!std::strcmp(t, "object"))  return Variant::OBJECT;
    // T2.E -- the typed defopaque names emitted by the generator's
    // ARENA_TYPES / per-class hierarchy. Accepting them lets users write
    // `(defgodot-export start : Vec2Handle (node/vec2 0.0 0.0))` in the
    // block surface; the macro stringifies the type symbol, and this is
    // where that string lands. The runtime treatment is the same as
    // for the lowercase aliases above.
    if (!std::strcmp(t, "Vec2Handle"))   return Variant::VECTOR2;
    if (!std::strcmp(t, "Vec3Handle"))   return Variant::VECTOR3;
    if (!std::strcmp(t, "ColorHandle"))  return Variant::COLOR;
    if (!std::strcmp(t, "Rect2Handle"))  return Variant::RECT2;
    // Class handles (NodeHandle, Node2DHandle, Sprite2DHandle, ...) all
    // land as OBJECT for the inspector. We accept anything matching the
    // `<X>Handle` suffix on the assumption that the X is an ALLOWLIST'd
    // class name; the inspector just sees a typed Object slot either way.
    // Transform2DHandle / Transform3DHandle / ArrayHandle / DictHandle
    // intentionally fall through -- those need arena builders before they
    // can be used as inspector defaults (deferred T2.E follow-up).
    {
        size_t len = std::strlen(t);
        const char *suffix = "Handle";
        size_t slen = std::strlen(suffix);
        if (len > slen && !std::memcmp(t + len - slen, suffix, slen)) {
            // Exclude the arena Handle types we have not wired builders for.
            if (std::strcmp(t, "Transform2DHandle") &&
                std::strcmp(t, "Transform3DHandle") &&
                std::strcmp(t, "ArrayHandle") &&
                std::strcmp(t, "DictHandle")) {
                return Variant::OBJECT;
            }
        }
    }
    return Variant::NIL;
}

static Variant tg_turi_to_variant_typed(TuriValue v, Variant::Type t) {
    switch (t) {
        case Variant::FLOAT:
            if (v.tag == TURI_FLOAT) return (double)v.as_float;
            if (v.tag == TURI_INT)   return (double)v.as_int;
            return 0.0;
        case Variant::INT:
            if (v.tag == TURI_INT)   return (int64_t)v.as_int;
            if (v.tag == TURI_FLOAT) return (int64_t)v.as_float;
            return (int64_t)0;
        case Variant::BOOL:
            return (v.tag == TURI_BOOL) ? (bool)v.as_bool : false;
        case Variant::STRING:
            return String((v.tag == TURI_CSTR && v.as_cstr) ? v.as_cstr : "");
        case Variant::VECTOR2:
        case Variant::VECTOR3:
        case Variant::COLOR:
        case Variant::RECT2: {
            // Aggregate types live in the per-frame Variant arena.
            // Reach into the arena and copy out the live Variant.
            if (v.tag != TURI_INT) return Variant();
            const Variant *vp = variant_arena_lookup(v.as_int);
            return vp ? *vp : Variant();
        }
        case Variant::OBJECT: {
            // Plain :int Object handle (not arena-tagged).
            if (v.tag != TURI_INT) return Variant();
            Object *o = (Object *)(intptr_t)v.as_int;
            return Variant(o);
        }
        default: return Variant();
    }
}

// (godot-export NAME TYPE DEFAULT) -- declares an inspector-visible property
// for the currently-reloading script. Idempotent: a second call with the
// same NAME updates the type/default in place.
static TuriValue tg_native_export(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 3) {
        UtilityFunctions::printerr("turmeric-godot: (godot-export) expected 3 args (name type default)");
        return turi_nil();
    }
    TurmericScript *script = g_reloading_script;
    if (!script) {
        UtilityFunctions::printerr("turmeric-godot: (godot-export) called outside script reload");
        return turi_nil();
    }
    if (args[0].tag != TURI_CSTR || !args[0].as_cstr ||
        args[1].tag != TURI_CSTR || !args[1].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-export) name and type must be strings");
        return turi_nil();
    }
    Variant::Type vt = tg_parse_export_type(args[1].as_cstr);
    if (vt == Variant::NIL) {
        UtilityFunctions::printerr(String("turmeric-godot: (godot-export) unsupported type: ") +
                                   String(args[1].as_cstr));
        return turi_nil();
    }
    script->add_export(StringName(args[0].as_cstr), vt,
                       tg_turi_to_variant_typed(args[2], vt));
    return turi_nil();
}

// Variant -> TuriValue for prop-get returns. Primitives go through the
// shared primitive marshaller. Aggregate types (VECTOR2 / VECTOR3 /
// COLOR / RECT2) get pushed into the per-frame Variant arena so the
// script receives a tagged :int handle compatible with the existing
// godot-vec2-x / godot-color-r / etc. accessors. STRING types go
// through the per-frame string arena -- the cstr is valid for the
// rest of the current outer cb_call. (The elaborator types godot-
// prop-get as :int regardless, so callers that need a statically
// typed :cstr should reach for godot-prop-get-c instead.)
static TuriValue tg_variant_to_turi(const Variant &v) {
    const Variant::Type t = v.get_type();
    switch (t) {
        case Variant::STRING:
        case Variant::STRING_NAME:
        case Variant::NODE_PATH: {
            String s = v;
            CharString cs = s.utf8();
            return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
        }
        case Variant::VECTOR2:
        case Variant::VECTOR3:
        case Variant::COLOR:
        case Variant::RECT2:
            return turi_int(variant_arena_push(v));
        case Variant::OBJECT: {
            Object *o = (Object *)v;
            return turi_int((int64_t)(intptr_t)o);
        }
        default:
            return variant_to_turi_primitive(v, nullptr);
    }
}

// String-aware prop-get: same lookup as tg_native_prop_get, but the
// STRING case routes through the per-frame string arena so the cstr
// outlives the call. Registered as godot-prop-get-c (TUR_NRT_CSTR);
// caller picks this variant when reading a string-typed export.
static TuriValue tg_native_prop_get_c(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-prop-get-c) expected 1 cstr arg (name)");
        return turi_cstr(string_arena_push("", 0));
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->script) {
        UtilityFunctions::printerr("turmeric-godot: (godot-prop-get-c) called outside an instance method");
        return turi_cstr(string_arena_push("", 0));
    }
    StringName name(args[0].as_cstr);
    const ExportDecl *d = self->script->find_export(name);
    if (!d) {
        UtilityFunctions::printerr(String("turmeric-godot: (godot-prop-get-c) undeclared property: ") +
                                   String(args[0].as_cstr));
        return turi_cstr(string_arena_push("", 0));
    }
    std::string key = args[0].as_cstr;
    auto it = self->property_values.find(key);
    const Variant v = (it != self->property_values.end()) ? it->second : d->default_value;
    if (v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME) {
        String s = v;
        CharString cs = s.utf8();
        return turi_cstr(string_arena_push(cs.get_data(), (size_t)cs.length()));
    }
    return turi_cstr(string_arena_push("", 0));
}

// (godot-prop-get NAME) -- read a declared export on the current instance.
// Falls back to the script-level default if the inspector has not assigned.
static TuriValue tg_native_prop_get(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 1 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-prop-get) expected 1 cstr arg (name)");
        return turi_nil();
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->script) {
        UtilityFunctions::printerr("turmeric-godot: (godot-prop-get) called outside an instance method");
        return turi_nil();
    }
    StringName name(args[0].as_cstr);
    const ExportDecl *d = self->script->find_export(name);
    if (!d) {
        UtilityFunctions::printerr(String("turmeric-godot: (godot-prop-get) undeclared property: ") +
                                   String(args[0].as_cstr));
        return turi_nil();
    }
    std::string key = args[0].as_cstr;
    auto it = self->property_values.find(key);
    return tg_variant_to_turi((it != self->property_values.end()) ? it->second
                                                                  : d->default_value);
}

// (godot-signal NAME ARG-NAME-1 ARG-TYPE-1 ...) -- declares a signal on the
// currently-reloading script. Variadic: pairs of (name, type) after the
// signal name describe the signal's args. Zero-arg signals are fine.
static TuriValue tg_native_signal(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n < 1 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-signal) expected at least a signal name (cstr)");
        return turi_nil();
    }
    TurmericScript *script = g_reloading_script;
    if (!script) {
        UtilityFunctions::printerr("turmeric-godot: (godot-signal) called outside script reload");
        return turi_nil();
    }
    if (((n - 1) & 1u) != 0) {
        UtilityFunctions::printerr(String("turmeric-godot: (godot-signal '") +
                                   String(args[0].as_cstr) +
                                   String("') arg list must be (name type) pairs"));
        return turi_nil();
    }
    std::vector<SignalArg> sig_args;
    for (uint32_t i = 1; i + 1 < n; i += 2) {
        if (args[i].tag != TURI_CSTR || !args[i].as_cstr ||
            args[i + 1].tag != TURI_CSTR || !args[i + 1].as_cstr) {
            UtilityFunctions::printerr("turmeric-godot: (godot-signal) arg name/type must be strings");
            return turi_nil();
        }
        Variant::Type vt = tg_parse_export_type(args[i + 1].as_cstr);
        if (vt == Variant::NIL) {
            UtilityFunctions::printerr(String("turmeric-godot: (godot-signal) unsupported arg type: ") +
                                       String(args[i + 1].as_cstr));
            return turi_nil();
        }
        sig_args.push_back(SignalArg{StringName(args[i].as_cstr), vt});
    }
    script->add_signal(StringName(args[0].as_cstr), std::move(sig_args));
    return turi_nil();
}

// (emit-signal NAME ARGS...) -- emits a declared signal on the current
// instance's owner object. Variadic; arity checked against the declaration.
static TuriValue tg_native_emit_signal(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n < 1 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (emit-signal) expected a signal name (cstr)");
        return turi_nil();
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->owner) {
        UtilityFunctions::printerr("turmeric-godot: (emit-signal) called outside an instance method");
        return turi_nil();
    }
    StringName sig_name(args[0].as_cstr);
    const SignalDecl *decl = self->script ? self->script->find_signal(sig_name) : nullptr;
    if (!decl) {
        UtilityFunctions::printerr(String("turmeric-godot: (emit-signal) undeclared signal: ") +
                                   String(args[0].as_cstr));
        return turi_nil();
    }
    const uint32_t sig_argc = n - 1;
    if (sig_argc != decl->args.size()) {
        UtilityFunctions::printerr(String("turmeric-godot: (emit-signal '") +
                                   String(args[0].as_cstr) +
                                   String("') wrong arg count: expected ") +
                                   String::num_int64((int64_t)decl->args.size()) +
                                   String(", got ") +
                                   String::num_int64((int64_t)sig_argc));
        return turi_nil();
    }
    Array call_args;
    call_args.push_back(sig_name);
    for (uint32_t i = 0; i < sig_argc; i++) {
        call_args.push_back(tg_turi_to_variant_typed(args[i + 1], decl->args[i].type));
    }
    self->owner->callv(StringName("emit_signal"), call_args);
    return turi_nil();
}

// (godot-prop-set NAME VAL) -- write a declared export on the current
// instance. Coerces VAL to the declared type when sensible.
static TuriValue tg_native_prop_set(TuriEnv *env, TuriValue *args, uint32_t n, void *ud) {
    (void)env; (void)ud;
    if (n != 2 || args[0].tag != TURI_CSTR || !args[0].as_cstr) {
        UtilityFunctions::printerr("turmeric-godot: (godot-prop-set) expected 2 args (name value)");
        return turi_nil();
    }
    TurmericInstance *self = g_current_instance;
    if (!self || !self->script) {
        UtilityFunctions::printerr("turmeric-godot: (godot-prop-set) called outside an instance method");
        return turi_nil();
    }
    StringName name(args[0].as_cstr);
    const ExportDecl *d = self->script->find_export(name);
    if (!d) {
        UtilityFunctions::printerr(String("turmeric-godot: (godot-prop-set) undeclared property: ") +
                                   String(args[0].as_cstr));
        return turi_nil();
    }
    self->property_values[std::string(args[0].as_cstr)] =
        tg_turi_to_variant_typed(args[1], d->type);
    return turi_nil();
}

#define TG_LOG(method) \
    std::fprintf(stdout, "[turmeric-godot] %s called\n", method); \
    std::fflush(stdout)

TurmericLanguage::TurmericLanguage() {
    TG_LOG("ctor");
    s_singleton = this;
}

TurmericLanguage::~TurmericLanguage() {
    TG_LOG("dtor");
    if (s_singleton == this) s_singleton = nullptr;
}

// --- Identity ---

String TurmericLanguage::_get_name() const {
    return String("Turmeric");
}

String TurmericLanguage::_get_type() const {
    return String("TurmericScript");
}

String TurmericLanguage::_get_extension() const {
    return String("tur");
}

PackedStringArray TurmericLanguage::_get_recognized_extensions() const {
    PackedStringArray exts;
    exts.push_back("tur");
    return exts;
}

PackedStringArray TurmericLanguage::_get_reserved_words() const {
    PackedStringArray words;
    // A minimal placeholder set; the real list will come from the elaborator.
    const char *kws[] = {
        "defn", "defmacro", "defstruct", "deftype", "definstance", "defclass",
        "let", "letrec", "fn", "if", "cond", "when", "unless", "do", "for",
        "while", "import", "export", "module", "and", "or", "not",
    };
    for (const char *k : kws) {
        words.push_back(k);
    }
    return words;
}

PackedStringArray TurmericLanguage::_get_comment_delimiters() const {
    PackedStringArray d;
    d.push_back(";");
    return d;
}

PackedStringArray TurmericLanguage::_get_string_delimiters() const {
    PackedStringArray d;
    d.push_back("\" \"");
    return d;
}

// --- Lifecycle ---

void TurmericLanguage::init_turi() {
    turi_init(false);
    // Gap 1 fix: register host natives as process-global defaults. Every
    // TurmericScript's TuriEnv (created in TurmericScript::ctor) auto-binds
    // these at turi_env_new time -- no per-env re-registration boilerplate.
    //
    // Return-type signatures (TUR_NRT_*, landed upstream in 78329855e):
    //   * Untyped natives use plain turi_register_default_native and keep
    //     the historical :int default. godot-call / godot-prop-get /
    //     godot-array-get / godot-dict-get all return dynamic types -- a
    //     per-method return type isn't statically expressible, so :int
    //     stays right for those.
    //   * Everything else gets a precise return type so curated wrappers
    //     can declare honest signatures (godot-vec2-x : float, etc.).
    turi_register_default_native_typed("godot-println",   tg_native_println,      nullptr, TUR_NRT_VOID);
    turi_register_default_native_typed("godot-export",    tg_native_export,       nullptr, TUR_NRT_VOID);
    turi_register_default_native      ("godot-prop-get",  tg_native_prop_get,     nullptr);                  // dynamic
    // Typed aliases over the same C dispatch -- callers pick by property
    // type (float / int / bool) and the elaborator sees the right return
    // type for arithmetic / branching at the use site.
    turi_register_default_native_typed("godot-prop-get-f", tg_native_prop_get,    nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-prop-get-i", tg_native_prop_get,    nullptr, TUR_NRT_INT);
    turi_register_default_native_typed("godot-prop-get-b", tg_native_prop_get,    nullptr, TUR_NRT_BOOL);
    turi_register_default_native_typed("godot-prop-get-c", tg_native_prop_get_c,  nullptr, TUR_NRT_CSTR);
    turi_register_default_native_typed("godot-prop-set",  tg_native_prop_set,     nullptr, TUR_NRT_VOID);
    turi_register_default_native_typed("godot-signal",    tg_native_signal,       nullptr, TUR_NRT_VOID);
    turi_register_default_native_typed("emit-signal",     tg_native_emit_signal,  nullptr, TUR_NRT_VOID);

    // G3.a -- generic ClassDB proxy + Variant arena.
    turi_register_default_native      ("godot-self",       tg_native_godot_self,    nullptr);                // :int Object handle
    turi_register_default_native      ("godot-singleton",  tg_native_godot_singleton, nullptr);              // :int Object handle
    turi_register_default_native_typed("godot-num->str",  tg_native_godot_num_to_str, nullptr, TUR_NRT_CSTR);
    turi_register_default_native_typed("godot-connect",       tg_native_godot_connect,       nullptr, TUR_NRT_VOID);
    turi_register_default_native_typed("godot-connect-typed", tg_native_godot_connect_typed, nullptr, TUR_NRT_VOID);
    turi_register_default_native      ("godot-call",       tg_native_godot_call,    nullptr);                // dynamic
    // Codegen v2 typed variants -- gen_godot_facade.py picks the right one
    // per JSON return type so the generated wrapper declares an honest type.
    turi_register_default_native_typed("godot-call-v",     tg_native_godot_call_v,  nullptr, TUR_NRT_VOID);
    turi_register_default_native_typed("godot-call-f",     tg_native_godot_call_f,  nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-call-b",     tg_native_godot_call_b,  nullptr, TUR_NRT_BOOL);
    turi_register_default_native_typed("godot-call-c",     tg_native_godot_call_c,  nullptr, TUR_NRT_CSTR);
    turi_register_default_native      ("godot-vec2",       tg_native_godot_vec2,    nullptr);                // :int arena handle
    turi_register_default_native      ("godot-vec3",       tg_native_godot_vec3,    nullptr);
    turi_register_default_native_typed("godot-vec2-x",     tg_native_godot_vec2_x,  nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-vec2-y",     tg_native_godot_vec2_y,  nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-vec3-x",     tg_native_godot_vec3_x,  nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-vec3-y",     tg_native_godot_vec3_y,  nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-vec3-z",     tg_native_godot_vec3_z,  nullptr, TUR_NRT_FLOAT);
    turi_register_default_native      ("godot-color",      tg_native_godot_color,   nullptr);
    turi_register_default_native_typed("godot-color-r",    tg_native_godot_color_r, nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-color-g",    tg_native_godot_color_g, nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-color-b",    tg_native_godot_color_b, nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-color-a",    tg_native_godot_color_a, nullptr, TUR_NRT_FLOAT);

    // G3.a follow-up -- Rect2 / Transform / Array / Dictionary.
    turi_register_default_native      ("godot-rect2",            tg_native_godot_rect2,            nullptr);
    turi_register_default_native_typed("godot-rect2-x",          tg_native_godot_rect2_x,          nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-rect2-y",          tg_native_godot_rect2_y,          nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-rect2-w",          tg_native_godot_rect2_w,          nullptr, TUR_NRT_FLOAT);
    turi_register_default_native_typed("godot-rect2-h",          tg_native_godot_rect2_h,          nullptr, TUR_NRT_FLOAT);
    turi_register_default_native      ("godot-xform2d-origin",   tg_native_godot_xform2d_origin,   nullptr);  // vec2 handle
    turi_register_default_native_typed("godot-xform2d-rotation", tg_native_godot_xform2d_rotation, nullptr, TUR_NRT_FLOAT);
    turi_register_default_native      ("godot-xform3d-origin",   tg_native_godot_xform3d_origin,   nullptr);  // vec3 handle
    turi_register_default_native      ("godot-array-len",        tg_native_godot_array_len,        nullptr);  // :int length
    turi_register_default_native      ("godot-array-get",        tg_native_godot_array_get,        nullptr);  // dynamic
    turi_register_default_native_typed("godot-dict-has",         tg_native_godot_dict_has,         nullptr, TUR_NRT_BOOL);
    turi_register_default_native      ("godot-dict-get",         tg_native_godot_dict_get,         nullptr);  // dynamic
}

void TurmericLanguage::shutdown_turi() {
    turi_clear_default_natives();
}

void TurmericLanguage::smoke_test() {
    // Stand up a throwaway env so the smoke test is independent of any script.
    TuriEnv *env = turi_env_new();
    char type_tag[64] = {0};
    TuriValue v = turi_eval_typed(env, "(+ 1 2)", type_tag, sizeof(type_tag));
    char repr[128] = {0};
    turi_value_repr(repr, sizeof(repr), v);
    std::fprintf(stdout, "[turmeric-godot] libturi smoke: (+ 1 2) = %s : %s\n",
                 repr, type_tag);
    std::fflush(stdout);

    // godot-println round-trip via the default-native registry.
    (void)turi_eval(env, "(godot-println \"hello from turmeric (via native)\")");
    turi_env_free(env);
}

// A5 -- register the project settings the AOT path consults. We do this
// from _init so the editor's Project Settings dialog discovers them on
// first run; the settings persist into project.godot only after the user
// edits a value, which matches how other engines surface their hooks
// (`turmeric/...` always shows up under Advanced).
static void register_project_settings() {
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (!ps) return;

    // turmeric/execution_mode -- "interpreter" (default) | "aot"
    {
        const String key = String("turmeric/execution_mode");
        if (!ps->has_setting(key)) {
            ps->set_setting(key, Variant(String("interpreter")));
        }
        ps->set_initial_value(key, Variant(String("interpreter")));
        Dictionary info;
        info["name"]        = key;
        info["type"]        = (int)Variant::STRING;
        info["hint"]        = 2; // PROPERTY_HINT_ENUM
        info["hint_string"] = String("interpreter,aot");
        ps->add_property_info(info);
    }

    // turmeric/tur_binary -- override for `tur` lookup; blank uses PATH
    {
        const String key = String("turmeric/tur_binary");
        if (!ps->has_setting(key)) {
            ps->set_setting(key, Variant(String("")));
        }
        ps->set_initial_value(key, Variant(String("")));
        Dictionary info;
        info["name"]        = key;
        info["type"]        = (int)Variant::STRING;
        info["hint"]        = 14; // PROPERTY_HINT_GLOBAL_FILE
        info["hint_string"] = String("");
        ps->add_property_info(info);
    }
}

void TurmericLanguage::_init() {
    TG_LOG("_init");
    init_turi();
    smoke_test();
    register_project_settings();
}

void TurmericLanguage::_finish() {
    TG_LOG("_finish");
    shutdown_turi();
}

// --- Feature flags ---

bool TurmericLanguage::_is_using_templates() { return false; }
bool TurmericLanguage::_has_named_classes() const { return false; }
bool TurmericLanguage::_supports_builtin_mode() const { return false; }
bool TurmericLanguage::_supports_documentation() const { return false; }
bool TurmericLanguage::_can_inherit_from_file() const { return false; }

// --- Validation / creation ---

// G3.d -- structured _validate. The editor calls this on every save (and on
// some keystroke debounce) with the *current buffer* contents; we run the
// source through a throwaway libturi env so parse + elaboration diagnostics
// show up inline. The env is discarded after validation, so any side
// effects from top-level forms in the script are bounded to validation
// time (acceptable for v1; matches how GDScript's _validate also runs the
// class-body elaboration).
//
// Returned shape (matches Godot's expectation; mirrors GDScript):
//   {
//     "valid":      bool,
//     "errors":     [{ "path", "line", "column", "message" }, ...],
//     "warnings":   [{ "path", "line", "column", "message" }, ...],
//     "functions":  []  // not populated v1
//     "safe_lines": PackedInt32Array()  // not populated v1
//   }
namespace {
struct ValidateDiag {
    int level;          // 0=error, 1=warning, 2=note, 3=help
    String code;
    String message;
    String file;
    int    line;
    int    col_start;
    int    col_end;
};
thread_local std::vector<ValidateDiag> *g_validate_sink = nullptr;
} // namespace

static void validate_collect_sink(TuriEnv * /*env*/, int level, const char *code,
                                  const char *file, uint32_t line,
                                  uint32_t col_start, uint32_t col_end,
                                  const char *message, void * /*ud*/) {
    if (!g_validate_sink) return;
    ValidateDiag d;
    d.level     = level;
    d.code      = String(code ? code : "");
    d.message   = String(message ? message : "");
    d.file      = String(file ? file : "");
    d.line      = (int)line;
    d.col_start = (int)col_start;
    d.col_end   = (int)col_end;
    g_validate_sink->push_back(std::move(d));
}

static Dictionary diag_to_dict(const ValidateDiag &d, const String &script_path) {
    Dictionary out;
    out["path"]    = d.file.is_empty() ? script_path : d.file;
    out["line"]    = d.line;
    out["column"]  = d.col_start;
    String msg = d.message;
    if (!d.code.is_empty()) msg = d.code + String(": ") + msg;
    out["message"] = msg;
    return out;
}

Dictionary TurmericLanguage::_validate(const String &p_script,
                                       const String &p_path,
                                       bool p_validate_functions,
                                       bool p_validate_errors,
                                       bool p_validate_warnings,
                                       bool p_validate_safe_lines) const {
    (void)p_validate_functions; (void)p_validate_safe_lines;
    Dictionary result;
    Array errors;
    Array warnings;

    std::vector<ValidateDiag> diags;
    std::vector<ValidateDiag> *prev_sink = g_validate_sink;
    g_validate_sink = &diags;

    TuriEnv *env = turi_env_new();
    if (!env) {
        g_validate_sink = prev_sink;
        result["valid"]     = false;
        Dictionary e;
        e["path"] = p_path; e["line"] = 0; e["column"] = 0;
        e["message"] = String("turmeric-godot: failed to allocate validation env");
        errors.push_back(e);
        result["errors"]    = errors;
        result["warnings"]  = warnings;
        result["functions"] = Array();
        result["safe_lines"] = PackedInt32Array();
        return result;
    }
    turi_env_set_diag_sink(env, validate_collect_sink, nullptr);

    // Resolve (import ...) relative to the script's own directory if known --
    // this matches the behavior _reload sets up so validate sees the same
    // module graph.
    if (!p_path.is_empty()) {
        String dir = p_path.get_base_dir();
        if (!dir.is_empty()) {
            CharString dir_cs = dir.utf8();
            turi_env_set_module_base_dir(env, dir_cs.get_data());
        }
    }

    CharString src_utf8 = p_script.utf8();
    TuriValue v = turi_eval(env, src_utf8.get_data());
    // We don't care about the value; we care about what the diag sink collected.
    // A TURI_ERROR with no sinked diag is a fallback "something went wrong".
    if (v.tag == TURI_ERROR && diags.empty()) {
        ValidateDiag fallback;
        fallback.level     = 0;
        fallback.message   = String(v.as_error ? v.as_error : "<unknown error>");
        fallback.line      = 0;
        fallback.col_start = 0;
        diags.push_back(std::move(fallback));
    }

    turi_env_free(env);
    g_validate_sink = prev_sink;

    bool any_errors = false;
    for (const auto &d : diags) {
        if (d.level <= 0) {
            if (p_validate_errors) errors.push_back(diag_to_dict(d, p_path));
            any_errors = true;
        } else if (d.level == 1) {
            if (p_validate_warnings) warnings.push_back(diag_to_dict(d, p_path));
        }
        // notes / help: dropped for v1
    }

    result["valid"]      = !any_errors;
    result["errors"]     = errors;
    result["warnings"]   = warnings;
    result["functions"]  = Array();
    result["safe_lines"] = PackedInt32Array();
    return result;
}

Object *TurmericLanguage::_create_script() const {
    TG_LOG("_create_script");
    return memnew(TurmericScript);
}

// --- Reloading ---

void TurmericLanguage::_reload_all_scripts() {
    TG_LOG("_reload_all_scripts");
}

void TurmericLanguage::_reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
    TG_LOG("_reload_tool_script");
}

// --- Threading ---

void TurmericLanguage::_thread_enter() {}
void TurmericLanguage::_thread_exit() {}

// --- Frame ---

void TurmericLanguage::_frame() {
    // Intentionally silent: this fires every editor frame.
}

bool TurmericLanguage::_handles_global_class_type(const String &p_type) const {
    // No global script class registration in v1 -- the editor's class
    // browser doesn't list Turmeric script types yet. Returning false
    // for everything is the correct null implementation and clears the
    // "Required virtual method must be overridden" warning the editor
    // logs at startup. A real implementation lands when scripts can
    // declare a class_name equivalent.
    (void)p_type;
    return false;
}

#undef TG_LOG

} // namespace godot
