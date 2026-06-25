# turmeric-godot

A Godot 4 GDExtension that registers Turmeric (`.tur`) as a scripting language.

**Status:** G2 in progress -- script source is evaluated by libturi, lifecycle
hooks (`_ready`, `_process`, `_input`, ...) dispatch to user defns,
`load("res://x.tur")` round-trips through the ResourceFormatLoader, and
`(godot-export ...)` properties round-trip to the inspector. Signals, AOT
mode, and the paddle-pong demo are still pending. See
[`docs/upcoming/v1/godot-language-binding-plan.md`](https://github.com/rjungemann/turmeric/blob/main/docs/upcoming/v1/godot-language-binding-plan.md)
in the turmeric repo for the full plan.

## Script-side natives

| Native | Effect |
| --- | --- |
| `(godot-println msg)` | Routes a cstr through Godot's print pipeline. |
| `(godot-export name type default)` | Declares an inspector-visible property. Call at top level. `type` is `"float" \| "int" \| "bool" \| "string"`. |
| `(godot-prop-get name)` | Reads the export on the current instance (falls back to the declared default). Strings return nil in v1. |
| `(godot-prop-set name val)` | Writes the export on the current instance. Coerces to the declared type. |

## Build (macOS arm64)

```sh
git submodule update --init --recursive
python3 -m SCons platform=macos arch=arm64 target=template_debug
```

Output lands in `bin/`. The `.gdextension` manifest at the repo root points at
those binaries.

## Try it

```sh
/Applications/Godot.app/Contents/MacOS/Godot --headless \
  --path examples/spike --quit 2>&1 | grep -i turmeric
```

You should see `[turmeric-godot] initialize(level=...)` and the language
registration trace.

## Layout

- `src/` -- C++ shim (GDExtension entry, `TurmericLanguage`).
- `godot-cpp/` -- submodule, pinned to `4.3-stable`.
- `examples/spike/` -- minimal Godot project that loads the extension.
- `SConstruct` -- build script.
- `turmeric-godot.gdextension` -- the manifest Godot reads.
