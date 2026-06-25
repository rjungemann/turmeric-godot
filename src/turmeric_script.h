#ifndef TURMERIC_GODOT_TURMERIC_SCRIPT_H
#define TURMERIC_GODOT_TURMERIC_SCRIPT_H

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot {

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
    void _update_exports() override;

    // --- Instance creation (G1: stub — returns null) ---
    void *_instance_create(Object *p_for_object) const override;
    void *_placeholder_instance_create(Object *p_for_object) const override;

private:
    String source_code;
    bool   loaded = false;
};

} // namespace godot

#endif
