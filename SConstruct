#!/usr/bin/env python
"""
turmeric-godot SConstruct.

Driven by the standard godot-cpp SConstruct.

Build (macOS arm64):
    python3 -m SCons platform=macos arch=arm64 target=template_debug

Binaries land under examples/spike/bin/ so the bundled Godot test project can
load them via `res://bin/...`.
"""

import os

env = SConscript("godot-cpp/SConstruct")

# --- libturi linkage ---------------------------------------------------------
# TURMERIC_ROOT may be set via env or ARGUMENTS; defaults to ../turmeric
turmeric_root = ARGUMENTS.get(
    "turmeric_root",
    os.environ.get("TURMERIC_ROOT", os.path.abspath("../turmeric")),
)
libturi_a = os.path.join(turmeric_root, "build-rel", "src", "libturi.a")
if not os.path.isfile(libturi_a):
    print("ERROR: libturi.a not found at {}".format(libturi_a))
    print("Build it with: (cd {} && cmake --build build-rel --target libturi -j)".format(turmeric_root))
    Exit(1)

env.Append(CPPPATH=["src/", os.path.join(turmeric_root, "src")])
env.Append(LIBS=[File(libturi_a)])
# libturi pulls in dlopen/dlsym for spice loading on macOS.
if env["platform"] == "macos":
    env.Append(LINKFLAGS=["-Wl,-no_warn_duplicate_libraries"])

sources = Glob("src/*.cpp") + Glob("src/bridge/*.cpp")

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
