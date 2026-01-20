// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <any>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>

#include <boost/asio/awaitable.hpp>

namespace fawkes {

namespace asio = boost::asio;

// TODO(KC): Consider make middlewares immutable.

// Forward declarations for concepts.
class request;
class response;

enum class middleware_result : std::uint8_t {
    abort,
    proceed,
};

// Concepts for synchronous middlewares.
template<typename T>
concept has_pre_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).pre_handle(req, resp) } -> std::same_as<middleware_result>;
};

template<typename T>
concept has_post_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).post_handle(req, resp) } -> std::same_as<middleware_result>;
};

// Concepts for coroutine middlewares.
template<typename T>
concept has_coro_pre_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).pre_handle(req, resp) }
      -> std::same_as<asio::awaitable<middleware_result>>;
};

template<typename T>
concept has_coro_post_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).post_handle(req, resp) }
      -> std::same_as<asio::awaitable<middleware_result>>;
};

template<typename T>
concept is_middleware = has_pre_handle<T> || has_post_handle<T> ||
                        has_coro_pre_handle<T> || has_coro_post_handle<T>;

namespace detail {

// Helper to invoke pre_handle on a single middleware, returns bool for short-circuit.
template<is_middleware M>
asio::awaitable<bool> try_pre_handle(M& middleware,
                                     request& req,
                                     response& resp,
                                     middleware_result& result) {
    if constexpr (has_coro_pre_handle<M&>) {
        result = co_await middleware.pre_handle(req, resp);
    } else if constexpr (has_pre_handle<M&>) {
        result = middleware.pre_handle(req, resp);
    }
    co_return result == middleware_result::proceed;
}

// Helper to invoke post_handle on a single middleware, returns bool for short-circuit.
template<is_middleware M>
asio::awaitable<bool> try_post_handle(M& middleware,
                                      request& req,
                                      response& resp,
                                      middleware_result& result) {
    if constexpr (has_coro_post_handle<M&>) {
        result = co_await middleware.post_handle(req, resp);
    } else if constexpr (has_post_handle<M&>) {
        result = middleware.post_handle(req, resp);
    }
    co_return result == middleware_result::proceed;
}

template<std::size_t... I, is_middleware... Mws>
asio::awaitable<middleware_result> apply_pre_handles(std::tuple<Mws...>& middlewares,
                                                     std::index_sequence<I...> /*idx_seq*/,
                                                     request& req,
                                                     response& resp) {
    middleware_result result = middleware_result::proceed;
    // Short circuit using && fold expression.
    [[maybe_unused]] auto _ =
        (co_await try_pre_handle(std::get<I>(middlewares), req, resp, result) && ...);
    co_return result;
}

template<std::size_t... I, is_middleware... Mws>
asio::awaitable<middleware_result> apply_post_handles(std::tuple<Mws...>& middlewares,
                                                      std::index_sequence<I...> /*idx_seq*/,
                                                      request& req,
                                                      response& resp) {
    constexpr auto N = sizeof...(I);
    middleware_result result = middleware_result::proceed;
    // Short circuit using && fold expression (reverse order).
    [[maybe_unused]] auto _ =
        (co_await try_post_handle(std::get<N - I - 1>(middlewares), req, resp, result) && ...);
    co_return result;
}

template<is_middleware... Mws>
asio::awaitable<middleware_result> run_middlewares_pre_handle(std::tuple<Mws...>& middlewares,
                                                              request& req,
                                                              response& resp) {
    if constexpr (sizeof...(Mws) == 0) {
        co_return middleware_result::proceed;
    } else {
        using idx_seq_t = std::make_index_sequence<sizeof...(Mws)>;
        co_return co_await apply_pre_handles(middlewares, idx_seq_t{}, req, resp);
    }
}

template<is_middleware... Mws>
asio::awaitable<middleware_result> run_middlewares_post_handle(std::tuple<Mws...>& middlewares,
                                                               request& req,
                                                               response& resp) {
    if constexpr (sizeof...(Mws) == 0) {
        co_return middleware_result::proceed;
    } else {
        using idx_seq_t = std::make_index_sequence<sizeof...(Mws)>;
        co_return co_await apply_post_handles(middlewares, idx_seq_t{}, req, resp);
    }
}

} // namespace detail

// A type-erased middleware set.
class middleware_chain {
public:
    template<is_middleware... Mws>
    void set(std::tuple<Mws...>&& middlewares) {
        using middlewares_t = std::tuple<Mws...>;
        static_assert(std::tuple_size_v<middlewares_t> > 0, "middlewares cannot be empty");

        middlewares_ = std::move(middlewares);
        void* const ptr = std::any_cast<middlewares_t>(&middlewares_);
        assert(ptr != nullptr);
        auto& mws = *static_cast<middlewares_t*>(ptr);

        // NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)
        // Safe: mws references middlewares_ which is a member with the same lifetime as pre_impl_.
        pre_impl_ = [&mws](request& req,
                           response& resp) -> asio::awaitable<middleware_result> {
            co_return co_await detail::run_middlewares_pre_handle(mws, req, resp);
        };

        // Safe: mws references middlewares_ which is a member with the same lifetime as post_impl_.
        post_impl_ = [&mws](request& req,
                            response& resp) -> asio::awaitable<middleware_result> {
            co_return co_await detail::run_middlewares_post_handle(mws, req, resp);
        };
        // NOLINTEND(*-avoid-capturing-lambda-coroutines)
    }

    [[nodiscard]] asio::awaitable<middleware_result> pre_handle(request& req,
                                                                response& resp) const {
        if (!pre_impl_) {
            co_return middleware_result::proceed;
        }

        co_return co_await pre_impl_(req, resp);
    }

    [[nodiscard]] asio::awaitable<middleware_result> post_handle(request& req,
                                                                 response& resp) const {
        if (!post_impl_) {
            co_return middleware_result::proceed;
        }

        co_return co_await post_impl_(req, resp);
    }

private:
    std::any middlewares_;
    std::function<asio::awaitable<middleware_result>(request&, response&)> pre_impl_;
    std::function<asio::awaitable<middleware_result>(request&, response&)> post_impl_;
};

struct middlewares {
    template<is_middleware... Mws>
    static auto use(Mws... mws) -> std::tuple<Mws...> {
        return std::make_tuple(std::move(mws)...);
    }
};

} // namespace fawkes
