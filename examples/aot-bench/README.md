# aot-bench

Wiring smoke test for the AOT path. Times one million `hot(x)` calls
into `bench.tur` and reports `ns/call`.

**For throughput comparison between AOT and interpreter, run
[`examples/aot-bench-heavy/`](../aot-bench-heavy) instead.** This
bench's body (`(+ x 1)`) is too cheap to surface the AOT-vs-interp
delta -- both modes are dominated by Godot's ~250 ns per-call Variant
overhead. The heavy bench uses a 50-frame recursive body that lifts
the body cost above the fixed cost; that's the one with measurable
speedup numbers. This one stays around as the "did the wire-up still
work?" check.

Original microbench from
[`docs/archive/godot-binding-aot-plan.md`](../../../turmeric/docs/archive/godot-binding-aot-plan.md)
Phase A6.

## Build + first import

```sh
# Build the GDExtension (mise + pipx:scons is the recommended setup).
cd ../turmeric-godot
scons platform=macos arch=arm64 target=template_debug

# One-time project import so .godot/ is populated and the .tur loader registers.
/Applications/Godot.app/Contents/MacOS/Godot --headless \
    --path examples/aot-bench --editor --quit-after 2
```

## Run

```sh
# Interpreter baseline.
/Applications/Godot.app/Contents/MacOS/Godot --headless \
    --path examples/aot-bench --script res://main.gd

# AOT.
TURMERIC_GODOT_AOT=1 /Applications/Godot.app/Contents/MacOS/Godot --headless \
    --path examples/aot-bench --script res://main.gd
```

## Expected output

### Interpreter run (no env var, default project setting)

```
[turmeric res://bench.tur] _reload (len=...)
[aot-bench] _ready

========================================================
  AOT BENCH START  --  GDScript reports mode=interpreter
========================================================

========================================================
  AOT BENCH RESULT
  Reported mode (GDScript-side):   interpreter
  Iterations:                      1000000
  Per call:                        ~250-300 ns
========================================================
```

Note the **absence** of any `[turmeric ... AOT]` line.

### AOT run (`TURMERIC_GODOT_AOT=1`)

```
[turmeric res://bench.tur] _reload (len=...)
[turmeric res://bench.tur AOT] loaded 2 exports from \
    /.../.godot/turmeric-cache/<hash>/build/lib/libtg_script_<hash>.so (built)
[aot-bench] _ready
[turmeric-godot AOT] first dispatch routed via AOT: bench/hot (mangled=bench__hot)

========================================================
  AOT BENCH START  --  GDScript reports mode=aot
========================================================

========================================================
  AOT BENCH RESULT
  Reported mode (GDScript-side):   aot
  Iterations:                      1000000
  Per call:                        << interpreter (target: >=5x faster)
========================================================
```

The two `[turmeric ... AOT]` lines are the load-bearing signal: the
first proves the build + dlopen worked, the second proves cb_call
actually routed through the AOT symbol table for the hot loop. If they
are missing in an AOT run, the bench is silently running in interpreter
mode regardless of the env var or `mode=aot` line.

## Cache

The first AOT run pays the `tur build --shared` subprocess (1-3s);
subsequent runs hit the cache and load instantly. Drop
`.godot/turmeric-cache/` to force a rebuild.

`hot` is intentionally trivial (`(+ x 1)`). That's a feature for
detecting wiring breaks but a limitation for showing speedup -- see
below.

## Why this bench shows a small delta

Measured on macOS arm64, Godot 4.3, debug GDExtension:

| Mode         | ns/call |
|--------------|---------|
| interpreter  | ~269    |
| AOT (cold)   | ~265    |
| AOT (warm)   | ~262    |

The ~7 ns difference is real (AOT trades a 30 ns interpreter closure
walk for a 10 ns trampoline-through-`tur_ffi_thunk_call`) but it's
swimming inside ~250 ns of fixed-cost Godot infrastructure that *both*
modes pay every iteration:

| Per-call slice                                          | ~ns |
|---------------------------------------------------------|-----|
| `holder.call(...)` -> `Object::callp` -> `cb_call`      | 150 |
| `Variant(p_args[i])` + `(int64_t)v` + return Variant    |  50 |
| AOT-vs-interp delta (the slice we set out to compress)  |  20 |

The 5x target from
[`godot-binding-aot-plan.md`](../../../turmeric/docs/archive/godot-binding-aot-plan.md)'s
A6 success criterion is achievable but not at this body weight -- you
need a `hot` whose body cost exceeds the Variant fixed cost. A loop of
~100 arithmetic ops, a recursive sum-to-N, or any other body that walks
more than 1-2 AST nodes per call would show the AOT path pulling away
from interpreter linearly with body weight. This bench's value, today,
is as a wiring + cache-hit/cache-miss smoke test, not as a throughput
demonstration.
