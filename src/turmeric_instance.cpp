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
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

extern "C" {
#include "turi/eval.h"
#include "turi/env.h"
#include "turi/value.h"
}

namespace godot {

thread_local TurmericInstance *g_current_instance = nullptr;

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
    return (self && self->script) ? self->script->get_turi_env() : nullptr;
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

    // G2 :exports — make `self` reachable to godot-prop-get / godot-prop-set
    // for the duration of this call. Save/restore so nested calls
    // (script method calling another script method) don't lose the outer.
    TurmericInstance *prev_inst = g_current_instance;
    g_current_instance = self;
    TuriValue ret = turi_call(env, fn, args, n);
    g_current_instance = prev_inst;

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

// G2 :exports — property list backing storage. Each row's `name` field
// points into name_storage (pointer-stable because reserve() is called before
// any push_back). empty_name / empty_string are shared anchors for the
// class_name / hint_string fields.
struct PropertyListBuf {
    StringName  empty_name;
    String      empty_string;
    std::vector<StringName>              name_storage;
    std::vector<GDExtensionPropertyInfo> infos;
};

static const GDExtensionPropertyInfo *cb_get_property_list(
        GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    if (!self || !self->script) {
        if (r_count) *r_count = 0;
        return nullptr;
    }
    const std::vector<ExportDecl> &decls = self->script->get_exports();
    if (decls.empty()) {
        if (r_count) *r_count = 0;
        return nullptr;
    }
    // If a prior list is outstanding (Godot didn't call free yet), drop it.
    // Godot's protocol pairs the two; this is defensive.
    if (self->property_list_buf) {
        delete (PropertyListBuf *)self->property_list_buf;
        self->property_list_buf = nullptr;
    }
    PropertyListBuf *buf = new PropertyListBuf();
    buf->name_storage.reserve(decls.size());
    buf->infos.reserve(decls.size());
    for (const auto &d : decls) {
        buf->name_storage.push_back(d.name);
        GDExtensionPropertyInfo info{};
        info.type        = (GDExtensionVariantType)d.type;
        info.name        = (GDExtensionStringNamePtr)&buf->name_storage.back();
        info.class_name  = (GDExtensionStringNamePtr)&buf->empty_name;
        info.hint        = 0;       // PROPERTY_HINT_NONE
        info.hint_string = (GDExtensionStringPtr)&buf->empty_string;
        info.usage       = 7;       // PROPERTY_USAGE_DEFAULT (storage | editor | network)
        buf->infos.push_back(info);
    }
    self->property_list_buf = buf;
    if (r_count) *r_count = (uint32_t)buf->infos.size();
    return buf->infos.data();
}

static void cb_free_property_list(GDExtensionScriptInstanceDataPtr p_instance,
                                  const GDExtensionPropertyInfo * /*p_list*/,
                                  uint32_t /*p_count*/) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    if (!self || !self->property_list_buf) return;
    delete (PropertyListBuf *)self->property_list_buf;
    self->property_list_buf = nullptr;
}

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

// G2 :exports — name lookup helper. Returns the script's ExportDecl (and its
// utf8 key) for the given Godot StringName, or nullptr if the property was
// not declared via godot-export.
static const ExportDecl *find_export_for(TurmericInstance *self,
                                         const StringName &name,
                                         std::string *out_key) {
    if (!self || !self->script) return nullptr;
    const ExportDecl *d = self->script->find_export(name);
    if (!d) return nullptr;
    if (out_key) {
        CharString cs = String(name).utf8();
        out_key->assign(cs.get_data(), cs.length());
    }
    return d;
}

static GDExtensionBool cb_set(GDExtensionScriptInstanceDataPtr p_instance,
                              GDExtensionConstStringNamePtr p_name,
                              GDExtensionConstVariantPtr p_value) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    StringName name = *reinterpret_cast<const StringName *>(p_name);
    std::string key;
    const ExportDecl *d = find_export_for(self, name, &key);
    if (!d) return 0;
    Variant v(p_value);
    // Coerce numeric/bool variants to the declared type so the script always
    // sees the type it asked for; mismatches that can't coerce stay nil.
    if (v.get_type() != d->type) {
        switch (d->type) {
            case Variant::FLOAT:  v = (double)v;   break;
            case Variant::INT:    v = (int64_t)v;  break;
            case Variant::BOOL:   v = (bool)v;     break;
            case Variant::STRING: v = String(v);   break;
            default: break;
        }
    }
    self->property_values[key] = v;
    return 1;
}

static GDExtensionBool cb_get(GDExtensionScriptInstanceDataPtr p_instance,
                              GDExtensionConstStringNamePtr p_name,
                              GDExtensionVariantPtr r_ret) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    StringName name = *reinterpret_cast<const StringName *>(p_name);
    std::string key;
    const ExportDecl *d = find_export_for(self, name, &key);
    if (!d) return 0;
    auto it = self->property_values.find(key);
    Variant out = (it != self->property_values.end()) ? it->second : d->default_value;
    if (r_ret) {
        internal::gdextension_interface_variant_new_copy(r_ret, out._native_ptr());
    }
    return 1;
}

static GDExtensionVariantType cb_get_property_type(
        GDExtensionScriptInstanceDataPtr p_instance,
        GDExtensionConstStringNamePtr p_name,
        GDExtensionBool *r_is_valid) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    StringName name = *reinterpret_cast<const StringName *>(p_name);
    const ExportDecl *d = find_export_for(self, name, nullptr);
    if (!d) {
        if (r_is_valid) *r_is_valid = 0;
        return GDEXTENSION_VARIANT_TYPE_NIL;
    }
    if (r_is_valid) *r_is_valid = 1;
    return (GDExtensionVariantType)d->type;
}

static void cb_free(GDExtensionScriptInstanceDataPtr p_instance) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    if (self && self->property_list_buf) {
        delete (PropertyListBuf *)self->property_list_buf;
        self->property_list_buf = nullptr;
    }
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
