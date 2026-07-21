# turmeric-godot

A Godot 4 GDExtension that registers Turmeric (`.tur`) as a scripting language.

**Status:** G2 in progress -- script source is evaluated by libturi, lifecycle
hooks (`_ready`, `_process`, `_input`, ...) dispatch to user defns,
`load("res://x.tur")` round-trips through the ResourceFormatLoader,
`(godot-export ...)` properties round-trip to the inspector, and
`(godot-signal ...)` declarations surface in the Node dock with
`(emit-signal ...)` firing them at runtime. AOT mode and the paddle-pong
demo are still pending. See
[`docs/upcoming/v1/godot-language-binding-plan.md`](https://github.com/rjungemann/turmeric/blob/main/docs/upcoming/v1/godot-language-binding-plan.md)
in the turmeric repo for the full plan.

## Script-side natives

| Native | Effect |
| --- | --- |
| `(godot-println msg)` | Routes a cstr through Godot's print pipeline. |
| `(godot-export name type default)` | Declares an inspector-visible property. Call at top level. `type` is `"float" \| "int" \| "bool" \| "string"`. |
| `(godot-prop-get name)` | Reads the export on the current instance (falls back to the declared default). Strings return nil in v1. |
| `(godot-prop-set name val)` | Writes the export on the current instance. Coerces to the declared type. |
| `(godot-signal name [arg-name arg-type]...)` | Declares a signal on the script; appears in the Node dock. Call at top level. Variadic (name,type) pairs after the signal name. |
| `(emit-signal name args...)` | Emits a declared signal on the current instance's owner. Arity checked against the declaration. |

## Build

The GDExtension statically links `libturi.a` from the sibling
[turmeric](https://github.com/rjungemann/turmeric) repo, so build that first,
then drive the standard godot-cpp SCons setup.

**Prerequisites**

- The `turmeric` repo checked out next to this one (`../turmeric`), or set
  `TURMERIC_ROOT` to its path.
- `git submodule update --init --recursive` (fetches `godot-cpp`).
- SCons. On distros that mark the system Python "externally managed" (PEP 668
  -- most current Linux), install it in a venv rather than fighting `pip`:
  ```sh
  python3 -m venv .venv && .venv/bin/pip install scons
  # then use .venv/bin/scons in place of `python3 -m SCons` below
  ```

**1. Build `libturi.a` (Release)** -- from the `turmeric` checkout:

```sh
cd ../turmeric
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --target libturi -j
```

> **Linux:** add `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` to the `cmake -S`
> line. `libturi.a` is linked into a shared object, and Linux `ld` rejects
> non-PIC objects in a `.so` (`relocation R_X86_64_TPOFF32 ... can not be
> used when making a shared object`). macOS compiles PIC by default, so the
> flag is a no-op there.

**2. Build the GDExtension** -- back in this repo:

```sh
# macOS (arm64)
python3 -m SCons platform=macos arch=arm64 target=template_debug

# Linux (x86_64)
python3 -m SCons platform=linux arch=x86_64 target=template_debug
```

Swap `target=template_release` for the release variant. Binaries land in
`examples/spike/bin/`; the `.gdextension` manifests point at them.

## Try it

**macOS:**

```sh
/Applications/Godot.app/Contents/MacOS/Godot --headless \
  --path examples/spike --quit 2>&1 | grep -i turmeric
```

**Linux** -- with a Godot 4.3 editor binary (download from
<https://godotengine.org/download/linux/>; the `.gdextension` pins
`compatibility_minimum = "4.3"`). The spike is a no-op project with no main
scene, so `--quit` alone exits before the language registers -- run it as the
editor for a couple of frames instead:

```sh
godot --headless --editor --path examples/spike --quit-after 3 2>&1 | grep -i turmeric
```

Either way you should see `[turmeric-godot] initialize(level=...)`, the
`registered Turmeric script language + resource format` trace, and (at editor
level) a `libturi smoke: (+ 1 2) = 3 : int` line confirming the
statically-linked runtime evaluates Turmeric.

## Layout

- `src/` -- C++ shim (GDExtension entry, `TurmericLanguage`).
- `godot-cpp/` -- submodule, pinned to `4.3-stable`.
- `examples/spike/` -- minimal Godot project that loads the extension.
- `SConstruct` -- build script.
- `turmeric-godot.gdextension` -- the manifest Godot reads.
