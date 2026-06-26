// aot_dispatch.cpp -- see aot_dispatch.h.

#include "aot_dispatch.h"
#include "aot_image.h"

#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <cstring>

extern "C" {
#include "runtime/ffi_dispatch_thunk.h"
}

namespace godot {
namespace aot {

namespace {

// Pull an int64 out of a Variant for an :int / :bool arg. Bool / int /
// float widths all coerce cleanly via the Variant cast operators -- no
// truncation surprises that we wouldn't see in GDScript too.
int64_t variant_to_i(const Variant &v, AotType t) {
    switch (t) {
        case AotType::Bool: return (int64_t)((bool)v ? 1 : 0);
        case AotType::Int:  return (int64_t)v;
        case AotType::Ptr:  return (int64_t)v; // raw handle; caller's risk
        default:            return (int64_t)v;
    }
}

double variant_to_f(const Variant &v) {
    return (double)v;
}

// Build a Variant from the scalar an AOT call returned. cstr returns
// borrow the const char * directly into a Godot String, which copies the
// bytes -- so the int64 lifetime ends with the call.
Variant scalar_to_variant(AotType t, int64_t out_i, double out_f) {
    switch (t) {
        case AotType::Void:    return Variant();
        case AotType::Bool:    return Variant(out_i != 0);
        case AotType::Int:     return Variant((int64_t)out_i);
        case AotType::Float:   return Variant(out_f);
        case AotType::Cstr: {
            const char *s = (const char *)(intptr_t)out_i;
            if (!s) return Variant(String());
            return Variant(String::utf8(s));
        }
        case AotType::Ptr:     return Variant((int64_t)out_i);
        case AotType::Unknown: return Variant();
    }
    return Variant();
}

bool type_is_marshalable(AotType t, bool /*is_return*/) {
    switch (t) {
        case AotType::Void:    return true;
        case AotType::Bool:    return true;
        case AotType::Int:     return true;
        case AotType::Float:   return true;
        case AotType::Cstr:    return true;
        case AotType::Ptr:     return false;  // unsafe to marshal blind
        case AotType::Unknown: return false;
    }
    return false;
}

} // namespace

const AotExport *resolve_aot_method(const AotImage *image,
                                     const char *method_name) {
    if (!image || !method_name) return nullptr;
    // Try the top-level "_" module first (matches a script written with
    // bare top-level defns, the common case in the binding examples).
    // If that misses, scan by bare name -- a script using
    // `(defmodule bench (export hot))` lands `hot` under module "bench",
    // and Godot still calls it as `instance.call("hot")`.
    const AotExport *e = image->find("_", method_name);
    if (!e) e = image->find_by_name(method_name);
    return e;
}

bool dispatch_aot_call(const AotImage *image,
                      const char *method_name,
                      const GDExtensionConstVariantPtr *p_args,
                      int64_t n_args,
                      Variant *out_return,
                      GDExtensionCallError *out_error) {
    const AotExport *e = resolve_aot_method(image, method_name);
    if (!e) return false;
    return dispatch_aot_call_with(image, e, p_args, n_args,
                                   out_return, out_error);
}

bool dispatch_aot_call_with(const AotImage *image,
                             const AotExport *e,
                             const GDExtensionConstVariantPtr *p_args,
                             int64_t n_args,
                             Variant *out_return,
                             GDExtensionCallError *out_error) {
    if (!image || !e) return false;

    // One-shot route log so the user can confirm AOT actually fired.
    // Subsequent dispatches don't log; in a 1M-iteration hot loop the
    // first-call line is the only signal that survives the noise.
    if (!image->note_first_dispatch_logged()) {
        UtilityFunctions::print(
            String("[turmeric-godot AOT] first dispatch routed via AOT: ") +
            String(e->module.c_str()) + String("/") +
            String(e->name.c_str()) +
            String(" (mangled=") + String(e->mangled.c_str()) + String(")"));
    }

    // Variadic dispatch needs a cons-list builder we haven't wired yet.
    if (e->is_variadic) return false;

    // Arity mismatch -- treat as not-our-call. Godot will surface a clean
    // CALL_ERROR_TOO_FEW/MANY via the interpreter path if it also can't
    // resolve it.
    if ((int64_t)e->n_args != n_args) return false;

    if (!type_is_marshalable(e->ret_type, /*is_return=*/true)) return false;
    for (uint8_t i = 0; i < e->n_args; i++) {
        if (!type_is_marshalable(e->arg_types[i], false)) return false;
    }

    // Per-call scratch. The CharString[] holds utf8 byte buffers for any
    // :cstr arg; the pointers in i_vals must not outlive these. arg_chars
    // is the class string (n_args of 'i'/'f') that tur_ffi_thunk_call uses
    // to select the trampoline.
    int64_t   i_vals[kAotMaxArity] = {0};
    double    f_vals[kAotMaxArity] = {0};
    CharString str_owners[kAotMaxArity];
    char      arg_chars[kAotMaxArity + 1] = {0};

    for (uint8_t i = 0; i < e->n_args; i++) {
        arg_chars[i] = aot_type_class(e->arg_types[i]);
        Variant v(p_args[i]);
        switch (e->arg_types[i]) {
            case AotType::Float:
                f_vals[i] = variant_to_f(v);
                break;
            case AotType::Cstr: {
                // Stringify whatever Variant Godot handed us (so a script
                // passing an int to a :cstr param still terminates with a
                // sensible diagnostic rather than a UB cast). The
                // CharString keeps the utf8 bytes alive past the call.
                String s = v.operator String();
                str_owners[i] = s.utf8();
                i_vals[i] = (int64_t)(intptr_t)str_owners[i].get_data();
                break;
            }
            case AotType::Bool:
            case AotType::Int:
            case AotType::Ptr:
            default:
                i_vals[i] = variant_to_i(v, e->arg_types[i]);
                break;
        }
    }
    arg_chars[e->n_args] = '\0';

    int64_t out_i = 0;
    double  out_f = 0.0;
    char ret_class = aot_type_class(e->ret_type);
    int rc = tur_ffi_thunk_call(ret_class, arg_chars, e->n_args, e->fn_ptr,
                                i_vals, f_vals, &out_i, &out_f);
    if (rc != 0) {
        // Shape not in the pre-generated table. Don't fall back -- the
        // interpreter can't recover from this either (the table covers
        // every shape the compiler can emit at the supported arities), so
        // a clean error beats silent reinterpretation.
        if (out_error) {
            out_error->error    = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
            out_error->argument = 0;
            out_error->expected = 0;
        }
        return true;
    }

    if (out_return) {
        *out_return = scalar_to_variant(e->ret_type, out_i, out_f);
    }
    if (out_error) {
        out_error->error    = GDEXTENSION_CALL_OK;
        out_error->argument = 0;
        out_error->expected = 0;
    }
    return true;
}

} // namespace aot
} // namespace godot
