#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sets project(VERSION) in CMakeLists.txt, commits that and creates the matching annotated tag, so the tag and the
# version the application reports can't differ. Pushes nothing: scripts/release.sh runs this, then publishes.
# Usage: scripts/version_and_tag.sh X.Y.Z
set -euo pipefail

die() {
    echo "$1" >&2
    exit 1
}

[[ $# -eq 1 ]] || die "Usage: $0 X.Y.Z"
version="$1"
# project(VERSION) only takes numbers, so no pre-release suffixes
[[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || die "Not an X.Y.Z version: $version"

cd "$(dirname "${BASH_SOURCE[0]}")/.."

[[ "$(git branch --show-current)" == main ]] || die "Releases are made from main"
[[ -z "$(git status --porcelain)" ]] || die "The working tree has uncommitted changes"
if git rev-parse --quiet --verify "refs/tags/$version" >/dev/null; then
    die "Tag $version already exists"
fi

current="$(sed -nE 's/^project\(kortoisegit VERSION ([0-9.]+) .*/\1/p' CMakeLists.txt)"
[[ -n "$current" ]] || die "Couldn't find project(VERSION) in CMakeLists.txt"
if [[ "$version" == "$current" || "$(printf '%s\n%s\n' "$current" "$version" | sort -V | tail -n1)" != "$version" ]]; then
    die "$version is not newer than the current version $current"
fi

export GIT_AUTHOR_NAME="James Jones" GIT_AUTHOR_EMAIL="this.verb@gmail.com"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"

sed -i -E "s/^(project\(kortoisegit VERSION )[0-9.]+/\1$version/" CMakeLists.txt
git commit --quiet --message "Release $version" -- CMakeLists.txt
git tag --annotate --message "KortoiseGit $version" "$version"

echo "Tagged $version."
