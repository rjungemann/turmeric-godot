# turmeric-godot

A Godot 4 GDExtension that registers Turmeric (`.tur`) as a scripting language.

**Status:** v1 scope complete, plus a post-v1 T3/T4 follow-up series. Script
source is evaluated by libturi, lifecycle hooks (`_ready`, `_process`,
`_input`, ...) dispatch to user defns, `load("res://x.tur")` round-trips
through the ResourceFormatLoader, `(godot-export ...)` properties round-trip
to the inspector, and `(godot-signal ...)` declarations surface in the Node
dock with `(emit-signal ...)` firing them at runtime. The `defgodot-script`
block surface, the ClassDB allowlist (53 classes), the paddle-pong demo and
the AOT cache have all landed; macOS, Linux and Windows all build.

**The AOT path now compiles scripts that touch the engine.** The extension
exports a real C entry point for every fixed-arity `godot-*` native
(`src/bridge/native_abi.h`), and the AOT stager writes those out as
`(extern-c ...)` declarations plus the baked-in prelude into a `tg-godot`
module beside the staged script. Symbols bind at `dlopen`, against the running
extension. Try it without editing anything:

```sh
cd examples/paddle-pong-tur
TURMERIC_GODOT_AOT=1 TUR_BIN=/path/to/tur \
  godot --headless --path . --script scripts/pong_driver.gd
```

`ball.tur` and `paddle.tur` build and dispatch through AOT and the behavioural
assertions pass; `score.tur` calls the variadic `(godot-call ...)` directly,
which has no compiled entry point, so it prints a note naming the substitute
and falls back to the interpreter.

> **Windows** needs a `tur` newer than v0.46.0 for AOT. On macOS and Linux the
> staged library leaves the `godot_*` symbols unresolved until `dlopen` binds
> them against the running extension; PE resolves every import at link time,
> so the stager writes a `:build-opts` block linking the staged library
> against this extension's own DLL -- and `tur build --shared` only honours
> that block from the fix in turmeric's `cmd_build_multi_files` onward. With
> an older `tur` every staged build fails at the link with
> `undefined reference to godot_println` and the script falls back to the
> interpreter. Verified with the same command above: `ball.tur` and
> `paddle.tur` load 110 exports each, dispatch through AOT, and the assertions
> pass, cold cache and warm.

**What still cannot be AOT-compiled**, each reported as a build-log note rather
than a bare "unknown function":

- the variadic natives -- `godot-call{,-v,-f,-b,-c}`, `godot-signal`,
  `emit-signal`. Use the arity-typed `godot-callx-*` family, or a curated
  prelude wrapper such as `(node/get-node self path)`;
- the 2234-wrapper generated facade (`label/set-text`, `node2d/...`), which is
  built on those variadic natives and is not staged yet;
- `godot-connect-typed`, and the `timer/one-shot` / `after` prelude wrappers
  over it -- a Turmeric closure has no shared representation between the
  interpreter and compiled code.

Full status, with what was measured and what remains:
[godot-aot-staged-build-lacks-godot-natives](https://github.com/rjungemann/turmeric/blob/main/docs/reported/godot-aot-staged-build-lacks-godot-natives.md).

Plans, in the turmeric repo:
[the completed v1 plan](https://github.com/rjungemann/turmeric/blob/main/docs/archive/godot-language-binding-plan.md)
and
[the current refresh](https://github.com/rjungemann/turmeric/blob/main/docs/upcoming/godot-binding-refresh-plan.md),
which covers what came after it and sequences the remaining work.

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

# Windows (x86_64) -- from an MSYS2 UCRT64 shell
python3 -m SCons platform=windows arch=x86_64 target=template_debug
```

Swap `target=template_release` for the release variant. Binaries land in
`examples/spike/bin/`; the `.gdextension` manifests point at them.

> **Windows:** build with MinGW-w64 under MSYS2 UCRT64, matching how `libturi.a`
> itself is built -- the shim links that archive statically, so the two must come
> from the same toolchain. `SConstruct` *searches* for `libturi.a` rather than
> assuming `build-rel/`, because the Windows bring-up builds into `build-win/`;
> it prints which archive it chose. Point it somewhere explicit with
> `libturi=<path>` or `TURMERIC_LIBTURI` when you have more than one build tree.

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
