// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/router.hpp"

#include <string_view>
#include <tuple>
#include <utility>

#include <boost/beast/http/status.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

namespace fawkes {

namespace {

const route_handler_t default_not_found_handler = // NOLINT(bugprone-throwing-static-initialization)
    [](request& /*req*/, response& resp) -> asio::awaitable<middleware_result> {
    const json::object body{
        {"error", json::object{{"message", "The requested resource was not found."}}}};
    resp.json(http::status::not_found, json::serialize(body));
    co_return middleware_result::proceed;
};

} // namespace

const route_handler_t* router::locate_route(request& req) const {
    const auto locator = [](http::verb method, std::string_view path, const auto& routes)
        -> std::tuple<const route_handler_t*, path_params> {
        path_params ps;
        const auto tree_it = routes.find(method);
        if (tree_it == routes.end()) {
            return std::make_tuple(nullptr, std::move(ps));
        }
        return std::make_tuple(tree_it->second.locate(path, ps), std::move(ps));
    };

    const auto method = req.header().method();
    const auto path = req.path();

    auto [handler, ps] = locator(method, path, routes_);

    // Fallback to the GET route, if possible.
    if (!handler && method == http::verb::head) {
        std::tie(handler, ps) = locator(http::verb::get, path, routes_);
    }

    req.params() = std::move(ps);
    return handler;
}

asio::awaitable<void> router::dispatch(request& req, response& resp) const {
    const auto* route_handler = locate_route(req);
    if (route_handler) {
        boost::ignore_unused(co_await base_middlewares_.run(req, resp, *route_handler));
        co_return;
    }

    const auto& not_found = not_found_handler_ ? not_found_handler_ : default_not_found_handler;
    boost::ignore_unused(co_await base_middlewares_.run(req, resp, not_found));
    co_return;
}

} // namespace fawkes
