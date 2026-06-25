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

// Per-env diagnostic sink: attribute libturi diagnostics to this script in
// Godot's error panel instead of letting them go to global stderr.
static void script_diag_sink(TuriEnv * /*env*/, int level, const char *code,
                             const char *file, uint32_t line,
                             uint32_t /*col_start*/, uint32_t /*col_end*/,
                             const char *message, void *ud) {
    String script_path = ud ? *reinterpret_cast<const String *>(ud) : String("<unknown>");
    String prefix = String("[turmeric ") + script_path +
                    (file ? (String(":") + String(file) + ":" + String::num_int64(line)) : String("")) +
                    String(code ? (String(" ") + String(code)) : String("")) +
                    String("] ");
    String full = prefix + String(message ? message : "");
    if (level >= 2) {
        UtilityFunctions::printerr(full);
    } else {
        UtilityFunctions::print(full);
    }
}

// G2 :exports — TLS pointer to the script whose source is currently being
// evaluated by turi_eval. The `godot-export` native (registered in
// turmeric_language.cpp) reads it to know which script's export list to
// populate. Nested reload is not a real case (one editor thread, one script
// at a time), but the save/restore keeps the invariant honest.
thread_local TurmericScript *g_reloading_script = nullptr;

TurmericScript::TurmericScript() {
    std::fprintf(stdout, "[turmeric-godot] TurmericScript::ctor\n");
    std::fflush(stdout);
    // Per-script env (libturi-per-embed-env-and-peripherals, Gap 1 fix):
    // turi_env_new pulls in any natives registered with
    // turi_register_default_native -- godot-println is one of them, seeded
    // by TurmericLanguage at init.
    turi_env = turi_env_new();
}

TurmericScript::~TurmericScript() {
    std::fprintf(stdout, "[turmeric-godot] TurmericScript::dtor\n");
    std::fflush(stdout);
    if (turi_env) {
        turi_env_free(turi_env);
        turi_env = nullptr;
    }
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

    if (!turi_env) {
        UtilityFunctions::printerr("turmeric-godot: script has no TuriEnv");
        return ERR_UNAVAILABLE;
    }

    // Gap 2 fix: reset between reloads instead of destroy + recreate. Drops
    // every defn from the prior source but keeps the natives the env was
    // born with.
    turi_env_reset(turi_env);

    // G2 :exports — drop the prior reload's declarations before the new
    // source registers them. Doing it after env reset means a script that
    // removed an export will no longer offer it to the inspector.
    clear_exports();

    // Gap 3 fix: route this env's diagnostics through our sink so they
    // surface in Godot's Output panel attributed to this script.
    static thread_local String s_path_buf;  // sink reads via pointer
    s_path_buf = get_path();
    if (s_path_buf.is_empty()) s_path_buf = String("<inline>");
    turi_env_set_diag_sink(turi_env, script_diag_sink, &s_path_buf);

    // Gap 4 fix: resolve (import ...) relative to the script's own directory.
    if (!get_path().is_empty()) {
        String dir = get_path().get_base_dir();
        if (!dir.is_empty()) {
            CharString dir_cs = dir.utf8();
            turi_env_set_module_base_dir(turi_env, dir_cs.get_data());
        }
    }

    if (source_code.is_empty()) {
        loaded = true; // empty is valid; nothing to evaluate
        return OK;
    }

    CharString src_utf8 = source_code.utf8();
    // G2 :exports — claim the TLS slot so `godot-export` natives invoked
    // from top-level forms know which script to register against. Restore
    // (not nullify) so nested reloads — should they ever exist — pop cleanly.
    TurmericScript *prev_reloading = g_reloading_script;
    g_reloading_script = this;
    TuriValue v = turi_eval(turi_env, src_utf8.get_data());
    g_reloading_script = prev_reloading;
    if (v.tag == TURI_ERROR) {
        // The diag sink already surfaced the structured diagnostic; this
        // line is the eval-level summary.
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

// --- G2 :exports ---

void TurmericScript::add_export(const StringName &name, Variant::Type type,
                                const Variant &default_value) {
    for (auto &e : exports) {
        if (e.name == name) {
            e.type          = type;
            e.default_value = default_value;
            return;
        }
    }
    exports.push_back(ExportDecl{name, type, default_value});
}

const ExportDecl *TurmericScript::find_export(const StringName &name) const {
    for (const auto &e : exports) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace godot
