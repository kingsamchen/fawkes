// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/middlewares/cors.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <esl/strings.h>
#include <spdlog/spdlog.h>

#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"

namespace fawkes {
namespace {

constexpr std::string_view hdr_allow_credentials = "Access-Control-Allow-Credentials";
constexpr std::string_view hdr_allow_methods = "Access-Control-Allow-Methods";
constexpr std::string_view hdr_allow_headers = "Access-Control-Allow-Headers";
constexpr std::string_view hdr_allow_private_network = "Access-Control-Allow-Private-Network";
constexpr std::string_view hdr_allow_origin = "Access-Control-Allow-Origin";
constexpr std::string_view hdr_cache_max_age = "Access-Control-Max-Age";
constexpr std::string_view hdr_expose_headers = "Access-Control-Expose-Headers";
constexpr std::string_view hdr_vary = "Vary";

} // namespace

middleware_result cors::pre_handle(request& req, response& resp) const {
    const auto origin_it = req.header().find("Origin");

    // Not a CORS request.
    if (origin_it == req.header().end() || is_origin_same_as_host(origin_it->value(), req)) {
        return middleware_result::proceed;
    }

    if (!is_origin_allowed(origin_it->value())) {
        resp.set_status(http::status::forbidden);
        return middleware_result::abort;
    }

    if (!std::holds_alternative<allow_origin_all>(allow_origin_policy_)) {
        resp.header().set(http::field::access_control_allow_origin, origin_it->value());
    }

    if (req.header().method() == http::verb::options) {
        handle_preflight(resp.header());
        resp.set_status(options_resp_status_);
        return middleware_result::abort;
    }

    handle_normal_cors(resp.header());

    return middleware_result::proceed;
}

// static
bool cors::is_origin_same_as_host(std::string_view origin, const request& req) {
    static constexpr std::string_view schema_http = "http://";
    static constexpr std::string_view schema_https = "https://";

    // As-per RFC, `origin` consists of schema / host / port.
    if (origin.starts_with(schema_http)) {
        origin.remove_prefix(schema_http.size());
    } else if (origin.starts_with(schema_https)) {
        origin.remove_prefix(schema_https.size());
    }

    // `Host` field in request header also carries the port part.
    const auto it = req.header().find("Host");

    // Maybe malformed http/1.1 request.
    // Treat as same as request host to take normal flow.
    if (it == req.header().end()) {
        SPDLOG_WARN("Suspicious request carries no Host field; {} {}",
                    req.header().method_string(), req.header().target());
        return true;
    }

    return origin == it->value();
}

// static
auto cors::generate_preflight_headers(const options& opts) -> http_header {
    http_header hdrs;

    if (opts.allow_credentials) {
        hdrs[hdr_allow_credentials].emplace_back("true");
    }

    if (!opts.allow_methods.empty()) {
        auto methods = esl::strings::join(opts.allow_methods,
                                          ", ",
                                          [](http::verb verb, std::string& out) {
                                              out.append(http::to_string(verb));
                                          });
        hdrs[hdr_allow_methods].push_back(std::move(methods));
    }

    if (!opts.allow_headers.empty()) {
        auto headers = esl::strings::join(opts.allow_headers,
                                          ", ",
                                          [](http::field field, std::string& out) {
                                              out.append(http::to_string(field));
                                          });
        hdrs[hdr_allow_headers].push_back(std::move(headers));
    }

    if (opts.allow_private_network) {
        hdrs[hdr_allow_private_network].emplace_back("true");
    }

    if (opts.max_age > std::chrono::seconds{0}) {
        hdrs[hdr_cache_max_age].push_back(std::to_string(opts.max_age.count()));
    }

    if (std::holds_alternative<allow_origin_all>(opts.allow_origin_policy)) {
        hdrs[hdr_allow_origin].emplace_back("*");
    } else {
        auto& vary = hdrs[hdr_vary];
        vary.emplace_back(http::to_string(http::field::origin));
        vary.emplace_back(http::to_string(http::field::access_control_request_method));
        vary.emplace_back(http::to_string(http::field::access_control_request_headers));
    }

    return hdrs;
}

// static
auto cors::generate_cors_headers(const options& opts) -> http_header {
    http_header hdrs;

    if (opts.allow_credentials) {
        hdrs[hdr_allow_credentials].emplace_back("true");
    }

    if (!opts.expose_headers.empty()) {
        auto headers = esl::strings::join(opts.expose_headers,
                                          ", ",
                                          [](http::field field, std::string& out) {
                                              out.append(http::to_string(field));
                                          });
        hdrs[hdr_expose_headers].push_back(std::move(headers));
    }

    if (std::holds_alternative<allow_origin_all>(opts.allow_origin_policy)) {
        hdrs[hdr_allow_origin].emplace_back("*");
    } else {
        hdrs[hdr_vary].emplace_back(http::to_string(http::field::origin));
    }

    return hdrs;
}

} // namespace fawkes
