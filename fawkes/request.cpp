// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/request.hpp"

#include <string_view>
#include <utility>

#include <boost/url/parse.hpp>
#include <boost/url/parse_query.hpp>
#include <spdlog/spdlog.h>

#include "fawkes/errors.hpp"

namespace fawkes {

request::request(impl_type&& req_impl)
    : impl_(std::move(req_impl)) {
    const auto target = impl_.target();
    const auto pos = target.find('?');
    const auto path = target.substr(0, pos);
    auto or_path = urls::parse_origin_form(path);
    if (or_path.has_error()) {
        throw http_error(http::status::bad_request, "invalid url path");
    }

    url_ = *or_path;
    url_.path(urls::string_token::assign_to(path_));

    if (pos != std::string_view::npos) {
        // Discard whole query string if it is malformed.
        const auto or_query = urls::parse_query(target.substr(pos + 1));
        if (or_query.has_error()) {
            spdlog::error("malformed query string discarded");
        } else {
            url_.encoded_params().assign(or_query->begin(), or_query->end());
        }
    }
}

} // namespace fawkes
