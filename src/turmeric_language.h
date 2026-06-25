#ifndef TURMERIC_GODOT_TURMERIC_LANGUAGE_H
#define TURMERIC_GODOT_TURMERIC_LANGUAGE_H

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

struct TuriEnv;

namespace godot {

// G0 spike: ScriptLanguageExtension that registers `.tur` and prints to
// stdout on every callback. No actual script execution -- just proves the
// GDExtension boundary loads, the language registers, and the editor sees
// our extension as a recognized scripting language.
class TurmericLanguage : public ScriptLanguageExtension {
    GDCLASS(TurmericLanguage, ScriptLanguageExtension)

protected:
    static void _bind_methods();

public:
    TurmericLanguage();
    ~TurmericLanguage();

    // Process-wide singleton, set by initialize_turmeric_godot_module.
    static TurmericLanguage *singleton();

private:
    void init_turi();
    void shutdown_turi();
    void smoke_test();

public:

    // --- Identity ---
    String _get_name() const override;
    String _get_type() const override;
    String _get_extension() const override;
    PackedStringArray _get_recognized_extensions() const override;
    PackedStringArray _get_reserved_words() const override;
    PackedStringArray _get_comment_delimiters() const override;
    PackedStringArray _get_string_delimiters() const override;

    // --- Lifecycle ---
    void _init() override;
    void _finish() override;

    // --- Feature flags ---
    bool _is_using_templates() override;
    bool _has_named_classes() const override;
    bool _supports_builtin_mode() const override;
    bool _supports_documentation() const override;
    bool _can_inherit_from_file() const override;

    // --- Validation / creation ---
    Dictionary _validate(const String &p_script,
                         const String &p_path,
                         bool p_validate_functions,
                         bool p_validate_errors,
                         bool p_validate_warnings,
                         bool p_validate_safe_lines) const override;
    Object *_create_script() const override;

    // --- Reloading ---
    void _reload_all_scripts() override;
    void _reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override;

    // --- Threading (required by the engine) ---
    void _thread_enter() override;
    void _thread_exit() override;

    // --- Frame hooks (called every frame in the editor) ---
    void _frame() override;

    // --- Test affordance ---
    // _validate is virtual-only on ScriptLanguageExtension and is not bound
    // as a publicly invocable method, so GDScript drivers can't call it
    // directly. This thin wrapper exposes the same code path under a bound
    // name so headless test drivers can exercise the diagnostic shape.
    Dictionary validate_source(const String &p_script, const String &p_path);
};

} // namespace godot

#endif // TURMERIC_GODOT_TURMERIC_LANGUAGE_H
