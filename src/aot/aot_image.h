// aot_image.h -- Phase A2 of docs/upcoming/v1/godot-binding-aot-plan.md.
//
// An AotImage owns a dlopen handle and a parsed export table for one
// per-script .so produced by aot_cache. The export records use the
// 'i'/'f'/'v' class encoding shared with src/runtime/ffi_dispatch.h, so A3
// can route each call through the matching `tur_ffi_call_<ret>_<args>`
// trampoline without reparsing.
//
// Lifetime: load() takes ownership of the dlopen handle; ~AotImage calls
// dlclose. Hot-reload is "tear down old image, build + load new one" -- a
// TurmericScript owns at most one image at a time, so handle generations
// never overlap. The image's exports vector is stable for the image's
// lifetime; A3's dispatch caches AotExport* pointers without copying.

#ifndef TURMERIC_GODOT_AOT_IMAGE_H
#define TURMERIC_GODOT_AOT_IMAGE_H

#include <memory>
#include <string>
#include <vector>

namespace godot {
namespace aot {

// Mirrors TUR_SPICE_MAX_ARITY in src/turi/spice_loader.h. The compiler's
// MAX_FN_ARITY is 16; any defn over that is rejected long before the
// manifest hits us.
constexpr int kAotMaxArity = 16;

// Narrow type tags parsed from the manifest. The ffi_dispatch class char
// ('i'/'f'/'v') is coarser -- 'i' covers Int/Bool/Cstr/Ptr -- and is what
// tur_ffi_thunk_call wants. The marshaller needs the narrow tag to decide
// e.g. "this 'i' is a Cstr, utf8 the Variant before passing".
enum class AotType : uint8_t {
    Void,
    Bool,
    Int,    // covers :int and the sized int variants
    Cstr,
    Ptr,
    Float,  // covers :float / :float32 / :float64
    Unknown // anything we don't yet marshal (e.g. :any, struct types)
};

// 'v' for Void, 'f' for Float, 'i' for Bool/Int/Cstr/Ptr, '?' for Unknown.
inline char aot_type_class(AotType t) {
    switch (t) {
        case AotType::Void:    return 'v';
        case AotType::Float:   return 'f';
        case AotType::Bool:
        case AotType::Int:
        case AotType::Cstr:
        case AotType::Ptr:     return 'i';
        case AotType::Unknown: return '?';
    }
    return '?';
}

struct AotExport {
    std::string module;    // defmodule name; "_" for top-level forms
    std::string name;      // defn name (the Turmeric-visible identifier)
    std::string mangled;   // C symbol name actually dlsym'd
    void       *fn_ptr = nullptr;

    AotType ret_type = AotType::Unknown;
    AotType arg_types[kAotMaxArity] = {};
    uint8_t n_args     = 0;
    bool    is_variadic = false;
    AotType rest_type = AotType::Unknown; // valid iff is_variadic
};

class AotImage {
public:
    // Load a built .so + manifest. On any failure returns nullptr and writes
    // a human-readable reason to *out_err.
    static std::unique_ptr<AotImage> load(const std::string &lib_path,
                                           const std::string &manifest_path,
                                           std::string *out_err);

    // dlclose the handle; export records are released by vector destruction.
    ~AotImage();

    AotImage(const AotImage &)            = delete;
    AotImage &operator=(const AotImage &) = delete;

    const std::string &lib_path()      const { return lib_path_; }
    const std::string &manifest_path() const { return manifest_path_; }

    size_t           n_exports() const { return exports_.size(); }
    const AotExport &at(size_t i) const { return exports_[i]; }

    // Find by (module, name). Returns nullptr when absent. A `nullptr`
    // `module` is shorthand for the catch-all "_" namespace; pass it when
    // resolving a lifecycle method like "_ready" the user wrote at top
    // level outside a (defmodule ...) form.
    const AotExport *find(const char *module, const char *name) const;

    // Cross-module name lookup. Walks every export and returns the first
    // record whose `name` matches, regardless of module. dispatch_aot_call
    // uses this as a fallback so `hot` declared inside
    // `(defmodule bench (export hot))` still resolves when Godot calls
    // `instance.call("hot")` with the bare name.
    const AotExport *find_by_name(const char *name) const;

    // Has this image already logged its "first dispatch routed via AOT"
    // line? Marked mutable so const dispatch paths can flip it. The flag
    // is cleared once per image; reload drops the image, so a new build
    // re-arms the log.
    bool note_first_dispatch_logged() const {
        bool was = first_dispatch_logged_;
        first_dispatch_logged_ = true;
        return was;
    }

private:
    AotImage() = default;

    std::string             lib_path_;
    std::string             manifest_path_;
    void                   *handle_ = nullptr;
    std::vector<AotExport>  exports_;
    mutable bool            first_dispatch_logged_ = false;
};

// Parse a single manifest line of the form:
//   "mod/name -> mangled :: (:t1 :t2 [& :rt]) -> :ret"
// Returns true on success, false on malformed input. `out->fn_ptr` is left
// null -- the caller resolves it via dlsym after dlopen.
bool parse_manifest_line(const char *line, size_t len, AotExport *out);

// Map a manifest type tag (":int", ":float", ":cstr", ...) to its narrow
// AotType. Unrecognised tags map to AotType::Unknown.
AotType manifest_type_to_aot(const std::string &tag);

} // namespace aot
} // namespace godot

#endif
