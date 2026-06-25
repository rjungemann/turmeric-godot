#ifndef TURMERIC_GODOT_SYNTAX_HIGHLIGHTER_H
#define TURMERIC_GODOT_SYNTAX_HIGHLIGHTER_H

#include <godot_cpp/classes/editor_syntax_highlighter.hpp>
#include <godot_cpp/classes/syntax_highlighter.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// G4 -- minimal token-coloring for .tur source. Two thin classes share
// one tokenizer:
//
//   TurmericSyntaxHighlighter        : SyntaxHighlighter
//     Registered at SCENE init level. Usable headlessly via the bound
//     highlight_line_for_test for regression coverage; also fine for
//     non-editor runtime UIs (e.g. an in-game text editor).
//
//   TurmericEditorSyntaxHighlighter  : EditorSyntaxHighlighter
//     Registered at EDITOR init level (the base type only exists then).
//     Adds _get_name + _get_supported_languages so the addons/
//     turmeric-godot-editor @tool plugin can hand it to
//     ScriptEditor.register_syntax_highlighter.

class TurmericSyntaxHighlighter : public SyntaxHighlighter {
    GDCLASS(TurmericSyntaxHighlighter, SyntaxHighlighter)

protected:
    static void _bind_methods();

public:
    TurmericSyntaxHighlighter();
    ~TurmericSyntaxHighlighter();

    Dictionary _get_line_syntax_highlighting(int32_t p_line) const override;

    // Test affordance; mirrors the engine entry point but takes the
    // line text directly so headless drivers can exercise the tokenizer
    // without a TextEdit.
    Dictionary highlight_line_for_test(const String &p_line) const;
};

class TurmericEditorSyntaxHighlighter : public EditorSyntaxHighlighter {
    GDCLASS(TurmericEditorSyntaxHighlighter, EditorSyntaxHighlighter)

protected:
    static void _bind_methods();

public:
    TurmericEditorSyntaxHighlighter();
    ~TurmericEditorSyntaxHighlighter();

    Dictionary _get_line_syntax_highlighting(int32_t p_line) const override;
    String     _get_name() const override;
    PackedStringArray _get_supported_languages() const override;
};

} // namespace godot

#endif
