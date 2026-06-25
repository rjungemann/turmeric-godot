@tool
extends SceneTree

# Tiny driver that loads hello.tur through Godot's resource pipeline.
# That triggers TurmericLanguage to vend a TurmericScript, whose
# _reload() invokes libturi to evaluate the source.

func _init() -> void:
	print("[driver] instantiating TurmericScript directly ...")
	var s = ClassDB.instantiate("TurmericScript")
	if s == null:
		printerr("[driver] ClassDB.instantiate returned null")
		quit()
		return
	print("[driver] got: ", s)
	s.source_code = "(godot-println \"hello from a TurmericScript built in GDScript\")"
	var err: int = s.reload()
	print("[driver] reload returned: ", err)
	quit()
