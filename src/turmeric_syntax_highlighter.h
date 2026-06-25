#ifndef TURMERIC_GODOT_SYNTAX_HIGHLIGHTER_H
#define TURMERIC_GODOT_SYNTAX_HIGHLIGHTER_H

#include <godot_cpp/classes/syntax_highlighter.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// G4 -- minimal token-coloring for .tur source. Subclasses SyntaxHighlighter
// (not EditorSyntaxHighlighter) so the same instance is usable both at
// runtime (e.g. an in-game text editor) and in the editor, once a thin
// @tool plugin registers it. Editor wiring is a follow-up; the highlighter
// surface itself is exercised in headless tests via highlight_line_for_test
// since SyntaxHighlighter::_get_line_syntax_highlighting reads its source
// through a live TextEdit attachment that doesn't exist headlessly.
class TurmericSyntaxHighlighter : public SyntaxHighlighter {
    GDCLASS(TurmericSyntaxHighlighter, SyntaxHighlighter)

protected:
    static void _bind_methods();

public:
    TurmericSyntaxHighlighter();
    ~TurmericSyntaxHighlighter();

    // Engine-virtual entry point: get the current line's text via the
    // attached TextEdit, tokenize, and return column -> {color} dict.
    Dictionary _get_line_syntax_highlighting(int32_t p_line) const override;

    // Test-only -- mirrors the same tokenization pipeline but takes the
    // line text directly so headless drivers can exercise it without
    // standing up a TextEdit.
    Dictionary highlight_line_for_test(const String &p_line) const;

private:
    // Token colors. Held as members so a future editor-plugin pass can
    // bind them to the current theme.
    Color color_keyword;
    Color color_comment;
    Color color_string;
    Color color_number;
    Color color_paren;
    Color color_symbol;

    // Core tokenizer: takes a single line (no embedded newlines) and
    // produces the {column -> {color: ...}} dict Godot expects.
    Dictionary tokenize(const String &p_line) const;
};

} // namespace godot

#endif
