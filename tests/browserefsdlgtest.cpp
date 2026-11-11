// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "browserefsdlg.hpp"
#include "apputils.hpp"
#include "deleteremotetagdlg.hpp"
#include "progressdlg.hpp"
#include "pullfetchdlg.hpp"
#include "testrepo.hpp"

#include "utils/runtimeerror.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <algorithm>
#include <functional>
#include <memory>

namespace
{

using namespace kgt;

constexpr int waitTimeout = 10000;
constexpr int pollInterval = 20;

/**
 * Calls @p handle with the next modal dialog of type T once it is shown, polling from the event loop.
 * @param handle What to do with it.
 * @param ready Whether the dialog is ready to be handled; by default as soon as it shows.
 */
template<typename T>
void whenModal(std::function<void(T *)> handle, std::function<bool(T *)> ready = {})
{
    auto *timer = new QTimer(QCoreApplication::instance());
    QObject::connect(timer, &QTimer::timeout, timer, [timer, handle, ready, elapsed = 0]() mutable {
        elapsed += pollInterval;
        auto *dialog = qobject_cast<T *>(QApplication::activeModalWidget());
        if (dialog && (!ready || ready(dialog))) {
            timer->stop();
            timer->deleteLater();
            handle(dialog);
        } else if (elapsed > waitTimeout) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(pollInterval);
}

/**
 * Answers the next message box with Yes.
 * @param[out] text Receives the message's text.
 */
void answerYes(QString *text)
{
    whenModal<QDialog>([text](QDialog *box) {
        for (const QLabel *label : box->findChildren<QLabel *>()) {
            *text += label->text();
        }
        const auto *buttons = box->findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        buttons->button(QDialogButtonBox::Yes)->click();
    });
}

/**
 * @param repo A repository.
 * @param ref A ref.
 * @returns Whether it exists.
 */
bool refExists(const TestRepo& repo, const QString& ref)
{
    try {
        std::ignore = repo.git().run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"), ref});
        return true;
    } catch (const RuntimeError&) {
        return false;
    }
}

/**
 * @param model The list model.
 * @param column A column.
 * @returns The column's texts, row by row.
 */
QStringList columnTexts(const RefLeafListModel *model, int column)
{
    QStringList texts;
    for (int row = 0; row < model->rowCount(); ++row) {
        texts.append(model->index(row, column).data().toString());
    }
    return texts;
}

/**
 * @param model The list model.
 * @param name A Branch Name cell's text.
 * @returns Its row, or -1.
 */
int rowOf(const RefLeafListModel *model, const QString& name)
{
    return static_cast<int>(columnTexts(model, RefLeafListModel::NameColumn).indexOf(name));
}

/**
 * @param menu A menu.
 * @param text An action's text.
 * @returns The action, or nullptr.
 */
QAction *findAction(const QMenu& menu, const QString& text)
{
    for (QAction *action : menu.actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

/**
 * @param menu A menu.
 * @returns Its entries' texts, without separators.
 */
QStringList actionTexts(const QMenu& menu)
{
    QStringList texts;
    for (const QAction *action : menu.actions()) {
        if (!action->isSeparator()) {
            texts.append(action->text());
        }
    }
    return texts;
}

/**
 * @param repo A repository.
 * @param key A config key.
 * @returns Its value, or an empty string if it isn't set.
 */
QString configValue(const TestRepo& repo, const QString& key)
{
    return repo.git().configValue(key);
}

/**
 * Waits for the dialog's refs to load.
 * @param dlg The dialog.
 * @returns Whether they loaded in time.
 */
bool waitLoaded(const BrowseRefsDlg& dlg)
{
    QSignalSpy spy(&dlg, &BrowseRefsDlg::refreshed);
    return spy.wait(waitTimeout);
}

/**
 * Selects the second tag in the dialog, deletes it and closes the dialog once the list has been loaded again.
 * @param tagDlg The dialog, its tags loaded.
 */
void deleteSecondTag(const DeleteRemoteTagDlg *tagDlg)
{
    tagDlg->tagList()->selectionModel()->select(tagDlg->tagList()->model()->index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(tagDlg->selectAllBox()->checkState(), Qt::PartiallyChecked);
    // The confirmation's buttons are Delete and Abort
    whenModal<QDialog>(
        [](QDialog *box) {
            const auto *buttons = box->findChild<QDialogButtonBox *>();
            QVERIFY(buttons);
            const QList<QAbstractButton *> all = buttons->buttons();
            const auto deleteButton = std::ranges::find(all, QStringLiteral("&Delete"), &QAbstractButton::text);
            QVERIFY(deleteButton != all.end());
            (*deleteButton)->click();
        },
        [](QDialog *box) {
            return !qobject_cast<DeleteRemoteTagDlg *>(box) && !qobject_cast<ProgressDlg *>(box);
        });
    whenModal<ProgressDlg>(
        [](ProgressDlg *progress) {
            progress->accept();
        },
        [](const ProgressDlg *progress) {
            return progress->isDone();
        });
    QObject::connect(tagDlg, &DeleteRemoteTagDlg::refreshed, tagDlg, &QDialog::reject);
    tagDlg->deleteButton()->click();
}

} // namespace

/**
 * BrowseRefsDlg on a repository with local branches, a remote with branches, and both kinds of tag.
 */
class BrowseRefsDlgTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        TestRepo::isolateGit();
        m_upstream = std::make_unique<TestRepo>();
        m_upstream->write(QStringLiteral("a.txt"), "a\n");
        m_upstream->commitAll(QStringLiteral("initial"));
        m_upstream->run({QStringLiteral("branch"), QStringLiteral("topic")});
    }

    void init()
    {
        m_repo = std::make_unique<TestRepo>();
        const TestRepo& repo = *m_repo;
        repo.run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), m_upstream->path()});
        repo.run({QStringLiteral("fetch"), QStringLiteral("--quiet"), QStringLiteral("origin")});
        repo.run({QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("--quiet"), QStringLiteral("origin/main")});
        repo.run({QStringLiteral("branch"), QStringLiteral("--quiet"), QStringLiteral("--set-upstream-to=origin/main"), QStringLiteral("main")});
        repo.run({QStringLiteral("branch"), QStringLiteral("merged")});
        repo.run({QStringLiteral("branch"), QStringLiteral("feature/x")});
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("-b"), QStringLiteral("other")});
        repo.write(QStringLiteral("b.txt"), "b\n");
        repo.commitAll(QStringLiteral("other work"));
        repo.run({QStringLiteral("checkout"), QStringLiteral("--quiet"), QStringLiteral("main")});
        repo.run({QStringLiteral("config"), QStringLiteral("branch.other.description"), QStringLiteral("Other\nwork")});
        repo.run({QStringLiteral("tag"), QStringLiteral("v1")});
        repo.run({QStringLiteral("tag"), QStringLiteral("-a"), QStringLiteral("v2"), QStringLiteral("-m"), QStringLiteral("Release 2")});
    }

    void cleanup()
    {
        m_repo.reset();
    }

    void treeAndList()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));

        // The tree: refs, then heads (with feature), remotes/origin and tags
        const auto *treeModel = qobject_cast<QStandardItemModel *>(dlg.refTree()->model());
        QVERIFY(treeModel);
        const QStandardItem *root = treeModel->item(0);
        QCOMPARE(root->text(), QStringLiteral("refs"));
        QStringList top;
        for (int row = 0; row < root->rowCount(); ++row) {
            top.append(root->child(row)->text());
        }
        QCOMPARE(top, (QStringList{QStringLiteral("heads"), QStringLiteral("remotes"), QStringLiteral("tags")}));
        QCOMPARE(root->child(0)->child(0)->text(), QStringLiteral("feature"));
        QCOMPARE(root->child(1)->child(0)->text(), QStringLiteral("origin"));

        // HEAD: refs/heads is selected with main in it, which is bold
        const RefLeafListModel *model = dlg.listModel();
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn),
                 (QStringList{QStringLiteral("feature/x"), QStringLiteral("main"), QStringLiteral("merged"), QStringLiteral("other")}));
        QCOMPARE(dlg.selectedRef(true, false), QStringLiteral("refs/heads/main"));
        const int mainRow = rowOf(model, QStringLiteral("main"));
        QVERIFY(model->index(mainRow, 0).data(Qt::FontRole).value<QFont>().bold());
        QVERIFY(!model->index(rowOf(model, QStringLiteral("other")), 0).data(Qt::FontRole).value<QFont>().bold());
        QCOMPARE(dlg.infoText(), QStringLiteral("Showing 4 ref(s), 1 ref(s) selected"));

        // The columns
        const QString mainHash = QString::fromUtf8(m_repo->git().run({QStringLiteral("rev-parse"), QStringLiteral("main")})).trimmed();
        QCOMPARE(model->index(mainRow, RefLeafListModel::UpstreamColumn).data().toString(), QStringLiteral("origin/main"));
        QCOMPARE(model->index(mainRow, RefLeafListModel::MsgColumn).data().toString(), QStringLiteral("initial"));
        QCOMPARE(model->index(mainRow, RefLeafListModel::LastAuthorColumn).data().toString(), QStringLiteral("Test"));
        QCOMPARE(model->index(mainRow, RefLeafListModel::LastCommitterColumn).data().toString(), QStringLiteral("Test"));
        QCOMPARE(model->index(mainRow, RefLeafListModel::HashColumn).data().toString(), mainHash);
        QVERIFY(!model->index(mainRow, RefLeafListModel::AuthorDateColumn).data().toString().isEmpty());
        QVERIFY(!model->index(mainRow, RefLeafListModel::CommitterDateColumn).data().toString().isEmpty());
        QCOMPARE(model->index(rowOf(model, QStringLiteral("other")), RefLeafListModel::DescriptionColumn).data().toString(), QStringLiteral("Other work"));
        QCOMPARE(model->headerData(RefLeafListModel::NameColumn, Qt::Horizontal).toString(), QStringLiteral("Branch Name"));
        QCOMPARE(model->headerData(RefLeafListModel::HashColumn, Qt::Horizontal).toString(), QStringLiteral("SHA-1"));
        QVERIFY(!dlg.refList()->isColumnHidden(RefLeafListModel::UpstreamColumn));

        // Sorting by the subject puts "other work" after "initial"
        dlg.refList()->sortByColumn(RefLeafListModel::MsgColumn, Qt::DescendingOrder);
        QCOMPARE(model->index(0, RefLeafListModel::NameColumn).data().toString(), QStringLiteral("other"));
        dlg.refList()->sortByColumn(RefLeafListModel::NameColumn, Qt::AscendingOrder);

        // Tags: the annotated one has its own subject; Tracked branch and Description are hidden
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags"), true));
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), (QStringList{QStringLiteral("v1"), QStringLiteral("v2")}));
        QCOMPARE(columnTexts(model, RefLeafListModel::MsgColumn), (QStringList{QStringLiteral("initial"), QStringLiteral("Release 2")}));
        QCOMPARE(model->entry(1).tree->ref.objectType, GitObjectType::Tag);
        QCOMPARE(model->index(1, RefLeafListModel::LastAuthorColumn).data().toString(), QStringLiteral("Test"));
        QVERIFY(dlg.refList()->isColumnHidden(RefLeafListModel::UpstreamColumn));
        QVERIFY(dlg.refList()->isColumnHidden(RefLeafListModel::DescriptionColumn));

        // The remote's branches, and the root with everything below it
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes/origin"), true));
        QStringList remoteBranches = columnTexts(model, RefLeafListModel::NameColumn);
        // Newer git also creates origin/HEAD on fetch
        remoteBranches.removeOne(QStringLiteral("HEAD"));
        QCOMPARE(remoteBranches, (QStringList{QStringLiteral("main"), QStringLiteral("topic")}));
        const int remoteCount = model->rowCount();
        QVERIFY(dlg.selectRef(QStringLiteral("refs"), true));
        QCOMPARE(model->rowCount(), 4 + remoteCount + 2);
        QVERIFY(columnTexts(model, RefLeafListModel::NameColumn).contains(QStringLiteral("remotes/origin/topic")));

        // A partial name finds the leaf
        QCOMPARE(dlg.fullRefName(QStringLiteral("origin/topic")), QStringLiteral("refs/remotes/origin/topic"));
        QVERIFY(dlg.selectRef(QStringLiteral("v2"), false));
        QCOMPARE(dlg.selectedRef(true, false), QStringLiteral("refs/tags/v2"));

        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/main"), true));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        const QPixmap screenshot = dlg.grab();
        QVERIFY(!screenshot.isNull());
        if (const QString screenshotDir = qEnvironmentVariable("KGT_SCREENSHOT_DIR"); !screenshotDir.isEmpty()) {
            QVERIFY(screenshot.save(QDir(screenshotDir).absoluteFilePath(QStringLiteral("browserefsdlg.png"))));
        }
    }

    void filter()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        const RefLeafListModel *model = dlg.listModel();

        dlg.setFilter(QStringLiteral("FEAT"), LogFilterAll);
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), QStringList{QStringLiteral("feature/x")});
        // Negated
        dlg.setFilter(QStringLiteral("!feat"), LogFilterAll);
        QCOMPARE(model->rowCount(), 3);
        // Words must all match; "-" excludes; "+" is an alternative
        dlg.setFilter(QStringLiteral("m -merged"), LogFilterRefName);
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), QStringList{QStringLiteral("main")});
        dlg.setFilter(QStringLiteral("merged +feature"), LogFilterRefName);
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), (QStringList{QStringLiteral("feature/x"), QStringLiteral("merged")}));
        // Only the subject
        dlg.setFilter(QStringLiteral("work"), LogFilterSubject);
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), QStringList{QStringLiteral("other")});
        dlg.setFilter(QStringLiteral("other"), LogFilterSubject);
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), QStringList{QStringLiteral("other")});
        dlg.setFilter(QStringLiteral("feature"), LogFilterSubject);
        QCOMPARE(model->rowCount(), 0);
        // The SHA-1
        const QString otherHash = QString::fromUtf8(m_repo->git().run({QStringLiteral("rev-parse"), QStringLiteral("other")})).trimmed();
        dlg.setFilter(otherHash.left(10), LogFilterRevs);
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), QStringList{QStringLiteral("other")});
        // A quoted word
        dlg.setFilter(QStringLiteral("\"release 2\""), LogFilterSubject);
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags"), true));
        QCOMPARE(columnTexts(model, RefLeafListModel::NameColumn), QStringList{QStringLiteral("v2")});
        dlg.setFilter({}, LogFilterAll);
        QCOMPARE(model->rowCount(), 2);
    }

    void deleteBranchAndTag()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));

        // An unmerged branch: TortoiseGit warns
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/other"), true));
        std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        QVERIFY(findAction(*menu, QStringLiteral("Show &log")));
        QVERIFY(findAction(*menu, QStringLiteral("Copy ref names")));
        QVERIFY(findAction(*menu, QStringLiteral("Copy SHA-1")));
        QAction *deleteBranch = findAction(*menu, QStringLiteral("Delete branch"));
        QVERIFY(deleteBranch);
        QString question;
        answerYes(&question);
        deleteBranch->trigger();
        QVERIFY(question.contains(QStringLiteral("Do you really want to delete \"other\"?")));
        QVERIFY(question.contains(QStringLiteral("This branch is not fully merged into HEAD.")));
        QVERIFY(waitLoaded(dlg));
        QVERIFY(!refExists(*m_repo, QStringLiteral("refs/heads/other")));
        QVERIFY(rowOf(dlg.listModel(), QStringLiteral("other")) < 0);

        // A merged branch: no warning
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/merged"), true));
        menu = dlg.createListContextMenu();
        question.clear();
        answerYes(&question);
        findAction(*menu, QStringLiteral("Delete branch"))->trigger();
        QVERIFY(!question.contains(QStringLiteral("not fully merged")));
        QVERIFY(waitLoaded(dlg));
        QVERIFY(!refExists(*m_repo, QStringLiteral("refs/heads/merged")));

        // A tag
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags/v1"), true));
        menu = dlg.createListContextMenu();
        QVERIFY(!findAction(*menu, QStringLiteral("Delete branch")));
        question.clear();
        answerYes(&question);
        findAction(*menu, QStringLiteral("Delete tag"))->trigger();
        QVERIFY(question.contains(QStringLiteral("Do you really want to delete \"v1\"?")));
        QVERIFY(waitLoaded(dlg));
        QVERIFY(!refExists(*m_repo, QStringLiteral("refs/tags/v1")));
        QVERIFY(refExists(*m_repo, QStringLiteral("refs/tags/v2")));

        // A remote branch is deleted on the remote, one push per remote
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes/origin/topic"), true));
        menu = dlg.createListContextMenu();
        QVERIFY(findAction(*menu, QStringLiteral("Delete remote branch")));
        QCOMPARE(Git::deleteRemoteRefsArguments(QStringLiteral("origin"), {QStringLiteral("refs/heads/topic"), QStringLiteral("refs/heads/a/b")}),
                 (QStringList{QStringLiteral("push"),
                              QStringLiteral("--progress"),
                              QStringLiteral("--"),
                              QStringLiteral("origin"),
                              QStringLiteral(":refs/heads/topic"),
                              QStringLiteral(":refs/heads/a/b")}));
    }

    void splitRemoteBranchName()
    {
        const QStringList remotes{QStringLiteral("origin"), QStringLiteral("up")};
        const std::optional<RemoteBranch> split = BrowseRefsDlg::splitRemoteBranchName(QStringLiteral("refs/remotes/up/feature/x"), remotes);
        QCOMPARE(split.value_or(RemoteBranch{}).remote, QStringLiteral("up"));
        QCOMPARE(split.value_or(RemoteBranch{}).branch, QStringLiteral("feature/x"));
        QVERIFY(!BrowseRefsDlg::splitRemoteBranchName(QStringLiteral("refs/remotes/other/x"), remotes));
    }

    void pickRef()
    {
        // The picker returns the selected ref's full name
        whenModal<BrowseRefsDlg>(
            [](BrowseRefsDlg *dlg) {
                QCOMPARE(dlg->selectedRef(true, false), QStringLiteral("refs/tags/v2"));
                QVERIFY(dlg->selectRef(QStringLiteral("refs/heads/merged"), true));
                dlg->accept();
            },
            [](BrowseRefsDlg *dlg) {
                return !dlg->isLoading() && dlg->listModel()->rowCount() > 0;
            });
        QCOMPARE(BrowseRefsDlg::pickRef(m_repo->path(), {.initialRef = QStringLiteral("v2"), .pickRefKind = PickRefAll, .pickMultipleRefs = false}),
                 QStringLiteral("refs/heads/merged"));

        // Cancelled
        whenModal<BrowseRefsDlg>(
            [](BrowseRefsDlg *dlg) {
                dlg->reject();
            },
            [](BrowseRefsDlg *dlg) {
                return !dlg->isLoading() && dlg->listModel()->rowCount() > 0;
            });
        QVERIFY(BrowseRefsDlg::pickRef(m_repo->path()).isEmpty());

        // Only remote branches; Current Branch picks the current branch
        BrowseRefsDlg dlg(m_repo->path());
        dlg.setPickMode({.initialRef = QStringLiteral("HEAD"), .pickRefKind = PickRefRemote, .pickMultipleRefs = false});
        QVERIFY(waitLoaded(dlg));
        const auto *treeModel = qobject_cast<QStandardItemModel *>(dlg.refTree()->model());
        QCOMPARE(treeModel->item(0)->rowCount(), 1);
        QCOMPARE(treeModel->item(0)->child(0)->text(), QStringLiteral("remotes"));
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes/origin/topic"), true));
        std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        QCOMPARE(menu->actions().constFirst()->text(), QStringLiteral("Select"));
        for (QPushButton *button : dlg.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Current Branch")) {
                button->click();
            }
        }
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.pickedRef(), QStringLiteral("main"));
    }

    void singleRefMenu()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));

        // The current branch, with an upstream: no merge into itself
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/main"), true));
        std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        QCOMPARE(actionTexts(*menu),
                 (QStringList{QStringLiteral("Show &log"),
                              QStringLiteral("Show &RefLog"),
                              QStringLiteral("Compare with &working tree"),
                              QStringLiteral("Sw&itch/Checkout to this..."),
                              QStringLiteral("Edit description"),
                              QStringLiteral("Rename"),
                              QStringLiteral("Unset tracked branch"),
                              QStringLiteral("Select tracked branch"),
                              QStringLiteral("Delete branch"),
                              QStringLiteral("Copy ref names"),
                              QStringLiteral("Copy SHA-1")}));
        QCOMPARE(menu->defaultAction()->text(), QStringLiteral("Show &log"));

        // Another branch without an upstream can be merged into the current one
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/other"), true));
        menu = dlg.createListContextMenu();
        QVERIFY(findAction(*menu, QStringLiteral("&Merge to \"main\"...")));
        QVERIFY(!findAction(*menu, QStringLiteral("Unset tracked branch")));
        QVERIFY(findAction(*menu, QStringLiteral("Select tracked branch")));

        // A remote branch: fetch from its remote and create a branch, but no rename or description
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes/origin/topic"), true));
        menu = dlg.createListContextMenu();
        QCOMPARE(actionTexts(*menu),
                 (QStringList{QStringLiteral("Show &log"),
                              QStringLiteral("Show &RefLog"),
                              QStringLiteral("Compare with &working tree"),
                              QStringLiteral("Fetch from \"origin\""),
                              QStringLiteral("&Merge to \"main\"..."),
                              QStringLiteral("Sw&itch/Checkout to this..."),
                              QStringLiteral("Create &Branch..."),
                              QStringLiteral("Delete remote branch"),
                              QStringLiteral("Copy ref names"),
                              QStringLiteral("Copy SHA-1")}));

        // An annotated tag isn't a commit: only the delete and copies
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags/v2"), true));
        menu = dlg.createListContextMenu();
        QCOMPARE(actionTexts(*menu), (QStringList{QStringLiteral("Delete tag"), QStringLiteral("Copy ref names"), QStringLiteral("Copy SHA-1")}));
        // A lightweight one is
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags/v1"), true));
        menu = dlg.createListContextMenu();
        QVERIFY(findAction(*menu, QStringLiteral("Show &log")));
        QVERIFY(findAction(*menu, QStringLiteral("Sw&itch/Checkout to this...")));
        QVERIFY(!findAction(*menu, QStringLiteral("Show &RefLog")));
        QVERIFY(!findAction(*menu, QStringLiteral("Rename")));
    }

    void twoRefMenu()
    {
        QCOMPARE(BrowseRefsDlg::twoSelectedRefs({QStringLiteral("refs/heads/a"), QStringLiteral("refs/heads/b")},
                                                QStringLiteral("refs/heads/a"),
                                                QStringLiteral("..")),
                 QStringLiteral("b..a"));
        QCOMPARE(BrowseRefsDlg::twoSelectedRefs({QStringLiteral("refs/heads/a"), QStringLiteral("refs/tags/v")},
                                                QStringLiteral("refs/tags/v"),
                                                QStringLiteral("...")),
                 QStringLiteral("a...tags/v"));

        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        // Select other, then main: main was selected last and goes second
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/other"), true));
        const RefLeafListModel *model = dlg.listModel();
        dlg.refList()->selectionModel()->select(model->index(rowOf(model, QStringLiteral("main")), 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        QCOMPARE(actionTexts(*menu),
                 (QStringList{QStringLiteral("Compare selected refs"),
                              QStringLiteral("Show changes as &unified diff"),
                              QStringLiteral("Show log of other..main"),
                              QStringLiteral("Show log of other...main"),
                              QStringLiteral("Delete 2 branches"),
                              QStringLiteral("Copy ref names"),
                              QStringLiteral("Copy SHA-1")}));
    }

    void unifiedDiffArguments()
    {
        QCOMPARE(AppUtils::unifiedDiffArguments(QStringLiteral("a"), QStringLiteral("b"), -1),
                 (QStringList{QStringLiteral("diff-tree"),
                              QStringLiteral("-r"),
                              QStringLiteral("-p"),
                              QStringLiteral("--stat"),
                              QStringLiteral("--end-of-options"),
                              QStringLiteral("a"),
                              QStringLiteral("b"),
                              QStringLiteral("--")}));
        QVERIFY(AppUtils::unifiedDiffArguments(QStringLiteral("a"), QStringLiteral("b"), 5).contains(QStringLiteral("--unified=5")));
        // The command runs
        const QString diff = QString::fromUtf8(m_repo->git().run(AppUtils::unifiedDiffArguments(QStringLiteral("main"), QStringLiteral("other"), -1)));
        QVERIFY(diff.contains(QStringLiteral("+++ b/b.txt")));
    }

    void rename()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        RefLeafListModel *model = dlg.listModel();

        // Only branch names are editable
        const int featureRow = rowOf(model, QStringLiteral("feature/x"));
        QVERIFY(model->flags(model->index(featureRow, RefLeafListModel::NameColumn)).testFlag(Qt::ItemIsEditable));
        QVERIFY(!model->flags(model->index(featureRow, RefLeafListModel::MsgColumn)).testFlag(Qt::ItemIsEditable));

        // Rename edits the name in place, relative to the tree node the list shows: refs/heads/feature
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/feature/x"), true));
        const std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        findAction(*menu, QStringLiteral("Rename"))->trigger();
        const auto *editor = dlg.refList()->viewport()->findChild<QLineEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->text(), QStringLiteral("x"));

        // Finishing the edit runs git branch -m and reloads, with the renamed branch selected
        QVERIFY(model->setData(dlg.refList()->currentIndex(), QStringLiteral("y")) == false);
        QVERIFY(waitLoaded(dlg));
        QVERIFY(!refExists(*m_repo, QStringLiteral("refs/heads/feature/x")));
        QVERIFY(refExists(*m_repo, QStringLiteral("refs/heads/feature/y")));
        QCOMPARE(dlg.selectedRef(true, false), QStringLiteral("refs/heads/feature/y"));

        // Tags can't be renamed
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags/v1"), true));
        QVERIFY(!model->flags(model->index(0, RefLeafListModel::NameColumn)).testFlag(Qt::ItemIsEditable));
    }

    void trackedBranch()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));

        // Unset tracked branch drops branch.main.remote and .merge
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/main"), true));
        std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        findAction(*menu, QStringLiteral("Unset tracked branch"))->trigger();
        QVERIFY(waitLoaded(dlg));
        QVERIFY(configValue(*m_repo, QStringLiteral("branch.main.remote")).isEmpty());
        QVERIFY(configValue(*m_repo, QStringLiteral("branch.main.merge")).isEmpty());
        QCOMPARE(dlg.listModel()->index(rowOf(dlg.listModel(), QStringLiteral("main")), RefLeafListModel::UpstreamColumn).data().toString(), QString());

        // Select tracked branch picks a remote branch from a picker that only lists them
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/main"), true));
        menu = dlg.createListContextMenu();
        whenModal<BrowseRefsDlg>(
            [](BrowseRefsDlg *picker) {
                QVERIFY(picker->selectRef(QStringLiteral("refs/remotes/origin/topic"), true));
                picker->accept();
            },
            [](BrowseRefsDlg *picker) {
                return !picker->isLoading() && picker->listModel()->rowCount() > 0;
            });
        findAction(*menu, QStringLiteral("Select tracked branch"))->trigger();
        QVERIFY(waitLoaded(dlg));
        QCOMPARE(configValue(*m_repo, QStringLiteral("branch.main.remote")), QStringLiteral("origin"));
        QCOMPARE(configValue(*m_repo, QStringLiteral("branch.main.merge")), QStringLiteral("refs/heads/topic"));
    }

    void editDescription()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads/other"), true));
        const std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        QString shown;
        whenModal<QInputDialog>([&shown](QInputDialog *input) {
            shown = input->textValue();
            input->setTextValue(QStringLiteral("New description"));
            input->accept();
        });
        findAction(*menu, QStringLiteral("Edit description"))->trigger();
        QCOMPARE(shown, QStringLiteral("Other\nwork"));
        QVERIFY(waitLoaded(dlg));
        QCOMPARE(configValue(*m_repo, QStringLiteral("branch.other.description")), QStringLiteral("New description"));
    }

    void fetchFromPreselectsTheRemote()
    {
        m_repo->run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("up"), m_upstream->path()});
        m_repo->run({QStringLiteral("fetch"), QStringLiteral("--quiet"), QStringLiteral("up")});
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes/up/topic"), true));
        const std::unique_ptr<QMenu> menu = dlg.createListContextMenu();
        QString remote;
        whenModal<PullFetchDlg>([&remote](PullFetchDlg *fetch) {
            remote = fetch->options().remoteUrl;
            fetch->reject();
        });
        QAction *fetchFrom = findAction(*menu, QStringLiteral("Fetch from \"up\""));
        QVERIFY(fetchFrom);
        fetchFrom->trigger();
        QCOMPARE(remote, QStringLiteral("up"));
        // Refreshed afterwards
        QVERIFY(waitLoaded(dlg));
    }

    void treeMenu()
    {
        m_repo->run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("up"), m_upstream->path()});
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));

        // Right-clicking a node selects it
        const auto *treeModel = qobject_cast<QStandardItemModel *>(dlg.refTree()->model());
        const QStandardItem *tagsItem = treeModel->item(0)->child(2);
        QCOMPARE(tagsItem->text(), QStringLiteral("tags"));
        dlg.show();
        dlg.refTree()->scrollTo(tagsItem->index());
        const QRect rect = dlg.refTree()->visualRect(tagsItem->index());
        QTimer::singleShot(0, [] {
            if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                popup->close();
            }
        });
        Q_EMIT dlg.refTree()->customContextMenuRequested(rect.center());
        QCOMPARE(dlg.selectedRef(false, false), QStringLiteral("refs/tags"));

        // Tags: Create Tag, Delete all tags and one Delete remote tags per remote
        std::unique_ptr<QMenu> menu = dlg.createTreeContextMenu();
        QCOMPARE(actionTexts(*menu),
                 (QStringList{QStringLiteral("Create &Tag..."),
                              QStringLiteral("Delete all tags"),
                              QStringLiteral("Delete remote tags on \"origin\"..."),
                              QStringLiteral("Delete remote tags on \"up\"..."),
                              QStringLiteral("Copy ref names")}));

        // Heads: Create Branch
        QVERIFY(dlg.selectRef(QStringLiteral("refs/heads"), true));
        menu = dlg.createTreeContextMenu();
        QCOMPARE(actionTexts(*menu), (QStringList{QStringLiteral("Create &Branch..."), QStringLiteral("Copy ref names")}));

        // Remotes: Manage Remotes only, and on a remote also Fetch and Delete remote tags
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes"), true));
        menu = dlg.createTreeContextMenu();
        QCOMPARE(actionTexts(*menu), (QStringList{QStringLiteral("Manage Remotes"), QStringLiteral("Copy ref names")}));
        QVERIFY(dlg.selectRef(QStringLiteral("refs/remotes/origin"), true));
        menu = dlg.createTreeContextMenu();
        QCOMPARE(actionTexts(*menu),
                 (QStringList{QStringLiteral("Manage Remotes"),
                              QStringLiteral("Fetch from \"origin\""),
                              QStringLiteral("Delete remote tags..."),
                              QStringLiteral("Copy ref names")}));

        // The root: only Copy ref names
        QVERIFY(dlg.selectRef(QStringLiteral("refs"), true));
        menu = dlg.createTreeContextMenu();
        QCOMPARE(actionTexts(*menu), QStringList{QStringLiteral("Copy ref names")});
    }

    void deleteAllTags()
    {
        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags"), true));
        const std::unique_ptr<QMenu> menu = dlg.createTreeContextMenu();
        QString question;
        answerYes(&question);
        findAction(*menu, QStringLiteral("Delete all tags"))->trigger();
        QVERIFY(question.contains(QStringLiteral("Do you really want to permanently delete the 2 selected refs?")));
        QVERIFY(waitLoaded(dlg));
        QVERIFY(!refExists(*m_repo, QStringLiteral("refs/tags/v1")));
        QVERIFY(!refExists(*m_repo, QStringLiteral("refs/tags/v2")));
        QVERIFY(refExists(*m_repo, QStringLiteral("refs/heads/main")));
        // With no tags left, their tree node is gone
        const auto *treeModel = qobject_cast<QStandardItemModel *>(dlg.refTree()->model());
        const QStandardItem *root = treeModel->item(0);
        for (int row = 0; row < root->rowCount(); ++row) {
            QVERIFY(root->child(row)->text() != QStringLiteral("tags"));
        }
    }

    void parseRemoteTags()
    {
        const QByteArray hash1(40, 'a');
        const QByteArray hash2(40, 'b');
        const QByteArray output =
            hash1 + "\trefs/tags/v10\n" + hash1 + "\trefs/tags/v2\n" + hash1 + "\trefs/tags/v2^{}\n" + hash2 + "\trefs/heads/main\nwarning: something\n";
        QCOMPARE(Git::parseRemoteTags(output), (QStringList{QStringLiteral("v2"), QStringLiteral("v2^{}"), QStringLiteral("v10")}));
        QCOMPARE(Git::remoteTagsArguments(QStringLiteral("origin")),
                 (QStringList{QStringLiteral("ls-remote"), QStringLiteral("-t"), QStringLiteral("--"), QStringLiteral("origin")}));
    }

    void deleteRemoteTags()
    {
        // A bare remote with an annotated and a lightweight tag
        const TestRepo remote;
        remote.run({QStringLiteral("init"), QStringLiteral("--quiet"), QStringLiteral("--bare"), remote.path() + QStringLiteral("/bare.git")});
        const QString bare = remote.path() + QStringLiteral("/bare.git");
        m_repo->run({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("bare"), bare});
        m_repo->run(
            {QStringLiteral("push"), QStringLiteral("--quiet"), QStringLiteral("bare"), QStringLiteral("main"), QStringLiteral("v1"), QStringLiteral("v2")});

        BrowseRefsDlg dlg(m_repo->path());
        QVERIFY(waitLoaded(dlg));
        QVERIFY(dlg.selectRef(QStringLiteral("refs/tags"), true));
        const std::unique_ptr<QMenu> menu = dlg.createTreeContextMenu();
        QStringList listed;
        bool deleteEnabledBeforeSelecting = true;
        whenModal<DeleteRemoteTagDlg>(
            [&](DeleteRemoteTagDlg *tagDlg) {
                listed = tagDlg->tags();
                deleteEnabledBeforeSelecting = tagDlg->deleteButton()->isEnabled();
                deleteSecondTag(tagDlg);
            },
            [](const DeleteRemoteTagDlg *tagDlg) {
                return !tagDlg->isLoading();
            });
        findAction(*menu, QStringLiteral("Delete remote tags on \"bare\"..."))->trigger();
        QCOMPARE(listed, (QStringList{QStringLiteral("v1"), QStringLiteral("v2")}));
        QVERIFY(!deleteEnabledBeforeSelecting);
        const QString remoteTags = QString::fromUtf8(m_repo->git().run(Git::remoteTagsArguments(QStringLiteral("bare"))));
        QVERIFY(remoteTags.contains(QStringLiteral("refs/tags/v1")));
        QVERIFY(!remoteTags.contains(QStringLiteral("refs/tags/v2")));
        // Local tags are untouched
        QVERIFY(refExists(*m_repo, QStringLiteral("refs/tags/v2")));
    }

    void showLogNamesTheRef()
    {
        const TestRepo repo;
        repo.write(QStringLiteral("f.txt"), "1");
        repo.commitAll(QStringLiteral("One"));
        const BrowseRefsDlg dlg(repo.path());
        QCOMPARE(dlg.showLogArguments(QStringLiteral("refs/heads/topic")),
                 (QStringList{QStringLiteral("--command"),
                              QStringLiteral("log"),
                              QStringLiteral("--path"),
                              repo.path(),
                              QStringLiteral("--range"),
                              QStringLiteral("refs/heads/topic")}));
    }

private:
    std::unique_ptr<TestRepo> m_upstream;
    std::unique_ptr<TestRepo> m_repo;
};

QTEST_MAIN(BrowseRefsDlgTest)

#include "browserefsdlgtest.moc"
