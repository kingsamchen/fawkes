// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <string_view>
#include <utility>

#include <boost/beast/http/verb.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "fawkes/path_params.hpp"
#include "fawkes/tree.hpp"

namespace fawkes {

namespace beast = boost::beast;

class router {
public:
    // Throws `std::invalid_argument` if there is path conflict.
    void add_route(beast::http::verb verb, std::string_view path, route_handler_t&& handler) {
        routes_[verb].add_route(path, std::move(handler));
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

    // TODO(KC): support middleware.

private:
    boost::unordered::unordered_flat_map<beast::http::verb, node> routes_;
};

} // namespace fawkes
