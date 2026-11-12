#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Points the AUR package at the release scripts/version_and_tag.sh last made: sets pkgver to project(VERSION), resets
# pkgrel, fills in the tarball's checksum, regenerates .SRCINFO and commits both. It needs the release tag pushed,
# because the checksum is of GitHub's tarball of that tag. scripts/release.sh runs it at the right point.
# Usage: packaging/aur/update.sh
set -euo pipefail

die() {
    echo "$1" >&2
    exit 1
}

cd "$(dirname "${BASH_SOURCE[0]}")"
root="$(git rev-parse --show-toplevel)"

command -v updpkgsums >/dev/null || die "updpkgsums is missing: install pacman-contrib"
[[ "$(git branch --show-current)" == main ]] || die "Update the package on main"
[[ -z "$(git status --porcelain)" ]] || die "The working tree has uncommitted changes"

version="$(sed -nE 's/^project\(kortoisegit VERSION ([0-9.]+) .*/\1/p' "$root/CMakeLists.txt")"
[[ -n "$version" ]] || die "Couldn't find project(VERSION) in CMakeLists.txt"
git ls-remote --exit-code --tags origin "refs/tags/$version" >/dev/null ||
    die "Tag $version is not on origin yet: git push origin main $version"

sed -i -E -e "s/^pkgver=.*/pkgver=$version/" -e "s/^pkgrel=.*/pkgrel=1/" PKGBUILD
updpkgsums
makepkg --printsrcinfo >.SRCINFO
# updpkgsums leaves the downloaded tarball behind
rm -f -- kortoisegit-"$version".tar.gz

export GIT_AUTHOR_NAME="James Jones" GIT_AUTHOR_EMAIL="this.verb@gmail.com"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"
git commit --quiet --message "Update the AUR package to $version" -- PKGBUILD .SRCINFO

echo "PKGBUILD is at $version. Check it builds with: cd packaging/aur && makepkg -f"
echo "Then push main and publish it to the AUR (packaging/aur/README.md)."
