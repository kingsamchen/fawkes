// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <exception>
#include <optional>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>

namespace test_util {

namespace asio = boost::asio;

template<typename T>
T run_awaitable_sync(asio::io_context& ioc, asio::awaitable<T> awaitable) {
    std::optional<T> result;
    std::exception_ptr eptr;
    asio::co_spawn(ioc, std::move(awaitable), [&result, &eptr](std::exception_ptr error, T res) {
        // MSVC doesn't provide move semantics explicitly for `std::exception_ptr`.
        eptr = error;
        if (!eptr) {
            result = std::move(res);
        }
    });
    ioc.run();
    ioc.restart();
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    return std::move(result.value()); // NOLINT(bugprone-unchecked-optional-access)
}

void run_awaitable_sync(asio::io_context& ioc, asio::awaitable<void> awaitable);

} // namespace test_util
