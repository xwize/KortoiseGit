// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "utils/error.hpp"

#include <stdexcept>

namespace kgt
{

/**
 * The exception thrown for every failure that isn't returned as a Result<T>: I/O, a failed git command, a
 * constructor that can't fully initialise, anything that has to travel up several levels. It carries one Error.
 */
class RuntimeError : public std::runtime_error
{
public:
    /**
     * @param error The failure this exception reports.
     */
    explicit RuntimeError(Error error);

    /**
     * @returns The failure this exception reports, for a catch that switches on its category.
     */
    [[nodiscard]] const Error &error() const noexcept;

private:
    Error m_error;
};

} // namespace kgt
