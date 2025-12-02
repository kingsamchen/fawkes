// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <concepts>
#include <exception>
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
#include "fawkes/middleware.hpp"
#include "fawkes/path_params.hpp"
#include "fawkes/tree.hpp"

namespace fawkes {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace json = boost::json;

template<typename T>
struct is_asio_awaitable : std::false_type {};

template<typename Executor>
struct is_asio_awaitable<asio::awaitable<void, Executor>> : std::true_type {};

template<typename T>
constexpr bool is_asio_awaitable_v = is_asio_awaitable<T>::value;

template<typename F>
concept is_user_handler = std::invocable<F, const request&, response&> &&
                          is_asio_awaitable_v<std::invoke_result_t<F, const request&, response&>>;

class router {
public:
    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void add_route(beast::http::verb verb, std::string_view path, H&& handler) {
        add_route(verb, path, {}, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void add_route(beast::http::verb verb,
                   std::string_view path,
                   std::tuple<Mws...>&& middlewares,
                   H&& handler) {
        route_handler_t route_handler =
            [mws = std::move(middlewares),
             user_handler = std::forward<H>(handler)](request& req, response& resp) mutable
            -> asio::awaitable<middleware_result> {
            using enum middleware_result;

            // Throwing from user handler would not abort either per-route middlewares or
            // router-level middlewares.
            // However, throwing from any middleware would be like aborting from the middleware.

            if (detail::run_middlewares_pre_handle(mws, req, resp) == abort) {
                co_return abort;
            }

            try {
                co_await user_handler(std::as_const(req), resp);
            } catch (const http_error& ex) {
                json::object err{{"message", ex.what()}};
                if (const auto& ec = ex.error_code(); ec.has_value()) {
                    err["code"] = *ec;
                }
                const json::object body{{"error", std::move(err)}};
                resp.json(ex.status_code(), json::serialize(body));
            } catch (const std::exception& ex) {
                const json::object body{{"error", json::object{{"message", ex.what()}}}};
                resp.json(http::status::internal_server_error, json::serialize(body));
            }

            co_return detail::run_middlewares_post_handle(mws, req, resp);
        };
        routes_[verb].add_route(path, std::move(route_handler));
    }

    // `path` must outlive `ps`.
    const route_handler_t* locate_route(beast::http::verb verb, std::string_view path,
                                        path_params& ps) const {
        const auto tree_it = routes_.find(verb);
        if (tree_it == routes_.end()) {
            return nullptr;
        }

        return tree_it->second.locate(path, ps);
    }

    // Router level middlewares, applied to all routes.
    template<is_middleware... Mws>
    void use(Mws... mws) {
        base_middlewares_.set(std::make_tuple(std::move(mws)...));
    }

    [[nodiscard]] middleware_result run_pre_handle(request& req, response& resp) const {
        return base_middlewares_.pre_handle(req, resp);
    }

    [[nodiscard]] middleware_result run_post_handle(request& req, response& resp) const {
        return base_middlewares_.post_handle(req, resp);
    }

private:
    boost::unordered_flat_map<beast::http::verb, node> routes_;
    middleware_chain base_middlewares_;
};

} // namespace fawkes
