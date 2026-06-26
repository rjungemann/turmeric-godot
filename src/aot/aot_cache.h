// aot_cache.h -- Phase A1 of docs/upcoming/v1/godot-binding-aot-plan.md.
//
// For a given Turmeric script (path + source bytes), stage a transient
// `tur build --shared` project under
// `<godot_project>/.godot/turmeric-cache/<hash>/`, invoke the compiler if
// the cached shared library is stale, and return the paths the dlopen layer
// (aot_image) needs.
//
// The hash mixes the script's project-relative path, its source bytes, and
// the `tur` binary's realpath + mtime so a compiler upgrade or any source
// edit forces a rebuild. The hash is the *only* cache key; we never trust
// the manifest alone, because a successful build with stale sources would
// leave a manifest that names symbols that no longer match the script.

#ifndef TURMERIC_GODOT_AOT_CACHE_H
#define TURMERIC_GODOT_AOT_CACHE_H

#include <string>

namespace godot {
namespace aot {

struct BuildOutputs {
    std::string lib_path;       // absolute path to the built .so
    std::string manifest_path;  // absolute path to exports.manifest
    std::string stage_dir;      // absolute path to <hash>/ (parent of build/)
    std::string metadata_path;  // absolute path to the exports.metadata sidecar
    bool        cache_hit = false;
};

struct BuildError {
    std::string message;        // human-readable; suitable for the diag sink
    int         exit_code = 0;  // tur exit code when applicable, else 0
};

// Compute the cache key for (script_path, source). Exposed so callers can
// reason about cache layout in tests / diagnostics; ensure_built recomputes
// it internally.
std::string compute_script_hash(const std::string &script_path,
                                const char *source_bytes, size_t source_len,
                                const std::string &tur_bin);

// Resolve the `tur` binary by precedence:
//   1. TUR_BIN env var
//   2. project_setting_override (when non-empty)
//   3. `tur` on PATH (returned as bare "tur")
std::string resolve_tur_bin(const std::string &project_setting_override);

// Locate (or create) the cache root for `godot_project_dir`. Returns
// `<godot_project_dir>/.godot/turmeric-cache`.
std::string cache_root_for(const std::string &godot_project_dir);

// Predict the cache layout for a given script without touching the
// filesystem -- pure path arithmetic. Used by the A4 fast-path probe to
// look up `lib_path`/`manifest_path`/`metadata_path` ahead of any work,
// and by `ensure_built` internally so the two paths agree on layout.
BuildOutputs predict_outputs(const std::string &godot_project_dir,
                              const std::string &script_path,
                              const char *source_bytes, size_t source_len,
                              const std::string &tur_bin);

// Stage + build (or hit cache). On success returns true and populates
// `out`; on failure returns false and populates `err`.
//
// `godot_project_dir` is the absolute path to the Godot project root (the
// dir holding project.godot). When empty, the cache is rooted at
// `$TMPDIR/turmeric-godot-cache/`.
bool ensure_built(const std::string &godot_project_dir,
                  const std::string &script_path,
                  const char *source_bytes, size_t source_len,
                  const std::string &tur_bin,
                  BuildOutputs *out, BuildError *err);

} // namespace aot
} // namespace godot

#endif
