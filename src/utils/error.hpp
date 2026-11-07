// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

#include <source_location>

/**
 * @namespace kgt
 * KortoiseGit's project namespace. Every class and free function of the plugin, the app and the shared
 * modules lives in it.
 */
namespace kgt
{

/**
 * What kind of failure an Error describes, so a caller can handle some kinds and rethrow the rest.
 */
enum class ErrorCategory {
    CommandLine, // Bad or missing command line arguments
    Git, // A git command failed or printed something unexpected
    Io, // Reading or writing a file, or starting a process, failed
    Parse, // Text (git output, a setting) could not be parsed
};

/**
 * One failure: its category, a message for the user and the place in the source that raised it.
 * Reported either as the error of a Result<T> (utils/result.hpp) or inside a RuntimeError
 * (utils/runtimeerror.hpp).
 */
struct Error {
    ErrorCategory category{}; // What kind of failure this is
    QString message; // Translated text that can be shown to the user
    std::source_location location = std::source_location::current(); // Where the error was raised
};

} // namespace kgt
