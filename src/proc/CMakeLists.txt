# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

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
    aboutdlg.cpp
    aboutdlg.hpp
    adddlg.cpp
    adddlg.hpp
    apputils.cpp
    apputils.hpp
    browserefsdlg.cpp
    browserefsdlg.hpp
    browserefsdlgfilter.cpp
    browserefsdlgfilter.hpp
    chooseversion.cpp
    chooseversion.hpp
    clonedlg.cpp
    clonedlg.hpp
    cmdlineargs.cpp
    cmdlineargs.hpp
    commitdlg.cpp
    commitdlg.hpp
    createrepodlg.cpp
    createrepodlg.hpp
    createbranchtagdlg.cpp
    createbranchtagdlg.hpp
    filterhelper.cpp
    filterhelper.hpp
    commands/aboutcommand.cpp
    commands/aboutcommand.hpp
    commands/addcommand.cpp
    commands/addcommand.hpp
    commands/branchcommand.cpp
    commands/branchcommand.hpp
    commands/clonecommand.cpp
    commands/clonecommand.hpp
    commands/commitcommand.cpp
    commands/commitcommand.hpp
    commands/createrepositorycommand.cpp
    commands/createrepositorycommand.hpp
    commands/diffcommand.cpp
    commands/diffcommand.hpp
    commands/command.cpp
    commands/command.hpp
    commands/fetchcommand.cpp
    commands/fetchcommand.hpp
    commands/logcommand.cpp
    commands/logcommand.hpp
    commands/mergecommand.cpp
    commands/mergecommand.hpp
    commands/prevdiffcommand.cpp
    commands/prevdiffcommand.hpp
    commands/pullcommand.cpp
    commands/pullcommand.hpp
    commands/pushcommand.cpp
    commands/pushcommand.hpp
    commands/rebasecommand.cpp
    commands/rebasecommand.hpp
    commands/refbrowsecommand.cpp
    commands/refbrowsecommand.hpp
    commands/revertcommand.cpp
    commands/revertcommand.hpp
    commands/switchcommand.cpp
    commands/switchcommand.hpp
    commands/tagcommand.cpp
    commands/tagcommand.hpp
    gitclioutputparser.cpp
    gitclioutputparser.hpp
    gitdiff.cpp
    gitdiff.hpp
    gitloglist.cpp
    gitloglist.hpp
    gitloglist_filter.cpp
    gitloglistmodel.cpp
    gitloglistmodel.hpp
    lanes.cpp
    lanes.hpp
    gitstatuslistctrl.cpp
    gitstatuslistctrl.hpp
    gitstatuslistmodel.cpp
    gitstatuslistmodel.hpp
    gitswitchdlg.cpp
    gitswitchdlg.hpp
    icons.qrc
    logdlg.cpp
    logdlg.hpp
    logdlgfilter.cpp
    logdlgfilter.hpp
    mergeabortdlg.cpp
    mergeabortdlg.hpp
    mergedlg.cpp
    mergedlg.hpp
    progresscommands/addprogresscommand.cpp
    progresscommands/addprogresscommand.hpp
    progresscommands/revertprogresscommand.cpp
    progresscommands/revertprogresscommand.hpp
    progressdlg.cpp
    progressdlg.hpp
    pullfetchdlg.cpp
    pullfetchdlg.hpp
    pushdlg.cpp
    pushdlg.hpp
    rebasedlg.cpp
    rebasedlg.hpp
    rebasedlg_rebase.cpp
    refleaflistmodel.cpp
    refleaflistmodel.hpp
    revertdlg.cpp
    revertdlg.hpp
    shadowtree.cpp
    shadowtree.hpp
)

target_include_directories(kgtproc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kgtproc PUBLIC
    kgtgit
    kgtutils
    Qt6::Core
    Qt6::Widgets
    KF6::ConfigCore
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
