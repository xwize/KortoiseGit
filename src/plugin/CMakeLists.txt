# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

# Plugin ID, .so name and translation domain all differ from the stock fileviewgitplugin, so both can be installed
kcoreaddons_add_plugin(kortoisegitplugin INSTALL_NAMESPACE "dolphin/vcs")

ecm_qt_declare_logging_category(kortoisegitplugin
    HEADER kortoisegitplugin_debug.hpp
    IDENTIFIER KGT_PLUGIN
    CATEGORY_NAME org.kde.kortoisegit.plugin
    DESCRIPTION "KortoiseGit Dolphin plugin"
    EXPORT KortoiseGit
)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/kortoisegitplugin_debug.cpp PROPERTIES SKIP_LINTING ON)

target_sources(kortoisegitplugin PRIVATE
    shellext.cpp
    shellext.hpp
)

# The installed location of kortoisegitproc, for when the plugin is not running from the build tree
target_compile_definitions(kortoisegitplugin PRIVATE KGT_INSTALL_BINDIR="${KDE_INSTALL_FULL_BINDIR}")

target_link_libraries(kortoisegitplugin PRIVATE
    Qt6::Core
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::KIOCore
    DolphinVcs
    kgtgit
    ${CMAKE_DL_LIBS}
)
