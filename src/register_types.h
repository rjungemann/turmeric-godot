#ifndef TURMERIC_GODOT_REGISTER_TYPES_H
#define TURMERIC_GODOT_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_turmeric_godot_module(ModuleInitializationLevel p_level);
void uninitialize_turmeric_godot_module(ModuleInitializationLevel p_level);

#endif // TURMERIC_GODOT_REGISTER_TYPES_H
