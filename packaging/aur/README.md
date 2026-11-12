<!--
SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# AUR package

`PKGBUILD` builds a KortoiseGit release tag from GitHub's tarball and installs it under `/usr`. `.SRCINFO` is
generated from it (`makepkg --printsrcinfo > .SRCINFO`) and must be committed alongside every change to it.

## Releasing a new version

1. Make the release: `scripts/release.sh X.Y.Z`, then `git push origin main X.Y.Z`.
2. Update the package: `packaging/aur/update.sh`. It sets `pkgver`, resets `pkgrel` to 1, fills in the checksum of
   the pushed tag's tarball, regenerates `.SRCINFO` and commits. The checksum can only be taken once the tag is on
   GitHub, so this can't be part of step 1.
3. Check it builds, without installing anything: `cd packaging/aur && makepkg -f`. Look over the package with
   `pacman -Qlp kortoisegit-*.pkg.tar.zst` and `namcap PKGBUILD kortoisegit-*.pkg.tar.zst`.
4. `git push origin main`, then publish (below).

A fix to the PKGBUILD alone, with no new release, bumps `pkgrel` by hand instead; regenerate `.SRCINFO` and commit
both.

## Publishing to the AUR

Needs an AUR account with your SSH key added. The first time, clone the (empty) package repository:

```sh
git clone ssh://aur@aur.archlinux.org/kortoisegit.git ~/aur/kortoisegit
```

Then for each update:

```sh
cp packaging/aur/PKGBUILD packaging/aur/.SRCINFO ~/aur/kortoisegit/
cd ~/aur/kortoisegit
git add PKGBUILD .SRCINFO
git commit -m "Update to X.Y.Z-1"
git push
```

The AUR only accepts the `master` branch, and only a commit whose `.SRCINFO` matches its `PKGBUILD`.
