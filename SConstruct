#!/usr/bin/env python
"""
turmeric-godot SConstruct.

Driven by the standard godot-cpp SConstruct.

Build:
    python3 -m SCons platform=macos   arch=arm64  target=template_debug  # macOS
    python3 -m SCons platform=linux   arch=x86_64 target=template_debug  # Linux
    python3 -m SCons platform=windows arch=x86_64 target=template_debug  # Windows

libturi.a (in ../turmeric) must be built PIC on Linux so it can link into the
GDExtension .so -- configure it with -DCMAKE_POSITION_INDEPENDENT_CODE=ON.
See README.md "Build" for the full prerequisite steps.

Windows is a MinGW-w64 / UCRT64 build (run this from an MSYS2 UCRT64 shell, and
build godot-cpp with use_mingw=yes). PE code is position-independent already, so
there is no PIC prerequisite there. See the libturi block below for which build
tree is picked up.

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
#
# Which build tree to take libturi.a from. `libturi=<path>` (or TURMERIC_LIBTURI)
# names the archive outright; otherwise we search, because the Windows bring-up
# builds into `build-win/` rather than `build-rel/` and requiring a second
# Release tree just to link the shim would be a needless step. The chosen path
# is printed -- picking up a Debug archive when you meant Release is exactly the
# kind of thing that should not happen silently.
#
libturi_a = ARGUMENTS.get("libturi", os.environ.get("TURMERIC_LIBTURI", ""))
if not libturi_a:
    if env["platform"] == "windows":
        candidates = ["build-win-rel", "build-win", "build-rel"]
    else:
        candidates = ["build-rel"]
    for cand in candidates:
        probe = os.path.join(turmeric_root, cand, "src", "libturi.a")
        if os.path.isfile(probe):
            libturi_a = probe
            break
    else:
        print("ERROR: libturi.a not found under {} in any of: {}".format(
            turmeric_root, ", ".join(candidates)))
        if env["platform"] == "windows":
            print("Build it from an MSYS2 UCRT64 shell with:")
            print("  (cd {} && cmake --build build-win --target libturi -j)".format(turmeric_root))
        else:
            print("Build it with: (cd {} && cmake --build build-rel --target libturi -j)".format(turmeric_root))
        print("Or point at one explicitly: scons libturi=/path/to/libturi.a")
        Exit(1)
elif not os.path.isfile(libturi_a):
    print("ERROR: libturi.a not found at {} (from libturi= / TURMERIC_LIBTURI)".format(libturi_a))
    Exit(1)
print("[turmeric-godot] linking libturi from {}".format(libturi_a))

env.Append(CPPPATH=["src/", os.path.join(turmeric_root, "src")])
env.Append(LIBS=[File(libturi_a)])
# libturi pulls in dlopen/dlsym for spice loading on macOS.
if env["platform"] == "macos":
    env.Append(LINKFLAGS=["-Wl,-no_warn_duplicate_libraries"])
# libturi's reactor uses select() and pthreads, and its path helpers use
# PathIsRelative. glibc carries all of that in libc; MinGW splits it across
# Winsock, winpthreads and shlwapi, so they must be named explicitly. Same three
# the compiler's own link line adds under _WIN32 (src/main.c).
if env["platform"] == "windows":
    env.Append(LIBS=["ws2_32", "shlwapi", "pthread"])

sources = Glob("src/*.cpp") + Glob("src/bridge/*.cpp") + Glob("src/aot/*.cpp")

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

# --- Sync built framework into every examples/*/ project ---------------------
# Each demo project (paddle-pong-tur, future examples) holds its own
# turmeric-godot.gdextension manifest pointing at res://bin/... -- and
# needs the built framework dropped under its bin/. Rather than the
# user copying after every build, post a SCons action that mirrors
# examples/spike/bin/ into every sibling example with a manifest.
import shutil

def _sync_examples(target, source, env):
    spike_bin = "examples/spike/bin"
    if not os.path.isdir(spike_bin):
        return
    artifacts = [os.path.join(spike_bin, e) for e in os.listdir(spike_bin)
                 if not e.startswith(".")]
    if not artifacts:
        return
    for entry in os.listdir("examples"):
        proj = os.path.join("examples", entry)
        if entry == "spike" or not os.path.isdir(proj):
            continue
        if not os.path.isfile(os.path.join(proj, "turmeric-godot.gdextension")):
            continue
        dst_bin = os.path.join(proj, "bin")
        os.makedirs(dst_bin, exist_ok=True)
        for art in artifacts:
            dst = os.path.join(dst_bin, os.path.basename(art))
            if os.path.isdir(art):
                if os.path.exists(dst):
                    shutil.rmtree(dst)
                shutil.copytree(art, dst, symlinks=False)
            else:
                shutil.copy2(art, dst)
        print("[turmeric-godot] synced bin -> {}".format(dst_bin))

env.AddPostAction(library, _sync_examples)
