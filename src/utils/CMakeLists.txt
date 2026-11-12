# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(kgtutils STATIC)

target_sources(kgtutils PRIVATE
    buildconfig.hpp
    error.hpp
    result.hpp
    runtimeerror.cpp
    runtimeerror.hpp
    urlfinder.cpp
    urlfinder.hpp
    version.cpp
    version.hpp
)

# Regenerated on every build, for the current commit; only version.cpp includes it
find_package(Git QUIET)
set(kgt_build_version_header ${CMAKE_CURRENT_BINARY_DIR}/kortoisegit_buildversion.hpp)
add_custom_target(kgtbuildversion
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DOUTPUT=${kgt_build_version_header}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -P ${PROJECT_SOURCE_DIR}/cmake/GenerateBuildVersion.cmake
    BYPRODUCTS ${kgt_build_version_header}
    VERBATIM
)
add_dependencies(kgtutils kgtbuildversion)

# Linked into the Dolphin plugin, which is a shared module
set_target_properties(kgtutils PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(kgtutils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(kgtutils PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kgtutils PUBLIC Qt6::Core)
