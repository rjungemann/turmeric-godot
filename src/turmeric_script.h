#ifndef TURMERIC_GODOT_TURMERIC_SCRIPT_H
#define TURMERIC_GODOT_TURMERIC_SCRIPT_H

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <vector>

struct TuriEnv;

namespace godot {

// G2 :exports — one declaration registered by `(godot-export ...)` at the
// top level of a script. The script-level default is the value the inspector
// shows for a freshly-attached node before the user types anything.
struct ExportDecl {
    StringName    name;
    Variant::Type type;
    Variant       default_value;
};

// G2 :signals — one declaration registered by `(godot-signal ...)` at the
// top level of a script. Surfaces in the Node dock under the script's
// signals, just like GDScript's `signal` keyword.
struct SignalArg {
    StringName    name;
    Variant::Type type;
};
struct SignalDecl {
    StringName             name;
    std::vector<SignalArg> args;
};

// G1: a TurmericScript holds the source code of one `.tur` file and, on
// reload, hands the source to libturi for evaluation. Per-node instance
// dispatch (the GDExtensionScriptInstanceInfo3 function-table dance) is the
// next slice; this class currently returns null instances so a script that
// is attached to a node will print a warning rather than execute lifecycle
// callbacks.
class TurmericScript : public ScriptExtension {
    GDCLASS(TurmericScript, ScriptExtension)

protected:
    static void _bind_methods() {}

public:
    TurmericScript();
    ~TurmericScript();

    // --- Source ---
    bool _has_source_code() const override;
    String _get_source_code() const override;
    void _set_source_code(const String &p_code) override;
    Error _reload(bool p_keep_state) override;

    // --- Identity ---
    ScriptLanguage *_get_language() const override;
    bool _is_valid() const override;
    bool _is_tool() const override;
    bool _can_instantiate() const override;
    StringName _get_instance_base_type() const override;
    Ref<Script> _get_base_script() const override;
    bool _inherits_script(const Ref<Script> &p_script) const override;

    // --- Methods (stubbed for now) ---
    bool _has_method(const StringName &p_method) const override;
    bool _has_script_signal(const StringName &p_signal) const override;
    TypedArray<Dictionary> _get_script_signal_list() const override;
    void _update_exports() override;

    // --- Instance creation ---
    void *_instance_create(Object *p_for_object) const override;
    void *_placeholder_instance_create(Object *p_for_object) const override;

    // --- Access for TurmericInstance dispatch ---
    TuriEnv *get_turi_env() const { return turi_env; }

    // --- G2 :exports ---
    // Called by the `godot-export` native during _reload. Idempotent across
    // reloads because clear_exports() runs first.
    void add_export(const StringName &name, Variant::Type type,
                    const Variant &default_value);
    void clear_exports() { exports.clear(); }
    const std::vector<ExportDecl> &get_exports() const { return exports; }
    // Returns nullptr if `name` was not declared via `godot-export`.
    const ExportDecl *find_export(const StringName &name) const;

    // --- G2 :signals ---
    void add_signal(const StringName &name, std::vector<SignalArg> args);
    void clear_signals() { signals.clear(); }
    const std::vector<SignalDecl> &get_signals() const { return signals; }
    const SignalDecl *find_signal(const StringName &name) const;

private:
    String                  source_code;
    bool                    loaded   = false;
    TuriEnv                *turi_env = nullptr;
    std::vector<ExportDecl> exports;
    std::vector<SignalDecl> signals;
};

} // namespace godot

#endif
