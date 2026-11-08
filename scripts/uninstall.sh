#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Removes everything scripts/install.sh installed, together with KortoiseGit's settings:
#   curl -fsSL https://raw.githubusercontent.com/xwize/KortoiseGit/main/scripts/uninstall.sh | bash
# Does nothing when KortoiseGit is not installed.
set -euo pipefail

# Everything runs from main(), called on the last line, so that `curl | bash` has read the whole script first
main() {
    local prefix="$HOME/.local"
    local config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
    local data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
    local state_home="${XDG_STATE_HOME:-$HOME/.local/state}"
    local cache_home="${XDG_CACHE_HOME:-$HOME/.cache}"
    local data_dir="$data_home/kortoisegit"
    local manifest="$data_dir/install_manifest.txt"
    local env_dir="$config_home/plasma-workspace/env"
    local env_script="$env_dir/kortoisegit.sh"

    if [[ ! -f $manifest && ! -f $env_script ]]; then
        echo "KortoiseGit is not installed."
        exit 0
    fi

    echo "==> Removing KortoiseGit from $prefix"
    if [[ -f $manifest ]]; then
        remove_manifest_files "$manifest" "$prefix"
    fi
    rm -rf -- "$data_dir"
    rm -f -- "$env_script"
    rmdir -- "$env_dir" 2>/dev/null || true

    disable_dolphin_plugin "$config_home/dolphinrc"

    # kortoisegitproc's own settings, window state and caches, all named after its KAboutData component name
    rm -f -- "$config_home/kortoisegitprocrc" "$state_home/kortoisegitprocstaterc"
    rm -rf -- "$data_home/kortoisegitproc" "$cache_home/kortoisegitproc"

    if command -v kbuildsycoca6 >/dev/null; then
        kbuildsycoca6 </dev/null >/dev/null 2>&1 || true
    fi

    echo "KortoiseGit is uninstalled. Log out and log in again to clear it from Dolphin's plugin path."
}

# Takes KortoiseGit out of the version control plugins enabled in Configure Dolphin, if the user ticked it there
disable_dolphin_plugin() {
    local dolphinrc="$1"
    [[ -f $dolphinrc ]] || return 0
    command -v kreadconfig6 >/dev/null && command -v kwriteconfig6 >/dev/null || return 0

    local enabled kept=() plugins plugin
    enabled="$(kreadconfig6 --file "$dolphinrc" --group VersionControl --key enabledPlugins)"
    [[ ,$enabled, == *,KortoiseGit,* ]] || return 0
    IFS=, read -ra plugins <<<"$enabled"
    for plugin in "${plugins[@]}"; do
        [[ $plugin == KortoiseGit ]] || kept+=("$plugin")
    done
    if [[ ${#kept[@]} -eq 0 ]]; then
        # An empty list is Dolphin's default
        kwriteconfig6 --file "$dolphinrc" --group VersionControl --key enabledPlugins --delete
    else
        kwriteconfig6 --file "$dolphinrc" --group VersionControl --key enabledPlugins "$(IFS=,; echo "${kept[*]}")"
    fi
    echo "==> Disabled KortoiseGit in Dolphin's version control settings"
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
