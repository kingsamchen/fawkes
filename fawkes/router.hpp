// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <concepts>
#include <string_view>
#include <tuple>
#include <utility>

#include <boost/beast/http/verb.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <esl/ignore_unused.h>

#include "fawkes/middleware.hpp"
#include "fawkes/path_params.hpp"
#include "fawkes/tree.hpp"

namespace fawkes {

namespace beast = boost::beast;

template<typename F>
concept is_user_handler = std::invocable<F, const request&, response&>;

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
            [this,
             mws = std::move(middlewares),
             user_handler = std::forward<H>(handler)](request& req, response& resp) mutable {
                using enum middleware_result;

                if (base_middlewares_.pre_handle(req, resp) == abort ||
                    detail::run_middlewares_pre_handle(mws, req, resp) == abort) {
                    return;
                }

                user_handler(std::as_const(req), resp);

                if (detail::run_middlewares_post_handle(mws, req, resp) == abort) {
                    return;
                }
                esl::ignore_unused(base_middlewares_.post_handle(req, resp));
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

private:
    boost::unordered::unordered_flat_map<beast::http::verb, node> routes_;
    middleware_chain base_middlewares_;
};

} // namespace fawkes
