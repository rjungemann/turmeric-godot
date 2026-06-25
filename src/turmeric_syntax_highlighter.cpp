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

TurmericSyntaxHighlighter::TurmericSyntaxHighlighter() {
    // A monochrome-friendly palette; the editor-plugin pass can override
    // these from the user's theme.
    color_keyword = Color(0.55f, 0.70f, 1.00f);   // blue
    color_comment = Color(0.45f, 0.55f, 0.45f);   // dim green
    color_string  = Color(0.85f, 0.65f, 0.45f);   // amber
    color_number  = Color(0.70f, 0.85f, 0.55f);   // pale green
    color_paren   = Color(0.65f, 0.65f, 0.65f);   // grey
    color_symbol  = Color(0.90f, 0.90f, 0.90f);   // near-white
}

TurmericSyntaxHighlighter::~TurmericSyntaxHighlighter() = default;

void TurmericSyntaxHighlighter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("highlight_line_for_test", "line"),
                         &TurmericSyntaxHighlighter::highlight_line_for_test);
}

Dictionary TurmericSyntaxHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
    TextEdit *te = get_text_edit();
    if (!te) return Dictionary();
    return tokenize(te->get_line(p_line));
}

Dictionary TurmericSyntaxHighlighter::highlight_line_for_test(const String &p_line) const {
    return tokenize(p_line);
}

// Emit one column entry. Godot interprets the dict as a sorted-by-key
// sequence of (column, attributes) pairs; the attributes apply from the
// given column until the next entry. So a "default" color row is
// implicit -- we push a row at each transition.
static void push_run(Dictionary &out, int32_t col, const Color &c) {
    Dictionary entry;
    entry["color"] = c;
    out[col] = entry;
}

Dictionary TurmericSyntaxHighlighter::tokenize(const String &p_line) const {
    Dictionary out;
    CharString cs = p_line.utf8();
    const char *s = cs.get_data();
    const int n = (int)cs.length();
    int i = 0;
    while (i < n) {
        char c = s[i];

        // Comment: ; or ;; or ;;; to end-of-line.
        if (c == ';') {
            push_run(out, i, color_comment);
            // Comment runs to end-of-line; no need to push another color.
            break;
        }

        // String literal: "..." with simple \-escape.
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

        // Whitespace: don't emit a transition; the previous color persists.
        if (std::isspace((unsigned char)c)) {
            i++;
            continue;
        }

        // Parens / brackets / braces.
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
            push_run(out, i, color_paren);
            push_run(out, i + 1, color_symbol);
            i++;
            continue;
        }

        // Number: leading digit, optionally negative if preceded by space
        // (we don't try to distinguish unary minus from `-` symbol; the
        // simpler rule is "starts with digit, then digits / dot / e").
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

        // Symbol / keyword.
        if (is_symbol_start(c)) {
            int j = i + 1;
            while (j < n && is_symbol_cont(s[j])) j++;
            if (is_keyword(s + i, (size_t)(j - i))) {
                push_run(out, i, color_keyword);
                push_run(out, j, color_symbol);
            } else {
                // Same as default; only emit if previous was non-symbol.
                push_run(out, i, color_symbol);
            }
            i = j;
            continue;
        }

        // Unknown -- advance one char to avoid an infinite loop.
        i++;
    }
    return out;
}

} // namespace godot
