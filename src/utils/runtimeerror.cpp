// SPDX-FileCopyrightText: 2026 James Jones <this.verb@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "utils/runtimeerror.hpp"

#include <utility>

namespace kgt
{

RuntimeError::RuntimeError(Error error)
    : std::runtime_error(error.message.toStdString())
    , m_error(std::move(error))
{
}

const Error& RuntimeError::error() const noexcept
{
    return m_error;
}

} // namespace kgt
