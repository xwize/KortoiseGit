# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

set(kgt_tests
    aboutdlgtest
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
    reflogtest
    resulttest
    reverttest
    stashrestoretest
    stashsavetest
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

# The Dolphin plugin itself never reaches the real session bus, so it can't start a real cache
set_tests_properties(shellexttest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=disabled:")

# The status cache, with real git and inotify and without a bus
ecm_add_test(gitstatuscachetest.cpp LINK_LIBRARIES kgtcache kgtplugin Qt6::Test)

# The plugin and the cache talking over a private session bus
find_program(KGT_DBUS_RUN_SESSION NAMES dbus-run-session REQUIRED)
add_executable(remotecachelinktest remotecachelinktest.cpp)
target_link_libraries(remotecachelinktest PRIVATE kgtcache kgtplugin Qt6::Test)
target_compile_definitions(remotecachelinktest PRIVATE KGT_CACHE_EXE="$<TARGET_FILE:kortoisegitcache>")
add_dependencies(remotecachelinktest kortoisegitcache)
add_test(NAME remotecachelinktest COMMAND ${KGT_DBUS_RUN_SESSION} -- $<TARGET_FILE:remotecachelinktest>)
set_tests_properties(remotecachelinktest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
