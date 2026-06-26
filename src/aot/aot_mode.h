// aot_mode.h -- Phase A5 of docs/upcoming/v1/godot-binding-aot-plan.md.
//
// Resolves the execution mode for one TurmericScript reload. Precedence,
// highest-wins:
//   1. TURMERIC_GODOT_AOT env var
//        "1" / "true" / "aot"           -> Aot
//        "0" / "false" / "interpreter"  -> Interpreter
//        (anything else / unset)        -> not authoritative; fall through
//   2. Per-script `#mode <mode>` directive at the top of the file
//        Recognised modes: aot, interpreter
//   3. ProjectSettings `turmeric/execution_mode` (string)
//   4. Default: Interpreter
//
// The env var sits above the project setting because it is the dev-loop
// override -- "I want to AOT this one editor session without changing the
// committed project.godot". The `#mode` directive is the per-script knob
// that committed code uses to opt in/out regardless of the project default.

#ifndef TURMERIC_GODOT_AOT_MODE_H
#define TURMERIC_GODOT_AOT_MODE_H

#include <cstddef>
#include <string>

namespace godot {
namespace aot {

enum class ExecutionMode {
    Interpreter,
    Aot,
};

// Read the project setting `turmeric/execution_mode`, returning the empty
// string when ProjectSettings is unavailable or the setting is unset.
// Exposed for the language _init code so it can register the setting.
std::string project_execution_mode_setting();

// Read the project setting `turmeric/tur_binary` (path override for the
// `tur` compiler binary). Exposed for resolve_tur_bin's precedence chain.
std::string project_tur_binary_setting();

// Scan the first ~1KB of source bytes for a `#mode <ident>` line. Returns
// the matched identifier (e.g. "aot", "interpreter") or empty when no
// directive is present. Tolerates leading blank lines, `;`/`;;`/`;;;`
// comments, and an opening `#lang sweet-exp` line.
std::string parse_mode_directive(const char *src, size_t len);

// Apply the precedence chain. `source` may be null/empty -- the directive
// step is just skipped. `setting_override` is the project-setting value
// fetched separately so this function doesn't have to live downstream of
// the godot-cpp ProjectSettings header (handy in tests).
ExecutionMode resolve_execution_mode(const char *source, size_t source_len,
                                     const std::string &setting_override);

// Convenience -- pulls the setting via ProjectSettings, then resolves.
ExecutionMode resolve_execution_mode(const char *source, size_t source_len);

} // namespace aot
} // namespace godot

#endif
