#include "turmeric_script.h"
#include "turmeric_instance.h"
#include "turmeric_language.h"
#include <godot_cpp/classes/engine_debugger.hpp>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <utility>

#include <cstdio>

extern "C" {
#include "turi/eval.h"
#include "turi/env.h"
#include "turi/value.h"
}

#include "bridge/prelude.h"
#include "bridge/generated_facade.h"

#include "aot/aot_cache.h"
#include "aot/aot_image.h"
#include "aot/aot_metadata.h"
#include "aot/aot_mode.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <cstdlib>
#include <sys/stat.h>

namespace {
inline bool aot_path_is_file(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Mirror script_diag_sink's prefix shape so AOT failures attribute to the
// right script in Godot's Output panel instead of looking like global
// extension noise. `path` is the script's res:// (or "<inline>") path.
inline godot::String aot_diag_prefix(const godot::String &path) {
    return godot::String("[turmeric ") + path + godot::String(" AOT] ");
}
} // namespace

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

    // Enable in-editor breakpoint debugger if active
    if (EngineDebugger::get_singleton()->is_active()) {
        turi_debug_enable(turi_env, nullptr, nullptr);
        turi_debug_arm_breakpoints(turi_env);
        turi_debug_set_pause_handler(turi_env, TurmericLanguage::tg_pause_handler, nullptr);
        turi_debug_set_bp_match_handler(turi_env, TurmericLanguage::tg_bp_match_handler, nullptr);
    }
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
    // T4.B starter -- the editor sets p_keep_state=true when it wants
    // inspector-edited @export values to survive the reload. The actual
    // capture/replay dance is driven by Godot via cb_get_property_state
    // + cb_set (see turmeric_instance.cpp); the script side just notes
    // the mode so the log makes the path obvious.
    std::fprintf(stdout,
                 "[turmeric-godot] TurmericScript::_reload (len=%lld, keep_state=%s)\n",
                 (long long)source_code.length(),
                 p_keep_state ? "yes" : "no");
    std::fflush(stdout);

    if (!turi_env) {
        UtilityFunctions::printerr("turmeric-godot: script has no TuriEnv");
        return ERR_UNAVAILABLE;
    }

    // Gap 2 fix: reset between reloads instead of destroy + recreate. Drops
    // every defn from the prior source but keeps the natives the env was
    // born with.
    turi_env_reset(turi_env);

    // G2 :exports / :signals — drop the prior reload's declarations before
    // the new source registers them. Doing it after env reset means a
    // script that removed an export/signal no longer offers it.
    clear_exports();
    clear_signals();

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

    // Phase A4 -- when AOT is on AND the cache slot for this exact source
    // already holds a built library, manifest, AND export/signal metadata
    // sidecar, skip the interpreter eval entirely. cb_call dispatches into
    // the AOT image; the interp env is unused on this path beyond the
    // builtin natives turi_env_new seeded.
    //
    // We still drop the prior generation before the probe so a failed
    // fast-path doesn't leave us with two live handles when the slow path
    // also rebuilds.
    CharString src_utf8_early = source_code.utf8();
    const aot::ExecutionMode mode = aot::resolve_execution_mode(
        src_utf8_early.get_data(), (size_t)src_utf8_early.length());
    const bool aot_enabled = (mode == aot::ExecutionMode::Aot) &&
                              !get_path().is_empty();
    aot_image_.reset();

    const String aot_prefix = aot_diag_prefix(
        get_path().is_empty() ? String("<inline>") : get_path());

    if (aot_enabled && !source_code.is_empty()) {
        ProjectSettings *ps = ProjectSettings::get_singleton();
        String script_abs = ps ? ps->globalize_path(get_path()) : get_path();
        String project_abs = ps ? ps->globalize_path(String("res://")) : String();
        CharString project_cs = project_abs.utf8();
        CharString script_cs  = script_abs.utf8();
        const char *src_ptr = src_utf8_early.get_data();
        size_t      src_len = (size_t)src_utf8_early.length();
        std::string tur_bin = aot::resolve_tur_bin(aot::project_tur_binary_setting());

        aot::BuildOutputs preds = aot::predict_outputs(
            std::string(project_cs.get_data()),
            std::string(script_cs.get_data()),
            src_ptr, src_len, tur_bin);

        if (aot_path_is_file(preds.lib_path) &&
            aot_path_is_file(preds.manifest_path) &&
            aot_path_is_file(preds.metadata_path)) {
            std::vector<ExportDecl> ex;
            std::vector<SignalDecl> sg;
            std::string merr;
            if (aot::read_metadata(preds.metadata_path, &ex, &sg, &merr)) {
                std::string ierr;
                auto image = aot::AotImage::load(preds.lib_path,
                                                  preds.manifest_path, &ierr);
                if (image) {
                    for (auto &e : ex) add_export(e.name, e.type, e.default_value);
                    for (auto &s : sg) add_signal(s.name, std::move(s.args));
                    aot_image_ = std::move(image);
                    loaded = true;
                    UtilityFunctions::print(
                        aot_prefix +
                        String("fast-path: ") +
                        String::num_int64((int64_t)aot_image_->n_exports()) +
                        String(" exports + ") +
                        String::num_int64((int64_t)ex.size()) +
                        String(" inspector exports, ") +
                        String::num_int64((int64_t)sg.size()) +
                        String(" signals from ") +
                        String(preds.lib_path.c_str()));
                    return OK;
                }
                UtilityFunctions::printerr(
                    aot_prefix +
                    String("fast-path: image load failed (") +
                    String(ierr.c_str()) +
                    String("); falling through to interp eval"));
            } else {
                UtilityFunctions::printerr(
                    aot_prefix +
                    String("fast-path: metadata read failed (") +
                    String(merr.c_str()) +
                    String("); falling through to interp eval"));
            }
            // Fall through: the slow path will rebuild + rewrite metadata.
        }
    }

    // G3.b -- evaluate the baked-in node/... prelude before the user's
    // source so the curated facade (node/set-position, node/get-modulate,
    // ...) is in scope. Prelude failures are fatal: a broken prelude means
    // every script in the editor would silently lose access to the facade,
    // which is the kind of bug we want loud.
    {
        TuriValue pv = turi_eval(turi_env, TG_PRELUDE_SOURCE);
        if (pv.tag == TURI_ERROR) {
            UtilityFunctions::printerr(
                String("turmeric-godot: baked-in prelude failed to eval: ") +
                String(pv.as_error ? pv.as_error : "<unknown>"));
            loaded = false;
            return ERR_BUG;
        }
    }

    // G3.c -- evaluate the codegen'd extension_api.json facade after the
    // hand-written prelude so curated names (node/set-position, ...)
    // take precedence over the per-class generated forms
    // (node2d/set-position, ...). The generator skips prelude-covered
    // method names on Node to avoid two competing definitions.
    {
        TuriValue gv = turi_eval(turi_env, TG_GENERATED_FACADE_SOURCE);
        if (gv.tag == TURI_ERROR) {
            UtilityFunctions::printerr(
                String("turmeric-godot: generated facade failed to eval: ") +
                String(gv.as_error ? gv.as_error : "<unknown>"));
            loaded = false;
            return ERR_BUG;
        }
    }

    if (source_code.is_empty()) {
        loaded = true; // empty is valid; nothing to evaluate
        return OK;
    }

    CharString src_utf8 = std::move(src_utf8_early);
    // G2 :exports — claim the TLS slot so `godot-export` natives invoked
    // from top-level forms know which script to register against. Restore
    // (not nullify) so nested reloads — should they ever exist — pop cleanly.
    TurmericScript *prev_reloading = g_reloading_script;
    g_reloading_script = this;
    CharString path_utf8 = get_path().utf8();
    TuriValue v = turi_eval_with_path(turi_env, src_utf8.get_data(), path_utf8.get_data());
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

    // Phase A1+A2+A4 -- slow path. The interp eval above has populated
    // exports_ / signals_ through the godot-export / godot-signal natives.
    // Now build (or pick up the cached) AOT image and persist an
    // exports.metadata sidecar so future reloads with the same source
    // bytes skip straight to the fast path above.
    if (aot_enabled) {
        ProjectSettings *ps = ProjectSettings::get_singleton();
        String script_abs   = ps ? ps->globalize_path(get_path()) : get_path();
        String project_abs  = ps ? ps->globalize_path(String("res://")) : String();
        CharString project_cs = project_abs.utf8();
        CharString script_cs  = script_abs.utf8();
        const char *src_ptr  = src_utf8.get_data();
        size_t      src_len  = (size_t)src_utf8.length();
        std::string tur_bin  = aot::resolve_tur_bin(std::string());

        aot::BuildOutputs outs;
        aot::BuildError   berr;
        bool ok = aot::ensure_built(std::string(project_cs.get_data()),
                                     std::string(script_cs.get_data()),
                                     src_ptr, src_len, tur_bin,
                                     &outs, &berr);
        if (!ok) {
            UtilityFunctions::printerr(aot_prefix + String(berr.message.c_str()));
        } else {
            std::string ierr;
            auto image = aot::AotImage::load(outs.lib_path, outs.manifest_path,
                                              &ierr);
            if (!image) {
                UtilityFunctions::printerr(aot_prefix + String(ierr.c_str()));
            } else {
                aot_image_ = std::move(image);
                // Persist export/signal decls so the next reload of the
                // same source bytes hits the fast path. We write
                // unconditionally on slow path -- a corrupted sidecar
                // from a crashed editor would just force one more rebuild.
                std::string werr;
                if (!aot::write_metadata(outs.metadata_path, exports, signals, &werr)) {
                    UtilityFunctions::printerr(
                        aot_prefix + String("metadata write failed: ") +
                        String(werr.c_str()));
                }
                UtilityFunctions::print(
                    aot_prefix + String("loaded ") +
                    String::num_int64((int64_t)aot_image_->n_exports()) +
                    String(" exports from ") +
                    String(outs.lib_path.c_str()) +
                    (outs.cache_hit ? String(" (cache hit)") : String(" (built)")));
            }
        }
    }

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
    return find_signal(p_signal) != nullptr;
}

TypedArray<Dictionary> TurmericScript::_get_script_signal_list() const {
    TypedArray<Dictionary> out;
    for (const auto &s : signals) {
        Dictionary sig;
        sig["name"] = s.name;
        Array args;
        for (const auto &a : s.args) {
            Dictionary arg;
            arg["name"]        = a.name;
            arg["type"]        = (int)a.type;
            arg["class_name"]  = StringName();
            arg["hint"]        = 0;     // PROPERTY_HINT_NONE
            arg["hint_string"] = String();
            arg["usage"]       = 6;     // PROPERTY_USAGE_STORAGE | EDITOR
            args.push_back(arg);
        }
        sig["args"]         = args;
        sig["default_args"] = Array();
        sig["flags"]        = 1;        // METHOD_FLAG_NORMAL
        sig["id"]           = 0;
        out.push_back(sig);
    }
    return out;
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

void TurmericScript::add_signal(const StringName &name, std::vector<SignalArg> args) {
    for (auto &s : signals) {
        if (s.name == name) {
            s.args = std::move(args);
            return;
        }
    }
    signals.push_back(SignalDecl{name, std::move(args)});
}

const SignalDecl *TurmericScript::find_signal(const StringName &name) const {
    for (const auto &s : signals) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

} // namespace godot
