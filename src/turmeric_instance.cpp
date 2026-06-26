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

#include "bridge/variant_marshal.h"

#include "aot/aot_dispatch.h"
#include "aot/aot_image.h"

namespace godot {

thread_local TurmericInstance *g_current_instance = nullptr;

// T1.B -- per-instance AOT cache. Lookup returns nullptr to mean
// "miss, do the slow path"; a cache miss is a (name, export_=nullptr)
// entry, so a method that always falls through to interp (lifecycle
// hooks not in the manifest) doesn't re-scan the exports vector each
// call. cb_call distinguishes "cache says miss" from "not in cache"
// via the StringName -- an empty name slot is uninitialised.
const aot::AotExport *
TurmericInstance::aot_cache_lookup(const aot::AotImage *image,
                                    const StringName &name,
                                    bool *out_cached) const {
    if (out_cached) *out_cached = false;
    if (!image || image != aot_cache_image) return nullptr;
    for (const auto &slot : aot_cache) {
        if (slot.name == name && !slot.name.is_empty()) {
            if (out_cached) *out_cached = true;
            return slot.export_;
        }
    }
    return nullptr;
}

void
TurmericInstance::aot_cache_insert(const aot::AotImage *image,
                                    const StringName &name,
                                    const aot::AotExport *ex) {
    if (!image) return;
    if (image != aot_cache_image) {
        // Whole-cache invalidation -- a script reload swapped the
        // image under us. Clear every slot before re-keying so a stale
        // pointer from the previous image can never escape.
        for (auto &slot : aot_cache) {
            slot.name    = StringName();
            slot.export_ = nullptr;
        }
        aot_cache_image = image;
        aot_cache_head  = 0;
    }
    aot_cache[aot_cache_head & 7] = AotCacheEntry{name, ex};
    aot_cache_head = (uint8_t)((aot_cache_head + 1) & 7);
}

// Primitive Variant <-> TuriValue marshalling lives in bridge/variant_marshal.
// Local aliases keep the existing call sites short.
namespace {
inline TuriValue variant_to_turi(const Variant &v, CharString *owner) {
    return variant_to_turi_primitive(v, owner);
}
inline Variant turi_to_variant(TuriValue v) {
    return turi_to_variant_primitive(v);
}
} // namespace

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

    // Phase A3 + T1.B: when the script has an AotImage bound, route
    // through the pre-generated trampoline table first. The image only
    // carries the user's own defns, so a miss (return false) falls back
    // to the interpreter -- this is how curated natives + lifecycle
    // methods the user did not AOT-export keep working.
    //
    // T1.B adds a per-instance ring cache so the linear scan over the
    // exports vector (find / find_by_name) happens once per (image,
    // method) pair instead of once per call. A cached miss is also
    // stored so a method that consistently isn't in the manifest
    // (lifecycle hooks routed to interp) doesn't keep paying the scan.
    if (self && self->script) {
        const aot::AotImage *image = self->script->get_aot_image();
        if (image) {
            bool cached = false;
            const aot::AotExport *ex =
                self->aot_cache_lookup(image, method, &cached);
            if (!cached) {
                ex = aot::resolve_aot_method(image, method_cs.get_data());
                self->aot_cache_insert(image, method, ex);
            }
            if (ex) {
                Variant aot_ret;
                GDExtensionCallError aot_err{};
                if (aot::dispatch_aot_call_with(image, ex,
                                                 p_args, p_argument_count,
                                                 &aot_ret, &aot_err)) {
                    if (r_return) {
                        internal::gdextension_interface_variant_new_copy(
                            r_return, aot_ret._native_ptr());
                    }
                    if (r_error) *r_error = aot_err;
                    return;
                }
            }
        }
    }

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
    // G3.a — bracket a Variant arena frame around this script-method call so
    // (godot-vec2 ...)/(godot-vec3 ...) handles created during the call are
    // reclaimed on return. Nested calls (script method calling another script
    // method via callv) save/restore via the frame token.
    VariantArenaFrame arena_frame = variant_arena_enter();
    TuriValue ret = turi_call(env, fn, args, n);
    variant_arena_leave(arena_frame);
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

// T4.B starter -- hot reload preserves inspector-edited @export values.
//
// Godot drives the capture/replay dance when Script::_reload(p_keep_state=true)
// fires: BEFORE tearing down the instance, it walks our snapshot via
// cb_get_property_state; AFTER reload it re-creates the instance and replays
// each captured pair via cb_set. The replay path already coerces to the
// declared type (see cb_set above), so a reload that changes an export's
// type drops the stale value cleanly rather than reinterpreting bytes.
//
// Without this hook the snapshot is empty -> Godot has nothing to replay
// -> values reset to defaults. That's the friction the plan calls out.
static void cb_get_property_state(GDExtensionScriptInstanceDataPtr p_instance,
                                  GDExtensionScriptInstancePropertyStateAdd p_add_func,
                                  void *p_userdata) {
    TurmericInstance *self = (TurmericInstance *)p_instance;
    if (!self || !self->script || !p_add_func) return;
    // Only stream values for exports the *current* script declares. If a
    // reload removes an export, capturing its stale value would just be
    // overwritten by a no-op (cb_set rejects unknown names) -- but skipping
    // up front saves the round trip and keeps the log noise down.
    for (const ExportDecl &decl : self->script->get_exports()) {
        CharString name_utf8 = String(decl.name).utf8();
        std::string key(name_utf8.get_data(), name_utf8.length());
        auto it = self->property_values.find(key);
        if (it == self->property_values.end()) continue;
        p_add_func(decl.name._native_ptr(), it->second._native_ptr(), p_userdata);
    }
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
    /* get_property_state_func      */ cb_get_property_state,
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
