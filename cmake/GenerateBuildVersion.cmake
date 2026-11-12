# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

# Writes the header holding the version this build reports. Runs on every build (cmake -P), because a commit doesn't
# re-run CMake's configure step. The header is only rewritten when the version changes, so nothing else rebuilds.
#
# Takes -DSOURCE_DIR, -DOUTPUT, -DPROJECT_VERSION and -DGIT_EXECUTABLE.
#
# PROJECT_VERSION (project() in the top-level CMakeLists.txt) is the version; scripts/version_and_tag.sh tags from it. This
# automates what TortoiseGit does by hand with PREVIEW_INFO in src/version.h: anything that isn't exactly a release
# gets its commit appended, as in 0.5.0+12.g1a2b3c4 or 0.5.0+12.g1a2b3c4.dirty. Without git metadata (a source
# tarball) the version is reported as it stands.

cmake_minimum_required(VERSION 3.27)

set(version_glob "[0-9]*.[0-9]*.[0-9]*")
set(build_version "${PROJECT_VERSION}")

function(run_git output_variable)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ${ARGN}
        OUTPUT_VARIABLE output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result
        ERROR_QUIET
    )
    if(result EQUAL 0)
        set(${output_variable} "${output}" PARENT_SCOPE)
    else()
        set(${output_variable} "" PARENT_SCOPE)
    endif()
endfunction()

if(GIT_EXECUTABLE)
    run_git(toplevel rev-parse --show-toplevel)
endif()

# makepkg unpacks a tarball inside the AUR package's own clone: that repository's history isn't ours
set(in_own_repository FALSE)
if(toplevel)
    file(REAL_PATH "${toplevel}" toplevel)
    file(REAL_PATH "${SOURCE_DIR}" real_source_dir)
    if(toplevel STREQUAL real_source_dir)
        set(in_own_repository TRUE)
    endif()
endif()

if(in_own_repository)
    # A release tag must name the version the code reports
    run_git(head_tags tag --points-at HEAD --list "${version_glob}")
    if(head_tags)
        string(REPLACE "\n" ";" head_tags "${head_tags}")
        if(NOT PROJECT_VERSION IN_LIST head_tags)
            message(FATAL_ERROR "HEAD is tagged ${head_tags} but project(VERSION) says ${PROJECT_VERSION}. "
                                "Make releases with scripts/release.sh.")
        endif()
    endif()

    run_git(described describe --tags --match "${version_glob}" --long --dirty --always)
    if(described MATCHES "-([0-9]+)-(g[0-9a-f]+)(-dirty)?$")
        set(commits_since_tag "${CMAKE_MATCH_1}")
        set(hash "${CMAKE_MATCH_2}")
        set(dirty "${CMAKE_MATCH_3}")
        if(NOT commits_since_tag EQUAL 0 OR dirty)
            set(build_version "${PROJECT_VERSION}+${commits_since_tag}.${hash}")
            if(dirty)
                string(APPEND build_version ".dirty")
            endif()
        endif()
    elseif(described)
        # No release tag reachable, as in a shallow clone: --always gave just the hash
        string(REPLACE "-" "." described "${described}")
        set(build_version "${PROJECT_VERSION}+g${described}")
    endif()
endif()

# Only touches the file if its contents change
file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT "#define KORTOISEGIT_BUILD_VERSION \"@build_version@\"\n" @ONLY)
