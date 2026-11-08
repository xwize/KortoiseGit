# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KORTOISEGIT
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/kortoisegit_version.hpp
)

ecm_qt_declare_logging_category(kgtproc_logging
    HEADER kortoisegitproc_debug.hpp
    IDENTIFIER KGT_PROC
    CATEGORY_NAME org.kde.kortoisegit.proc
    DESCRIPTION "KortoiseGit application (kortoisegitproc)"
    EXPORT KortoiseGit
)
set_source_files_properties(${kgtproc_logging} PROPERTIES SKIP_LINTING ON)

# Everything but main(), so the tests can link it
add_library(kgtproc STATIC)

target_sources(kgtproc PRIVATE
    ${kgtproc_logging}
    adddlg.cpp
    adddlg.hpp
    apputils.cpp
    apputils.hpp
    clonedlg.cpp
    clonedlg.hpp
    cmdlineargs.cpp
    cmdlineargs.hpp
    commitdlg.cpp
    commitdlg.hpp
    createrepodlg.cpp
    createrepodlg.hpp
    commands/addcommand.cpp
    commands/addcommand.hpp
    commands/clonecommand.cpp
    commands/clonecommand.hpp
    commands/commitcommand.cpp
    commands/commitcommand.hpp
    commands/createrepositorycommand.cpp
    commands/createrepositorycommand.hpp
    commands/command.cpp
    commands/command.hpp
    commands/logcommand.cpp
    commands/logcommand.hpp
    commands/pushcommand.cpp
    commands/pushcommand.hpp
    commands/revertcommand.cpp
    commands/revertcommand.hpp
    gitclioutputparser.cpp
    gitclioutputparser.hpp
    gitloglist.cpp
    gitloglist.hpp
    gitloglistmodel.cpp
    gitloglistmodel.hpp
    gitstatuslistctrl.cpp
    gitstatuslistctrl.hpp
    gitstatuslistmodel.cpp
    gitstatuslistmodel.hpp
    icons.qrc
    logdlg.cpp
    logdlg.hpp
    progresscommands/revertprogresscommand.cpp
    progresscommands/revertprogresscommand.hpp
    progressdlg.cpp
    progressdlg.hpp
    pushdlg.cpp
    pushdlg.hpp
    revertdlg.cpp
    revertdlg.hpp
)

target_include_directories(kgtproc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kgtproc PUBLIC
    kgtgit
    kgtutils
    Qt6::Core
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::KIOGui
    KF6::KIOWidgets
    KF6::WidgetsAddons
)

add_executable(kortoisegitproc main.cpp)
target_link_libraries(kortoisegitproc PRIVATE kgtproc)

install(TARGETS kortoisegitproc ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
ecm_qt_install_logging_categories(
    EXPORT KortoiseGit
    FILE kortoisegit.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
)
