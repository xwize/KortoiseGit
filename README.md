<!--
SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# KortoiseGit

A port of [TortoiseGit](https://tortoisegit.org/) to KDE: a Dolphin plugin plus a standalone Qt 6 / KDE Frameworks 6
application. Its dialogs, context menus, commands and behaviour follow TortoiseGit's, so anyone who knows TortoiseGit
can use it straight away.

**KortoiseGit is not affiliated with, or endorsed by, the TortoiseGit project.**

It has two parts, split the way TortoiseGit splits TortoiseShell and TortoiseGitProc:

- `kortoisegitplugin`, a Dolphin version control plugin that shows each file's git status as an overlay and adds
  the context menu entries. Each entry starts
  `kortoisegitproc` as a separate process, so no dialog ever runs inside Dolphin.
- `kortoisegitproc`, the application that runs every dialog, such as `kortoisegitproc --command log --path <path>`.
  It can also be run from a terminal.

Status: early. Dolphin shows status overlays. The commands are `add`, `commit`, `revert` and `push` (TortoiseGit's
Add, Commit, Revert and Push dialogs, also in Dolphin's context menu), `clone`, `repocreate` and `log`. Push asks for passwords and host keys
through ksshaskpass, or your own `SSH_ASKPASS`/`GIT_ASKPASS`, and never on a terminal.

## Building on Arch Linux

```sh
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules clang \
    qt6-base kcoreaddons ki18n kio dolphin tl-expected
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```

Presets (all Ninja, each building in `build/<preset>/`):

| Preset | Compiler | Notes |
|---|---|---|
| `gcc-debug` | GCC | Runs clang-tidy on every file; any warning fails the build |
| `gcc-release` | GCC | As above, optimised |
| `clang-debug` | Clang | No clang-tidy, for a faster build |
| `clang-sanitize` | Clang | ASan + UBSan |

Add `-DKGT_CLANG_TIDY=OFF` to a GCC configure for a faster local build without clang-tidy.

Configuring also installs a git pre-commit hook that checks formatting; run `git clang-format` before committing.

Never install into `/usr`: pacman owns it.

## Running from the build tree

```sh
build/gcc-debug/bin/kortoisegitproc --command log --path ~/src/some-repo
build/gcc-debug/bin/kortoisegitproc --help
```

To use the plugin in your own Dolphin without installing it:

```sh
scripts/dolphin-dev.sh            # or: scripts/dolphin-dev.sh clang-debug
```

It builds the plugin and the app, then restarts Dolphin with `QT_PLUGIN_PATH` pointing at the build tree. Closing that
Dolphin and starting it normally goes back to the installed plugins. The plugin finds `kortoisegitproc` next to itself
in the build tree, so nothing needs installing.

**Enable either KortoiseGit or Dolphin's own Git plugin, not both.** In Configure Dolphin, tick KortoiseGit and untick
Git in the list of version control plugins. Both plugins claim `.git` folders, and Dolphin uses only one.

## CLion

Open the project folder and enable the presets in Settings > Build, Execution, Deployment > CMake. To run or debug the
app, use the `kortoisegitproc` target with program arguments `--command log --path <repo>`. To debug the plugin, add a
run configuration that starts `/usr/bin/dolphin` with the environment variable
`QT_PLUGIN_PATH=<project>/build/<preset>/bin`, as `scripts/dolphin-dev.sh` does.

## Licence

GPL-2.0-or-later; see [LICENSES/](LICENSES/). Files ported from TortoiseGit keep its copyright lines and say which
file they come from.
