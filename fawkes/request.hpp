// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <any>
#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/url/url.hpp>

#include "fawkes/cookie.hpp"
#include "fawkes/path_params.hpp"
#include "fawkes/query_params.hpp"

namespace fawkes {

namespace asio = boost::asio;
namespace urls = boost::urls;
namespace http = boost::beast::http;

struct conn_info {
    asio::ip::tcp::endpoint local;
    asio::ip::tcp::endpoint remote;
};

class request {
public:
    using impl_type = http::request<http::string_body>;
    using header_type = impl_type::header_type;

    request() = default;

    // Throws `std::invalid_argument` if path part of the URL is invalid.
    request(impl_type&& req_impl, conn_info&& info);

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

    [[nodiscard]] const asio::ip::tcp::endpoint& local_endpoint() const noexcept {
        return conn_info_.local;
    }

    [[nodiscard]] const asio::ip::tcp::endpoint& remote_endpoint() const noexcept {
        return conn_info_.remote;
    }

    // Throws `std::bad_any_cast` if `T` does not match the type of the managed
    // content, or the `ctx_` does not have a value.
    template<typename T>
    requires(!std::is_reference_v<T>)
    [[nodiscard]] T& context_as() {
        return std::any_cast<T&>(ctx_);
    }

    // Throws `std::bad_any_cast` if `T` does not match the type of the managed
    // content, or the `ctx_` does not have a value.
    template<typename T>
    requires(!std::is_reference_v<T>)
    [[nodiscard]] const T& context_as() const {
        return std::any_cast<const T&>(ctx_);
    }

    [[nodiscard]] std::any& context() noexcept {
        return ctx_;
    }

    [[nodiscard]] const std::any& context() const noexcept {
        return ctx_;
    }

    [[nodiscard]] const impl_type& as_impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] impl_type& as_impl() noexcept {
        return impl_;
    }

private:
    conn_info conn_info_;
    impl_type impl_;
    urls::url url_;
    std::string path_; // Percent-decoded.
    path_params ps_;
    std::any ctx_;
};

static_assert(std::is_nothrow_move_constructible_v<request> &&
              std::is_nothrow_move_assignable_v<request> &&
              std::copyable<request>);

} // namespace fawkes
