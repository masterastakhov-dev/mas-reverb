#!/bin/bash
# MASTER VERB installer: copies the plugins into your user plug-in folders
# and removes the macOS download quarantine flag.
cd "$(dirname "$0")" || exit 1
set -e

AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU_DIR" "$VST3_DIR"

rm -rf "$AU_DIR/MASTER VERB.component" "$VST3_DIR/MASTER VERB.vst3"
cp -R "MASTER VERB.component" "$AU_DIR/"
cp -R "MASTER VERB.vst3" "$VST3_DIR/"
xattr -dr com.apple.quarantine "$AU_DIR/MASTER VERB.component" "$VST3_DIR/MASTER VERB.vst3" 2>/dev/null || true

# make the system rescan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "MASTER VERB installed:"
echo "  AU   -> $AU_DIR/MASTER VERB.component"
echo "  VST3 -> $VST3_DIR/MASTER VERB.vst3"
echo ""
echo "Restart your DAW. In Logic: Audio FX > Audio Units > masterastakhovsound > MASTER VERB"
