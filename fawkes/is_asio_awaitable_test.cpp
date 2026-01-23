// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <doctest/doctest.h>

#include "fawkes/is_asio_awaitable.hpp"

namespace {

namespace asio = boost::asio;

TEST_SUITE_BEGIN("Type Traits/is_asio_awaitable");

struct foo {};

struct bar {};

TEST_CASE("Check if asio::awaitable with default executor") {
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<void>>);
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<int>>);
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<foo>>);
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<bar>>);

    SUBCASE("not asio::awaitable") {
        static_assert(!fawkes::is_asio_awaitable_v<void>);
        static_assert(!fawkes::is_asio_awaitable_v<int>);
        static_assert(!fawkes::is_asio_awaitable_v<foo>);
        static_assert(!fawkes::is_asio_awaitable_v<bar>);
    }
}

TEST_CASE("Check if asio::awaitable with non-default executor") {
    using my_executor = asio::io_context::executor_type;

    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<void, my_executor>>);
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<int, my_executor>>);
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<foo, my_executor>>);
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<bar, my_executor>>);
}

TEST_CASE("Check if asio::awaitable of specified type") {
    using my_executor = asio::io_context::executor_type;

    static_assert(fawkes::is_asio_awaitable_of_v<asio::awaitable<void>, void>);
    static_assert(fawkes::is_asio_awaitable_of_v<asio::awaitable<foo>, foo>);
    static_assert(fawkes::is_asio_awaitable_of_v<asio::awaitable<bar, my_executor>, bar>);

    SUBCASE("not in expected type") {
        static_assert(!fawkes::is_asio_awaitable_of_v<asio::awaitable<void>, int>);
        static_assert(!fawkes::is_asio_awaitable_of_v<asio::awaitable<foo, my_executor>, bar>);
    }

    SUBCASE("not even asio::awataible") {
        static_assert(!fawkes::is_asio_awaitable_of_v<void, int>);
        static_assert(!fawkes::is_asio_awaitable_of_v<foo, foo>);
    }
}

TEST_SUITE_END(); // Type Traits/is_asio_awaitable

} // namespace
