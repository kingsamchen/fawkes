// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <boost/beast/http/status.hpp>

namespace fawkes {

namespace http = boost::beast::http;

class http_error : public std::runtime_error {
public:
    http_error(http::status error_status, const std::string& what)
        : std::runtime_error(what),
          error_status_(error_status) {}

    http_error(http::status error_status, int error_code, const std::string& what)
        : std::runtime_error(what),
          error_status_(error_status),
          error_code_(error_code) {}

    [[nodiscard]] http::status status_code() const noexcept {
        return error_status_;
    }

    [[nodiscard]] const std::optional<int>& error_code() const noexcept {
        return error_code_;
    }

private:
    http::status error_status_;
    std::optional<int> error_code_;
};

} // namespace fawkes
