// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>

#include "fawkes/cookie.hpp"
#include "fawkes/mime.hpp"

namespace fawkes {

namespace http = boost::beast::http;

class response {
public:
    using impl_type = http::response<http::string_body>;

    response() = default;

    explicit response(impl_type&& resp_impl)
        : impl_(std::move(resp_impl)) {}

    response(unsigned int version, bool keep_alive) {
        impl_.version(version);
        impl_.keep_alive(keep_alive);
        impl_.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    }

    [[nodiscard]] const impl_type::header_type& header() const noexcept {
        return impl_.base();
    }

    [[nodiscard]] impl_type::header_type& header() noexcept {
        return impl_.base();
    }

    // If the actual status code is not a known code, this function returns `status::unknown`,
    // call `status_code()` to return the status code number.
    [[nodiscard]] http::status status() const noexcept {
        return impl_.result();
    }

    // Returns the status code number.
    [[nodiscard]] std::uint32_t status_code() const noexcept {
        return impl_.result_int();
    }

    void set_status(http::status status) {
        impl_.result(status);
    }

    void set_status_code(std::uint32_t status_code) {
        impl_.result(status_code);
    }

    void text(http::status status, std::string_view text) {
        impl_.result(status);
        impl_.set(http::field::content_type, mime::text);
        impl_.body() = text;
    }

    void text(http::status status, std::string&& text) {
        impl_.result(status);
        impl_.set(http::field::content_type, mime::text);
        impl_.body() = std::move(text);
    }

    void json(http::status status, std::string_view data) {
        impl_.result(status);
        impl_.set(http::field::content_type, mime::json);
        impl_.body() = data;
    }

    void json(http::status status, std::string&& data) {
        impl_.result(status);
        impl_.set(http::field::content_type, mime::json);
        impl_.body() = std::move(data);
    }

    void add_set_cookie(const cookie& cookie) {
        const auto value = cookie.to_string();
        if (!value.empty()) {
            header().insert(http::field::cookie, value);
        }
    }

    // TODO(KC): add set_cookies() for http-client uses, and it should parse all Set-Cookie headers
    // and return them.

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
};

static_assert(std::is_nothrow_move_constructible_v<response> &&
              std::is_nothrow_move_assignable_v<response>);

} // namespace fawkes
