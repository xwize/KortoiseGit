#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds KortoiseGit's main branch and installs it for the current user, into ~/.local. Running it again upgrades.
#   curl -fsSL https://raw.githubusercontent.com/xwize/KortoiseGit/main/scripts/install.sh | bash
# scripts/uninstall.sh removes everything this installs.
#
# KGT_REPO and KGT_BRANCH choose another repository or branch, for testing this script.
set -euo pipefail

# Everything runs from main(), called on the last line, so that `curl | bash` has read the whole script before any of
# it runs, and no command can swallow the rest of it from stdin
main() {
    local repo="${KGT_REPO:-https://github.com/xwize/KortoiseGit.git}"
    local branch="${KGT_BRANCH:-main}"
    local prefix="$HOME/.local"
    local config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
    local data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/kortoisegit"
    local env_script="$config_home/plasma-workspace/env/kortoisegit.sh"

    if [[ $EUID -eq 0 ]]; then
        echo "Run this as your own user, not as root: KortoiseGit installs into ~/.local." >&2
        exit 1
    fi

    check_dependencies

    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT

    echo "==> Downloading KortoiseGit ($branch)"
    git clone --quiet --depth 1 --branch "$branch" "$repo" "$tmp/src" </dev/null

    echo "==> Building"
    # No tests and no clang-tidy: they are for development, and the user's build needs neither
    cmake -S "$tmp/src" -B "$tmp/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DBUILD_TESTING=OFF \
        -DKGT_CLANG_TIDY=OFF \
        -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF \
        </dev/null >"$tmp/configure.log" || { cat "$tmp/configure.log" >&2; exit 1; }
    cmake --build "$tmp/build" </dev/null

    # An upgrade first removes the files of the previous install, so none that the new version dropped are left over
    if [[ -f "$data_dir/install_manifest.txt" ]]; then
        echo "==> Removing the previous version"
        remove_manifest_files "$data_dir/install_manifest.txt" "$prefix"
    fi

    echo "==> Installing into $prefix"
    cmake --install "$tmp/build" </dev/null >/dev/null
    mkdir -p "$data_dir"
    cp "$tmp/build/install_manifest.txt" "$data_dir/install_manifest.txt"

    # After installing, so that whatever starts the cache next gets the new version
    stop_status_cache

    # Dolphin finds version control plugins in Qt's plugin path; Plasma sources this script at login
    local plugin plugin_dir
    plugin="$(grep -m1 '/dolphin/vcs/kortoisegitplugin\.so$' "$data_dir/install_manifest.txt")"
    plugin_dir="${plugin%/dolphin/vcs/kortoisegitplugin.so}"
    mkdir -p "$(dirname "$env_script")"
    cat >"$env_script" <<EOF
# Written by the KortoiseGit installer; its uninstall script removes it.
# Lets Dolphin find the KortoiseGit plugin installed in ~/.local.
case ":\${QT_PLUGIN_PATH:-}:" in
    *":$plugin_dir:"*) ;;
    *) export QT_PLUGIN_PATH="$plugin_dir\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}" ;;
esac
EOF

    if command -v kbuildsycoca6 >/dev/null; then
        kbuildsycoca6 </dev/null >/dev/null 2>&1 || true
    fi

    echo
    echo "KortoiseGit is installed in $prefix."
    echo "To finish:"
    echo "  1. Log out and log in again, so Dolphin picks up the plugin."
    echo "  2. In Dolphin, open Configure Dolphin > Interface > Version Control, tick KortoiseGit and untick Git."
    if [[ ":$PATH:" != *":$prefix/bin:"* ]]; then
        echo "  (kortoisegitproc is in $prefix/bin, which is not on your PATH; Dolphin finds it without that.)"
    fi
}

# Stops with the pacman command that installs whatever build dependency is missing. Installs nothing itself.
check_dependencies() {
    local packages=(base-devel git cmake ninja extra-cmake-modules qt6-base kcoreaddons ki18n kio dolphin)
    if command -v pacman >/dev/null; then
        local missing
        missing="$(pacman -T "${packages[@]}" || true)"
        if [[ -n $missing ]]; then
            echo "Some build dependencies are missing. Install them with:" >&2
            echo "  sudo pacman -S --needed ${missing//$'\n'/ }" >&2
            exit 1
        fi
        return
    fi
    # Not Arch: check for the tools, and let CMake report any missing library
    local tool
    for tool in git cmake ninja c++; do
        if ! command -v "$tool" >/dev/null; then
            echo "$tool is missing. KortoiseGit needs git, CMake, Ninja, a C++20 compiler, extra-cmake-modules," >&2
            echo "Qt 6, KDE Frameworks 6 (CoreAddons, I18n, KIO) and Dolphin's development files." >&2
            exit 1
        fi
    done
}

# Stops a running kortoisegitcache. Linux keeps 15 characters of a process's name, so the full name would match nothing
stop_status_cache() {
    pkill -x -u "$EUID" kortoisegitcach 2>/dev/null || true
}

# Removes the files listed in a CMake install manifest, and any directories that leaves empty, staying inside prefix
remove_manifest_files() {
    local manifest="$1" prefix="$2" file dir
    # CMake does not end the manifest with a newline
    while IFS= read -r file || [[ -n $file ]]; do
        [[ $file == "$prefix"/* ]] || continue
        rm -f -- "$file"
        dir="$(dirname -- "$file")"
        while [[ $dir == "$prefix"/* ]] && rmdir -- "$dir" 2>/dev/null; do
            dir="$(dirname -- "$dir")"
        done
    done <"$manifest"
}

main "$@"
