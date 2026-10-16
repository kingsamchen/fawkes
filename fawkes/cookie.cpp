// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/cookie.hpp"

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <absl/strings/ascii.h>
#include <absl/strings/str_split.h>
#include <boost/url/grammar.hpp>
#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace fawkes {

namespace urls = boost::urls;

namespace {

// token = 1*tchar
// tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "." /
//         "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
bool is_token(std::string_view str) {
    static constexpr auto token_chars = urls::grammar::lut_chars("!#$%&'*+-.^_`|~") +
                                        urls::grammar::alnum_chars;
    return urls::grammar::parse(str, urls::grammar::token_rule(token_chars)).has_value();
}

bool valid_cookie_value(std::string_view value) noexcept {
    // cookie-octet = %x21 / %x23-2B / %x2D-3A / %x3C-5B / %x5D-7E
    // lut_chars will be optimized into a lookup table at compile time.
    static constexpr auto cookie_octets = urls::grammar::lut_chars([](char ch) noexcept {
        return 0x21 <= ch && ch <= 0x7E && ch != '"' && ch != ',' && ch != ';' && ch != '\\';
    });
    return std::ranges::all_of(value, cookie_octets);
}

bool valid_cookie_path_value(std::string_view value) noexcept {
    return std::ranges::all_of(value, [](unsigned char ch) noexcept {
        return 0x20 <= ch && ch < 0x7F && ch != ';';
    });
}

bool valid_cookie_domain(std::string_view domain) noexcept {
    constexpr std::size_t max_domain_length = 253;
    constexpr std::size_t max_label_length = 63;
    if (domain.empty() || domain.size() > max_domain_length) {
        return false;
    }

    std::size_t label_length = 0;
    bool label_ends_with_alnum = false;
    for (const char ch : domain) {
        const bool is_alnum = absl::ascii_isalnum(static_cast<unsigned char>(ch));
        if (is_alnum) {
            if (++label_length > max_label_length) {
                return false;
            }
            label_ends_with_alnum = true;
        } else if (ch == '.') {
            if (label_length == 0 || !label_ends_with_alnum) {
                return false;
            }
            label_length = 0;
            label_ends_with_alnum = false;
        } else if (ch == '-' && label_length != 0) {
            if (++label_length > max_label_length) {
                return false;
            }
            label_ends_with_alnum = false;
        } else {
            return false;
        }
    }
    return label_length != 0 && label_ends_with_alnum;
}

bool valid_expires(std::chrono::sys_seconds expires) noexcept {
    using namespace std::chrono_literals;
    constexpr std::chrono::sys_seconds min_expires = std::chrono::sys_days{1601y / 1 / 1};
    // Convert to seconds first; hours and minutes may use 32-bit counts on some implementations.
    constexpr std::chrono::sys_seconds max_expires =
        std::chrono::sys_seconds{std::chrono::sys_days{9999y / 12 / 31}} + 23h + 59min + 59s;
    // As per RFC, the year must not be less than 1601; we also add a upper bound year check
    // to make it more reasonable.
    return min_expires <= expires && expires <= max_expires;
}

} // namespace

void cookie_view::parse_cookie_value(std::string_view cookie_value) {
    auto pairs = absl::StrSplit(cookie_value, ';', absl::SkipEmpty{});
    for (auto p : pairs) {
        const auto trimmed = absl::StripAsciiWhitespace(p);
        // Cookie value may contain `=`.
        std::pair<std::string_view, std::string_view> fields =
            absl::StrSplit(trimmed, absl::MaxSplits('=', 1));
        auto& [name, value] = fields;
        if (name.empty()) {
            SPDLOG_WARN("Malformed cookie entry, skipped; cookie={}", trimmed);
            continue;
        }

        if (!is_token(name)) {
            SPDLOG_WARN("Name of the cookie entry is not a http token, skipped; name={}", name);
            continue;
        }

        // Remove surrounding quotes.
        if (value.size() >= 2 && value.starts_with('"') && value.ends_with('"')) {
            value.remove_prefix(1);
            value.remove_suffix(1);
        }

        if (!valid_cookie_value(value)) {
            SPDLOG_WARN("Invalid value of the cookie entry, skipped; {}={}", name, value);
            continue;
        }

        // Preserve duplicates in encounter order.
        cookies_[name].push_back(value);
    }
}

std::string cookie::to_string() const {
    std::string str;
    if (name.empty() || !is_token(name)) {
        SPDLOG_ERROR("Invalid cookie name, abort; name={}", name);
        return str;
    }

    if (!valid_cookie_value(value)) {
        SPDLOG_ERROR("Invalid cookie value, abort; name={}", name);
        return str;
    }

    // Derived from typical length of cookie attributes, rule of thumb.
    constexpr std::size_t extra_cookie_len = 110;
    str.reserve(name.size() + value.size() + domain.size() + path.size() + extra_cookie_len);

    str.append(name).append(1, '=').append(value);

    if (!path.empty()) {
        if (valid_cookie_path_value(path)) {
            str.append("; Path=").append(path);
        } else {
            SPDLOG_WARN("Invalid path value, skipped; path={}", path);
        }
    }

    if (!domain.empty()) {
        // Leading `.` is no longer required.
        const auto domain_value = domain.starts_with('.') ? std::string_view{domain}.substr(1)
                                                          : std::string_view{domain};
        if (valid_cookie_domain(domain_value)) {
            str.append("; Domain=").append(domain_value);
        } else {
            SPDLOG_WARN("Invalid domain value, skipped; domain={}", domain);
        }
    }

    if (max_age.has_value()) {
        str.append("; Max-Age=").append(std::to_string(max_age->count()));
    }

    if (expires.has_value()) {
        if (valid_expires(*expires)) {
            // Format calendar components directly to avoid platform-specific gmtime limits.
            const auto date = std::chrono::floor<std::chrono::days>(*expires);
            str.append("; Expires=")
                .append(fmt::format("{:%a}, {:%d %b %Y} {:%H:%M:%S} GMT",
                                    std::chrono::weekday{date}, std::chrono::year_month_day{date},
                                    *expires - date));
        } else {
            SPDLOG_WARN("Invalid expires value, skipped; expires_in_seconds={}",
                        expires->time_since_epoch());
        }
    }

    if (secure) {
        str.append("; Secure");
    }

    if (http_only) {
        str.append("; HttpOnly");
    }

    // Undefined behavior if not in one of pre-defined values.
    if (same_site != same_site_policy::use_default) {
        str.append("; SameSite=").append(fawkes::to_string(same_site));
    }

    return str;
}

std::expected<void, cookie_error> cookie::validate() const {
    if (name.empty() || !is_token(name)) {
        return std::unexpected{cookie_error::invalid_name};
    }

    if (!valid_cookie_value(value)) {
        return std::unexpected{cookie_error::invalid_value};
    }

    if (!path.empty() && !valid_cookie_path_value(path)) {
        return std::unexpected{cookie_error::invalid_path};
    }

    if (!domain.empty()) {
        const auto domain_value = domain.starts_with('.') ? std::string_view{domain}.substr(1)
                                                          : std::string_view{domain};
        if (!valid_cookie_domain(domain_value)) {
            return std::unexpected{cookie_error::invalid_domain};
        }
    }

    if (expires.has_value() && !valid_expires(*expires)) {
        return std::unexpected{cookie_error::invalid_expires};
    }

    return {};
}

} // namespace fawkes
