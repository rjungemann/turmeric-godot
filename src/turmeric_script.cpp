#include "turmeric_script.h"
#include "turmeric_instance.h"
#include "turmeric_language.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>

extern "C" {
#include "turi/eval.h"
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

static TurmericLanguage *get_lang() {
    return TurmericLanguage::singleton();
}

TurmericScript::TurmericScript() {
    std::fprintf(stdout, "[turmeric-godot] TurmericScript::ctor\n");
    std::fflush(stdout);
}

TurmericScript::~TurmericScript() {
    std::fprintf(stdout, "[turmeric-godot] TurmericScript::dtor\n");
    std::fflush(stdout);
}

// --- Source ---

bool TurmericScript::_has_source_code() const { return !source_code.is_empty(); }

String TurmericScript::_get_source_code() const { return source_code; }

void TurmericScript::_set_source_code(const String &p_code) {
    source_code = p_code;
    loaded = false;
}

Error TurmericScript::_reload(bool p_keep_state) {
    (void)p_keep_state;
    std::fprintf(stdout, "[turmeric-godot] TurmericScript::_reload (len=%lld)\n",
                 (long long)source_code.length());
    std::fflush(stdout);

    TurmericLanguage *lang = get_lang();
    if (!lang || !lang->get_turi_env()) {
        UtilityFunctions::printerr("turmeric-godot: language singleton not available");
        return ERR_UNAVAILABLE;
    }

    if (source_code.is_empty()) {
        loaded = true; // empty is valid; nothing to evaluate
        return OK;
    }

    CharString src_utf8 = source_code.utf8();
    TuriValue v = turi_eval(lang->get_turi_env(), src_utf8.get_data());
    if (v.tag == TURI_ERROR) {
        UtilityFunctions::printerr(String("turmeric-godot: eval failed: ") +
                                   (v.as_error ? v.as_error : "<unknown>"));
        loaded = false;
        return ERR_PARSE_ERROR;
    }
    loaded = true;
    return OK;
}

// --- Identity ---

ScriptLanguage *TurmericScript::_get_language() const { return get_lang(); }

bool TurmericScript::_is_valid() const { return loaded; }
bool TurmericScript::_is_tool() const { return false; }
bool TurmericScript::_can_instantiate() const { return loaded; }
StringName TurmericScript::_get_instance_base_type() const { return StringName("Object"); }
Ref<Script> TurmericScript::_get_base_script() const { return Ref<Script>(); }
bool TurmericScript::_inherits_script(const Ref<Script> &p_script) const {
    (void)p_script;
    return false;
}

bool TurmericScript::_has_method(const StringName &p_method) const {
    (void)p_method;
    return false;
}
bool TurmericScript::_has_script_signal(const StringName &p_signal) const {
    (void)p_signal;
    return false;
}
void TurmericScript::_update_exports() {}

// --- Instance creation: G1 stub ---

void *TurmericScript::_instance_create(Object *p_for_object) const {
    return turmeric_instance_create(const_cast<TurmericScript *>(this), p_for_object);
}

void *TurmericScript::_placeholder_instance_create(Object *p_for_object) const {
    (void)p_for_object;
    return nullptr;
}

} // namespace godot
