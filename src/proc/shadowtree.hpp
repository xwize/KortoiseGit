// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlg.h (CShadowTree): the refs split at their slashes into a
 * tree. Inner nodes are the ref browser's tree; leaves are the refs its list shows.
 */

#pragma once

#include "git/gitrevrefbrowser.hpp"

#include <QString>

#include <map>

namespace kgt
{

/**
 * One node of the ref tree: "refs", "heads", "feature" in refs/heads/feature/x, or a leaf such as "x" holding the
 * ref's data. Children are kept sorted by name. Nodes never move once created, so pointers to them stay valid
 * until the tree is cleared.
 */
struct ShadowTree {
    using ShadowTreeMap = std::map<QString, ShadowTree>;

    /**
     * Ported from CShadowTree::GetNextSub: steps one path component down.
     * @param[in,out] nameLeft The rest of the name; its first component is removed.
     * @param createIfNotExist Whether to add the child if it is missing.
     * @returns The child, or nullptr if it is missing (and not created) or the component is empty.
     */
    [[nodiscard]] ShadowTree *nextSub(QString& nameLeft, bool createIfNotExist);

    /**
     * @returns Whether this node has no children, so is a ref (or the empty root).
     */
    [[nodiscard]] bool isLeaf() const;

    /**
     * @returns The full name, such as refs/heads/main.
     */
    [[nodiscard]] QString refName() const;

    /**
     * Ported from CShadowTree::IsFrom. With from = refs/heads: refs/heads/master and refs/heads match,
     * refs/headsh doesn't. With from = refs/heads/: refs/heads/master matches, refs/heads doesn't.
     * @param from A prefix.
     * @returns Whether this node is at or below it.
     */
    [[nodiscard]] bool isFrom(QLatin1StringView from) const;

    /**
     * @returns The name below refs/heads/, for a local branch.
     */
    [[nodiscard]] QString refsHeadsName() const;

    /**
     * Ported from CShadowTree::FindLeaf.
     * @param partialRefName A name such as main or origin/main.
     * @returns The first leaf, in order, whose full name ends with it, or nullptr.
     */
    [[nodiscard]] const ShadowTree *findLeaf(const QString& partialRefName) const;

    QString name; // m_csRefName: this component
    GitRevRefBrowser ref; // The ref's data, for a leaf
    ShadowTreeMap children; // m_ShadowTree
    ShadowTree *parent = nullptr; // m_pParent; nullptr for the root
};

} // namespace kgt
