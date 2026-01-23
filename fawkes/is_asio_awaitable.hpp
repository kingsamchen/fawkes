// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <type_traits>

#include <boost/asio/awaitable.hpp>

namespace fawkes {

namespace asio = boost::asio;

template<typename T>
struct is_asio_awaitable : std::false_type {};

template<typename T, typename Executor>
struct is_asio_awaitable<asio::awaitable<T, Executor>> : std::true_type {};

template<typename T>
constexpr bool is_asio_awaitable_v = is_asio_awaitable<T>::value;

template<typename T, typename E>
struct is_asio_awaitable_of : std::false_type {};

template<typename T, typename Executor>
struct is_asio_awaitable_of<asio::awaitable<T, Executor>, T> : std::true_type {};

template<typename T, typename E>
constexpr bool is_asio_awaitable_of_v = is_asio_awaitable_of<T, E>::value;

} // namespace fawkes
