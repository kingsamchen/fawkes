// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/request.hpp"

#include <string_view>
#include <utility>

#include <boost/beast/http/status.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/parse_query.hpp>
#include <spdlog/spdlog.h>

#include "fawkes/errors.hpp"

namespace fawkes {

request::request(impl_type&& req_impl, conn_info&& info)
    : conn_info_(std::move(info)),
      impl_(std::move(req_impl)) {
    const auto target = impl_.target();
    const auto pos = target.find('?');
    const auto path = target.substr(0, pos);
    auto or_path = urls::parse_origin_form(path);
    if (or_path.has_error()) {
        throw invalid_request_target("invalid request path", target);
    }

    url_ = *or_path;
    url_.path(urls::string_token::assign_to(path_));

    if (pos != std::string_view::npos) {
        // Discard whole query string if it is malformed.
        const auto raw_query = target.substr(pos + 1);
        const auto or_query = urls::parse_query(raw_query);
        if (or_query.has_error()) {
            SPDLOG_ERROR("malformed query string discarded; query={}", raw_query);
        } else {
            url_.encoded_params().assign(or_query->begin(), or_query->end());
        }
    }
}

} // namespace fawkes
