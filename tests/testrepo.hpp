// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * A throwaway git repository for the tests that run real git.
 */

#pragma once

#include "git/git.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <tuple>

namespace kgt
{

/**
 * A repository on branch main in a temporary directory, removed when it goes out of scope. Call isolateGit()
 * once, in initTestCase(), before creating one.
 */
class TestRepo
{
public:
    TestRepo()
        : m_git(m_dir.path())
    {
        Q_ASSERT(m_dir.isValid());
        run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--initial-branch=main")});
    }

    /**
     * Keeps the user's own git configuration (signing, hooks, renames off) out of the tests, and gives commits
     * a fixed identity.
     */
    static void isolateGit()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
    }

    /**
     * @returns Runs git at the root.
     */
    [[nodiscard]] const Git& git() const
    {
        return m_git;
    }

    /**
     * @returns The root directory.
     */
    [[nodiscard]] QString path() const
    {
        return m_dir.path();
    }

    /**
     * @param relative A path relative to the root.
     * @returns Its absolute path.
     */
    [[nodiscard]] QString filePath(const QString& relative) const
    {
        return m_dir.filePath(relative);
    }

    /**
     * Runs git at the root, discarding its output.
     * @param arguments The arguments after "git".
     */
    void run(const QStringList& arguments) const
    {
        std::ignore = m_git.run(arguments);
    }

    /**
     * Creates or overwrites a file, and any directories above it.
     * @param path Relative to the root.
     * @param content What the file holds.
     */
    void write(const QString& path, const QByteArray& content) const
    {
        const QString fullPath = m_dir.filePath(path);
        QVERIFY(QDir().mkpath(QFileInfo(fullPath).absolutePath()));
        QFile file(fullPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(content), content.size());
    }

    /**
     * @param path Relative to the root.
     */
    void remove(const QString& path) const
    {
        QVERIFY(QFile::remove(m_dir.filePath(path)));
    }

    /**
     * Stages and commits everything.
     * @param message The commit message.
     */
    void commitAll(const QString& message) const
    {
        run({QStringLiteral("add"), QStringLiteral("--all")});
        run({QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), message});
    }

private:
    QTemporaryDir m_dir;
    Git m_git;
};

} // namespace kgt
