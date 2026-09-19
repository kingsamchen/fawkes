// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <concepts>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "fawkes/errors.hpp"
#include "fawkes/is_asio_awaitable.hpp"
#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/tree.hpp"

namespace fawkes {

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;

template<typename F>
concept is_user_handler =
    std::move_constructible<std::decay_t<F>> &&
    (!std::is_lvalue_reference_v<F> || std::copy_constructible<std::decay_t<F>>) &&
    std::invocable<const std::decay_t<F>&, const request&, response&> &&
    is_asio_awaitable_of_v<std::invoke_result_t<const std::decay_t<F>&, const request&, response&>,
                           void>;

class router {
public:
    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void add_route(http::verb verb, std::string_view path, H&& handler) {
        add_route(verb, path, {}, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void add_route(http::verb verb,
                   std::string_view path,
                   std::tuple<Mws...>&& middlewares,
                   H&& handler) {
        // The lambda coroutine is stored and kept alive in routes.
        route_handler_t route_handler =
            [mws = std::move(middlewares), // NOLINT(*-avoid-capturing-lambda-coroutines)
             user_handler = std::forward<H>(handler)](request& req, response& resp)
            -> asio::awaitable<middleware_result> {
            using enum middleware_result;

            const auto adapted_user_handler =
                [&user_handler]( // NOLINT(*-avoid-capturing-lambda-coroutines)
                    request& handler_req, response& handler_resp)
                -> asio::awaitable<middleware_result> {
                try {
                    co_await user_handler(std::as_const(handler_req), handler_resp);
                } catch (const http_error& ex) {
                    // `http_error` is a part of the expected handler outcome, the request
                    // handling doesn't fail.
                    json::object err{{"message", ex.what()}};
                    if (const auto& ec = ex.error_code(); ec.has_value()) {
                        err["code"] = *ec;
                    }
                    const json::object body{{"error", std::move(err)}};
                    handler_resp.json(ex.status_code(), json::serialize(body));
                }
                co_return proceed;
            };
            co_return co_await detail::run_middlewares(mws, req, resp, adapted_user_handler);
        };
        routes_[verb].add_route(path, std::move(route_handler));
    }

    // The path params of `req` will be updated.
    [[nodiscard]] const route_handler_t* locate_route(request& req) const;

    [[nodiscard]] asio::awaitable<void> dispatch(request& req, response& resp) const;

    // Router level middlewares, applied to all routes.
    template<is_middleware... Mws>
    void use(Mws... mws) {
        base_middlewares_.set(std::make_tuple(std::move(mws)...));
    }

private:
    boost::unordered_flat_map<http::verb, node> routes_;
    middleware_chain base_middlewares_;
};

static_assert(std::is_nothrow_move_constructible_v<router>);
static_assert(std::is_move_assignable_v<router>);

} // namespace fawkes
