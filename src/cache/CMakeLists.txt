# SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

ecm_qt_declare_logging_category(kgtcache_logging
    HEADER cachedebug.hpp
    IDENTIFIER KGT_CACHE
    CATEGORY_NAME org.kde.kortoisegit.cache
    DESCRIPTION "KortoiseGit status cache"
    EXPORT KortoiseGit
)
set_source_files_properties(${kgtcache_logging} PROPERTIES SKIP_LINTING ON)

# Everything but main(), so the tests can link it
add_library(kgtcache STATIC)

target_sources(kgtcache PRIVATE
    ${kgtcache_logging}
    cacheddirectory.cpp
    cacheddirectory.hpp
    cacheinterface.hpp
    cacheservice.cpp
    cacheservice.hpp
    directorywatcher.cpp
    directorywatcher.hpp
    foldercrawler.cpp
    foldercrawler.hpp
    gitstatuscache.cpp
    gitstatuscache.hpp
    shellupdater.cpp
    shellupdater.hpp
)

target_include_directories(kgtcache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kgtcache PUBLIC kgtgit kgtutils Qt6::Core Qt6::DBus)

add_executable(kortoisegitcache tgitcache.cpp)
target_link_libraries(kortoisegitcache PRIVATE kgtcache)
install(TARGETS kortoisegitcache ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Starts the cache on demand once installed; from the build tree the plugin starts it itself
configure_file(org.kde.kortoisegitcache.service.in ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kortoisegitcache.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kortoisegitcache.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
