// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include <boost/beast/http/message.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/url/decode_view.hpp>

namespace fawkes {

namespace http = boost::beast::http;
namespace urls = boost::urls;

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

    [[nodiscard]] std::optional<std::string> get(std::string_view name) const {
        auto it = cookies_.find(name);
        if (it == cookies_.end()) {
            return std::nullopt;
        }

        return std::string(it->second.begin(), it->second.end());
    }

private:
    static std::size_t estimate_cookie_count(std::string_view value) {
        return static_cast<std::size_t>(std::ranges::count(value, ';')) + 1;
    }

    void parse_cookie_value(std::string_view cookie_value);

    boost::unordered_flat_map<std::string_view, urls::decode_view> cookies_;
};

enum class same_site_policy : std::uint8_t {
    use_default, // not emitting the attribute
    strict,
    lax,
    none,
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
    std::string value; // Will percent-escape when stringify.

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

    // If `name` is not a http token, fails and returns an empty string, and `value` will be
    // percent-escaped when necessary.
    // Other fileds will be skipped if invalid.
    [[nodiscard]] std::string to_string() const;
};

} // namespace fawkes
