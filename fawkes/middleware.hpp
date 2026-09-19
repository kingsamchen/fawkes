// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "fawkes/is_asio_awaitable.hpp"
#include "fawkes/response.hpp"

namespace fawkes {

// Forward declaration for concepts.
class request;

enum class middleware_result : std::uint8_t {
    abort,
    proceed,
};

template<typename T>
concept coro_middleware_result = is_asio_awaitable_of_v<T, middleware_result>;

template<typename T>
concept has_pre_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).pre_handle(req, resp) } -> std::same_as<middleware_result>;
};

template<typename T>
concept has_coro_pre_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).pre_handle(req, resp) } -> coro_middleware_result;
};

template<typename T>
concept has_post_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).post_handle(req, resp) } -> std::same_as<middleware_result>;
};

template<typename T>
concept has_coro_post_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).post_handle(req, resp) } -> coro_middleware_result;
};

template<typename T>
concept is_middleware = has_pre_handle<T> || has_coro_pre_handle<T> ||
                        has_post_handle<T> || has_coro_post_handle<T>;

using route_handler_t =
    std::move_only_function<asio::awaitable<middleware_result>(request&, response&) const>;

namespace detail {

struct middleware_call_result {
    middleware_result result{middleware_result::proceed};
    std::exception_ptr eptr;
};

struct middleware_run_state {
    std::size_t entered_count{0UZ};
    middleware_result result{middleware_result::proceed};
    std::exception_ptr eptr;

    void absorb(const middleware_call_result& call_result) noexcept(
        std::is_nothrow_copy_assignable_v<std::exception_ptr>) {
        if (call_result.result == middleware_result::abort) {
            result = middleware_result::abort;
        }
        if (!eptr && call_result.eptr) {
            eptr = call_result.eptr;
        }
        // TODO(KC): Log the omitted exception.
    }
};

template<is_middleware M>
asio::awaitable<middleware_call_result> invoke_pre_handle(const M& middleware,
                                                          request& req,
                                                          response& resp) {
    middleware_call_result call_result;
    try {
        if constexpr (has_pre_handle<M>) {
            call_result.result = middleware.pre_handle(req, resp);
        } else if constexpr (has_coro_pre_handle<M>) {
            call_result.result = co_await middleware.pre_handle(req, resp);
        }
    } catch (...) {
        call_result.result = middleware_result::abort;
        call_result.eptr = std::current_exception();
        resp.set_status(http::status::internal_server_error);
    }
    co_return call_result;
}

template<is_middleware M>
asio::awaitable<middleware_call_result> invoke_post_handle(const M& middleware,
                                                           request& req,
                                                           response& resp) {
    middleware_call_result call_result;
    try {
        if constexpr (has_post_handle<M>) {
            call_result.result = middleware.post_handle(req, resp);
        } else if constexpr (has_coro_post_handle<M>) {
            call_result.result = co_await middleware.post_handle(req, resp);
        }
    } catch (...) {
        call_result.result = middleware_result::abort;
        call_result.eptr = std::current_exception();
        resp.set_status(http::status::internal_server_error);
    }
    co_return call_result;
}

template<typename F>
asio::awaitable<middleware_call_result> invoke_inner_handler(const F& inner_handler,
                                                             request& req,
                                                             response& resp) {
    middleware_call_result call_result;
    try {
        call_result.result = co_await inner_handler(req, resp);
    } catch (...) {
        call_result.result = middleware_result::abort;
        call_result.eptr = std::current_exception();
        resp.set_status(http::status::internal_server_error);
    }
    co_return call_result;
}

template<std::size_t... I, is_middleware... Mws, typename F>
asio::awaitable<middleware_result> run_middlewares_impl(const std::tuple<Mws...>& middlewares,
                                                        std::index_sequence<I...> /*idx_seq*/,
                                                        request& req,
                                                        response& resp,
                                                        const F& inner_handler) {
    if constexpr (sizeof...(Mws) == 0) {
        const auto call_result = co_await invoke_inner_handler(inner_handler, req, resp);
        if (call_result.eptr) {
            std::rethrow_exception(call_result.eptr);
        }
        co_return call_result.result;
    } else {
        middleware_run_state state;

        // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
        auto enter = [&]<std::size_t Index>() -> asio::awaitable<bool> {
            const auto call_result = co_await invoke_pre_handle(std::get<Index>(middlewares),
                                                                req,
                                                                resp);
            // A middleware is entered only after its pre handler completes.
            if (!call_result.eptr) {
                ++state.entered_count;
            }
            state.absorb(call_result);
            co_return call_result.result == middleware_result::proceed;
        };
        const bool should_invoke_inner_handler = ((co_await enter.template operator()<I>()) && ...);
        if (should_invoke_inner_handler) {
            state.absorb(co_await invoke_inner_handler(inner_handler, req, resp));
        }

        // Every entered middleware is unwound, even if a post handler fails.
        // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
        auto leave = [&]<std::size_t Index>() -> asio::awaitable<void> {
            if (Index < state.entered_count) {
                state.absorb(co_await invoke_post_handle(std::get<Index>(middlewares), req, resp));
            }
            co_return;
        };
        ((co_await leave.template operator()<sizeof...(Mws) - I - 1>()), ...);

        if (state.eptr) {
            std::rethrow_exception(state.eptr);
        }
        co_return state.result;
    }
}

// If a middleware's pre_handle completes, including by returning an `abort`
// result, its post_handle is guaranteed to run during unwind.
// But if its pre_handle throws, its post_handle is skipped. If multiple
// exceptions occur, only the first is preserved and rethrown, allowing an
// exception from the user-provided route handler to take precedence.
template<is_middleware... Mws, typename F>
asio::awaitable<middleware_result> run_middlewares(const std::tuple<Mws...>& middlewares,
                                                   request& req,
                                                   response& resp,
                                                   const F& inner_handler) {
    using idx_seq_t = std::make_index_sequence<sizeof...(Mws)>;
    return run_middlewares_impl(middlewares, idx_seq_t{}, req, resp, inner_handler);
}

} // namespace detail

// A type-erased middleware set.
class middleware_chain {
public:
    template<is_middleware... Mws>
    void set(std::tuple<Mws...>&& middlewares) {
        using middlewares_t = std::tuple<Mws...>;
        static_assert(std::tuple_size_v<middlewares_t> > 0, "middlewares cannot be empty");

        mws_runner_ = [mws = std::move(middlewares)](request& req,
                                                     response& resp,
                                                     const route_handler_t& inner_handler)
            -> asio::awaitable<middleware_result> {
            return detail::run_middlewares(mws, req, resp, inner_handler);
        };
    }

    [[nodiscard]] asio::awaitable<middleware_result> run(
        request& req, response& resp, const route_handler_t& inner_handler) const {
        if (!mws_runner_) {
            return inner_handler(req, resp);
        }
        return mws_runner_(req, resp, inner_handler);
    }

private:
    using middleware_runner_t = std::move_only_function<asio::awaitable<middleware_result>(
        request&, response&, const route_handler_t&) const>;

    middleware_runner_t mws_runner_;
};

static_assert(std::is_nothrow_move_constructible_v<middleware_chain>);
static_assert(std::is_move_assignable_v<middleware_chain>);

struct middlewares {
    template<is_middleware... Mws>
    static auto use(Mws... mws) -> std::tuple<Mws...> {
        return std::make_tuple(std::move(mws)...);
    }
};

} // namespace fawkes
