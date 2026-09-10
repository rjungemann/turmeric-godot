// aot_natives.h -- the `tg-godot` module the AOT path stages next to a script.
//
// The interpreter hands a script two things standalone `tur` knows nothing
// about: the `godot-*` natives (C++ functions registered into the TuriEnv) and
// the baked-in prelude (Turmeric wrappers evaluated into that same env). A
// staged project sees neither, which is why `tur build --shared` used to stop
// at the first `(godot-export ...)` with `unknown function or operator`.
//
// staged_natives_module() rebuilds both for the compiler:
//
//   (defmodule tg-godot
//     (export ...)
//     (extern-c godot-export [name : cstr ty : cstr dflt : float] : void)
//     ...                                    <- one row per bridge/native_abi.h
//     <the prelude, verbatim, minus its ;;#aot-skip regions>)
//
// `extern-c` leaves the symbol undefined in the emitted C; the staged library
// links with `-undefined dynamic_lookup` (macOS) / `-shared` (elsewhere), and
// the dlopen in aot_image.cpp binds each one to the entry point this extension
// exports from bridge/native_abi.cpp. That is fix direction 2 of the report
// -- resolve at load, not at link -- and it needs no compiler-side change.
//
// A `defmodule` must be the first form in its file, so the declarations cannot
// simply be prepended to the user's source: any script using `(defmodule ...)`
// -- which is the only shape whose defns reach `exports.manifest` -- would stop
// compiling. They go in their own module instead, and inject_import() adds the
// one `(import tg-godot)` form the user's file needs.

#ifndef TURMERIC_GODOT_AOT_NATIVES_H
#define TURMERIC_GODOT_AOT_NATIVES_H

#include <string>

namespace godot {
namespace aot {

// The module's Turmeric name, and the filename it must be staged under
// (module resolution looks for `<name>.tur` beside the importing file).
extern const char *const kNativesModuleName;  // "tg-godot"
extern const char *const kNativesModuleFile;  // "tg-godot.tur"

// Build the full source text of the staged declarations module.
std::string staged_natives_module();

// Rewrite a script into the shape the staged build needs, without moving a
// single line.
//
// Two facts about `tur` force this, and between them they rule out simply
// prepending the declarations:
//
//   * `import is only allowed inside defmodule`, so a script written as bare
//     top-level forms cannot reach the `tg-godot` module at all; and
//   * `defmodule must be the first form in the file`, so the declarations
//     cannot be pasted above a script that already has one.
//
// A script that already opens with `(defmodule ...)` therefore just gets an
// `(import tg-godot)` spliced in after the module name. A script written as
// bare top-level forms -- which is every example in this repo except
// aot-bench -- gets wrapped:
//
//     (defmodule <module> (import tg-godot) (export <its defns>) <source> )
//
// with the whole opening spliced onto the front of the first real form's line
// and the closing paren appended at EOF. No newline is added anywhere, so
// `tur`'s diagnostics keep reporting the line numbers the user sees in their
// editor.
//
// Wrapping also has to blank the script's top-level *calls* -- `(godot-export
// "vel-x" "float" 240.0)` and friends. Inside a `defmodule` those are a hard
// error (TUR-E0711: "expression at (defmodule ...) top level is never
// evaluated"), and outside one the compiler drops them anyway; either way the
// interpreter pass in TurmericScript::_reload has already run them, which is
// what populates the inspector exports and the signal list. Blanked in place,
// again so nothing moves.
std::string prepare_staged_source(const std::string &src,
                                  const std::string &module_name);

// Append an explanatory note to a failed staged build's log for every
// `unknown function or operator '<name>'` it reports where <name> is a native
// this extension does register -- just not one with an AOT entry point. The
// bare compiler message is correct and unhelpful: the name plainly works when
// the same script runs interpreted. Returns `log` unchanged when nothing
// matches.
std::string annotate_build_log(const std::string &log);

// Blank every line of `prelude` that falls inside a `;;#aot-skip-begin` /
// `;;#aot-skip-end` pair, markers included. Exposed for tests; callers want
// staged_natives_module(). Blanking rather than deleting keeps the prelude's
// own line numbers intact, which matters when a staged build reports an error
// inside it.
std::string strip_aot_skip_regions(const std::string &prelude);

} // namespace aot
} // namespace godot

#endif
