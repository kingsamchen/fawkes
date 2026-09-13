// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/router.hpp"

#include <utility>

namespace fawkes {

const route_handler_t* router::locate_route(request& req) const {
    const auto tree_it = routes_.find(req.header().method());
    if (tree_it == routes_.end()) {
        return nullptr;
    }

    return tree_it->second.locate(req.path(), req.params());
}

} // namespace fawkes
