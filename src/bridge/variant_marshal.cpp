#include "variant_marshal.h"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <string>
#include <deque>
#include <vector>

namespace godot {

// --- Primitive marshalling --------------------------------------------------

TuriValue variant_to_turi_primitive(const Variant &v, CharString *str_owner_out) {
    switch (v.get_type()) {
        case Variant::NIL:
            return turi_nil();
        case Variant::BOOL:
            return turi_bool((bool)v);
        case Variant::INT:
            return turi_int((int64_t)v);
        case Variant::FLOAT:
            return turi_float((double)v);
        case Variant::STRING: {
            String s = v;
            CharString cs = s.utf8();
            if (str_owner_out) {
                *str_owner_out = cs;
                return turi_cstr(str_owner_out->get_data());
            }
            return turi_nil();
        }
        default:
            return turi_nil();
    }
}

Variant turi_to_variant_primitive(TuriValue v) {
    switch (v.tag) {
        case TURI_NIL:   return Variant();
        case TURI_BOOL:  return Variant((bool)v.as_bool);
        case TURI_INT:   return Variant((int64_t)v.as_int);
        case TURI_FLOAT: return Variant((double)v.as_float);
        case TURI_CSTR:  return Variant(String(v.as_cstr ? v.as_cstr : ""));
        case TURI_ERROR:
            UtilityFunctions::printerr(
                String("turmeric-godot: Turmeric returned error: ") +
                String(v.as_error ? v.as_error : "<unknown>"));
            return Variant();
        default:
            return Variant();
    }
}

// --- Variant arena ----------------------------------------------------------

namespace {
thread_local std::vector<Variant>     g_arena;
// std::deque (not std::vector) so that the c_str() pointers returned
// by string_arena_push() remain valid across subsequent pushes in the
// same cb_call frame. Vector's reallocation on growth would dangle any
// earlier-returned cstr -- the defgodot-script demo hit exactly this
// hazard with two prop-get-c reads + a num->str in flight. Deque's
// segmented storage gives stable element addresses.
thread_local std::deque<std::string>  g_str_arena;
} // namespace

int64_t variant_arena_push(const Variant &v) {
    const size_t idx = g_arena.size();
    g_arena.push_back(v);
    return (int64_t)((uint64_t)idx | TG_ARENA_TAG);
}

const Variant *variant_arena_lookup(int64_t tagged_handle) {
    if (!variant_arena_is_handle(tagged_handle)) return nullptr;
    const uint64_t idx = ((uint64_t)tagged_handle) & ~TG_ARENA_TAG;
    if (idx >= g_arena.size()) return nullptr;
    return &g_arena[(size_t)idx];
}

VariantArenaFrame variant_arena_enter() {
    return VariantArenaFrame{g_arena.size(), g_str_arena.size()};
}

void variant_arena_leave(VariantArenaFrame frame) {
    if (frame.saved_variant_depth <= g_arena.size())
        g_arena.resize(frame.saved_variant_depth);
    if (frame.saved_string_depth <= g_str_arena.size())
        g_str_arena.resize(frame.saved_string_depth);
}

const char *string_arena_push(const char *utf8, size_t len) {
    g_str_arena.emplace_back(utf8 ? utf8 : "", utf8 ? len : 0);
    return g_str_arena.back().c_str();
}

} // namespace godot
