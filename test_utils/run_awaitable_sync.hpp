// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <future>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

namespace test_util {

namespace asio = boost::asio;

template<typename T>
T run_awaitable_sync(asio::io_context& ioc, asio::awaitable<T> awaitable) {
    std::future<T> fut = asio::co_spawn(ioc, std::move(awaitable), asio::use_future);
    ioc.run();
    ioc.restart();
    return fut.get();
}

void run_awaitable_sync(asio::io_context& ioc, asio::awaitable<void> awaitable);

} // namespace test_util
