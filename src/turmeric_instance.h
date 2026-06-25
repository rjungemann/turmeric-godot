#ifndef TURMERIC_GODOT_TURMERIC_INSTANCE_H
#define TURMERIC_GODOT_TURMERIC_INSTANCE_H

#include <gdextension_interface.h>

#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <unordered_map>
#include <string>

namespace godot {

class Object;
class TurmericScript;

// Per-node-attachment state for a TurmericScript.
//
// Godot's script-instance protocol is a function-table struct, not a class
// hierarchy. Each attachment of a script to a node gets one of these as the
// opaque `instance_data` pointer; the static GDExtensionScriptInstanceInfo3
// table in turmeric_instance.cpp routes Godot callbacks back into here.
struct TurmericInstance {
    Object         *owner  = nullptr;  // borrowed: the Godot Object we're attached to
    TurmericScript *script = nullptr;  // borrowed: the script we belong to

    // G2 :exports — per-instance values for inspector-visible properties.
    // Key is the property name as utf8 std::string (StringName isn't a usable
    // std::unordered_map key out of the box). The script-level default is
    // consulted when a key is absent here, so a freshly-attached node mirrors
    // the values declared by `godot-export` until the user edits them.
    std::unordered_map<std::string, Variant> property_values;

    // G2 :exports — opaque PropertyListBuf*. Owned; freed via
    // cb_free_property_list (paired by Godot with each cb_get_property_list).
    // Defined in turmeric_instance.cpp.
    void *property_list_buf = nullptr;
};

// G2 :exports — TLS pointer to the instance currently in the body of a
// turi_call from cb_call. Read by the `godot-prop-get` / `godot-prop-set`
// natives so script-side property access targets the right node.
extern thread_local TurmericInstance *g_current_instance;

// Build a Godot script instance pointer wrapping a fresh TurmericInstance.
// Returns the opaque pointer Godot expects from Script::_instance_create;
// caller hands it straight back.
GDExtensionScriptInstancePtr turmeric_instance_create(TurmericScript *script, Object *for_object);

} // namespace godot

#endif
