// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "utils/result.hpp"
#include "utils/runtimeerror.hpp"

#include <QTest>

#include <memory>

namespace
{

using namespace kgt;

/**
 * @param text Text to read as a positive number.
 * @returns The number, or a Parse error.
 */
Result<int> parsePositive(const QString& text)
{
    bool isNumber = false;
    const int number = text.toInt(&isNumber);
    if (!isNumber || number <= 0) {
        return makeError(ErrorCategory::Parse, QStringLiteral("not a positive number: %1").arg(text));
    }
    return number;
}

} // namespace

/**
 * Result<T>, makeError() and getOrThrow() from utils/result.hpp.
 */
class ResultTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void holdsValue()
    {
        const Result<int> result = parsePositive(QStringLiteral("42"));
        QVERIFY(result);
        QCOMPARE(*result, 42);
    }

    void holdsError()
    {
        const int line = __LINE__ + 1;
        const Result<int> result = makeError(ErrorCategory::Parse, QStringLiteral("bad"));
        QVERIFY(!result);
        QCOMPARE(result.error().category, ErrorCategory::Parse);
        QCOMPARE(result.error().message, QStringLiteral("bad"));
        QCOMPARE(static_cast<int>(result.error().location.line()), line);
        QVERIFY(QByteArray(result.error().location.file_name()).endsWith("resulttest.cpp"));
    }

    void chains()
    {
        const Result<int> doubled = parsePositive(QStringLiteral("21")).map([](int value) {
            return value * 2;
        });
        QVERIFY(doubled);
        QCOMPARE(*doubled, 42);

        const Result<int> failed = parsePositive(QStringLiteral("-1")).and_then([](int value) -> Result<int> {
            return value + 1;
        });
        QVERIFY(!failed);
        QCOMPARE(failed.error().category, ErrorCategory::Parse);
    }

    void getOrThrowReturnsValue()
    {
        QCOMPARE(getOrThrow(parsePositive(QStringLiteral("7"))), 7);
        const std::unique_ptr<int> moved = getOrThrow(Result<std::unique_ptr<int>>(std::make_unique<int>(3)));
        QCOMPARE(*moved, 3);
    }

    void getOrThrowThrowsRuntimeError()
    {
        try {
            [[maybe_unused]] const int number = getOrThrow(parsePositive(QStringLiteral("x")));
            QFAIL("getOrThrow did not throw");
        } catch (const RuntimeError& e) {
            QCOMPARE(e.error().category, ErrorCategory::Parse);
            QCOMPARE(QString::fromStdString(e.what()), e.error().message);
        }
    }

    void getOrThrowVoid()
    {
        getOrThrow(Result<void>());
        QVERIFY_THROWS_EXCEPTION(RuntimeError, getOrThrow(Result<void>(makeError(ErrorCategory::Io, QStringLiteral("io")))));
    }
};

QTEST_GUILESS_MAIN(ResultTest)

#include "resulttest.moc"
