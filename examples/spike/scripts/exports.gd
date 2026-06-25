@tool
extends SceneTree

# G2 :exports — drives exports.tur through the Variant API and asserts that
# the inspector round-trip (set / get / default) works end-to-end.

func _fail(msg: String) -> void:
	printerr("[exports.gd] FAIL: ", msg)

func _init() -> void:
	print("[exports.gd] loading res://scripts/exports.tur ...")
	var script = load("res://scripts/exports.tur")
	if script == null:
		_fail("load returned null")
		quit(); return

	var node := Node.new()
	node.set_script(script)
	root.add_child(node)
	await process_frame

	# --- Default values come back via cb_get even without prior cb_set. ---
	var speed_default: float = node.get("speed")
	if not is_equal_approx(speed_default, 200.0):
		_fail("default speed: expected 200.0, got %s" % speed_default)
	var health_default: int = node.get("health")
	if health_default != 100:
		_fail("default health: expected 100, got %s" % health_default)
	var alive_default: bool = node.get("alive")
	if not alive_default:
		_fail("default alive: expected true, got %s" % alive_default)
	print("[exports.gd] defaults: speed=%s health=%s alive=%s" %
		[speed_default, health_default, alive_default])

	# --- Inspector-side write reads back via cb_get. ---
	node.set("speed", 350.0)
	node.set("health", 42)
	node.set("alive", false)
	var speed_after: float = node.get("speed")
	var health_after: int = node.get("health")
	var alive_after: bool = node.get("alive")
	if not is_equal_approx(speed_after, 350.0):
		_fail("after set speed: expected 350.0, got %s" % speed_after)
	if health_after != 42:
		_fail("after set health: expected 42, got %s" % health_after)
	if alive_after != false:
		_fail("after set alive: expected false, got %s" % alive_after)
	print("[exports.gd] after set: speed=%s health=%s alive=%s" %
		[speed_after, health_after, alive_after])

	# --- Script-side reads see what the inspector wrote. ---
	var script_speed: float = node.call("read-speed")
	if not is_equal_approx(script_speed, 350.0):
		_fail("(read-speed) saw %s, expected 350.0" % script_speed)
	var script_health: int = node.call("read-health")
	if script_health != 42:
		_fail("(read-health) saw %s, expected 42" % script_health)
	var script_alive: bool = node.call("read-alive")
	if script_alive != false:
		_fail("(read-alive) saw %s, expected false" % script_alive)

	# --- Script-side write reads back via cb_get. ---
	var bumped: int = node.call("bump-health", 8)
	if bumped != 50:
		_fail("(bump-health 8) returned %s, expected 50" % bumped)
	var health_after_bump: int = node.get("health")
	if health_after_bump != 50:
		_fail("after bump health: expected 50, got %s" % health_after_bump)
	print("[exports.gd] script-side round-trip OK: bumped health to %s" % health_after_bump)

	# --- Property list exposes everything we declared. ---
	var props: Array = node.get_property_list()
	var seen := {}
	for p in props:
		seen[p["name"]] = p["type"]
	for required in ["speed", "health", "alive", "nick"]:
		if not seen.has(required):
			_fail("property '%s' missing from get_property_list()" % required)
	print("[exports.gd] get_property_list() lists: ",
		["speed", "health", "alive", "nick"].map(func(n): return n + "=" + str(seen.get(n, -1))))

	print("[exports.gd] all assertions passed")
	node.queue_free()
	quit()
