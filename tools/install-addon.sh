#!/usr/bin/env bash
#
# G5 prep -- install the turmeric-godot GDExtension + editor addon
# into a Godot project. Removes the manual `cp -R bin/...framework`
# step from the binding-guide quickstart.
#
# Usage:
#   tools/install-addon.sh <target-project-dir>
#
# What gets copied:
#   <repo>/examples/spike/bin/* ->  <target>/bin/
#   <repo>/examples/spike/addons/turmeric-godot-editor/* ->
#       <target>/addons/turmeric-godot-editor/
#   <repo>/examples/spike/turmeric-godot.gdextension ->
#       <target>/turmeric-godot.gdextension
#
# Skipped if missing -- the spike must have been built at least once
# (python3 -m SCons ...) so bin/ has the per-platform frameworks. The
# script reports each copy step and exits non-zero on the first
# unexpected failure.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <target-project-dir>" >&2
    exit 2
fi

target="$1"
repo="$(cd "$(dirname "$0")/.." && pwd)"
spike="$repo/examples/spike"

if [[ ! -d "$target" ]]; then
    echo "ERROR: target '$target' is not a directory" >&2
    exit 1
fi
if [[ ! -f "$target/project.godot" ]]; then
    echo "WARN: '$target/project.godot' not found; this doesn't look like a Godot project" >&2
fi
if [[ ! -d "$spike/bin" ]] || ! ls "$spike/bin/" 2>/dev/null | grep -q .; then
    echo "ERROR: $spike/bin/ is empty -- build the GDExtension first:" >&2
    echo "       (cd $repo && python3 -m SCons platform=macos arch=arm64 target=template_debug -j4)" >&2
    exit 1
fi

copy_dir() {
    local src="$1" dst="$2"
    if [[ -e "$dst" ]]; then
        echo "  replacing $dst"
        rm -rf "$dst"
    else
        echo "  creating  $dst"
    fi
    mkdir -p "$(dirname "$dst")"
    cp -R "$src" "$dst"
}

copy_file() {
    local src="$1" dst="$2"
    if [[ -e "$dst" ]]; then
        echo "  replacing $dst"
    else
        echo "  creating  $dst"
    fi
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
}

echo "installing turmeric-godot into $target"
mkdir -p "$target/bin"
for fw in "$spike"/bin/*; do
    [[ -e "$fw" ]] || continue
    copy_dir "$fw" "$target/bin/$(basename "$fw")"
done

if [[ -d "$spike/addons/turmeric-godot-editor" ]]; then
    copy_dir "$spike/addons/turmeric-godot-editor" "$target/addons/turmeric-godot-editor"
else
    echo "  (no editor addon found in spike; skipped)"
fi

copy_file "$spike/turmeric-godot.gdextension" "$target/turmeric-godot.gdextension"

echo "done."
echo
echo "Next steps in $target:"
echo "  1. Open the project once in the editor so Godot imports the GDExtension."
echo "  2. (Optional) Enable 'Turmeric (editor integration)' under"
echo "     Project -> Project Settings -> Plugins for syntax coloring."
echo "  3. Right-click a node -> Attach Script... -> Language: Turmeric."
