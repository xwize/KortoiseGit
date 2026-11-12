<!--
SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# AUR package

`PKGBUILD` builds a KortoiseGit release tag from GitHub's tarball and installs it under `/usr`. `.SRCINFO` is
generated from it (`makepkg --printsrcinfo > .SRCINFO`) and must be committed alongside every change to it.

## Releasing a new version

`scripts/release.sh X.Y.Z` does the whole release, from `main` with a clean working tree:

1. `scripts/version_and_tag.sh X.Y.Z` sets `project(VERSION)`, commits and tags.
2. It shows what will be pushed and asks before pushing `main` and the tag. From here the release is public.
3. `packaging/aur/update.sh` sets `pkgver`, resets `pkgrel` to 1, fills in the checksum of GitHub's tarball of the
   pushed tag (which is why this can't happen before the push), regenerates `.SRCINFO` and commits.
4. `makepkg` builds the package here, installing nothing (its log is `makepkg.log`), and `namcap` checks it. Any
   namcap error stops the release.
5. It pushes `main` again, with the package commit.
6. `gh release create` makes the GitHub release, with notes generated from the merged changes and the built
   `kortoisegit-X.Y.Z-1-x86_64.pkg.tar.zst` attached.
7. It publishes `PKGBUILD` and `.SRCINFO` to the AUR, if the AUR clone is at `$KGT_AUR_DIR` (default
   `~/aur/kortoisegit`, see below). Otherwise it says so, and you publish by hand.

It needs `gh` (github-cli, logged in), `makepkg`, `updpkgsums` (pacman-contrib) and `namcap`. Each step is skipped when
it has already been done, so if one fails, fix the cause and run `scripts/release.sh X.Y.Z` again.

A fix to the PKGBUILD alone, with no new release, bumps `pkgrel` by hand instead; regenerate `.SRCINFO`
(`makepkg --printsrcinfo > .SRCINFO`), commit both and publish them as below.

## Publishing to the AUR

Needs an AUR account with your SSH key added. Clone the package repository once, where `scripts/release.sh` looks for
it (it is empty until the first publish):

```sh
git clone ssh://aur@aur.archlinux.org/kortoisegit.git ~/aur/kortoisegit
```

To publish by hand instead:

```sh
cp packaging/aur/PKGBUILD packaging/aur/.SRCINFO ~/aur/kortoisegit/
cd ~/aur/kortoisegit
git add PKGBUILD .SRCINFO
git commit -m "Update to X.Y.Z-1"
git push origin HEAD:master
```

The AUR only accepts the `master` branch, and only a commit whose `.SRCINFO` matches its `PKGBUILD`.
