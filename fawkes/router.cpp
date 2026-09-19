// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/router.hpp"

#include <utility>

#include <boost/beast/http/status.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

namespace fawkes {

const route_handler_t* router::locate_route(request& req) const {
    const auto tree_it = routes_.find(req.header().method());
    if (tree_it == routes_.end()) {
        return nullptr;
    }

    return tree_it->second.locate(req.path(), req.params());
}

asio::awaitable<void> router::dispatch(request& req, response& resp) const {
    const auto* route_handler = locate_route(req);
    if (route_handler) {
        co_await base_middlewares_.run(req, resp, *route_handler);
        co_return;
    }

    const route_handler_t not_found_handler =
        [](request& /*not_found_req*/, response& not_found_resp)
        -> asio::awaitable<middleware_result> {
        const json::object body{
            {"error", json::object{{"message", "Unknown resource"}}}};
        not_found_resp.json(http::status::not_found, json::serialize(body));
        co_return middleware_result::proceed;
    };
    co_await base_middlewares_.run(req, resp, not_found_handler);
    co_return;
}

} // namespace fawkes
