// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

namespace fawkes {

namespace beast = boost::beast;

class response {
public:
    using impl_type = beast::http::response<beast::http::string_body>;

    [[nodiscard]] const impl_type::header_type& header() const noexcept {
        return impl_.base();
    }

    [[nodiscard]] impl_type::header_type& mutable_header() noexcept {
        return impl_.base();
    }

    [[nodiscard]] const auto& body() const noexcept {
        return impl_.body();
    }

    [[nodiscard]] auto& mutable_body() noexcept {
        return impl_.body();
    }

    [[nodiscard]] const impl_type& as_impl() const noexcept {
        return impl_;
    }

    [[nodiscard]] impl_type& as_mutable_impl() noexcept {
        return impl_;
    }

private:
    impl_type impl_;
};

} // namespace fawkes
