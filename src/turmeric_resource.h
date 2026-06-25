#ifndef TURMERIC_GODOT_TURMERIC_RESOURCE_H
#define TURMERIC_GODOT_TURMERIC_RESOURCE_H

#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot {

// Read .tur files from Godot's resource pipeline. Without this,
// load("res://...tur") fails ERR_FILE_UNRECOGNIZED -- see
// docs/guides/godot-resource-loader-guide.md in the turmeric repo.
class TurmericResourceLoader : public ResourceFormatLoader {
    GDCLASS(TurmericResourceLoader, ResourceFormatLoader)
protected:
    static void _bind_methods() {}
public:
    PackedStringArray _get_recognized_extensions() const override;
    bool _handles_type(const StringName &p_type) const override;
    String _get_resource_type(const String &p_path) const override;
    Variant _load(const String &p_path,
                  const String &p_original_path,
                  bool p_use_sub_threads,
                  int32_t p_cache_mode) const override;
};

// Mirror saver so the editor can write .tur via Save As / Move / refactor.
class TurmericResourceSaver : public ResourceFormatSaver {
    GDCLASS(TurmericResourceSaver, ResourceFormatSaver)
protected:
    static void _bind_methods() {}
public:
    Error _save(const Ref<Resource> &p_resource,
                const String &p_path,
                uint32_t p_flags) override;
    bool _recognize(const Ref<Resource> &p_resource) const override;
    PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
};

} // namespace godot

#endif
