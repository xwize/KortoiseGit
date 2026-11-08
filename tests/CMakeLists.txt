# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

set(kgt_tests
    adddlgtest
    browserefsdlgtest
    clonetest
    cmdlineargstest
    commitdlgtest
    createrepositorytest
    difftest
    gitadmindirtest
    gitclioutputparsertest
    gitnumstattest
    gitrevloglisttest
    gitstatuslistctrltest
    gitstatustest
    gittest
    lanestest
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

# Runs the real executable to check exit codes and --help/--version
target_compile_definitions(proctest PRIVATE KGT_PROC_EXE="$<TARGET_FILE:kortoisegitproc>")
add_dependencies(proctest kortoisegitproc)

# The Dolphin plugin's library, run without Dolphin
foreach(test gitfolderstatustest shellexttest)
    ecm_add_test(${test}.cpp LINK_LIBRARIES kgtplugin Qt6::Test)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
