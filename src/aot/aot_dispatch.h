// aot_dispatch.h -- Phase A3 of docs/upcoming/v1/godot-binding-aot-plan.md.
//
// Direct-dispatch entry for Godot script-instance calls into an AotImage.
// Marshals Godot Variants into the shape `tur_ffi_thunk_call` wants
// (parallel int64_t / double arrays + class string), routes through the
// pre-generated `tur_ffi_call_<ret>_<args>` trampoline in
// src/runtime/ffi_dispatch.h, and marshals the scalar result back into a
// Variant.
//
// The plan calls for libffi here; we use Turmeric's own pre-generated
// trampoline table instead. Same dispatch model, no third-party dep, and
// the encoding is already shared with the spice loader so the manifest
// parser and dispatcher agree on what 'i'/'f'/'v' mean.
//
// Bail (return false from dispatch_aot_call) when:
//   - the method isn't in the manifest;
//   - any arg/ret type is Unknown / Ptr (we don't safely marshal those);
//   - the call is variadic (cons-list arg building isn't here yet).
// The caller falls back to the interpreter path on bail.

#ifndef TURMERIC_GODOT_AOT_DISPATCH_H
#define TURMERIC_GODOT_AOT_DISPATCH_H

#include <gdextension_interface.h>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>

namespace godot {
namespace aot {

class AotImage;
struct AotExport;

// Try to dispatch `method` through `image`. Returns true when the call ran
// via AOT (output stored in *out_return when non-null, error stored in
// *out_error when non-null). Returns false to indicate "fall back to
// interpreter" -- never throws and never partially populates outputs.
//
// `method` is matched first as ("_", method_name) -- the AOT plan ships
// scripts that put lifecycle methods (`_ready`, `_process`) at top level
// outside a (defmodule ...). Future work will resolve module-qualified
// names too.
bool dispatch_aot_call(const AotImage *image,
                      const char *method_name,
                      const GDExtensionConstVariantPtr *p_args,
                      int64_t n_args,
                      Variant *out_return,
                      GDExtensionCallError *out_error);

// T1.B -- split-out resolve step. Tries module "_" first, then a
// bare-name scan across every module. Returns nullptr on miss; callers
// (cb_call's per-instance cache) memoize the result -- including
// misses -- so the next dispatch with the same method name skips the
// linear scan entirely.
const AotExport *resolve_aot_method(const AotImage *image,
                                     const char *method_name);

// T1.B -- marshal + call once the export has been resolved. Same
// semantics as dispatch_aot_call but takes a pre-resolved pointer.
// `ex` must be non-null and must belong to `image`. Returns false
// only when the export's shape can't be dispatched (variadic, arity
// mismatch, Unknown/Ptr types) -- a precondition the caller's cache
// usually filtered out at insert time.
bool dispatch_aot_call_with(const AotImage *image,
                             const AotExport *ex,
                             const GDExtensionConstVariantPtr *p_args,
                             int64_t n_args,
                             Variant *out_return,
                             GDExtensionCallError *out_error);

} // namespace aot
} // namespace godot

#endif
