#include "turmeric_syntax_highlighter.h"

#include <godot_cpp/classes/text_edit.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cctype>
#include <cstring>

namespace godot {

// Keyword set -- intentionally small. Matches the reserved-word list
// turmeric_language.cpp reports via _get_reserved_words. Growing this
// only requires syncing the two sources of truth.
static bool is_keyword(const char *s, size_t n) {
    static const char *kws[] = {
        "defn", "defmacro", "defstruct", "deftype", "definstance", "defclass",
        "def", "let", "letrec", "fn", "if", "cond", "when", "unless", "do",
        "for", "while", "import", "export", "module", "and", "or", "not",
        "match", "case", "return", "true", "false", "nil",
    };
    for (size_t i = 0; i < sizeof(kws)/sizeof(kws[0]); ++i) {
        if (std::strlen(kws[i]) == n && std::strncmp(kws[i], s, n) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_symbol_start(char c) {
    return std::isalpha((unsigned char)c) || c == '_' || c == '-' || c == '+' ||
           c == '*' || c == '/' || c == '?' || c == '!' || c == '<' || c == '>' ||
           c == '=' || c == '&' || c == '%' || c == ':' || c == '.';
}

static bool is_symbol_cont(char c) {
    return is_symbol_start(c) || std::isdigit((unsigned char)c);
}

// Shared palette. A future editor-plugin pass can theme these from the
// user's editor settings; for now they're file-static so both highlighter
// classes paint the same colors.
static const Color color_keyword(0.55f, 0.70f, 1.00f);
static const Color color_comment(0.45f, 0.55f, 0.45f);
static const Color color_string (0.85f, 0.65f, 0.45f);
static const Color color_number (0.70f, 0.85f, 0.55f);
static const Color color_paren  (0.65f, 0.65f, 0.65f);
static const Color color_symbol (0.90f, 0.90f, 0.90f);

static void push_run(Dictionary &out, int32_t col, const Color &c) {
    Dictionary entry;
    entry["color"] = c;
    out[col] = entry;
}

// Core tokenizer -- shared by both highlighter classes.
static Dictionary tokenize_line(const String &p_line) {
    Dictionary out;
    CharString cs = p_line.utf8();
    const char *s = cs.get_data();
    const int n = (int)cs.length();
    int i = 0;
    while (i < n) {
        char c = s[i];

        if (c == ';') {
            push_run(out, i, color_comment);
            break;  // comment runs to end-of-line
        }

        if (c == '"') {
            push_run(out, i, color_string);
            int j = i + 1;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
                if (s[j] == '"') { j++; break; }
                j++;
            }
            push_run(out, j, color_symbol);
            i = j;
            continue;
        }

        if (std::isspace((unsigned char)c)) { i++; continue; }

        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
            push_run(out, i, color_paren);
            push_run(out, i + 1, color_symbol);
            i++;
            continue;
        }

        if (std::isdigit((unsigned char)c)) {
            push_run(out, i, color_number);
            int j = i + 1;
            while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.' ||
                             s[j] == 'e' || s[j] == 'E' || s[j] == '-' || s[j] == '+'))
                j++;
            push_run(out, j, color_symbol);
            i = j;
            continue;
        }

        if (is_symbol_start(c)) {
            int j = i + 1;
            while (j < n && is_symbol_cont(s[j])) j++;
            if (is_keyword(s + i, (size_t)(j - i))) {
                push_run(out, i, color_keyword);
                push_run(out, j, color_symbol);
            } else {
                push_run(out, i, color_symbol);
            }
            i = j;
            continue;
        }

        i++;  // unknown -- advance to avoid an infinite loop
    }
    return out;
}

// --- TurmericSyntaxHighlighter (SCENE-level; headless-testable) ------------

TurmericSyntaxHighlighter::TurmericSyntaxHighlighter()  = default;
TurmericSyntaxHighlighter::~TurmericSyntaxHighlighter() = default;

void TurmericSyntaxHighlighter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("highlight_line_for_test", "line"),
                         &TurmericSyntaxHighlighter::highlight_line_for_test);
}

Dictionary TurmericSyntaxHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
    TextEdit *te = get_text_edit();
    if (!te) return Dictionary();
    return tokenize_line(te->get_line(p_line));
}

Dictionary TurmericSyntaxHighlighter::highlight_line_for_test(const String &p_line) const {
    return tokenize_line(p_line);
}

// --- TurmericEditorSyntaxHighlighter (EDITOR-level; @tool plugin wires) ----

TurmericEditorSyntaxHighlighter::TurmericEditorSyntaxHighlighter()  = default;
TurmericEditorSyntaxHighlighter::~TurmericEditorSyntaxHighlighter() = default;

void TurmericEditorSyntaxHighlighter::_bind_methods() {}

Dictionary TurmericEditorSyntaxHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
    TextEdit *te = get_text_edit();
    if (!te) return Dictionary();
    return tokenize_line(te->get_line(p_line));
}

String TurmericEditorSyntaxHighlighter::_get_name() const {
    return String("Turmeric");
}

PackedStringArray TurmericEditorSyntaxHighlighter::_get_supported_languages() const {
    PackedStringArray langs;
    langs.push_back("tur");
    return langs;
}

} // namespace godot
