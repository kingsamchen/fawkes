// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <boost/beast/http/message.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

namespace fawkes {

namespace http = boost::beast::http;

// The request header refered by a `cookie_view` must retain valid.
class cookie_view {
public:
    using iterator = http::request_header<>::iterator;

    cookie_view(iterator begin, iterator end) {
        if (begin == end) {
            return;
        }

        const std::size_t cookie_cnt = estimate_cookie_count(begin->value());
        constexpr std::size_t should_reserve_minimum = 3;
        if (cookie_cnt > should_reserve_minimum) {
            cookies_.reserve(cookie_cnt);
        }

        for (const auto& field : std::ranges::subrange(begin, end)) {
            parse_cookie_value(field.value());
        }
    }

    [[nodiscard]] bool empty() const noexcept {
        return cookies_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return cookies_.size();
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return cookies_.contains(name);
    }

    // Returns the first value matched.
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept {
        auto it = cookies_.find(name);
        if (it == cookies_.end()) {
            return std::nullopt;
        }

        assert(!it->second.empty());
        return std::make_optional<std::string_view>(it->second[0]);
    }

    // Returns all values matched; the view must retain valid.
    [[nodiscard]] std::span<const std::string_view> get_all(std::string_view name) const& noexcept {
        auto it = cookies_.find(name);
        if (it == cookies_.end()) {
            return {};
        }

        return it->second;
    }

    [[nodiscard]] std::span<const std::string_view> get_all(std::string_view name) const&& = delete;

private:
    static std::size_t estimate_cookie_count(std::string_view value) {
        return static_cast<std::size_t>(std::ranges::count(value, ';')) + 1;
    }

    // Encodings are preserved, but surrounding quotes will be removed.
    void parse_cookie_value(std::string_view cookie_value);

    using value_type = boost::container::small_vector<std::string_view, 1>;
    boost::unordered_flat_map<std::string_view, value_type> cookies_;
};

enum class same_site_policy : std::uint8_t {
    use_default, // not emitting the attribute
    strict,
    lax,
    none,
};

enum class cookie_error {
    invalid_name = 1,
    invalid_value,
    invalid_path,
    invalid_domain,
    invalid_expires,
};

[[nodiscard]] inline std::string_view to_string(same_site_policy policy) {
    using enum same_site_policy;
    switch (policy) {
    case strict:
        return "Strict";
    case lax:
        return "Lax";
    case none:
        return "None";
    default:
        return "Default";
    }
}

struct cookie {
    std::string name;
    std::string value; // Encode arbitrary data before setting.

    std::string path;
    std::string domain;

    // `max_age` <= 0 means should expire the cookie immediately.
    std::optional<std::chrono::seconds> max_age;
    std::optional<std::chrono::sys_seconds> expires;
    std::string raw_expires; // for parsing `Set-Cookie` only.

    bool http_only{false};
    bool secure{false};

    same_site_policy same_site{same_site_policy::use_default};

    cookie(std::string c_name, std::string c_value)
        : name(std::move(c_name)),
          value(std::move(c_value)) {}

    // Returns an empty string if `name` is not an HTTP token or `value` contains
    // forbidden cookie bytes. Valid values are emitted unchanged.
    // Invalid optional attributes are skipped.
    [[nodiscard]] std::string to_string() const;

    // Checks serialization syntax and expiration range, returning the first error encountered.
    [[nodiscard]] std::expected<void, cookie_error> validate() const;
};

} // namespace fawkes
