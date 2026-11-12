# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.27)

# The one place the version is set: scripts/version_and_tag.sh changes it and creates the matching tag
project(kortoisegit VERSION 1.2.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.7.0")
set(KF_MIN_VERSION "6.0.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM ${KF_MIN_VERSION} CONFIG REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(ECMAddTests)
include(ECMInstallIcons)
include(FeatureSummary)
include(KDEClangFormat)
include(KDEGitCommitHooks)

# GUIDELINES.md reports failures with exceptions; KDECompilerSettings turns them off by default
kde_enable_exceptions()

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus Gui Widgets Test)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons I18n KIO WidgetsAddons)
find_package(DolphinVcs REQUIRED)

set_package_properties(DolphinVcs PROPERTIES
    DESCRIPTION "The Dolphin version control plugin library"
    URL "https://apps.kde.org/dolphin/"
    TYPE REQUIRED
    PURPOSE "Provides the KVersionControlPlugin interface"
)

add_compile_definitions(TRANSLATION_DOMAIN=\"kortoisegit\")

option(KGT_CLANG_TIDY "Run clang-tidy on every compiled file; its warnings fail the build" ON)
if(KGT_CLANG_TIDY)
    find_program(KGT_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    # clang-tidy parses GCC's command line: drop the GCC-only flags that clang rejects or warns about
    set(CMAKE_CXX_CLANG_TIDY
        ${KGT_CLANG_TIDY_EXE}
        --quiet
        --removed-arg=-mno-direct-extern-access
        --extra-arg=-Wno-unknown-warning-option
        --extra-arg=-Wno-unused-command-line-argument
    )
endif()

add_subdirectory(data/icons)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

file(GLOB_RECURSE KGT_CLANG_FORMAT_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.hpp
)
# The vendored tl-expected header stays byte-identical to upstream (.clang-format-ignore covers git clang-format)
list(FILTER KGT_CLANG_FORMAT_SOURCE_FILES EXCLUDE REGEX "/src/utils/expected\\.hpp$")
kde_clang_format(${KGT_CLANG_FORMAT_SOURCE_FILES})
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
