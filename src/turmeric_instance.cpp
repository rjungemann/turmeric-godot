#include "turmeric_instance.h"
#include "turmeric_language.h"
#include "turmeric_script.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdio>
#include <cstring>
#include <new>

extern "C" {
#include "turi/eval.h"
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

// --- Variant ↔ TuriValue marshalling ----------------------------------------
// G1 scope: primitives only (int, float, bool, String + nil). Everything else
// is best-effort (gets coerced to nil or stringified) and will be fleshed out
// in G2 alongside the per-script env work.

static TuriValue variant_to_turi(const Variant &v, CharString *str_owner_out) {
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
            // No owner provided: return nil rather than a dangling cstr.
            return turi_nil();
        }
        default:
            return turi_nil();
    }
}

static Variant turi_to_variant(TuriValue v) {
    switch (v.tag) {
        case TURI_NIL:   return Variant();
        case TURI_BOOL:  return Variant((bool)v.as_bool);
        case TURI_INT:   return Variant((int64_t)v.as_int);
        case TURI_FLOAT: return Variant((double)v.as_float);
        case TURI_CSTR:  return Variant(String(v.as_cstr ? v.as_cstr : ""));
        case TURI_ERROR: {
            UtilityFunctions::printerr(
                String("turmeric-godot: Turmeric returned error: ") +
                String(v.as_error ? v.as_error : "<unknown>"));
            return Variant();
        }
        default:
            return Variant();
    }
}

// --- Dispatch helpers --------------------------------------------------------

static TuriEnv *instance_env(TurmericInstance *self) {
    (void)self;
    // G1: one shared env across all scripts. See
    // turmeric/docs/reported/libturi-per-embed-env-and-peripherals.md
    // for the per-script-env story this is deferring.
    TurmericLanguage *lang = TurmericLanguage::singleton();
    return lang ? lang->get_turi_env() : nullptr;
}

static TuriValue lookup_method(TurmericInstance *self, const char *name) {
    TuriEnv *env = instance_env(self);
    if (!env || !name) return turi_nil();
    TuriValue v = turi_env_get(env, name);
    return (v.tag == TURI_CLOSURE) ? v : turi_nil();
}

// --- ScriptInstanceInfo3 callbacks ------------------------------------------

static GDExtensionObjectPtr cb_get_owner(GDExtensionScriptInstanceDataPtr p_instance) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    return self->owner ? self->owner->_owner : nullptr;
}

static GDExtensionObjectPtr cb_get_script(GDExtensionScriptInstanceDataPtr p_instance) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    return self->script ? self->script->_owner : nullptr;
}

static GDExtensionScriptLanguagePtr cb_get_language(GDExtensionScriptInstanceDataPtr p_instance) {
    (void)p_instance;
    TurmericLanguage *lang = TurmericLanguage::singleton();
    return lang ? lang->_owner : nullptr;
}

static GDExtensionBool cb_is_placeholder(GDExtensionScriptInstanceDataPtr p_instance) {
    (void)p_instance;
    return 0;
}

static GDExtensionBool cb_has_method(GDExtensionScriptInstanceDataPtr p_instance,
                                     GDExtensionConstStringNamePtr p_name) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    StringName name = *reinterpret_cast<const StringName *>(p_name);
    String s = name;
    CharString cs = s.utf8();
    TuriValue v = lookup_method(self, cs.get_data());
    return v.tag == TURI_CLOSURE ? 1 : 0;
}

static void cb_call(GDExtensionScriptInstanceDataPtr p_self,
                    GDExtensionConstStringNamePtr p_method,
                    const GDExtensionConstVariantPtr *p_args,
                    GDExtensionInt p_argument_count,
                    GDExtensionVariantPtr r_return,
                    GDExtensionCallError *r_error) {
    TurmericInstance *self = (TurmericInstance *)p_self;
    StringName method = *reinterpret_cast<const StringName *>(p_method);
    String method_s = method;
    CharString method_cs = method_s.utf8();

    TuriValue fn = lookup_method(self, method_cs.get_data());
    if (fn.tag != TURI_CLOSURE) {
        if (r_error) {
            r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
            r_error->argument = 0;
            r_error->expected = 0;
        }
        return;
    }

    TuriEnv *env = instance_env(self);
    if (!env) {
        if (r_error) r_error->error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
        return;
    }

    // Marshal args. Strings need their utf8 byte buffer to live for the
    // duration of the turi_call, hence the parallel owner array.
    const uint32_t n = (uint32_t)p_argument_count;
    TuriValue   args_buf[8];
    CharString  str_owners[8];
    TuriValue  *args = nullptr;
    if (n > 0) {
        if (n > 8) {
            UtilityFunctions::printerr(
                "turmeric-godot: cb_call: more than 8 args not yet supported");
            if (r_error) r_error->error = GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS;
            return;
        }
        for (uint32_t i = 0; i < n; i++) {
            Variant vi(p_args[i]);
            args_buf[i] = variant_to_turi(vi, &str_owners[i]);
        }
        args = args_buf;
    }

    TuriValue ret = turi_call(env, fn, args, n);

    if (r_return) {
        Variant rv = turi_to_variant(ret);
        internal::gdextension_interface_variant_new_copy(r_return, rv._native_ptr());
    }
    if (r_error) {
        r_error->error = GDEXTENSION_CALL_OK;
        r_error->argument = 0;
        r_error->expected = 0;
    }
}

static void cb_notification(GDExtensionScriptInstanceDataPtr /*p_instance*/,
                            int32_t /*p_what*/,
                            GDExtensionBool /*p_reversed*/) {
    // Intentionally empty: Godot routes lifecycle hooks (_ready, _process, ...)
    // through cb_call as method invocations on the script instance. The
    // notification stream is the engine's mechanism for *its own* baseclasses
    // (Node, etc.); having both fire would dispatch _ready twice.
}

static const GDExtensionPropertyInfo *cb_get_property_list(
        GDExtensionScriptInstanceDataPtr, uint32_t *r_count) {
    if (r_count) *r_count = 0;
    return nullptr;
}

static void cb_free_property_list(
        GDExtensionScriptInstanceDataPtr, const GDExtensionPropertyInfo *, uint32_t) {}

static const GDExtensionMethodInfo *cb_get_method_list(
        GDExtensionScriptInstanceDataPtr, uint32_t *r_count) {
    if (r_count) *r_count = 0;
    return nullptr;
}

static void cb_free_method_list(
        GDExtensionScriptInstanceDataPtr, const GDExtensionMethodInfo *, uint32_t) {}

static GDExtensionInt cb_get_method_argument_count(
        GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr,
        GDExtensionBool *r_is_valid) {
    if (r_is_valid) *r_is_valid = 0;
    return 0;
}

static GDExtensionBool cb_set(GDExtensionScriptInstanceDataPtr,
                              GDExtensionConstStringNamePtr,
                              GDExtensionConstVariantPtr) { return 0; }
static GDExtensionBool cb_get(GDExtensionScriptInstanceDataPtr,
                              GDExtensionConstStringNamePtr,
                              GDExtensionVariantPtr) { return 0; }
static GDExtensionVariantType cb_get_property_type(
        GDExtensionScriptInstanceDataPtr, GDExtensionConstStringNamePtr,
        GDExtensionBool *r_is_valid) {
    if (r_is_valid) *r_is_valid = 0;
    return GDEXTENSION_VARIANT_TYPE_NIL;
}

static void cb_free(GDExtensionScriptInstanceDataPtr p_instance) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    delete self;
}

// --- The static function table ----------------------------------------------

static const GDExtensionScriptInstanceInfo3 g_instance_info = {
    /* set_func                     */ cb_set,
    /* get_func                     */ cb_get,
    /* get_property_list_func       */ cb_get_property_list,
    /* free_property_list_func      */ cb_free_property_list,
    /* get_class_category_func      */ nullptr,
    /* property_can_revert_func     */ nullptr,
    /* property_get_revert_func     */ nullptr,
    /* get_owner_func               */ cb_get_owner,
    /* get_property_state_func      */ nullptr,
    /* get_method_list_func         */ cb_get_method_list,
    /* free_method_list_func        */ cb_free_method_list,
    /* get_property_type_func       */ cb_get_property_type,
    /* validate_property_func       */ nullptr,
    /* has_method_func              */ cb_has_method,
    /* get_method_argument_count_func */ cb_get_method_argument_count,
    /* call_func                    */ cb_call,
    /* notification_func            */ cb_notification,
    /* to_string_func               */ nullptr,
    /* refcount_incremented_func    */ nullptr,
    /* refcount_decremented_func    */ nullptr,
    /* get_script_func              */ cb_get_script,
    /* is_placeholder_func          */ cb_is_placeholder,
    /* set_fallback_func            */ nullptr,
    /* get_fallback_func            */ nullptr,
    /* get_language_func            */ cb_get_language,
    /* free_func                    */ cb_free,
};

// --- Factory ----------------------------------------------------------------

GDExtensionScriptInstancePtr turmeric_instance_create(TurmericScript *script, Object *for_object) {
    auto *data = new TurmericInstance{};
    data->owner  = for_object;
    data->script = script;
    return internal::gdextension_interface_script_instance_create3(&g_instance_info, data);
}

} // namespace godot
