#include "turmeric_resource.h"
#include "turmeric_script.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// --- Loader ----------------------------------------------------------------

PackedStringArray TurmericResourceLoader::_get_recognized_extensions() const {
    PackedStringArray a;
    a.push_back("tur");
    return a;
}

bool TurmericResourceLoader::_handles_type(const StringName &p_type) const {
    return p_type == StringName("Script") ||
           p_type == StringName("TurmericScript");
}

String TurmericResourceLoader::_get_resource_type(const String &p_path) const {
    if (p_path.get_extension().to_lower() == "tur") {
        return String("TurmericScript");
    }
    return String();
}

Variant TurmericResourceLoader::_load(const String &p_path,
                                       const String &p_original_path,
                                       bool /*p_use_sub_threads*/,
                                       int32_t /*p_cache_mode*/) const {
    (void)p_original_path;
    Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
    if (f.is_null()) {
        UtilityFunctions::printerr(String("turmeric-godot: cannot open ") + p_path);
        return Variant();
    }
    String src = f->get_as_text();
    Ref<TurmericScript> script;
    script.instantiate();
    script->set_path(p_path);
    script->set_source_code(src);
    Error err = script->reload();
    if (err != OK) {
        return Variant();
    }
    return script;
}

// --- Saver -----------------------------------------------------------------

Error TurmericResourceSaver::_save(const Ref<Resource> &p_resource,
                                    const String &p_path,
                                    uint32_t /*p_flags*/) {
    Ref<TurmericScript> script = p_resource;
    if (script.is_null()) return ERR_INVALID_PARAMETER;
    Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
    if (f.is_null()) return ERR_CANT_OPEN;
    f->store_string(script->get_source_code());
    return OK;
}

bool TurmericResourceSaver::_recognize(const Ref<Resource> &p_resource) const {
    return Ref<TurmericScript>(p_resource).is_valid();
}

PackedStringArray TurmericResourceSaver::_get_recognized_extensions(
        const Ref<Resource> &p_resource) const {
    PackedStringArray a;
    if (Ref<TurmericScript>(p_resource).is_valid()) {
        a.push_back("tur");
    }
    return a;
}

} // namespace godot
