@tool
extends EditorPlugin

# G4 editor wiring -- registers TurmericEditorSyntaxHighlighter with
# the Script Editor so .tur files pick up token coloring inline.
#
# The class itself is provided by the GDExtension at the EDITOR
# initialization level (see register_types.cpp); this addon instantiates
# it and routes it through the standard EditorPlugin lifecycle so the
# registration is paired with proper teardown on disable.

var _hl: EditorSyntaxHighlighter

func _enter_tree() -> void:
	if not ClassDB.class_exists("TurmericEditorSyntaxHighlighter"):
		push_error("[turmeric-godot] TurmericEditorSyntaxHighlighter class not registered; is the GDExtension loaded?")
		return
	_hl = ClassDB.instantiate("TurmericEditorSyntaxHighlighter")
	if _hl == null:
		push_error("[turmeric-godot] failed to instantiate TurmericEditorSyntaxHighlighter")
		return
	var se: ScriptEditor = EditorInterface.get_script_editor()
	if se == null:
		push_error("[turmeric-godot] ScriptEditor unavailable; cannot register highlighter")
		return
	se.register_syntax_highlighter(_hl)
	print("[turmeric-godot] registered TurmericEditorSyntaxHighlighter with ScriptEditor")

func _exit_tree() -> void:
	if _hl == null:
		return
	var se: ScriptEditor = EditorInterface.get_script_editor()
	if se != null:
		se.unregister_syntax_highlighter(_hl)
	_hl = null
	print("[turmeric-godot] unregistered TurmericEditorSyntaxHighlighter")
