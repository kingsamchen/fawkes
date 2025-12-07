// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/cookie.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <boost/url/authority_view.hpp>
#include <boost/url/decode_view.hpp>
#include <boost/url/encode.hpp>
#include <boost/url/grammar.hpp>
#include <boost/url/rfc/pct_encoded_rule.hpp>
#include <boost/url/rfc/unreserved_chars.hpp>
#include <esl/strings.h>
#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

namespace fawkes {

namespace urls = boost::urls;

namespace {

constexpr std::string_view ascii_space = " \t\r\n";

// token = 1*tchar
// tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "." /
//         "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
bool is_token(std::string_view str) {
    static constexpr auto token_chars = urls::grammar::lut_chars("!#$%&'*+-.^_`|~") +
                                        urls::grammar::alnum_chars;
    return urls::grammar::parse(str, urls::grammar::token_rule(token_chars)).has_value();
}

bool valid_cookie_path_value(std::string_view value) noexcept {
    return std::ranges::all_of(value, [](unsigned char ch) noexcept {
        return 0x20 <= ch && ch < 0x7F && ch != ';';
    });
}

bool valid_cookie_domain(std::string_view domain) noexcept {
    return urls::parse_authority(domain).has_value();
}

bool valid_expires(std::chrono::sys_seconds expires) noexcept {
    using namespace std::chrono_literals;
    const auto ymd = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(expires)};
    // As per RFC, the year must not be less than 1601.
    return ymd.ok() && ymd.year() >= 1601y; // NOLINT(readability-magic-numbers)
}

} // namespace

void cookie_view::parse_cookie_value(std::string_view cookie_value) {
    static constexpr std::string_view empty_value;

    auto pairs = esl::strings::split(cookie_value, ';', esl::strings::skip_empty{});
    for (auto p : pairs) {
        const auto trimmed = esl::strings::trim(p, ascii_space);
        auto fields = esl::strings::split(trimmed, '=')
                          .to<std::vector<std::string_view>>();
        // Must be `key=value`, while the `value` is allowed to be empty but the `name` can't
        // be empty.
        if (fields.size() > 2 || fields[0].empty()) {
            SPDLOG_WARN("Malformed cookie entry, skipped; cookie={}", trimmed);
            continue;
        }

        // Strictly, RFC doesn't allow an empty value without `=`, however a lot of popular
        // frameworks choose to support this case.
        if (fields.size() == 1) {
            fields.emplace_back(empty_value);
        }

        if (!is_token(fields[0])) {
            SPDLOG_WARN("Name of the cookie entry is not a http token, skipped; name={}",
                        fields[0]);
            continue;
        }

        // `urls::pct_string_view` has a rather relaxed parsing rule that would allow characters
        // like whitespace.
        if (urls::grammar::parse(fields[1], urls::pct_encoded_rule(urls::unreserved_chars))
                .has_error()) {
            SPDLOG_WARN("Invalid value of the cookie entry, skipped; {}={}", fields[0], fields[1]);
            continue;
        }

        // If there are multiple pairs with the same name, only store the first to be compliant
        // with RFC first-match policy.
        cookies_.insert({fields[0], urls::decode_view(fields[1])});
    }
}

std::string cookie::to_string() const {
    std::string str;
    if (name.empty() || !is_token(name)) {
        SPDLOG_ERROR("Invalid cookie name, abort; name={}", name);
        return str;
    }

    // Derived from typical length of cookie attributes, rule of thumb.
    constexpr std::size_t extra_cookie_len = 110;
    str.reserve(name.size() + value.size() + domain.size() + path.size() + extra_cookie_len);

    str.append(name).append(1, '=').append(urls::encode(value, urls::unreserved_chars));

    if (!path.empty()) {
        if (valid_cookie_path_value(path)) {
            str.append("; Path=").append(path);
        } else {
            SPDLOG_WARN("Invalid path value, skipped; path={}", path);
        }
    }

    if (!domain.empty()) {
        if (valid_cookie_domain(domain)) {
            // Leading `.` is no longer required.
            const auto sv = domain.starts_with('.') ? std::string_view{domain}.substr(1)
                                                    : std::string_view{domain};
            str.append("; Domain=").append(sv);
        } else {
            SPDLOG_WARN("Invalid domain value, skipped; domain={}", domain);
        }
    }

    if (max_age.has_value()) {
        str.append("; Max-Age=").append(std::to_string(max_age->count()));
    }

    if (expires.has_value()) {
        if (valid_expires(*expires)) {
            str.append("; Expires=").append(fmt::format("{:%a, %d %b %Y %H:%M:%S} GMT", *expires));
        } else {
            // Some extreme time_point values may cause format error.
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

} // namespace fawkes
