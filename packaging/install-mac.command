#!/bin/bash
# Installs PolySwell II for the current user and clears the quarantine flag (the plugin is not notarised).
cd "$(dirname "$0")" || exit 1
VST3="$HOME/Library/Audio/Plug-Ins/VST3"; AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3" "$AU"
rm -rf "$VST3/PolySwell II.vst3" "$AU/PolySwell II.component"
cp -R "PolySwell II.vst3" "$VST3/" && cp -R "PolySwell II.component" "$AU/" || { echo "Copy failed."; read -r -p "Press Return to close."; exit 1; }
xattr -dr com.apple.quarantine "$VST3/PolySwell II.vst3" "$AU/PolySwell II.component" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null   # so Logic / GarageBand see the new Audio Unit
echo "PolySwell II is installed. Open your DAW and rescan plugins."
read -r -p "Press Return to close."
