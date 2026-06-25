#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>

#include <cstdio>

#include "turmeric_language.h"
#include "turmeric_script.h"

using namespace godot;

static TurmericLanguage *turmeric_language_singleton = nullptr;

void initialize_turmeric_godot_module(ModuleInitializationLevel p_level) {
    std::fprintf(stdout, "[turmeric-godot] initialize(level=%d)\n", (int)p_level);
    std::fflush(stdout);

    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    GDREGISTER_CLASS(TurmericLanguage);
    GDREGISTER_CLASS(TurmericScript);

    turmeric_language_singleton = memnew(TurmericLanguage);
    Engine::get_singleton()->register_script_language(turmeric_language_singleton);

    std::fprintf(stdout, "[turmeric-godot] registered Turmeric script language\n");
    std::fflush(stdout);
}

void uninitialize_turmeric_godot_module(ModuleInitializationLevel p_level) {
    std::fprintf(stdout, "[turmeric-godot] uninitialize(level=%d)\n", (int)p_level);
    std::fflush(stdout);

    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    if (turmeric_language_singleton) {
        Engine::get_singleton()->unregister_script_language(turmeric_language_singleton);
        memdelete(turmeric_language_singleton);
        turmeric_language_singleton = nullptr;
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT turmeric_godot_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_turmeric_godot_module);
    init_obj.register_terminator(uninitialize_turmeric_godot_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
