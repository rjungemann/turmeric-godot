@tool
extends SceneTree

# G2 :signals — drives signals.tur and asserts every declared signal
# fires with the right args.

var ready_count := 0
var hit_log: Array = []
var score_log: Array = []

func _fail(msg: String) -> void:
	printerr("[signals.gd] FAIL: ", msg)

func _on_ready_signal() -> void:
	ready_count += 1

func _on_hit(damage: int) -> void:
	hit_log.append(damage)

func _on_scored(by: String, points: int) -> void:
	score_log.append([by, points])

func _init() -> void:
	var script = load("res://scripts/signals.tur")
	if script == null:
		_fail("load returned null"); quit(); return

	# Script-level: signal list reflects godot-signal declarations.
	# Explicit types here (instead of :=) -- script is loaded as
	# Resource so the inferred type is Variant, which Godot 4.3's
	# strict GDScript parser refuses to chain another := against.
	var sigs: Array = script.get_script_signal_list()
	var seen: Dictionary = {}
	for s in sigs:
		seen[s["name"]] = s.get("args", []).size()
	for required in ["ready-signal", "hit", "scored"]:
		if not seen.has(required):
			_fail("signal '%s' missing from get_script_signal_list()" % required)
	if seen.get("ready-signal", -1) != 0:
		_fail("'ready-signal' arity: expected 0, got %s" % seen.get("ready-signal", -1))
	if seen.get("hit", -1) != 1:
		_fail("'hit' arity: expected 1, got %s" % seen.get("hit", -1))
	if seen.get("scored", -1) != 2:
		_fail("'scored' arity: expected 2, got %s" % seen.get("scored", -1))

	# Attach the script. Signals are added automatically by the engine
	# because they're in _get_script_signal_list.
	var node := Node.new()
	node.set_script(script)
	root.add_child(node)

	node.connect("ready-signal", _on_ready_signal)
	node.connect("hit",          _on_hit)
	node.connect("scored",       _on_scored)

	# Wait for _ready to fire (which emits ready-signal).
	await process_frame

	if ready_count != 1:
		_fail("ready-signal fired %s times, expected 1" % ready_count)

	# Drive the other two from GDScript.
	node.call("take-damage", 7)
	node.call("take-damage", 3)
	node.call("score", "alice", 100)

	if hit_log != [7, 3]:
		_fail("hit emissions: expected [7, 3], got %s" % str(hit_log))
	if score_log.size() != 1 or score_log[0][0] != "alice" or score_log[0][1] != 100:
		_fail("scored emission: expected [['alice', 100]], got %s" % str(score_log))

	print("[signals.gd] all assertions passed: ready=%d hits=%s scored=%s" %
		[ready_count, str(hit_log), str(score_log)])

	node.queue_free()
	quit()
