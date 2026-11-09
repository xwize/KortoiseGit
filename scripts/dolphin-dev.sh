#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Restarts Dolphin with the KortoiseGit plugin from the build tree, without installing anything.
# Usage: scripts/dolphin-dev.sh [preset] [dolphin arguments...]   (preset defaults to gcc-debug)
# Closing that Dolphin and starting it normally goes back to the installed plugins.
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-gcc-debug}"
[[ $# -gt 0 ]] && shift
build_dir="$source_dir/build/$preset"
plugin="$build_dir/bin/dolphin/vcs/kortoisegitplugin.so"

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    cmake --preset "$preset" -S "$source_dir"
fi
# The plugin, the app it launches and the status cache
cmake --build "$build_dir" --target kortoisegitplugin kortoisegitproc kortoisegitcache

if [[ ! -f "$plugin" ]]; then
    echo "Plugin not found at $plugin" >&2
    exit 1
fi

wait_for_dolphin_exit() {
    for _ in {1..50}; do
        pgrep -x dolphin >/dev/null || break
        sleep 0.1
    done
}

# Ask Dolphin to quit cleanly, then kill it if it is still running after 5 seconds
if pgrep -x dolphin >/dev/null; then
    echo "Quitting Dolphin..."
    if command -v kquitapp6 >/dev/null; then
        kquitapp6 dolphin >/dev/null 2>&1 || true
    else
        pkill -x dolphin || true
    fi
    wait_for_dolphin_exit
    if pgrep -x dolphin >/dev/null; then
        echo "Dolphin did not quit; killing it"
        pkill -9 -x dolphin || true
        wait_for_dolphin_exit # SIGKILL is asynchronous
    fi
fi

# An already running or installed cache would own the bus name: start the build tree's instead. It exits when idle
pkill -x -u "$EUID" kortoisegitcach 2>/dev/null || true # Linux keeps 15 characters of a process's name
for _ in {1..50}; do
    pgrep -x -u "$EUID" kortoisegitcach >/dev/null || break
    sleep 0.1
done
setsid "$build_dir/bin/kortoisegitcache" >/dev/null 2>&1 </dev/null &
disown

echo "Starting Dolphin with $plugin"
echo "In Configure Dolphin, enable KortoiseGit and disable Git in the version control plugins, if not done already."
# --new-window: otherwise Dolphin hands over to any instance still on the bus and exits without loading our plugin
QT_PLUGIN_PATH="$build_dir/bin${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
    setsid dolphin --new-window "$@" >/dev/null 2>&1 </dev/null &
disown
