@tool
extends SceneTree

# aot-bench/main.gd -- run as a SceneTree script (matches the spike example's
# headless harness pattern). No main scene needed. `@tool` is required so the
# extension's ScriptLanguage initialises before this script loads `res://bench.tur`.
#
# Loads bench.tur, attaches it to a fresh Node, runs hot(x) in a tight loop,
# prints (mode, iterations, wall_time_us, ns_per_call), and quits.
#
# Drive two runs and diff:
#
#   /Applications/Godot.app/Contents/MacOS/Godot --headless --path . --script res://main.gd
#   TURMERIC_GODOT_AOT=1 ... --script res://main.gd
#
# The "mode" line in the output names whichever path took the call, so a
# stale env var can't masquerade as the wrong mode.

const ITERATIONS = 1_000_000

func _init() -> void:
	var script := load("res://bench.tur")
	if script == null:
		push_error("aot-bench: failed to load bench.tur")
		quit(1)
		return

	# A throwaway Node owned by the SceneTree root; needed because TurmericScript
	# only dispatches lifecycle / cb_call methods through an attached instance.
	var holder := Node.new()
	root.add_child(holder)
	holder.set_script(script)
	# One frame for the engine to bind the instance + fire _ready.
	await process_frame

	# Resolve mode for the print line below. The C++ side uses the same
	# precedence (env > #mode > project setting) so this mirrors what
	# turmeric-godot will actually do.
	var mode := "interpreter"
	if OS.has_environment("TURMERIC_GODOT_AOT"):
		var ev = OS.get_environment("TURMERIC_GODOT_AOT").to_lower()
		if ev in ["1", "true", "aot"]:
			mode = "aot"
		elif ev in ["0", "false", "interpreter", "interp"]:
			mode = "interpreter"
	elif ProjectSettings.has_setting("turmeric/execution_mode"):
		var ps_mode = String(ProjectSettings.get_setting("turmeric/execution_mode")).to_lower()
		if ps_mode == "aot":
			mode = "aot"

	print("")
	print("========================================================")
	print("  AOT BENCH START  --  GDScript reports mode=%s" % mode)
	print("--------------------------------------------------------")
	print("  What to look for above this line:")
	print("    [turmeric res://bench.tur AOT] loaded N exports ...")
	print("        ^ printed exactly when AOT actually built/loaded.")
	print("    [turmeric-godot AOT] first dispatch routed via AOT: ...")
	print("        ^ printed at the first hot()/etc call routed via AOT.")
	print("  Absence of BOTH lines = the run was pure interpreter,")
	print("  regardless of what `mode=%s` reports here." % mode)
	print("========================================================")
	print("")

	# Warm the call site (first call pays JIT-style bookkeeping for the
	# Variant call path; we want steady-state throughput).
	for i in 1000:
		holder.call("hot", i)

	var t0 := Time.get_ticks_usec()
	var acc := 0
	for i in ITERATIONS:
		acc = holder.call("hot", acc)
	var t1 := Time.get_ticks_usec()

	var dur_us := t1 - t0
	var ns_per_call := float(dur_us) * 1000.0 / float(ITERATIONS)

	print("")
	print("========================================================")
	print("  AOT BENCH RESULT")
	print("--------------------------------------------------------")
	print("  Reported mode (GDScript-side):   %s" % mode)
	print("  Iterations:                      %d" % ITERATIONS)
	print("  Accumulator (acc=iters expected): %d" % acc)
	print("  Wall time:                       %d us" % dur_us)
	print("  Per call:                        %.1f ns" % ns_per_call)
	print("========================================================")
	print("")

	quit(0)
