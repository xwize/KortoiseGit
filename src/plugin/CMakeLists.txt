# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

ecm_qt_declare_logging_category(kgtplugin_logging
    HEADER kortoisegitplugin_debug.hpp
    IDENTIFIER KGT_PLUGIN
    CATEGORY_NAME org.kde.kortoisegit.plugin
    DESCRIPTION "KortoiseGit Dolphin plugin"
    EXPORT KortoiseGit
)
set_source_files_properties(${kgtplugin_logging} PROPERTIES SKIP_LINTING ON)

# Everything but the factory, so the tests can link it
add_library(kgtplugin STATIC)

target_sources(kgtplugin PRIVATE
    ${kgtplugin_logging}
    gitfolderstatus.cpp
    gitfolderstatus.hpp
    resourceshell.qrc
    shellext.cpp
    shellext.hpp
)

# Linked into the plugin, which is a shared module
set_target_properties(kgtplugin PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The installed location of kortoisegitproc, for when the plugin is not running from the build tree
target_compile_definitions(kgtplugin PRIVATE KGT_INSTALL_BINDIR="${KDE_INSTALL_FULL_BINDIR}")

target_include_directories(kgtplugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kgtplugin PUBLIC
    kgtgit
    kgtutils
    Qt6::Core
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::KIOCore
    DolphinVcs
    ${CMAKE_DL_LIBS}
)

# Plugin ID, .so name and translation domain all differ from the stock fileviewgitplugin, so both can be installed
kcoreaddons_add_plugin(kortoisegitplugin SOURCES kortoisegitplugin.cpp INSTALL_NAMESPACE "dolphin/vcs")
target_link_libraries(kortoisegitplugin PRIVATE kgtplugin)
