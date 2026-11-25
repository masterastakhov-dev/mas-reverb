#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
name='MASTER SPACE 1.8.1 STUDIO.component'
bundle='com.masterastakhovsound.masterspace18levelmatch'
dir="${MAS_AU_INSTALL_DIR:-$HOME/Library/Audio/Plug-Ins/Components}"
verify() {
    [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist")" == "$bundle" ]]
    [[ "$(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:subtype' "$1/Contents/Info.plist")" == ML18 ]]
    /usr/bin/codesign --verify --strict "$1"
    /usr/bin/lipo -verify_arch arm64 x86_64 "$1/Contents/MacOS/MASTER SPACE 1.8.1 STUDIO"
}
verify "$name"
mkdir -p "$dir"
dst="$dir/$name"
if [[ -L "$dst" ]]; then exit 1; fi
if [[ -e "$dst" ]]; then verify "$dst"; fi
stage=$(mktemp -d "$dir/.master-space-level-match.XXXXXX")
trap 'rmdir "$stage" 2>/dev/null || true' EXIT
ditto "$name" "$stage/$name"
xattr -dr com.apple.quarantine "$stage/$name" 2>/dev/null || true
verify "$stage/$name"
if [[ -e "$dst" ]]; then mv "$dst" "$stage/previous"; fi
if ! mv "$stage/$name" "$dst"; then
    if [[ -e "$stage/previous" ]]; then mv "$stage/previous" "$dst"; fi
    exit 1
fi
if [[ -e "$stage/previous" ]]; then
    echo "Previous LEVEL MATCH build saved in $stage/previous"
fi
echo "Установлен $dst"
