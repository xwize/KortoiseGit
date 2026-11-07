# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

# The git layer (TortoiseGit's Git/ role): runs command-line git through QProcess and parses its output.
# Shared by the Dolphin plugin and kortoisegitproc.
add_library(kgtgit STATIC)

target_sources(kgtgit PRIVATE
    git.cpp
    git.hpp
    gitadmindir.cpp
    gitadmindir.hpp
    gitcall.cpp
    gitcall.hpp
    gitnumstat.cpp
    gitnumstat.hpp
    gitstatus.cpp
    gitstatus.hpp
    massivegittask.cpp
    massivegittask.hpp
    tgitpath.cpp
    tgitpath.hpp
)

# Linked into the Dolphin plugin, which is a shared module
set_target_properties(kgtgit PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(kgtgit PUBLIC kgtutils Qt6::Core)
