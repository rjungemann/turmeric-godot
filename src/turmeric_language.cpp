#include "turmeric_language.h"
#include "turmeric_script.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>
#include <cstring>

extern "C" {
#include "turi/eval.h"
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

static TurmericLanguage *s_singleton = nullptr;
TurmericLanguage *TurmericLanguage::singleton() { return s_singleton; }

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
    turi_register_default_native("godot-println", tg_native_println, nullptr);
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

Dictionary TurmericLanguage::_validate(const String &p_script,
                                       const String &p_path,
                                       bool p_validate_functions,
                                       bool p_validate_errors,
                                       bool p_validate_warnings,
                                       bool p_validate_safe_lines) const {
    TG_LOG("_validate");
    Dictionary result;
    result["valid"] = true; // spike: pretend everything compiles
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
