# turmeric-godot

A Godot 4 GDExtension that registers Turmeric (`.tur`) as a scripting language.

**Status:** G0 spike -- the extension registers `.tur` and prints to stdout on
every Godot callback. No actual script execution yet. See
[`docs/upcoming/v1/godot-language-binding-plan.md`](https://github.com/rjungemann/turmeric/blob/main/docs/upcoming/v1/godot-language-binding-plan.md)
in the turmeric repo for the full plan.

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
