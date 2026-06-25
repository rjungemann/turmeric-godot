#include "turmeric_language.h"
#include "turmeric_script.h"
#include "turmeric_instance.h"
#include "bridge/classdb_proxy.h"

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
#include <string>

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
}

Dictionary TurmericLanguage::validate_source(const String &p_script, const String &p_path) {
    return _validate(p_script, p_path,
                     /*functions*/ true, /*errors*/ true,
                     /*warnings*/ true,  /*safe_lines*/ false);
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
    if (!std::strcmp(t, "float"))  return Variant::FLOAT;
    if (!std::strcmp(t, "int"))    return Variant::INT;
    if (!std::strcmp(t, "bool"))   return Variant::BOOL;
    if (!std::strcmp(t, "string")) return Variant::STRING;
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

// Variant -> TuriValue for prop-get returns. Strings are not supported (the
// cstr would have to outlive the call; punt for v1) and return nil with a
// warning so the script can branch on the result.
static TuriValue tg_variant_to_turi(const Variant &v) {
    switch (v.get_type()) {
        case Variant::NIL:   return turi_nil();
        case Variant::BOOL:  return turi_bool((bool)v);
        case Variant::INT:   return turi_int((int64_t)v);
        case Variant::FLOAT: return turi_float((double)v);
        case Variant::STRING:
            UtilityFunctions::printerr(
                "turmeric-godot: (godot-prop-get) string-typed properties cannot "
                "be read from script in v1; returning nil");
            return turi_nil();
        default: return turi_nil();
    }
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
    turi_register_default_native("godot-println",  tg_native_println,  nullptr);
    turi_register_default_native("godot-export",   tg_native_export,   nullptr);
    turi_register_default_native("godot-prop-get", tg_native_prop_get, nullptr);
    turi_register_default_native("godot-prop-set", tg_native_prop_set, nullptr);
    turi_register_default_native("godot-signal",   tg_native_signal,   nullptr);
    turi_register_default_native("emit-signal",    tg_native_emit_signal, nullptr);
    // G3.a — generic ClassDB proxy + Variant arena (Vector2/Vector3 today).
    turi_register_default_native("godot-self",     tg_native_godot_self, nullptr);
    turi_register_default_native("godot-call",     tg_native_godot_call, nullptr);
    turi_register_default_native("godot-vec2",     tg_native_godot_vec2, nullptr);
    turi_register_default_native("godot-vec3",     tg_native_godot_vec3, nullptr);
    turi_register_default_native("godot-vec2-x",   tg_native_godot_vec2_x, nullptr);
    turi_register_default_native("godot-vec2-y",   tg_native_godot_vec2_y, nullptr);
    turi_register_default_native("godot-vec3-x",   tg_native_godot_vec3_x, nullptr);
    turi_register_default_native("godot-vec3-y",   tg_native_godot_vec3_y, nullptr);
    turi_register_default_native("godot-vec3-z",   tg_native_godot_vec3_z, nullptr);
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

void TurmericLanguage::_init() {
    TG_LOG("_init");
    init_turi();
    smoke_test();
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

#undef TG_LOG

} // namespace godot
