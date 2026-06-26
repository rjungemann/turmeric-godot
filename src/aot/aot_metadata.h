// aot_metadata.h -- Phase A4 of docs/upcoming/v1/godot-binding-aot-plan.md.
//
// Serialise the export/signal declarations a script registered during its
// last interp eval into a sidecar file next to the AOT manifest. On
// subsequent reloads (when the cache slot is still warm) the metadata file
// lets us skip the interp eval entirely -- A4's "init shim, but cached"
// answer.
//
// File format -- one record per line, tab-separated:
//   EXPORT\t<name>\t<type_int>\t<default_string>
//   SIGNAL\t<name>\t<argc>
//   SIGARG\t<name>\t<type_int>
// SIGARG lines immediately follow their SIGNAL. <default_string> is
// `Variant::stringify()` for primitive types (bool / int / float / string)
// and the empty string for everything else -- the reader then default-
// constructs a Variant of the declared type. That covers what
// godot-export's surface actually accepts today.

#ifndef TURMERIC_GODOT_AOT_METADATA_H
#define TURMERIC_GODOT_AOT_METADATA_H

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string>
#include <vector>

namespace godot {

// Forward decls -- pull in the script-level struct definitions only when
// implementing, not when including.
struct ExportDecl;
struct SignalDecl;
struct SignalArg;

namespace aot {

// Write all (exports, signals) recorded on a TurmericScript to `path`.
// Returns true on success; on failure fills *out_err with a human-readable
// reason and leaves any partial file in place (callers treat write
// failures as "fast path will miss next time" rather than fatal).
bool write_metadata(const std::string &path,
                    const std::vector<ExportDecl> &exports,
                    const std::vector<SignalDecl> &signals,
                    std::string *out_err);

// Read a metadata sidecar back into freshly-allocated export/signal
// vectors. Returns true on success. A missing file, malformed line, or
// unparseable type returns false with *out_err populated -- the caller
// falls back to a full interp eval.
bool read_metadata(const std::string &path,
                   std::vector<ExportDecl> *out_exports,
                   std::vector<SignalDecl> *out_signals,
                   std::string *out_err);

} // namespace aot
} // namespace godot

#endif
