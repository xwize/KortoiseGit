#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makes and publishes a release:
#   1. scripts/version_and_tag.sh: sets project(VERSION), commits and tags
#   2. pushes main and the tag, after asking
#   3. packaging/aur/update.sh: points the PKGBUILD at the pushed tag and commits
#   4. builds the Arch package with makepkg (installing nothing) and checks it with namcap
#   5. pushes main again, with the package commit
#   6. creates the GitHub release, with generated notes and the Arch package attached
#   7. publishes the PKGBUILD to the AUR, if its clone is at $KGT_AUR_DIR (default ~/aur/kortoisegit)
# Once the tag is pushed the release is public, so every step is skipped when already done: if one fails, fix the
# cause and run the script again with the same version.
# Usage: scripts/release.sh X.Y.Z
set -euo pipefail

die() {
    echo "$1" >&2
    exit 1
}

step() {
    echo "==> $1"
}

[[ $# -eq 1 ]] || die "Usage: $0 X.Y.Z"
version="$1"

cd "$(dirname "${BASH_SOURCE[0]}")/.."
root="$PWD"
aur_package="$root/packaging/aur"
aur_dir="${KGT_AUR_DIR:-$HOME/aur/kortoisegit}"

for tool in gh makepkg updpkgsums namcap; do
    command -v "$tool" >/dev/null || die "$tool is missing (gh: github-cli, updpkgsums: pacman-contrib)"
done
gh auth status >/dev/null 2>&1 || die "gh is not logged in: gh auth login"
[[ "$(git branch --show-current)" == main ]] || die "Releases are made from main"
[[ -z "$(git status --porcelain)" ]] || die "The working tree has uncommitted changes"

project_version() {
    sed -nE 's/^project\(kortoisegit VERSION ([0-9.]+) .*/\1/p' CMakeLists.txt
}

pkgbuild_value() {
    sed -nE "s/^$1=\(?'?([^')]*)'?\)?$/\1/p" "$aur_package/PKGBUILD"
}

tag_on_origin() {
    git ls-remote --exit-code --tags origin "refs/tags/$version" >/dev/null
}

# 1. Version and tag
if git rev-parse --quiet --verify "refs/tags/$version" >/dev/null; then
    [[ "$(project_version)" == "$version" ]] ||
        die "Tag $version exists but project(VERSION) is $(project_version): not resuming that release"
    step "Tag $version already made"
else
    step "Setting the version and tagging $version"
    scripts/version_and_tag.sh "$version"
fi

# 2. Push the release commit and tag: from here on the release is public
if tag_on_origin; then
    step "Tag $version already pushed"
else
    echo
    git log --oneline --decorate origin/main..main
    echo
    read -r -p "Push main and tag $version to origin and publish the release? [y/N] " answer || answer=""
    [[ "$answer" == [yY] ]] || die "Stopped before pushing. Run this again to carry on."
    step "Pushing main and $version"
    git push --quiet origin main "$version"
fi

# 3. Point the AUR package at the tag
if [[ "$(pkgbuild_value pkgver)" == "$version" && "$(pkgbuild_value sha256sums)" != SKIP ]]; then
    step "PKGBUILD already at $version"
else
    step "Updating the PKGBUILD to $version"
    packaging/aur/update.sh >/dev/null
fi

# 4. Build and check the package, in packaging/aur whatever makepkg.conf says
pkgrel="$(pkgbuild_value pkgrel)"
package_file="$aur_package/kortoisegit-$version-$pkgrel-$(uname -m).pkg.tar.zst"
step "Building the Arch package"
(
    cd "$aur_package"
    BUILDDIR="$aur_package" PKGDEST="$aur_package" SRCDEST="$aur_package" makepkg --force --cleanbuild --noconfirm \
        >"$aur_package/makepkg.log" 2>&1
) || die "makepkg failed: see $aur_package/makepkg.log"
[[ -f "$package_file" ]] || die "makepkg made no $package_file"
step "Checking it with namcap"
namcap_output="$(namcap "$aur_package/PKGBUILD" "$package_file")"
[[ -n "$namcap_output" ]] && echo "$namcap_output"
if grep -q ' E: ' <<<"$namcap_output"; then
    die "namcap reported errors"
fi

# 5. Push the package commit
if [[ -n "$(git log --oneline origin/main..main)" ]]; then
    step "Pushing main"
    git push --quiet origin main
fi

# 6. GitHub release
if gh release view "$version" >/dev/null 2>&1; then
    step "GitHub release $version exists: uploading the Arch package to it"
    gh release upload "$version" "$package_file" --clobber
else
    step "Creating the GitHub release $version"
    gh release create "$version" "$package_file" --verify-tag --title "KortoiseGit $version" --generate-notes
fi

# 7. AUR
if [[ -d "$aur_dir/.git" ]]; then
    step "Publishing to the AUR from $aur_dir"
    # A fresh clone of a new AUR package is empty: nothing to pull yet
    if git -C "$aur_dir" rev-parse --quiet --verify HEAD >/dev/null; then
        git -C "$aur_dir" pull --quiet --ff-only origin master
    fi
    cp "$aur_package/PKGBUILD" "$aur_package/.SRCINFO" "$aur_dir/"
    git -C "$aur_dir" add PKGBUILD .SRCINFO
    if ! git -C "$aur_dir" diff --cached --quiet; then
        GIT_AUTHOR_NAME="James Jones" GIT_AUTHOR_EMAIL="this.verb@gmail.com" \
            GIT_COMMITTER_NAME="James Jones" GIT_COMMITTER_EMAIL="this.verb@gmail.com" \
            git -C "$aur_dir" commit --quiet --message "Update to $version-$pkgrel"
        # The AUR only accepts master
        git -C "$aur_dir" push --quiet origin HEAD:master
    else
        step "The AUR already has $version-$pkgrel"
    fi
else
    echo "No AUR clone at $aur_dir: publish by hand (packaging/aur/README.md), or clone it there and run this again."
fi

echo
echo "Released $version."
