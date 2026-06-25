#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>

#include <cstdio>

#include "turmeric_language.h"
#include "turmeric_resource.h"
#include "turmeric_script.h"

using namespace godot;

static TurmericLanguage *turmeric_language_singleton = nullptr;
static Ref<TurmericResourceLoader> s_loader;
static Ref<TurmericResourceSaver>  s_saver;

void initialize_turmeric_godot_module(ModuleInitializationLevel p_level) {
    std::fprintf(stdout, "[turmeric-godot] initialize(level=%d)\n", (int)p_level);
    std::fflush(stdout);

    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    GDREGISTER_CLASS(TurmericLanguage);
    GDREGISTER_CLASS(TurmericScript);
    GDREGISTER_CLASS(TurmericResourceLoader);
    GDREGISTER_CLASS(TurmericResourceSaver);

    turmeric_language_singleton = memnew(TurmericLanguage);
    Engine::get_singleton()->register_script_language(turmeric_language_singleton);

    s_loader.instantiate();
    s_saver.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(s_loader);
    ResourceSaver::get_singleton()->add_resource_format_saver(s_saver);

    std::fprintf(stdout, "[turmeric-godot] registered Turmeric script language + resource format\n");
    std::fflush(stdout);
}

void uninitialize_turmeric_godot_module(ModuleInitializationLevel p_level) {
    std::fprintf(stdout, "[turmeric-godot] uninitialize(level=%d)\n", (int)p_level);
    std::fflush(stdout);

    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    if (s_loader.is_valid()) {
        ResourceLoader::get_singleton()->remove_resource_format_loader(s_loader);
        s_loader.unref();
    }
    if (s_saver.is_valid()) {
        ResourceSaver::get_singleton()->remove_resource_format_saver(s_saver);
        s_saver.unref();
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
