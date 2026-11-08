// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "git/tgitpath.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using kgt::TGitPath;

/**
 * TGitPath's path helpers, shared by the commit and log dialogs.
 */
class TGitPathTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_root = QFileInfo(m_dir->path()).canonicalFilePath();
        QVERIFY(QDir(m_root).mkpath(QStringLiteral("tree/sub")));
        QVERIFY(QFile::link(m_root + QStringLiteral("/tree"), m_root + QStringLiteral("/link")));
    }

    void resolvesLinksInTheDirectory()
    {
        QCOMPARE(TGitPath::resolvedPath(m_root + QStringLiteral("/link/sub/file.txt")), m_root + QStringLiteral("/tree/sub/file.txt"));
    }

    void keepsALinkItself()
    {
        QCOMPARE(TGitPath::resolvedPath(m_root + QStringLiteral("/link")), m_root + QStringLiteral("/link"));
    }

    void missingDirectoryStaysAbsolute()
    {
        const QString missing = m_root + QStringLiteral("/nowhere/file.txt");
        QCOMPARE(TGitPath::resolvedPath(missing), missing);
    }

    void relativeToTheRoot()
    {
        const QString tree = m_root + QStringLiteral("/tree");
        QCOMPARE(TGitPath::relativePaths(tree, {tree + QStringLiteral("/sub"), m_root + QStringLiteral("/link/a.txt")}),
                 (QStringList{QStringLiteral("sub"), QStringLiteral("a.txt")}));
    }

    void theRootMeansTheWholeTree()
    {
        const QString tree = m_root + QStringLiteral("/tree");
        QVERIFY(TGitPath::relativePaths(tree, {tree + QStringLiteral("/sub"), tree}).isEmpty());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;
};

QTEST_GUILESS_MAIN(TGitPathTest)

#include "tgitpathtest.moc"
