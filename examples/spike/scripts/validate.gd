@tool
extends SceneTree

# G3.d -- exercises TurmericLanguage._validate via the ScriptLanguage singleton.
# Validates two sources: one clean, one with a deliberate elaboration error,
# and checks the returned Dictionary shape (valid / errors / warnings).

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[validate.gd] FAIL: ", msg)

func _find_lang() -> ScriptLanguage:
	# ScriptLanguageExtension's "get_X" surface is implemented as virtual
	# methods (_get_extension etc.) so they're not directly callable from
	# GDScript on the parent class. Probe via has_method against either
	# spelling, then call.
	for i in range(Engine.get_script_language_count()):
		var lang := Engine.get_script_language(i)
		if lang == null:
			continue
		if lang.get_class() == "TurmericLanguage":
			return lang
	return null

func _init() -> void:
	var lang := _find_lang()
	if lang == null:
		_fail("Turmeric script language not registered"); quit(); return

	# --- 1. Clean source ----------------------------------------------------
	var clean := "(defn add1 [x : int] : int (+ x 1))\n"
	var clean_result: Dictionary = lang.call("validate_source", clean,
		"res://_validate_clean.tur")
	if not clean_result.get("valid", false):
		_fail("clean source reported invalid; errors=%s" % str(clean_result.get("errors", [])))
	if clean_result.get("errors", []).size() != 0:
		_fail("clean source reported %d errors; expected 0" % clean_result["errors"].size())

	# --- 2. Broken source (elaboration error: bool-required if on int) ------
	var broken := "(defn bad [] (if 7 1 2))\n"
	var broken_result: Dictionary = lang.call("validate_source", broken,
		"res://_validate_broken.tur")
	if broken_result.get("valid", true):
		_fail("broken source reported valid; expected invalid")
	var errs: Array = broken_result.get("errors", [])
	if errs.size() == 0:
		_fail("broken source had no errors")
	else:
		var e: Dictionary = errs[0]
		# Spot-check the dict shape; we don't pin the exact line/column since
		# the elaborator is the authority and may shift it as it improves.
		for key in ["path", "line", "column", "message"]:
			if not e.has(key):
				_fail("error[0] missing key: %s" % key)
		if String(e.get("message", "")).find("bool") == -1:
			_fail("error[0] message didn't mention 'bool': %s" % str(e.get("message", "")))

	if fail_count == 0:
		print("[validate.gd] all assertions passed: clean=valid broken='%s'" %
			str(errs[0].get("message", "")))
	else:
		printerr("[validate.gd] %d FAILURES" % fail_count)

	quit(0 if fail_count == 0 else 1)
