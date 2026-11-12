// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "filediffdlg.hpp"
#include "filedifflistmodel.hpp"
#include "testrepo.hpp"

#include <QLineEdit>
#include <QTest>

namespace
{

using namespace kgt;
using namespace Qt::StringLiterals;
using Action = TGitPath::Action;
using Actions = TGitPath::Actions;
using Model = FileDiffListModel;

constexpr int timeout = 10000;

/**
 * @param path The file.
 * @param what What happened to it.
 * @param add Lines added.
 * @param del Lines removed.
 * @param old Where it was renamed from.
 * @returns An entry of the list.
 */
FileDiffEntry entry(const QString& path, Actions what, const QString& add, const QString& del, const QString& old = {})
{
    return {
        .path = path,
        .oldPath = old,
        .actions = what,
        .linesAdded = add,
        .linesRemoved = del,
        .isDirectory = false,
    };
}

/**
 * @param path The new path.
 * @param oldPath The old path.
 * @returns A file renamed with one line changed.
 */
FileDiffEntry renamedEntry(const QString& path, const QString& oldPath)
{
    return entry(path, Action::Replaced, u"1"_s, u"1"_s, oldPath);
}

/**
 * @param path A file.
 * @returns It modified, with one line changed.
 */
FileDiffEntry modifiedEntry(const QString& path)
{
    return entry(path, Action::Modified, u"1"_s, u"1"_s);
}

/**
 * @param text Paths separated by spaces.
 * @returns The paths.
 */
QStringList names(const QString& text)
{
    return text.split(QLatin1Char(' '));
}

/**
 * @param entries Entries.
 * @returns Their paths, in order.
 */
QStringList pathsOf(const QList<FileDiffEntry>& entries)
{
    QStringList paths;
    for (const FileDiffEntry& listEntry : entries) {
        paths.append(listEntry.path);
    }
    return paths;
}

/**
 * @param entries Entries.
 * @param column The column to sort by, or -1.
 * @param ascending The order.
 * @returns Their paths after sorting.
 */
QStringList sortedPaths(QList<FileDiffEntry> entries, int column, bool ascending)
{
    Model::sort(entries, column, ascending);
    return pathsOf(entries);
}

/**
 * @param arguments What comes after "git difftool --no-prompt".
 * @returns The whole command line after "git".
 */
QStringList difftool(const QStringList& arguments)
{
    const QStringList start{u"--literal-pathspecs"_s, u"difftool"_s, u"--no-prompt"_s};
    return start + arguments;
}

/**
 * @param dlg A dialog that has started loading.
 * @returns Whether its list finished loading in time.
 */
bool waitForList(const FileDiffDlg& dlg)
{
    return QTest::qWaitFor(
        [&dlg] {
            return !dlg.isLoading();
        },
        timeout);
}

/**
 * @returns Five lines that stay the same, so git sees a rename.
 */
QByteArray commonLines()
{
    return "the same line\nthe same line\nthe same line\nthe same line\nthe same line\n";
}

} // namespace

class FileDiffDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
    }

    void sortsByPathUntilAColumnIsChosen()
    {
        // TortoiseGit sorts with no column too, which leaves the path to decide
        const QList<FileDiffEntry> entries{
            entry(u"b.txt"_s, Action::Modified, u"1"_s, u"1"_s),
            entry(u"a/z.txt"_s, Action::Added, u"3"_s, u"0"_s),
            entry(u"a.txt"_s, Action::Deleted, u"0"_s, u"2"_s),
        };
        const QStringList byPath{u"a.txt"_s, u"a/z.txt"_s, u"b.txt"_s};
        QCOMPARE(sortedPaths(entries, -1, true), byPath);
        QCOMPARE(sortedPaths(entries, Model::FileColumn, true), byPath);
        const QStringList reversed{u"b.txt"_s, u"a/z.txt"_s, u"a.txt"_s};
        QCOMPARE(sortedPaths(entries, Model::FileColumn, false), reversed);
    }

    void sortsByColumnThenPath()
    {
        const QList<FileDiffEntry> entries{
            entry(u"c.cpp"_s, Action::Modified, u"10"_s, u"1"_s),
            entry(u"b.png"_s, Action::Added, u"-"_s, u"-"_s),
            entry(u"a.cpp"_s, Action::Deleted, u"9"_s, u"5"_s),
            entry(u"d.cpp"_s, Action::Added, u"9"_s, u"0"_s),
        };
        // Equal extensions fall back on the path
        QCOMPARE(sortedPaths(entries, Model::ExtensionColumn, true), names(u"a.cpp c.cpp d.cpp b.png"_s));
        // By TortoiseGit's action value: added, modified, deleted
        QCOMPARE(sortedPaths(entries, Model::ActionColumn, true), names(u"b.png d.cpp c.cpp a.cpp"_s));
        // Line counts sort as numbers, and a binary file's "-" as nothing
        QCOMPARE(sortedPaths(entries, Model::LinesAddedColumn, true), names(u"b.png a.cpp d.cpp c.cpp"_s));
        // Descending reverses the tie-break too
        QCOMPARE(sortedPaths(entries, Model::LinesAddedColumn, false), names(u"c.cpp d.cpp a.cpp b.png"_s));
    }

    void filtersPathsAndRenameSources()
    {
        QList<FileDiffEntry> entries{
            entry(u"src/main.cpp"_s, Action::Modified, u"1"_s, u"1"_s),
            entry(u"docs/new.md"_s, Action::Replaced, u"0"_s, u"0"_s, u"src/old.md"_s),
            entry(u"README"_s, Action::Modified, u"2"_s, u"0"_s),
        };
        QCOMPARE(pathsOf(Model::filtered(entries, {})), pathsOf(entries));
        // A rename matches on the path it came from, and case doesn't matter
        QCOMPARE(pathsOf(Model::filtered(entries, u"SRC"_s)), names(u"src/main.cpp docs/new.md"_s));
        QCOMPARE(pathsOf(Model::filtered(entries, u"src -md"_s)), QStringList{u"src/main.cpp"_s});
        QCOMPARE(pathsOf(Model::filtered(entries, u"!src"_s)), QStringList{u"README"_s});

        // Without line counts all its changes were ignored, unless it is a submodule
        entries.append(entry(u"spaces.txt"_s, Action::Modified, {}, {}));
        FileDiffEntry submodule = entry(u"lib"_s, Action::Modified, {}, {});
        submodule.isDirectory = true;
        entries.append(submodule);
        QCOMPARE(pathsOf(Model::filtered(entries, {})), names(u"src/main.cpp docs/new.md README lib"_s));
    }

    void namesRenamedFiles()
    {
        const FileDiffEntry renamed = renamedEntry(u"new.txt"_s, u"old.txt"_s);
        QCOMPARE(Model::fileName(renamed), u"new.txt (from old.txt)"_s);
        QCOMPARE(Model::fileExtension(renamed), u".txt"_s);
        const FileDiffEntry plain = entry(u"dir.d/Makefile"_s, Action::Modified, u"1"_s, u"1"_s);
        QCOMPARE(Model::fileName(plain), u"dir.d/Makefile"_s);
        QCOMPARE(Model::fileExtension(plain), QString());
    }

    void recognisesTheWorkingTree()
    {
        QVERIFY(FileDiffDlg::isWorkingCopyRef(FileDiffDlg::workingCopyRef()));
        QVERIFY(FileDiffDlg::isWorkingCopyRef(u"00000000"_s));
        QVERIFY(!FileDiffDlg::isWorkingCopyRef(u"0000000"_s));
        QVERIFY(!FileDiffDlg::isWorkingCopyRef(FileDiffDlg::workingCopyRef() + QLatin1Char('0')));
        QVERIFY(!FileDiffDlg::isWorkingCopyRef(u"00000000a"_s));
    }

    void diffsEachKindOfFile()
    {
        const QString base = u"1111111"_s;
        const QString second = u"2222222"_s;
        const QString dashes = u"--"_s;
        const FileDiffEntry modified = modifiedEntry(u"a.txt"_s);
        const FileDiffEntry added = entry(u"a.txt"_s, Action::Added, u"1"_s, u"0"_s);
        const FileDiffEntry renamed = renamedEntry(u"new.txt"_s, u"old.txt"_s);

        const auto diff = [](const FileDiffEntry& file, const QString& rev1, const QString& rev2, bool unborn) {
            return FileDiffDlg::diffArguments(file, {.rev1 = rev1, .rev2 = rev2, .options = {}}, unborn);
        };
        // The base on the left, version 2 on the right
        QCOMPARE(diff(modified, base, second, false), difftool({base, second, dashes, u"a.txt"_s}));
        QCOMPARE(diff(renamed, base, second, false), difftool({base + u":old.txt"_s, second + u":new.txt"_s}));
        // Version 2 is the working tree
        QCOMPARE(diff(modified, base, {}, false), difftool({base, dashes, u"a.txt"_s}));
        QCOMPARE(diff(renamed, base, {}, false), difftool({base + u":old.txt"_s, dashes, u"new.txt"_s}));
        QCOMPARE(diff(added, base, {}, false), difftool({u"--no-index"_s, dashes, u"/dev/null"_s, u"a.txt"_s}));
        QCOMPARE(diff(modified, base, {}, true), difftool({u"--no-index"_s, dashes, u"/dev/null"_s, u"a.txt"_s}));
        // The base is the working tree: reversed, except a rename, which git difftool can't reverse
        QCOMPARE(diff(modified, {}, second, false), difftool({u"-R"_s, second, dashes, u"a.txt"_s}));
        QCOMPARE(diff(renamed, {}, second, false), difftool({second + u":new.txt"_s, dashes, u"old.txt"_s}));
    }

    void loadsRenamesBetweenCommits()
    {
        const TestRepo repo;
        repo.write(u"old.txt"_s, commonLines() + "one\n");
        repo.write(u"dir/b.txt"_s, "b\n");
        repo.commitAll(u"Add"_s);
        repo.run({u"mv"_s, u"old.txt"_s, u"new.txt"_s});
        repo.write(u"new.txt"_s, commonLines() + "two\n");
        repo.write(u"dir/b.txt"_s, "b2\n");
        repo.commitAll(u"Rename"_s);

        FileDiffDlg dlg(repo.path());
        dlg.setDiff({}, u"HEAD~1"_s, u"HEAD"_s);
        QVERIFY(waitForList(dlg));
        QCOMPARE(dlg.rev2().subject, u"Rename"_s);
        const FileDiffEntry rename = renamedEntry(u"new.txt"_s, u"old.txt"_s);
        const FileDiffEntry folderFile = modifiedEntry(u"dir/b.txt"_s);
        QCOMPARE(dlg.listModel()->entries(), (QList<FileDiffEntry>{folderFile, rename}));

        // Switching the sides turns the rename round
        dlg.switchLeftRight();
        QVERIFY(waitForList(dlg));
        QCOMPARE(dlg.rev1().subject, u"Rename"_s);
        const FileDiffEntry back = renamedEntry(u"old.txt"_s, u"new.txt"_s);
        QCOMPARE(dlg.listModel()->entries(), (QList<FileDiffEntry>{folderFile, back}));

        // A folder given to SetDiff only filters the list
        FileDiffDlg folder(repo.path());
        folder.setDiff(u"dir"_s, u"HEAD~1"_s, u"HEAD"_s);
        QVERIFY(waitForList(folder));
        QCOMPARE(folder.allEntries().size(), 2);
        QCOMPARE(folder.listModel()->entries(), QList<FileDiffEntry>{folderFile});
    }

    void loadsTheWorkingTreeOnEitherSide()
    {
        const TestRepo repo;
        repo.write(u"a.txt"_s, "one\n");
        repo.write(u"gone.txt"_s, "gone\n");
        repo.commitAll(u"First"_s);
        repo.write(u"a.txt"_s, "two\nthree\n");
        repo.remove(u"gone.txt"_s);
        repo.write(u"staged.txt"_s, "new\n");
        repo.run({u"add"_s, u"staged.txt"_s});

        FileDiffDlg dlg(repo.path());
        dlg.setDiff({}, u"HEAD"_s, FileDiffDlg::workingCopyRef());
        QVERIFY(waitForList(dlg));
        QVERIFY(dlg.rev2().hash.isEmpty());
        QCOMPARE(dlg.rev2().subject, u"Working Tree"_s);
        const QList<FileDiffEntry> toWorkingTree{
            entry(u"a.txt"_s, Action::Modified, u"2"_s, u"1"_s),
            entry(u"gone.txt"_s, Action::Deleted, u"0"_s, u"1"_s),
            entry(u"staged.txt"_s, Action::Added, u"1"_s, u"0"_s),
        };
        QCOMPARE(dlg.listModel()->entries(), toWorkingTree);

        dlg.switchLeftRight();
        QVERIFY(waitForList(dlg));
        const QList<FileDiffEntry> fromWorkingTree{
            entry(u"a.txt"_s, Action::Modified, u"1"_s, u"2"_s),
            entry(u"gone.txt"_s, Action::Added, u"1"_s, u"0"_s),
            entry(u"staged.txt"_s, Action::Deleted, u"0"_s, u"1"_s),
        };
        QCOMPARE(dlg.listModel()->entries(), fromWorkingTree);
    }

    void reportsAnInvalidVersion()
    {
        const TestRepo repo;
        repo.write(u"a.txt"_s, "one\n");
        repo.commitAll(u"First"_s);

        FileDiffDlg dlg(repo.path());
        dlg.setDiff({}, u"no-such-branch"_s, u"HEAD"_s);
        QVERIFY(!dlg.isLoading());
        QVERIFY(dlg.hintText().startsWith(u"\"no-such-branch\" is invalid."_s));
        QCOMPARE(dlg.listModel()->rowCount(), 0);

        // Typing a valid one loads the list
        dlg.revEdit(0)->setText(u"HEAD"_s);
        Q_EMIT dlg.revEdit(0)->returnPressed();
        QVERIFY(waitForList(dlg));
        QCOMPARE(dlg.hintText(), u"No differences found!"_s);
    }
};

QTEST_MAIN(FileDiffDlgTest)

#include "filediffdlgtest.moc"
