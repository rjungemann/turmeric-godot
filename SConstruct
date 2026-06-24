#!/usr/bin/env python
"""
turmeric-godot SConstruct.

Driven by the standard godot-cpp SConstruct.

Build (macOS arm64):
    python3 -m SCons platform=macos arch=arm64 target=template_debug

Binaries land under examples/spike/bin/ so the bundled Godot test project can
load them via `res://bin/...`.
"""

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "examples/spike/bin/libturmeric-godot.{}.{}.framework/libturmeric-godot.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "examples/spike/bin/libturmeric-godot{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
