@tool
extends SceneTree

# G4.2 -- exercise TurmericLanguage.complete_code_for_test.

var fail_count := 0

func _fail(msg: String) -> void:
	fail_count += 1
	printerr("[completion.gd] FAIL: ", msg)

func _find_lang() -> Object:
	for i in range(Engine.get_script_language_count()):
		var lang := Engine.get_script_language(i)
		if lang and lang.get_class() == "TurmericLanguage":
			return lang
	return null

func _option_names(result: Dictionary) -> Array:
	var out: Array = []
	for o in result.get("options", []):
		out.append(String((o as Dictionary).get("display", "")))
	return out

func _init() -> void:
	var lang: Object = _find_lang()
	if lang == null:
		_fail("Turmeric script language not registered"); quit(); return

	# 1. No prefix -- returns at least the prelude + generated defs.
	var all: Dictionary = lang.call("complete_code_for_test",
		"(defn foo [] 1)\n", "res://t.tur")
	var names := _option_names(all)
	if not names.has("foo"):
		_fail("expected user defn 'foo' in completion list")
	if not names.has("node/set-position"):
		_fail("expected prelude 'node/set-position' in completion list")
	if not names.has("node2d/set-skew"):
		_fail("expected generated 'node2d/set-skew' in completion list")

	# 2. Prefix filter: "node2d/set-" yields only setters on Node2D.
	var pf: Dictionary = lang.call("complete_code_for_test",
		"(node2d/set-", "res://t.tur")
	var pf_names := _option_names(pf)
	if pf_names.is_empty():
		_fail("prefix 'node2d/set-' returned no matches")
	for n in pf_names:
		if not String(n).begins_with("node2d/set-"):
			_fail("prefix filter let through: %s" % n)

	# 3. User defn shadows prelude in dedup order (user wins). Confirm
	#    only one entry per name even when both define it.
	var dup: Dictionary = lang.call("complete_code_for_test",
		"(defn node/self [] 0)\n(node/", "res://t.tur")
	var dup_names := _option_names(dup)
	var self_count := 0
	for n in dup_names:
		if n == "node/self": self_count += 1
	if self_count != 1:
		_fail("expected node/self exactly once in dedup; got %d" % self_count)

	if fail_count == 0:
		print("[completion.gd] all assertions passed; %d total names, %d filtered" %
			[names.size(), pf_names.size()])
	else:
		printerr("[completion.gd] %d FAILURES" % fail_count)

	quit(0 if fail_count == 0 else 1)
