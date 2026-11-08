// SPDX-FileCopyrightText: 2017-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/TortoiseProc/BrowseRefsDlgFilter.cpp

#include "browserefsdlgfilter.hpp"

#include "shadowtree.hpp"

namespace kgt
{

bool BrowseRefsDlgFilter::operator()(const ShadowTree& tree, const QString& ref) const
{
    if (!isFilterActive()) {
        return calculateFinalResult(true);
    }
    QString scratch;
    if (selectedFilters() & LogFilterRefName) {
        scratch += ref + QLatin1Char('\n');
    }
    if (selectedFilters() & LogFilterSubject) {
        scratch += tree.ref.subject + QLatin1Char('\n');
    }
    if (selectedFilters() & LogFilterAuthors) {
        scratch += tree.ref.authorName + QLatin1Char('\n');
    }
    if (selectedFilters() & LogFilterRevs) {
        scratch += tree.ref.hash + QLatin1Char('\n');
    }
    return calculateFinalResult(match(scratch));
}

} // namespace kgt
