# aot-bench-heavy

The throughput-demonstrating sibling of [`examples/aot-bench/`](../aot-bench).

`aot-bench/` is a wiring smoke test -- it proves AOT loads, dispatches,
and caches, but the body (`(+ x 1)`) is so cheap that Godot's
~250 ns Variant per-call infrastructure pins both modes to roughly the
same wall-clock. This bench replaces the body with `(step 50)`, a
recursive closure walk over 50 frames. The 50-frame body cost
dominates the Variant fixed cost, so the AOT-vs-interp delta surfaces.

## Build + first import

```sh
cd ../turmeric-godot
scons platform=macos arch=arm64 target=template_debug

# One-time project import.
/Applications/Godot.app/Contents/MacOS/Godot --headless \
    --path examples/aot-bench-heavy --editor --quit-after 2
```

## Run

```sh
# Interpreter baseline.
/Applications/Godot.app/Contents/MacOS/Godot --headless \
    --path examples/aot-bench-heavy --script res://main.gd

# AOT.
TURMERIC_GODOT_AOT=1 /Applications/Godot.app/Contents/MacOS/Godot --headless \
    --path examples/aot-bench-heavy --script res://main.gd
```

## What good output looks like

```
[turmeric res://bench.tur AOT] loaded 2 exports from .../libtg_script_<hash>.so (built)
[aot-bench-heavy] _ready
[turmeric-godot AOT] first dispatch routed via AOT: bench/hot (mangled=bench__hot)

========================================================
  AOT BENCH HEAVY RESULT
  Reported mode (GDScript-side):     aot
  Iterations:                        100000
  Per call (50 nested frames each):  XX.X ns
  Per inner frame:                   YY.Y ns
========================================================
```

`bench/hot` and `bench/step` get the same closure-walk treatment in
interpreter mode; in AOT mode each `step` invocation is a direct C
call into the dlopen'd library. The "Per inner frame" line is the
clearest single number to compare across runs -- if AOT and interp
both report the same per-frame cost, AOT isn't actually firing.

## Success criterion

Per [`godot-binding-aot-cleanup-plan.md`](../../../turmeric/docs/archive/godot-binding-aot-cleanup-plan.md)
T1.C: AOT at least 3x faster than interpreter at this workload. (The
parent plan's 5x target lives further out -- bodies where the Variant
fixed cost is a single-digit-percent of total per-call time. step(50)
gets close to that ratio without needing a runaway iteration count.)
