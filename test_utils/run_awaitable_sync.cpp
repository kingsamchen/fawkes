// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "test_utils/run_awaitable_sync.hpp"

#include <exception>
#include <utility>

#include <boost/asio/co_spawn.hpp>

namespace test_util {

void run_awaitable_sync(asio::io_context& ioc, asio::awaitable<void> awaitable) {
    std::exception_ptr eptr;
    asio::co_spawn(ioc, std::move(awaitable), [&eptr](std::exception_ptr error) {
        eptr = error;
    });
    ioc.run();
    ioc.restart();
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

} // namespace test_util
