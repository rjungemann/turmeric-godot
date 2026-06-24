#include "turmeric_language.h"

#include <cstdio>

namespace godot {

#define TG_LOG(method) \
    std::fprintf(stdout, "[turmeric-godot] %s called\n", method); \
    std::fflush(stdout)

TurmericLanguage::TurmericLanguage() {
    TG_LOG("ctor");
}

TurmericLanguage::~TurmericLanguage() {
    TG_LOG("dtor");
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

void TurmericLanguage::_init() {
    TG_LOG("_init");
}

void TurmericLanguage::_finish() {
    TG_LOG("_finish");
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
    return nullptr; // spike: no script objects yet
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
