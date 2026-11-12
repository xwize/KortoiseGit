// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "utils/error.hpp"
#include "utils/expected.hpp"
#include "utils/runtimeerror.hpp"

#include <source_location>
#include <utility>

namespace kgt
{

/**
 * The value of a function that can fail, or the Error that says why it didn't produce one.
 * For functions called often or whose error handling is simple; see GUIDELINES.md. Never call .value() on
 * it: check and dereference it, chain it (and_then, map, or_else) or use getOrThrow().
 */
template<typename T>
using Result = tl::expected<T, Error>;

/**
 * Builds the error branch of a Result, to return from a function that failed.
 * @param category What kind of failure this is.
 * @param message Translated text that can be shown to the user.
 * @param location Where the error is raised; leave it to the default.
 * @returns An unexpected value that converts to any Result<T>.
 */
[[nodiscard]] inline tl::unexpected<Error> makeError(ErrorCategory category, QString message, std::source_location location = std::source_location::current())
{
    return tl::unexpected<Error>(Error{.category = category, .message = std::move(message), .location = location});
}

/**
 * Unwraps a Result, turning its error into a RuntimeError for callers that report failures by exception.
 * @param result The Result to unwrap.
 * @returns The value held by @p result.
 */
template<typename T>
[[nodiscard]] T getOrThrow(Result<T> result)
{
    if (!result) {
        throw RuntimeError(std::move(result).error());
    }
    return std::move(*result);
}

/**
 * Throws the error of a Result<void>, if it has one.
 * @param result The Result to check.
 */
inline void getOrThrow(Result<void> result)
{
    if (!result) {
        throw RuntimeError(std::move(result).error());
    }
}

} // namespace kgt
