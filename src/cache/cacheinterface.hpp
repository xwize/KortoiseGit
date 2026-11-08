// SPDX-FileCopyrightText: 2005-2008, 2012 TortoiseSVN
// SPDX-FileCopyrightText: 2008-2017, 2019, 2023, 2025-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TGitCache/CacheInterface.h: what the Dolphin plugin and kortoisegitcache agree on.
 * The session bus takes the place of TGitCache's named pipes, and owning the bus name of its single-instance mutex.
 */

#pragma once

#include "git/gitstatus.hpp"

#include <QLatin1StringView>
#include <QList>
#include <QStringList>

#include <optional>

namespace kgt
{

inline constexpr QLatin1StringView cacheServiceName{"org.kde.kortoisegitcache"}; // GetCacheMutexName's role too
inline constexpr QLatin1StringView cacheObjectPath{"/StatusCache"};
inline constexpr QLatin1StringView cacheInterfaceName{"org.kde.kortoisegitcache"};
inline constexpr unsigned int cacheItemHasUntracked = 0x100; // Added to a packed state: an untracked file lies below the item

/**
 * What one item of a folder is, as the cache reports it.
 */
struct CacheItemState {
    GitWcStatus status = GitWcStatus::None; // The state to draw
    bool hasUntracked = false; // An untracked file lies somewhere below it, if it is a folder
};

/**
 * The cache's answer for one folder: TGITCacheResponse's role, for all of a folder's items at once as Dolphin asks.
 * An item it doesn't list is a tracked, unchanged file or an unversioned folder, as in GitFolderStatus.
 */
struct CacheFolderStatus {
    std::optional<GitWcStatus> wholeFolder; // Set when the folder lies inside an ignored or nested-repository folder
    bool hasUnversioned = false; // An untracked file lies somewhere in the folder
    QStringList names; // The items that have a state
    QList<int> states; // Their packed states, in the same order
};

/**
 * @param state An item's state.
 * @returns It as one integer, for the bus.
 */
[[nodiscard]] constexpr int packItemState(CacheItemState state)
{
    return static_cast<int>(static_cast<unsigned int>(state.status) | (state.hasUntracked ? cacheItemHasUntracked : 0U));
}

/**
 * @param packed What packItemState() returned.
 * @returns The state.
 */
[[nodiscard]] constexpr CacheItemState unpackItemState(int packed)
{
    const auto bits = static_cast<unsigned int>(packed);
    return CacheItemState{.status = static_cast<GitWcStatus>(bits & ~cacheItemHasUntracked), .hasUntracked = (bits & cacheItemHasUntracked) != 0};
}

} // namespace kgt
