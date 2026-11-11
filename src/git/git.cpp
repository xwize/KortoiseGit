// SPDX-FileCopyrightText: 2008-2026 TortoiseGit
// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Ported from TortoiseGit src/Git/Git.cpp (CGit::Run, CGit::StripRefName, CGit::HasWorkingTreeConflicts, the
// branch and tag lists and the ref name helpers)

#include "git/git.hpp"

#include "git/gitadmindir.hpp"
#include "utils/runtimeerror.hpp"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <tuple>
#include <utility>

namespace
{

using namespace Qt::StringLiterals;

constexpr auto remotesPrefix = "remotes/"_L1;

/**
 * @returns The order of StrCmpLogicalW, TortoiseGit's LogicalComparePredicate: numbers by value, case ignored.
 */
QCollator logicalCollator()
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

/**
 * @param output Text git printed.
 * @returns Its lines, without the empty ones.
 */
QList<QByteArray> nonEmptyLines(const QByteArray& output)
{
    QList<QByteArray> lines = output.split('\n');
    lines.removeIf([](const QByteArray& line) {
        return line.trimmed().isEmpty();
    });
    return lines;
}

/**
 * @param level A configuration file; not Effective, which has no switch.
 * @returns git config's switch for it.
 */
QString configLevelSwitch(kgt::ConfigLevel level)
{
    switch (level) {
    case kgt::ConfigLevel::Global:
        return QStringLiteral("--global");
    case kgt::ConfigLevel::System:
        return QStringLiteral("--system");
    case kgt::ConfigLevel::Effective:
        Q_ASSERT(false);
        [[fallthrough]];
    case kgt::ConfigLevel::Local:
        break;
    }
    return QStringLiteral("--local");
}

} // namespace

namespace kgt
{

Git::Git(QString workingDirectory)
    : m_workingDirectory(std::move(workingDirectory))
{
}

const QString& Git::workingDirectory() const
{
    return m_workingDirectory;
}

void Git::setRunTimeout(int timeoutMs)
{
    m_runTimeoutMs = timeoutMs;
}

QByteArray Git::run(const QStringList& arguments, RunMode mode, const QByteArray& input) const
{
    GitCall call(m_workingDirectory, arguments, mode, input);
    call.waitForFinished(m_runTimeoutMs);
    return call.output();
}

std::unique_ptr<GitCall> Git::runAsync(const QStringList& arguments, RunMode mode, const QByteArray& input) const
{
    return std::make_unique<GitCall>(m_workingDirectory, arguments, mode, input);
}

QString Git::workingTreeRoot(const QString& path)
{
    const QFileInfo info(path);
    const Git git(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    QByteArray root = git.run({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, RunMode::ReadOnly);
    // Only the newline git ends with: the directory name itself may end in a space
    if (root.endsWith('\n')) {
        root.chop(1);
    }
    return QString::fromUtf8(root);
}

QString Git::repositoryRoot(const QString& path)
{
    try {
        return workingTreeRoot(path);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Commands accept a bare repository; git runs in it without a path filter
        const QFileInfo info(path);
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
}

std::optional<QString> Git::takeField(const QByteArray& output, qsizetype& pos, char delimiter)
{
    const qsizetype end = output.indexOf(delimiter, pos);
    if (end <= pos) {
        return std::nullopt;
    }
    QString field = QString::fromUtf8(QByteArrayView(output).sliced(pos, end - pos));
    pos = end + 1;
    return field;
}

QString Git::stripRefName(const QString& ref)
{
    for (const QLatin1StringView prefix : {QLatin1StringView("refs/heads/"), QLatin1StringView("refs/")}) {
        if (ref.startsWith(prefix)) {
            return ref.mid(prefix.size());
        }
    }
    return ref;
}

bool Git::isHash(QByteArrayView text)
{
    constexpr qsizetype sha1Length = 40;
    constexpr qsizetype sha256Length = 64;
    if (text.size() != sha1Length && text.size() != sha256Length) {
        return false;
    }
    return std::ranges::all_of(text, [](char digit) {
        return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    });
}

bool Git::hasWorkingTreeConflicts() const
{
    return !run({QStringLiteral("ls-files"), QStringLiteral("-u")}, RunMode::ReadOnly).isEmpty();
}

Result<ConflictHashes> Git::parseConflictHashesFromLsFile(const QByteArray& output)
{
    if (output.isEmpty()) {
        return makeError(ErrorCategory::Parse, QStringLiteral("The file is not in conflict."));
    }
    const auto malformed = [] {
        return makeError(ErrorCategory::Parse, QStringLiteral("Unexpected output from git ls-files -u -t -z."));
    };
    ConflictHashes hashes;
    qsizetype pos = 0;
    while (pos < output.size()) {
        // M <mode> <hash> <stage>\t<name>\0
        const std::optional<QString> tag = takeField(output, pos, ' ');
        const std::optional<QString> mode = tag ? takeField(output, pos, ' ') : std::nullopt;
        const std::optional<QString> hash = mode ? takeField(output, pos, ' ') : std::nullopt;
        const std::optional<QString> stage = hash ? takeField(output, pos, '\t') : std::nullopt;
        const std::optional<QString> name = stage ? takeField(output, pos, '\0') : std::nullopt;
        if (!name || *tag != QLatin1Char('M') || !isHash(hash->toLatin1())) {
            return malformed();
        }
        const bool isFile = *mode != QLatin1String("160000");
        if (*stage == QLatin1Char('1')) {
            hashes.baseHash = *hash;
            hashes.baseIsFile = isFile;
        } else if (*stage == QLatin1Char('2')) {
            hashes.localHash = *hash;
            hashes.localIsFile = isFile;
        } else if (*stage == QLatin1Char('3')) {
            hashes.remoteHash = *hash;
            hashes.remoteIsFile = isFile;
        } else {
            return malformed();
        }
    }
    return hashes;
}

std::optional<QString> Git::guessRefForHash(const QString& hash) const
{
    // --points-at also finds annotated tags, which CGit::GetMapHashToFriendName lists peeled
    const QStringList arguments{
        QStringLiteral("for-each-ref"),
        QStringLiteral("--format=%(refname)"),
        QStringLiteral("--points-at=") + hash,
    };
    const QByteArray output = run(arguments, RunMode::ReadOnly);
    const QStringList refs = QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QList<QLatin1StringView> prefixes{
        QLatin1StringView("refs/heads/"),
        QLatin1StringView("refs/remotes/"),
        QLatin1StringView("refs/tags/"),
    };
    for (const QLatin1StringView prefix : prefixes) {
        const auto found = std::ranges::find_if(refs, [prefix](const QString& ref) {
            return ref.startsWith(prefix);
        });
        if (found != refs.cend()) {
            return found->mid(prefix.size());
        }
    }
    return std::nullopt;
}

bool Git::isUnbornBranch() const
{
    try {
        std::ignore = run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), QStringLiteral("HEAD")}, RunMode::ReadOnly);
        return false;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
    }
    try {
        // HEAD has no commit, but does it name a branch? Otherwise something else is wrong
        std::ignore = run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("HEAD")}, RunMode::ReadOnly);
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
}

QString Git::configValue(const QString& name) const
{
    try {
        QByteArray value = run({QStringLiteral("config"), QStringLiteral("--get"), name}, RunMode::ReadOnly);
        if (value.endsWith('\n')) {
            value.chop(1);
        }
        return QString::fromUtf8(value);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: not set
        return {};
    }
}

std::optional<QString> Git::configValueAt(const QString& name, ConfigLevel level) const
{
    QStringList arguments{QStringLiteral("config")};
    if (level != ConfigLevel::Effective) {
        arguments.append(configLevelSwitch(level));
    }
    arguments << QStringLiteral("--get") << name;
    try {
        QByteArray value = run(arguments, RunMode::ReadOnly);
        if (value.endsWith('\n')) {
            value.chop(1);
        }
        return QString::fromUtf8(value);
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: not set. A missing file, or --local outside a repository, counts the same
        return std::nullopt;
    }
}

bool Git::isConfigTrue(const QString& value)
{
    const QString lower = value.toLower();
    return lower == QLatin1String("true") || lower == QLatin1String("yes") || lower == QLatin1String("on") || lower == QLatin1String("1");
}

void Git::setConfigValue(const QString& name, const QString& value, ConfigLevel level) const
{
    // After --: a value may start with a dash
    std::ignore = run({QStringLiteral("config"), configLevelSwitch(level), QStringLiteral("--"), name, value});
}

void Git::unsetConfigValue(const QString& name, ConfigLevel level) const
{
    std::ignore = run({QStringLiteral("config"), configLevelSwitch(level), QStringLiteral("--unset"), QStringLiteral("--"), name});
}

void Git::deleteRef(const QString& reference) const
{
    // CGit::DeleteRef. Our refs never carry show-ref's ^{}: the log's come from git log's decorations
    static const QString headsPrefix = QStringLiteral("refs/heads/");
    static const QString tagsPrefix = QStringLiteral("refs/tags/");
    static const QString remotesPrefix = QStringLiteral("refs/remotes/");
    QStringList arguments;
    QString name;
    if (reference.startsWith(headsPrefix)) {
        arguments = {QStringLiteral("branch"), QStringLiteral("-D")};
        name = reference.mid(headsPrefix.size());
    } else if (reference.startsWith(tagsPrefix)) {
        arguments = {QStringLiteral("tag"), QStringLiteral("-d")};
        name = reference.mid(tagsPrefix.size());
    } else if (reference.startsWith(remotesPrefix)) {
        arguments = {QStringLiteral("branch"), QStringLiteral("-r"), QStringLiteral("-D")};
        name = reference.mid(remotesPrefix.size());
    } else {
        const QString message = QStringLiteral("unsupported reference type: %1").arg(reference);
        throw RuntimeError({.category = ErrorCategory::Git, .message = message});
    }
    arguments << QStringLiteral("--") << name;
    std::ignore = run(arguments);
}

QString Git::hash(const QString& ref) const
{
    // No need to look up a ref if it's already an OID
    if (isHash(ref.toLatin1())) {
        return ref;
    }
    if (ref.isEmpty()) {
        return {};
    }
    QString name = fixBranchName(ref);
    if (name.isEmpty()) {
        name = ref;
    }
    const QStringList arguments{QStringLiteral("rev-parse"),
                                QStringLiteral("--verify"),
                                QStringLiteral("-q"),
                                QStringLiteral("--end-of-options"),
                                name + QStringLiteral("^{commit}")};
    try {
        return QString::fromLatin1(run(arguments, RunMode::ReadOnly)).trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {};
    }
}

bool Git::isFastForward(const QString& from, const QString& target) const
{
    const QStringList arguments{QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), fixBranchName(from), fixBranchName(target)};
    try {
        std::ignore = run(arguments, RunMode::ReadOnly);
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
}

QString Git::commonAncestor(const QString& from, const QString& target) const
{
    const QStringList arguments{QStringLiteral("merge-base"), fixBranchName(from), fixBranchName(target)};
    try {
        return QString::fromLatin1(run(arguments, RunMode::ReadOnly)).trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {};
    }
}

QString Git::currentBranch() const
{
    const QString branch = GitAdminDir::currentBranchFromFile(GitAdminDir::worktreeAdminDirPath(m_workingDirectory));
    if (branch.isEmpty() || branch == QLatin1String("HEAD")) {
        return QStringLiteral("(no branch)");
    }
    return branch;
}

QString Git::fullRefName(const QString& shortRefName) const
{
    try {
        return QString::fromUtf8(run({QStringLiteral("rev-parse"),
                                      QStringLiteral("--symbolic-full-name"),
                                      QStringLiteral("--verify"),
                                      QStringLiteral("--end-of-options"),
                                      shortRefName},
                                     RunMode::ReadOnly))
            .trimmed();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return {};
    }
}

bool Git::isBranchTagNameUnique(const QString& name) const
{
    try {
        const QByteArray output = run({QStringLiteral("show-ref"),
                                       QStringLiteral("--tags"),
                                       QStringLiteral("--heads"),
                                       QStringLiteral("--"),
                                       QStringLiteral("refs/heads/") + name,
                                       QStringLiteral("refs/tags/") + name},
                                      RunMode::ReadOnly);
        return nonEmptyLines(output).size() <= 1;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: neither exists
        return true;
    }
}

bool Git::branchTagExists(const QString& name, bool isBranch) const
{
    try {
        const QByteArray output = run({QStringLiteral("show-ref"),
                                       isBranch ? QStringLiteral("--heads") : QStringLiteral("--tags"),
                                       QStringLiteral("--"),
                                       QStringLiteral("refs/heads/") + name,
                                       QStringLiteral("refs/tags/") + name},
                                      RunMode::ReadOnly);
        return !output.isEmpty();
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        // Exit code 1: none
        return false;
    }
}

bool Git::isBranchNameValid(const QString& name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('-')) || name == QLatin1String("HEAD")) {
        return false;
    }
    try {
        std::ignore = Git(QDir::tempPath()).run({QStringLiteral("check-ref-format"), QStringLiteral("refs/heads/") + name}, RunMode::ReadOnly);
        return true;
    } catch (const RuntimeError& e) {
        if (e.error().category != ErrorCategory::Git) {
            throw;
        }
        return false;
    }
}

QString Git::derefFetchHead() const
{
    QFile fetchHead(QDir(GitAdminDir::worktreeAdminDirPath(m_workingDirectory)).absoluteFilePath(QStringLiteral("FETCH_HEAD")));
    if (!fetchHead.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Each line is <hash> TAB [not-for-merge] TAB <description>; the lines to merge have nothing between the tabs
    int forMergeLineCount = 0;
    QByteArray hashToReturn;
    for (const QByteArray& line : fetchHead.readAll().split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 3) {
            continue;
        }
        if (fields.at(1).isEmpty()) {
            hashToReturn = fields.at(0);
            ++forMergeLineCount;
            if (forMergeLineCount > 1) {
                // More than one branch to merge: an octopus merge, no single ref to pick
                return {};
            }
        }
    }
    return QString::fromLatin1(hashToReturn);
}

void Git::getOneFile(const QString& refName, const QString& path, QIODevice& output) const
{
    // "^{}" dereferences a signed tag, as GetOneFile does. A submodule's entry is a commit, which cat-file refuses
    const QByteArray content = run({QStringLiteral("cat-file"), QStringLiteral("--filters"), refName + QStringLiteral("^{}:") + path}, RunMode::ReadOnly);
    if (output.write(content) != content.size()) {
        throw RuntimeError({.category = ErrorCategory::Io, .message = QStringLiteral("Could not write file.")});
    }
}

QString Git::fixBranchName(const QString& branchName) const
{
    if (branchName == QLatin1String("FETCH_HEAD")) {
        return derefFetchHead();
    }
    return branchName;
}

QStringList Git::refListArguments()
{
    return {QStringLiteral("show-ref"), QStringLiteral("-d")};
}

QStringList Git::parseRefList(const QByteArray& output)
{
    QStringList list;
    for (const QByteArray& line : nonEmptyLines(output)) {
        const qsizetype start = line.indexOf(' ');
        if (start < 0) {
            continue;
        }
        const QString name = QString::fromUtf8(line.sliced(start + 1));
        if (list.isEmpty() || name != list.constLast() + QStringLiteral("^{}")) {
            list.append(name);
        }
    }
    std::ranges::sort(list, logicalCollator());
    return list;
}

bool Git::hasRefLog(const QString& ref) const
{
    const QString worktreeAdminDir = GitAdminDir::worktreeAdminDirPath(m_workingDirectory);
    // A bare repository's folder is its admin directory
    const QString adminDir = worktreeAdminDir.isEmpty() ? m_workingDirectory : worktreeAdminDir;
    const QString logFile = QStringLiteral("logs/") + ref;
    return QDir(adminDir).exists(logFile) || QDir(GitAdminDir::commonAdminDirPath(adminDir)).exists(logFile);
}

QStringList Git::branchListArguments(unsigned int type)
{
    QStringList arguments{QStringLiteral("branch"), QStringLiteral("--no-color")};
    if ((type & BranchAll) == BranchAll) {
        arguments.append(QStringLiteral("-a"));
    } else if (type & BranchRemote) {
        arguments.append(QStringLiteral("-r"));
    }
    return arguments;
}

BranchList Git::parseBranchList(const QByteArray& output, unsigned int type, bool skipCurrent, bool hasFetchHead)
{
    BranchList result;
    QString current;
    bool headIsDetached = false;
    for (const QByteArray& rawLine : nonEmptyLines(output)) {
        const QByteArray line = rawLine.trimmed();
        if (line.contains(" -> ")) {
            // Something like remotes/origin/HEAD -> origin/main
            continue;
        }
        QString branch = QString::fromUtf8(line);
        if (line.startsWith('*')) {
            if (skipCurrent) {
                continue;
            }
            branch = branch.mid(2);
            // A detached HEAD shows as "(HEAD detached at ...)"
            if (branch.startsWith(QLatin1Char('('))) {
                headIsDetached = true;
                continue;
            }
            current = branch;
        } else if (line.startsWith('+')) {
            // Checked out in another worktree of the same repository (git 2.23 and later)
            branch = branch.mid(2);
        }
        if ((type & BranchRemote) != 0 && (type & BranchLocal) == 0) {
            branch.prepend(remotesPrefix);
        }
        result.branches.append(branch);
    }

    if ((type & BranchFetchHead) != 0 && hasFetchHead) {
        result.branches.append(QStringLiteral("FETCH_HEAD"));
    }

    // LogicalCompareBranchesPredicate, with TortoiseGit's default of local branches first
    const QCollator collator = logicalCollator();
    std::ranges::sort(result.branches, [&collator](const QString& left, const QString& right) {
        const bool leftIsRemote = left.startsWith(remotesPrefix);
        const bool rightIsRemote = right.startsWith(remotesPrefix);
        if (leftIsRemote != rightIsRemote) {
            return rightIsRemote;
        }
        return collator.compare(left, right) < 0;
    });

    if (!headIsDetached && !skipCurrent && !current.isEmpty()) {
        result.current = static_cast<int>(result.branches.indexOf(current));
    }
    return result;
}

QStringList Git::parseTagList(const QByteArray& output)
{
    QStringList tags;
    for (const QByteArray& line : nonEmptyLines(output)) {
        tags.append(QString::fromUtf8(line));
    }
    // TortoiseGit's default order; SortTagsReversed isn't a setting here yet
    const QCollator collator = logicalCollator();
    std::ranges::sort(tags, [&collator](const QString& left, const QString& right) {
        return collator.compare(left, right) < 0;
    });
    return tags;
}

} // namespace kgt
