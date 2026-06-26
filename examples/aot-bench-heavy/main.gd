@tool
extends SceneTree

# aot-bench-heavy/main.gd -- T1.C heavy-body driver. Mirrors
# examples/aot-bench/main.gd; the only differences are the lower
# iteration count (each hot() call does 50 recursive closure walks
# now, so 100k iters ~= 5M frames -- enough for steady-state without
# multi-second wall time) and the accumulator expectation (hot()
# returns x + 50, not x + 1, so the final acc is ITERATIONS*50).

const ITERATIONS = 100_000

func _init() -> void:
	var script := load("res://bench.tur")
	if script == null:
		push_error("aot-bench-heavy: failed to load bench.tur")
		quit(1)
		return

	var holder := Node.new()
	root.add_child(holder)
	holder.set_script(script)
	await process_frame

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
	print("  AOT BENCH HEAVY START  --  GDScript reports mode=%s" % mode)
	print("--------------------------------------------------------")
	print("  Each hot() call walks 50 recursive closure frames via")
	print("  step(50). The 50-frame body dominates the per-call cost,")
	print("  so the AOT-vs-interp delta should be visible above the")
	print("  ~250 ns Godot Variant fixed cost the original aot-bench")
	print("  was pinned by.")
	print("  Look for these lines above to confirm AOT actually fired:")
	print("    [turmeric res://bench.tur AOT] loaded N exports ...")
	print("    [turmeric-godot AOT] first dispatch routed via AOT: ...")
	print("========================================================")
	print("")

	for i in 100:
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
	print("  AOT BENCH HEAVY RESULT")
	print("--------------------------------------------------------")
	print("  Reported mode (GDScript-side):     %s" % mode)
	print("  Iterations:                        %d" % ITERATIONS)
	print("  Accumulator (expected iters*50):   %d" % acc)
	print("  Wall time:                         %d us" % dur_us)
	print("  Per call (50 nested frames each):  %.1f ns" % ns_per_call)
	print("  Per inner frame:                   %.1f ns" % (ns_per_call / 50.0))
	print("========================================================")
	print("")

	quit(0)
