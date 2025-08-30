// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/tree.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "fawkes/path_params.hpp"

namespace fawkes {

const route_handler_t* node::locate(std::string_view path, path_params& ps) const {
    if (path.size() == path_.size()) {
        return handler_ ? &handler_ : nullptr;
    }

    if (path.size() > path_.size() && path.starts_with(path_)) {
        path.remove_prefix(path_.size());

        if (!has_wild_child_) {
            const char idxc = path[0];
            const auto pos = indices_.find(idxc);
            if (pos == std::string::npos) {
                return nullptr;
            }

            return children_[pos]->locate(path, ps);
        }

        auto& child = *children_.front();
        if (child.type_ == type::param) {
            const auto param_end = path.find('/');
            ps.add(std::string_view{child.path_}.substr(1), path.substr(0, param_end));

            if (param_end == std::string_view::npos) {
                return child.handler_ ? &child.handler_ : nullptr;
            }

            // Go deeper.
            if (!child.children_.empty()) {
                return child.children_[0]->locate(path.substr(param_end), ps);
            }
        } else if (child.type_ == type::catch_all) {
            ps.add(std::string_view{child.path_}.substr(2), path);
            return &child.handler_;
        } else [[unlikely]] {
            throw std::runtime_error(fmt::format("node type '{}' of route '{}' is invalid",
                                                 esl::to_underlying(child.type_), child.path_));
        }
    }

    return nullptr;
}

void node::insert_route(std::string_view path, std::string_view full_path,
                        route_handler_t&& handler) {
    const auto len = detail::longest_common_prefix(path, path_);

    // Split current node to make node path equal to common prefix.
    if (len < path_.size()) {
        auto child = std::make_unique<node>();
        child->path_ = path_.substr(len);
        child->indices_ = indices_;
        child->has_wild_child_ = has_wild_child_;
        child->type_ = type::plain;
        child->priority_ = priority_ - 1;
        child->children_ = std::move(children_);
        child->handler_ = std::move(handler_);

        children_.clear(); // Reset to known state for reuse.
        children_.push_back(std::move(child));
        indices_ = path_[len];
        path_.erase(len);
        has_wild_child_ = false;
    }

    // `path` is subset of a route path denoted by current node.
    if (len == path.size()) {
        if (handler_) [[unlikely]] {
            throw std::invalid_argument(
                    fmt::format("a handler is already registered for path '{}'", full_path));
        }
        handler_ = std::move(handler);
        return;
    }

    path.remove_prefix(len);

    // Can have only one child if it is a wild child.
    if (has_wild_child_) {
        auto& child = *children_.front();
        ++child.priority_;
        if (path.starts_with(child.path_) &&
            child.type_ != type::catch_all &&
            (child.path_.size() == path.size() || path[child.path_.size()] == '/')) {
            child.insert_route(path, full_path, std::move(handler));
            return;
        }

        // Wildcard conflict.
        const auto segment = child.type_ != type::catch_all
                                     ? *esl::strings::split(path, '/').begin()
                                     : path;
        std::string prefix{full_path.substr(0, full_path.find(segment))};
        prefix += child.path_;
        throw std::invalid_argument(
                fmt::format("'{}' in path '{}' conflicts with existing wildcard '{}' in '{}'",
                            segment, full_path, child.path_, prefix));
    }

    const auto idxc = path[0];

    // If current node is a param then its `indices_` is always empty, and the node can have
    // at most one child, and the path of this child either is `/` or starts with `/`.
    if (type_ == type::param && idxc == '/' && !children_.empty()) {
        assert(children_.size() == 1);
        auto& child = *children_.front();
        ++child.priority_;
        child.insert_route(path, full_path, std::move(handler));
        return;
    }

    // Check if a child with the next path character exists.
    for (std::size_t i = 0; i < indices_.size(); ++i) {
        if (const auto c = indices_[i]; c == idxc) {
            i = increment_child_priority(i);
            children_[i]->insert_route(path, full_path, std::move(handler));
            return;
        }
    }

    if (idxc != ':' && idxc != '*') {
        indices_ += idxc;
        children_.push_back(std::make_unique<node>());
        auto& child = *children_.back();
        esl::ignore_unused(increment_child_priority(indices_.size() - 1));
        child.insert_path(path, full_path, detail::unknown_wildcard, std::move(handler));
        return;
    }

    insert_path(path, full_path, detail::unknown_wildcard, std::move(handler));
}

void node::insert_path(std::string_view path, std::string_view full_path,
                       detail::wildcard_result wildcard, route_handler_t&& handler) {
    // The invocation with no wildcard means we haven't scanned the `path` yet.
    // Let's scan the path in flight.
    if (!wildcard.found()) {
        wildcard = detail::find_wildcard(path);
        if (!wildcard.found()) {
            path_ = path;
            handler_ = std::move(handler);
            return;
        }

        if (!wildcard.valid_name()) {
            throw std::invalid_argument(
                    fmt::format("invalid wildcard in path '{}'", full_path));
        }

        if (!children_.empty()) {
            throw std::invalid_argument(fmt::format(
                    "wildcard segment '{}' conflicts with existing children in path '{}'",
                    wildcard.name, full_path));
        }
    }

    if (wildcard.name.starts_with(':')) {
        const auto plain_segments = path.substr(0, wildcard.pos);
        if (!plain_segments.empty()) {
            path_ = plain_segments;
            path.remove_prefix(plain_segments.size());
        }

        has_wild_child_ = true;

        // The param node.
        children_.push_back(std::make_unique<node>());
        auto& child = *children_.back();
        child.priority_ = 1;
        child.type_ = type::param;
        child.path_ = wildcard.name;

        // The path ends with the wildcard, the param node is the leaf.
        if (path.size() == wildcard.name.size()) {
            child.handler_ = std::move(handler);
            return;
        }

        // There are another non-wildcard subpath.
        child.children_.push_back(std::make_unique<node>());
        auto& grand_child = *child.children_.back();
        grand_child.priority_ = 1;
        grand_child.insert_path(path.substr(wildcard.name.size()),
                                full_path,
                                detail::unknown_wildcard,
                                std::move(handler));
    } else {
        if (wildcard.pos + wildcard.name.size() != path.size()) {
            throw std::invalid_argument(fmt::format(
                    "catch-all is only allowed at the end of the path in '{}'", full_path));
        }

        // e.g. `/hello/*name` would conflict with `/hello/` but not `/hello`.
        if (!path_.empty() && path_.ends_with('/')) {
            throw std::invalid_argument(fmt::format(
                    "catch-all conflicts with existing handle for path segment root in '{}'",
                    full_path));
        }

        // Move to leading `/`.
        assert(wildcard.pos > 0);
        if (path[--wildcard.pos] != '/') {
            throw std::invalid_argument(fmt::format(
                    "no / before catch-all in path '{}'", full_path));
        }

        path_ = path.substr(0, wildcard.pos);

        // First node is a catch-all node but with empty path.
        auto child = std::make_unique<node>();
        child->has_wild_child_ = true;
        child->type_ = type::catch_all;
        child->priority_ = 1;

        // Second node is a catch-all node holding the variable.
        auto grand_child = std::make_unique<node>();
        grand_child->path_ = path.substr(wildcard.pos);
        grand_child->type_ = type::catch_all;
        grand_child->priority_ = 1;
        grand_child->handler_ = std::move(handler);

        child->children_.push_back(std::move(grand_child));
        children_.push_back(std::move(child));
        indices_ = '/';
    }
}

std::size_t node::increment_child_priority(std::size_t pos) {
    assert(indices_.size() == children_.size());

    const auto prio = ++(children_[pos]->priority_);

    // Insertion sort like reordering.

    std::size_t new_pos = pos;
    for (; new_pos > 0 && children_[new_pos - 1]->priority_ < prio; --new_pos) {
        ;
    }

    if (new_pos != pos) {
        const auto begin_difft = static_cast<std::ptrdiff_t>(new_pos);
        const auto end_difft = static_cast<std::ptrdiff_t>(pos + 1);

        auto child = std::move(children_[pos]);
        std::shift_right(std::next(children_.begin(), begin_difft),
                         std::next(children_.begin(), end_difft),
                         1);
        children_[new_pos] = std::move(child);

        const auto idxc = indices_[pos];
        std::shift_right(std::next(indices_.begin(), begin_difft),
                         std::next(indices_.begin(), end_difft),
                         1);
        indices_[new_pos] = idxc;
    }

    return new_pos;
}

} // namespace fawkes
