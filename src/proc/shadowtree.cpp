// SPDX-FileCopyrightText: 2009-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlg.h and BrowseRefsDlg.cpp (CShadowTree)

#include "shadowtree.hpp"

namespace kgt
{

ShadowTree *ShadowTree::nextSub(QString& nameLeft, bool createIfNotExist)
{
    const qsizetype posSlash = nameLeft.indexOf(QLatin1Char('/'));
    QString nameSub;
    if (posSlash < 0) {
        nameSub = nameLeft;
        nameLeft.clear();
    } else {
        nameSub = nameLeft.left(posSlash);
        nameLeft = nameLeft.mid(posSlash + 1);
    }
    if (nameSub.isEmpty()) {
        return nullptr;
    }
    if (!createIfNotExist && !children.contains(nameSub)) {
        return nullptr;
    }
    ShadowTree& nextNode = children[nameSub];
    nextNode.name = nameSub;
    nextNode.parent = this;
    return &nextNode;
}

bool ShadowTree::isLeaf() const
{
    return children.empty();
}

QString ShadowTree::refName() const
{
    if (!parent) {
        return name;
    }
    return parent->refName() + QLatin1Char('/') + name;
}

bool ShadowTree::isFrom(QLatin1StringView from) const
{
    const QString fullName = refName();
    if (!fullName.startsWith(from)) {
        return false;
    }
    if (from.endsWith(QLatin1Char('/'))) {
        return true;
    }
    return fullName.size() == from.size() || fullName.at(from.size()) == QLatin1Char('/');
}

QString ShadowTree::refsHeadsName() const
{
    return refName().mid(QLatin1StringView("refs/heads/").size());
}

const ShadowTree *ShadowTree::findLeaf(const QString& partialRefName) const
{
    if (isLeaf()) {
        // A match of the leaf's name: try the whole name
        if (partialRefName.endsWith(name) && refName().endsWith(partialRefName)) {
            return this;
        }
        return nullptr;
    }
    for (const auto& [childName, child] : children) {
        if (const ShadowTree *found = child.findLeaf(partialRefName)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace kgt
