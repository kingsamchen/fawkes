// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <string>
#include <string_view>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/url/url.hpp>

#include "fawkes/cookie.hpp"
#include "fawkes/path_params.hpp"
#include "fawkes/query_params.hpp"

namespace fawkes {

namespace urls = boost::urls;
namespace http = boost::beast::http;

class request {
public:
    using impl_type = http::request<http::string_body>;

    request() = default;

    // Throws `http_error` if path part of the URL is invalid.
    explicit request(impl_type&& req_impl);

    // Path part of a request target, any percent-escapes are decoded.
    [[nodiscard]] std::string_view path() const noexcept {
        return path_;
    }

    // Request target in http request, may contain percent-escapes.
    // Equals to `as_impl().target()` if whole target is valid, i.e. percent-decoded.
    // If query string of the `as_impl().target()` contains invalid characters, `target()`
    // discards entire query string.
    [[nodiscard]] std::string_view target() const noexcept {
        return std::string_view{url_.data(), url_.size()};
    }

    [[nodiscard]] const path_params& params() const noexcept {
        return ps_;
    }

    [[nodiscard]] path_params& params() noexcept {
        return ps_;
    }

    [[nodiscard]] query_params_view queries() const noexcept {
        const urls::params_view ps = url_.params();
        return query_params_view(ps);
    }

    [[nodiscard]] query_params_ref queries() noexcept {
        const urls::params_ref ps = url_.params();
        return query_params_ref(ps);
    }

    [[nodiscard]] const impl_type::header_type& header() const noexcept {
        return impl_.base();
    }

    [[nodiscard]] impl_type::header_type& header() noexcept {
        return impl_.base();
    }

    [[nodiscard]] cookie_view cookies() const {
        auto [begin, end] = header().equal_range(http::field::cookie);
        return cookie_view(begin, end);
    }

    // TODO(KC): add set_cookies() for http-client uses, and set header directly to avoid extra
    // bookkeeping.

    [[nodiscard]] const auto& body() const noexcept {
        return impl_.body();
    }

    [[nodiscard]] auto& body() noexcept {
        return impl_.body();
    }

    [[nodiscard]] const impl_type& as_impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] impl_type& as_impl() noexcept {
        return impl_;
    }

private:
    impl_type impl_;
    urls::url url_;
    std::string path_; // Percent-decoded.
    path_params ps_;
};

static_assert(std::is_nothrow_move_constructible_v<request> &&
              std::is_nothrow_move_assignable_v<request>);

} // namespace fawkes
