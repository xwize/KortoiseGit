# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

set(kgt_tests
    adddlgtest
    appicontest
    browserefsdlgtest
    clonetest
    cmdlineargstest
    commitdlgtest
    createbranchtagtest
    createrepositorytest
    difftest
    gitadmindirtest
    gitclioutputparsertest
    gitloglistgraphtest
    gitnumstattest
    gitrevloglisttest
    gitstatuslistctrltest
    gitstatustest
    gittest
    lanestest
    logdlgfiltertest
    logdlgtest
    massivegittasktest
    mergetest
    proctest
    progressdlgtest
    pullfetchtest
    pushdlgtest
    rebasetest
    resulttest
    reverttest
    switchtest
)

foreach(test ${kgt_tests})
    ecm_add_test(${test}.cpp LINK_LIBRARIES kgtproc Qt6::Test)
    # The dialog tests need no display
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# The graph's pixel alignment again on a HiDPI screen
add_test(NAME gitloglistgraphtest-dpr2 COMMAND gitloglistgraphtest)
set_tests_properties(gitloglistgraphtest-dpr2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2")

# Runs the real executable to check exit codes and --help/--version
target_compile_definitions(proctest PRIVATE KGT_PROC_EXE="$<TARGET_FILE:kortoisegitproc>")
add_dependencies(proctest kortoisegitproc)

# The Dolphin plugin's library, run without Dolphin
foreach(test gitfolderstatustest shellexttest)
    ecm_add_test(${test}.cpp LINK_LIBRARIES kgtplugin Qt6::Test)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
