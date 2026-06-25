#ifndef TURMERIC_GODOT_TURMERIC_INSTANCE_H
#define TURMERIC_GODOT_TURMERIC_INSTANCE_H

#include <gdextension_interface.h>

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
};

// Build a Godot script instance pointer wrapping a fresh TurmericInstance.
// Returns the opaque pointer Godot expects from Script::_instance_create;
// caller hands it straight back.
GDExtensionScriptInstancePtr turmeric_instance_create(TurmericScript *script, Object *for_object);

} // namespace godot

#endif
