#include "turmeric_syntax_highlighter.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
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

// TgPalette is declared in the header so TurmericEditorSyntaxHighlighter
// can hold one as a field. The non-editor highlighter passes the static
// default; the editor one passes the EditorSettings-derived palette.

static const TgPalette &default_palette() {
    static const TgPalette p = {
        Color(0.55f, 0.70f, 1.00f), // keyword (blue)
        Color(0.45f, 0.55f, 0.45f), // comment (dim green)
        Color(0.85f, 0.65f, 0.45f), // string  (amber)
        Color(0.70f, 0.85f, 0.55f), // number  (pale green)
        Color(0.65f, 0.65f, 0.65f), // paren   (grey)
        Color(0.90f, 0.90f, 0.90f), // symbol  (near-white)
    };
    return p;
}

static void push_run(Dictionary &out, int32_t col, const Color &c) {
    Dictionary entry;
    entry["color"] = c;
    out[col] = entry;
}

// Core tokenizer -- both highlighter classes route here.
static Dictionary tokenize_line(const String &p_line, const TgPalette &pal) {
    Dictionary out;
    CharString cs = p_line.utf8();
    const char *s = cs.get_data();
    const int n = (int)cs.length();
    int i = 0;
    while (i < n) {
        char c = s[i];

        if (c == ';') {
            push_run(out, i, pal.comment);
            break;  // comment runs to end-of-line
        }

        if (c == '"') {
            push_run(out, i, pal.string_);
            int j = i + 1;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
                if (s[j] == '"') { j++; break; }
                j++;
            }
            push_run(out, j, pal.symbol);
            i = j;
            continue;
        }

        if (std::isspace((unsigned char)c)) { i++; continue; }

        if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
            push_run(out, i, pal.paren);
            push_run(out, i + 1, pal.symbol);
            i++;
            continue;
        }

        if (std::isdigit((unsigned char)c)) {
            push_run(out, i, pal.number);
            int j = i + 1;
            while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.' ||
                             s[j] == 'e' || s[j] == 'E' || s[j] == '-' || s[j] == '+'))
                j++;
            push_run(out, j, pal.symbol);
            i = j;
            continue;
        }

        if (is_symbol_start(c)) {
            int j = i + 1;
            while (j < n && is_symbol_cont(s[j])) j++;
            if (is_keyword(s + i, (size_t)(j - i))) {
                push_run(out, i, pal.keyword);
                push_run(out, j, pal.symbol);
            } else {
                push_run(out, i, pal.symbol);
            }
            i = j;
            continue;
        }

        i++;  // unknown -- advance to avoid an infinite loop
    }
    return out;
}

// --- TurmericSyntaxHighlighter (SCENE; headless-testable) ------------------

TurmericSyntaxHighlighter::TurmericSyntaxHighlighter()  = default;
TurmericSyntaxHighlighter::~TurmericSyntaxHighlighter() = default;

void TurmericSyntaxHighlighter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("highlight_line_for_test", "line"),
                         &TurmericSyntaxHighlighter::highlight_line_for_test);
}

Dictionary TurmericSyntaxHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
    TextEdit *te = get_text_edit();
    if (!te) return Dictionary();
    return tokenize_line(te->get_line(p_line), default_palette());
}

Dictionary TurmericSyntaxHighlighter::highlight_line_for_test(const String &p_line) const {
    return tokenize_line(p_line, default_palette());
}

// --- TurmericEditorSyntaxHighlighter (EDITOR; theme-aware) -----------------

// Refresh palette from EditorSettings text_editor/theme/highlighting/* keys.
// Falls back to the default for any key the user's theme doesn't define.
static TgPalette load_editor_palette() {
    TgPalette p = default_palette();
    EditorInterface *ei = EditorInterface::get_singleton();
    if (!ei) return p;
    Ref<EditorSettings> es_ref = ei->get_editor_settings();
    if (!es_ref.is_valid()) return p;
    EditorSettings *es = es_ref.ptr();
    struct Key { const char *path; Color *slot; };
    Key keys[] = {
        {"text_editor/theme/highlighting/keyword_color",  &p.keyword},
        {"text_editor/theme/highlighting/comment_color",  &p.comment},
        {"text_editor/theme/highlighting/string_color",   &p.string_},
        {"text_editor/theme/highlighting/number_color",   &p.number},
        {"text_editor/theme/highlighting/symbol_color",   &p.paren},
        {"text_editor/theme/highlighting/text_color",     &p.symbol},
    };
    for (const auto &k : keys) {
        if (es->has_setting(k.path)) {
            Variant v = es->get_setting(k.path);
            if (v.get_type() == Variant::COLOR) {
                *k.slot = (Color)v;
            }
        }
    }
    return p;
}

TurmericEditorSyntaxHighlighter::TurmericEditorSyntaxHighlighter()
    : palette(default_palette()) {}
TurmericEditorSyntaxHighlighter::~TurmericEditorSyntaxHighlighter() = default;

void TurmericEditorSyntaxHighlighter::_bind_methods() {}

void TurmericEditorSyntaxHighlighter::_update_cache() {
    palette = load_editor_palette();
}

Dictionary TurmericEditorSyntaxHighlighter::_get_line_syntax_highlighting(int32_t p_line) const {
    TextEdit *te = get_text_edit();
    if (!te) return Dictionary();
    return tokenize_line(te->get_line(p_line), palette);
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
