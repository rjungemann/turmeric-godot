// native_abi.cpp -- see native_abi.h.
//
// Two expansions of the same X-macro rows:
//   1. `extern "C"` entry points with default visibility, so a dlopen'd AOT
//      image resolves `godot_vec2` and friends out of this extension.
//   2. TG_NATIVE_ABI[], the table aot_natives.cpp turns into `(extern-c ...)`
//      declarations for the staged project.

#include "native_abi.h"

#include "classdb_proxy.h"
#include "turmeric_language.h"

#include <godot_cpp/variant/utility_functions.hpp>

extern "C" {
#include "turi/value.h"
}

namespace godot {

// The natives that live in turmeric_language.cpp are file-static there; the
// entry points need them, so they are re-declared through a small forwarding
// header rather than being made non-static (keeping the interpreter-facing
// surface unchanged). See turmeric_language.h.

namespace {

// --- Return marshalling -----------------------------------------------------
//
// A native hands back a TuriValue. Compiled code expects a plain scalar. The
// conversions below are deliberately tolerant in the same places the
// interpreter is: an :int-declared native that returns TURI_FLOAT (or vice
// versa) coerces rather than trapping, because the Variant behind it is
// genuinely dynamic and the declared type is the caller's assertion about
// which Godot property/method it is reading.
//
// TURI_ERROR is the one case that must not be silently coerced -- godot-preload
// returns it for a missing resource. Compiled code has nowhere to put an error
// value, so it is surfaced through Godot's error channel and the call yields a
// zero/empty result, matching what the interpreter path prints.

void tg_report_error(const TuriValue &v, const char *who) {
    UtilityFunctions::printerr(
        String("turmeric-godot (AOT): ") + String(who) + String(": ") +
        String(v.as_error ? v.as_error : "<unknown error>"));
}

int64_t tg_ret_I(TuriValue v, const char *who) {
    switch (v.tag) {
        case TURI_INT:   return v.as_int;
        case TURI_BOOL:  return v.as_bool ? 1 : 0;
        case TURI_FLOAT: return (int64_t)v.as_float;
        case TURI_NIL:   return 0;
        case TURI_ERROR: tg_report_error(v, who); return 0;
        default:         return 0;
    }
}

double tg_ret_F(TuriValue v, const char *who) {
    switch (v.tag) {
        case TURI_FLOAT: return v.as_float;
        case TURI_INT:   return (double)v.as_int;
        case TURI_BOOL:  return v.as_bool ? 1.0 : 0.0;
        case TURI_NIL:   return 0.0;
        case TURI_ERROR: tg_report_error(v, who); return 0.0;
        default:         return 0.0;
    }
}

bool tg_ret_B(TuriValue v, const char *who) {
    switch (v.tag) {
        case TURI_BOOL:  return v.as_bool;
        case TURI_INT:   return v.as_int != 0;
        case TURI_FLOAT: return v.as_float != 0.0;
        case TURI_ERROR: tg_report_error(v, who); return false;
        default:         return false;
    }
}

// The cstr a native returns lives in the per-frame string arena, which is
// popped by variant_arena_leave at the end of the enclosing cb_call. That is
// the same lifetime the interpreter path documents, so compiled code inherits
// the same rule: valid for the rest of this method call, copy to outlive it.
const char *tg_ret_C(TuriValue v, const char *who) {
    switch (v.tag) {
        case TURI_CSTR:  return v.as_cstr ? v.as_cstr : "";
        case TURI_ERROR: tg_report_error(v, who); return "";
        default:         return "";
    }
}

void tg_ret_V(TuriValue v, const char *who) {
    if (v.tag == TURI_ERROR) tg_report_error(v, who);
}

} // namespace

} // namespace godot

// --- Code generation --------------------------------------------------------
//
// Entry points are emitted at global scope with C linkage. `godot::` qualifies
// the implementations, which live in the namespace.

#define TG_CT_I int64_t
#define TG_CT_F double
#define TG_CT_B bool
#define TG_CT_C const char *
#define TG_CT_V void

#define TG_MK_I(x) turi_int(x)
#define TG_MK_F(x) turi_float(x)
#define TG_MK_B(x) turi_bool(x)
#define TG_MK_C(x) turi_cstr(x)

#define TG_RET_I(v, who) ::godot::tg_ret_I((v), (who))
#define TG_RET_F(v, who) ::godot::tg_ret_F((v), (who))
#define TG_RET_B(v, who) ::godot::tg_ret_B((v), (who))
#define TG_RET_C(v, who) ::godot::tg_ret_C((v), (who))
#define TG_RET_V(v, who) ::godot::tg_ret_V((v), (who))

// godot-cpp compiles the extension with -fvisibility=hidden. An AOT image
// resolves these by flat-namespace lookup at dlopen time (tur links the staged
// library with -undefined dynamic_lookup on macOS, -shared elsewhere), so they
// have to be visible in the dynamic symbol table.
#if defined(_WIN32)
#  define TG_ABI_EXPORT __declspec(dllexport)
#else
#  define TG_ABI_EXPORT __attribute__((visibility("default")))
#endif

#define TG_GEN0(TUR, CN, IMPL, R)                                                  \
    extern "C" TG_ABI_EXPORT TG_CT_##R CN(void) {                                  \
        TuriValue _a[1];                                                           \
        (void)_a;                                                                  \
        return TG_RET_##R(::godot::IMPL(nullptr, _a, 0, nullptr), TUR);            \
    }

#define TG_GEN1(TUR, CN, IMPL, R, T0, N0)                                          \
    extern "C" TG_ABI_EXPORT TG_CT_##R CN(TG_CT_##T0 a0) {                         \
        TuriValue _a[1] = { TG_MK_##T0(a0) };                                      \
        return TG_RET_##R(::godot::IMPL(nullptr, _a, 1, nullptr), TUR);            \
    }

#define TG_GEN2(TUR, CN, IMPL, R, T0, N0, T1, N1)                                  \
    extern "C" TG_ABI_EXPORT TG_CT_##R CN(TG_CT_##T0 a0, TG_CT_##T1 a1) {          \
        TuriValue _a[2] = { TG_MK_##T0(a0), TG_MK_##T1(a1) };                      \
        return TG_RET_##R(::godot::IMPL(nullptr, _a, 2, nullptr), TUR);            \
    }

#define TG_GEN3(TUR, CN, IMPL, R, T0, N0, T1, N1, T2, N2)                          \
    extern "C" TG_ABI_EXPORT TG_CT_##R CN(TG_CT_##T0 a0, TG_CT_##T1 a1,            \
                                          TG_CT_##T2 a2) {                         \
        TuriValue _a[3] = { TG_MK_##T0(a0), TG_MK_##T1(a1), TG_MK_##T2(a2) };      \
        return TG_RET_##R(::godot::IMPL(nullptr, _a, 3, nullptr), TUR);            \
    }

#define TG_GEN4(TUR, CN, IMPL, R, T0, N0, T1, N1, T2, N2, T3, N3)                  \
    extern "C" TG_ABI_EXPORT TG_CT_##R CN(TG_CT_##T0 a0, TG_CT_##T1 a1,            \
                                          TG_CT_##T2 a2, TG_CT_##T3 a3) {          \
        TuriValue _a[4] = { TG_MK_##T0(a0), TG_MK_##T1(a1),                        \
                            TG_MK_##T2(a2), TG_MK_##T3(a3) };                      \
        return TG_RET_##R(::godot::IMPL(nullptr, _a, 4, nullptr), TUR);            \
    }

TG_NATIVES_0(TG_GEN0)
TG_NATIVES_1(TG_GEN1)
TG_NATIVES_2(TG_GEN2)
TG_NATIVES_3(TG_GEN3)
TG_NATIVES_4(TG_GEN4)

// --- The declaration table --------------------------------------------------

#define TG_TY_I "int"
#define TG_TY_F "float"
#define TG_TY_B "bool"
#define TG_TY_C "cstr"
#define TG_TY_V "void"

#define TG_ROW0(TUR, CN, IMPL, R)                                                  \
    { TUR, #CN, "", TG_TY_##R },
#define TG_ROW1(TUR, CN, IMPL, R, T0, N0)                                          \
    { TUR, #CN, N0 " : " TG_TY_##T0, TG_TY_##R },
#define TG_ROW2(TUR, CN, IMPL, R, T0, N0, T1, N1)                                  \
    { TUR, #CN, N0 " : " TG_TY_##T0 " " N1 " : " TG_TY_##T1, TG_TY_##R },
#define TG_ROW3(TUR, CN, IMPL, R, T0, N0, T1, N1, T2, N2)                          \
    { TUR, #CN, N0 " : " TG_TY_##T0 " " N1 " : " TG_TY_##T1                        \
                " " N2 " : " TG_TY_##T2, TG_TY_##R },
#define TG_ROW4(TUR, CN, IMPL, R, T0, N0, T1, N1, T2, N2, T3, N3)                  \
    { TUR, #CN, N0 " : " TG_TY_##T0 " " N1 " : " TG_TY_##T1                        \
                " " N2 " : " TG_TY_##T2 " " N3 " : " TG_TY_##T3, TG_TY_##R },

namespace godot {

const TgNativeAbi TG_NATIVE_ABI[] = {
    TG_NATIVES_0(TG_ROW0)
    TG_NATIVES_1(TG_ROW1)
    TG_NATIVES_2(TG_ROW2)
    TG_NATIVES_3(TG_ROW3)
    TG_NATIVES_4(TG_ROW4)
};

const size_t TG_NATIVE_ABI_COUNT =
    sizeof(TG_NATIVE_ABI) / sizeof(TG_NATIVE_ABI[0]);

} // namespace godot
