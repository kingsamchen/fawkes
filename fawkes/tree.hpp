// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <esl/ignore_unused.h>
#include <esl/strings.h>
#include <esl/utility.h>
#include <fmt/format.h>

#include "fawkes/path_params.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"

namespace fawkes {

using route_handler_t = std::function<void(request&, response&)>;

namespace detail {

struct wildcard_result {
    std::string_view name;
    std::size_t pos{std::string_view::npos};

    [[nodiscard]] constexpr bool found() const noexcept {
        return pos != std::string_view::npos;
    }

    [[nodiscard]] constexpr bool valid_name() const noexcept {
        return name.size() > 1;
    }
};

// Valid since C++20.
static_assert(std::is_aggregate_v<wildcard_result>);

inline constexpr wildcard_result unknown_wildcard;

// Find a wildcard segment if existed and then retrieve the wildcard name if valid.
// A wildcard segment starts with `:` or `*` and the name cannot contain `:` and `*`.
// The name is always invalid if no wildcard found.
[[nodiscard]] constexpr wildcard_result find_wildcard(std::string_view path) noexcept {
    const auto start = path.find_first_of(":*");
    if (start == std::string_view::npos) {
        return wildcard_result{};
    }

    const auto stop = path.find_first_of(":*/", start + 1);
    if (stop == std::string_view::npos) {
        return wildcard_result{.name = path.substr(start), .pos = start};
    }

    if (path[stop] == '/') {
        return wildcard_result{.name = path.substr(start, stop - start), .pos = start};
    }

    return wildcard_result{.name = std::string_view{}, .pos = start};
}

// Returns length of common prefix.
[[nodiscard]] constexpr std::size_t longest_common_prefix(std::string_view s1,
                                                          std::string_view s2) noexcept {
    auto iters = std::ranges::mismatch(s1, s2);
    return static_cast<std::size_t>(std::distance(s1.begin(), iters.in1));
}

} // namespace detail

class node {
    enum class type : std::uint8_t {
        plain = 0,
        root,
        param,
        catch_all,
    };

public:
    // Add route path to the node.
    // Throws `std::invalid_argument` if there is path conflict.
    void add_route(std::string_view path, route_handler_t&& handler) {
        // The sub-tree rooted by the node has one more route.
        ++priority_;

        auto full_path = path;
        if (path_.empty() && indices_.empty()) {
            insert_path(path, full_path, detail::unknown_wildcard, std::move(handler));
            type_ = type::root;
            return;
        }

        insert_route(path, full_path, std::move(handler));
    }

    // Throws `std::runtime_error` if node type is invalid, this is an internal error,
    // and in most cases is caused by implementation bugs.
    const route_handler_t* locate(std::string_view path, path_params& ps) const;

private:
    // Find the target node to insert the path.
    // Throws `std::invalid_argument` if there is path conflict.
    void insert_route(std::string_view path, std::string_view full_path, route_handler_t&& handler);

    // Insert path into the target node.
    // Throws `std::invalid_argument` if there is path conflict.
    void insert_path(std::string_view path,
                     std::string_view full_path,
                     detail::wildcard_result wildcard,
                     route_handler_t&& handler);

    // Increments priority of the given child and reorders if necessary.
    // `pos` is the index into the child node.
    std::size_t increment_child_priority(std::size_t pos);

    std::string path_;
    std::string indices_;
    bool has_wild_child_{false};
    type type_{type::plain};
    int priority_{0};
    std::vector<std::unique_ptr<node>> children_;
    route_handler_t handler_;

    friend class node_test_inspector;
};

} // namespace fawkes
