// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <fmt/format.h>

namespace fawkes {
namespace detail {

// `key` refs to the path in a route tree node.
struct param {
    std::string_view key;
    std::string value;

    friend bool operator==(const param& lhs, const param& rhs) noexcept = default;
};

} // namespace detail

class path_params {
public:
    // Caller must make sure `key` outlives the path_params.
    void add(std::string_view key, std::string_view value) {
        ps_.push_back({.key = key, .value = std::string{value}});
    }

    // Throws `std::out_of_range` if there is no match.
    [[nodiscard]] std::string_view get(std::string_view key) const {
        const auto it = std::ranges::find_if(ps_, [key](const detail::param& param) {
            return param.key == key;
        });
        if (it == ps_.end()) {
            throw std::out_of_range(fmt::format("param with key={} not found", key));
        }
        return std::string_view{it->value};
    }

    [[nodiscard]] std::optional<std::string_view> try_get(std::string_view key) const {
        const auto it = std::ranges::find_if(ps_, [key](const detail::param& param) {
            return param.key == key;
        });
        if (it == ps_.end()) {
            return std::nullopt;
        }
        return std::optional<std::string_view>(std::in_place, it->value);
    }

    friend bool operator==(const path_params& lhs, const path_params& rhs) noexcept = default;

private:
    static constexpr std::size_t inline_param_count = 4UZ;
    boost::container::small_vector<detail::param, inline_param_count> ps_;
};

} // namespace fawkes
