// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "test_utils/run_awaitable_sync.hpp"

#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace test_util {

void run_awaitable_sync(asio::io_context& ioc, asio::awaitable<void> awaitable) {
    asio::co_spawn(ioc, std::move(awaitable), asio::detached);
    ioc.run();
    ioc.restart();
}

} // namespace test_util
