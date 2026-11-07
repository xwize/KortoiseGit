# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(kgtutils STATIC)

target_sources(kgtutils PRIVATE
    buildconfig.hpp
    error.hpp
    result.hpp
    runtimeerror.cpp
    runtimeerror.hpp
)

# Linked into the Dolphin plugin, which is a shared module
set_target_properties(kgtutils PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(kgtutils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kgtutils PUBLIC Qt6::Core tl::expected)
