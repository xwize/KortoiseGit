// SPDX-FileCopyrightText: 2017-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlgFilter.h: the ref browser's filter box.
 */

#pragma once

#include "filterhelper.hpp"

namespace kgt
{

struct ShadowTree;

/**
 * Matches a ref against the filter text, looking at its name, subject, author and SHA-1 as selected.
 */
class BrowseRefsDlgFilter : public FilterHelper
{
public:
    using FilterHelper::FilterHelper;

    /**
     * Ported from CBrowseRefsDlgFilter::operator().
     * @param tree The ref's leaf.
     * @param ref The name the list shows for it.
     * @returns Whether the ref is shown.
     */
    [[nodiscard]] bool operator()(const ShadowTree& tree, const QString& ref) const;
};

} // namespace kgt
