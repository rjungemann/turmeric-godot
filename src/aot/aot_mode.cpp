// aot_mode.cpp -- see aot_mode.h.

#include "aot_mode.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace godot {
namespace aot {

namespace {

constexpr size_t kDirectiveScanBudget = 1024;

// Convert ASCII to lowercase; non-ASCII left unchanged. This is the right
// behaviour for matching mode identifiers and directive keywords -- both
// are ASCII by construction.
std::string ascii_lower(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

bool parse_mode_string(const std::string &raw, ExecutionMode *out) {
    std::string s = ascii_lower(raw);
    if (s == "aot")               { *out = ExecutionMode::Aot;         return true; }
    if (s == "1" || s == "true")  { *out = ExecutionMode::Aot;         return true; }
    if (s == "interpreter" ||
        s == "interp")            { *out = ExecutionMode::Interpreter; return true; }
    if (s == "0" || s == "false") { *out = ExecutionMode::Interpreter; return true; }
    return false;
}

// Read one logical "line" out of the source buffer starting at *cursor.
// Advances *cursor past the line terminator. Returns the line's body
// trimmed of surrounding whitespace. Stops returning lines past
// kDirectiveScanBudget bytes so a 10MB script doesn't get scanned in
// full just to discover there's no directive.
bool next_directive_line(const char *src, size_t len, size_t *cursor,
                         std::string *out) {
    if (*cursor >= len || *cursor >= kDirectiveScanBudget) return false;
    size_t start = *cursor;
    size_t end   = start;
    while (end < len && end < kDirectiveScanBudget &&
           src[end] != '\n' && src[end] != '\r') {
        end++;
    }
    size_t line_end = end;
    if (end < len && (src[end] == '\r' || src[end] == '\n')) {
        end++;
        if (end < len && src[end - 1] == '\r' && src[end] == '\n') end++;
    }
    *cursor = end;

    // Trim leading/trailing whitespace.
    while (start < line_end &&
           (src[start] == ' ' || src[start] == '\t')) start++;
    while (line_end > start &&
           (src[line_end - 1] == ' ' || src[line_end - 1] == '\t')) line_end--;
    out->assign(src + start, line_end - start);
    return true;
}

bool line_is_skipable(const std::string &line) {
    if (line.empty()) return true;
    // Turmeric comments use `;`. Sweet-exp's `#lang` directive is the one
    // non-`#mode` `#`-prefixed line we want to keep walking past.
    if (line[0] == ';') return true;
    if (line.size() >= 5 && std::memcmp(line.data(), "#lang", 5) == 0 &&
        (line.size() == 5 || line[5] == ' ' || line[5] == '\t')) {
        return true;
    }
    return false;
}

} // namespace

std::string parse_mode_directive(const char *src, size_t len) {
    if (!src || len == 0) return std::string();
    size_t cursor = 0;
    std::string line;
    while (next_directive_line(src, len, &cursor, &line)) {
        if (line_is_skipable(line)) continue;
        // Either a `#mode <ident>` line (success) or any other content
        // (no directive). Either way we stop after the first non-skippable
        // line -- a directive that appears below real code is ignored, on
        // purpose, so the precedence model stays predictable.
        if (line.size() < 6) return std::string();
        if (std::memcmp(line.data(), "#mode", 5) != 0) return std::string();
        char sep = line[5];
        if (sep != ' ' && sep != '\t') return std::string();
        size_t i = 6;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t' &&
               line[j] != ';') {
            j++;
        }
        if (j == i) return std::string();
        return line.substr(i, j - i);
    }
    return std::string();
}

std::string project_execution_mode_setting() {
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (!ps) return std::string();
    Variant v = ps->get_setting_with_override(String("turmeric/execution_mode"));
    if (v.get_type() != Variant::STRING) return std::string();
    CharString cs = String(v).utf8();
    return std::string(cs.get_data(), (size_t)cs.length());
}

std::string project_tur_binary_setting() {
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (!ps) return std::string();
    Variant v = ps->get_setting_with_override(String("turmeric/tur_binary"));
    if (v.get_type() != Variant::STRING) return std::string();
    CharString cs = String(v).utf8();
    return std::string(cs.get_data(), (size_t)cs.length());
}

ExecutionMode resolve_execution_mode(const char *source, size_t source_len,
                                     const std::string &setting_override) {
    ExecutionMode m;
    // 1. env var
    if (const char *env = std::getenv("TURMERIC_GODOT_AOT"); env && *env) {
        if (parse_mode_string(std::string(env), &m)) return m;
    }
    // 2. #mode directive
    if (source && source_len > 0) {
        std::string dir = parse_mode_directive(source, source_len);
        if (!dir.empty() && parse_mode_string(dir, &m)) return m;
    }
    // 3. project setting
    if (!setting_override.empty() && parse_mode_string(setting_override, &m)) {
        return m;
    }
    // 4. default
    return ExecutionMode::Interpreter;
}

ExecutionMode resolve_execution_mode(const char *source, size_t source_len) {
    return resolve_execution_mode(source, source_len,
                                  project_execution_mode_setting());
}

} // namespace aot
} // namespace godot
