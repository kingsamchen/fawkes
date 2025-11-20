// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <chrono>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <esl/ignore_unused.h>
#include <esl/utility.h>

#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"

namespace fawkes {

namespace http = boost::beast::http;

class cors {
public:
    struct allow_origins {
        struct string_hash : std::hash<std::string_view> {
            using is_transparent = void;
            using is_avalanching = void;
        };

        boost::unordered_flat_set<std::string, string_hash, std::equal_to<>> origins;

        template<std::input_iterator It>
        allow_origins(It begin, It end)
            : origins(begin, end) {}

        allow_origins(std::initializer_list<std::string_view> init)
            : origins(init.begin(), init.end()) {}
    };

    struct allow_origin_if {
        std::function<bool(std::string_view)> predicate;

        template<typename F>
        requires std::invocable<F, std::string_view>
        explicit allow_origin_if(F&& fn)
            : predicate(std::forward<F>(fn)) {}
    };

    // Not compatible with `allow_credentials` as-per RFC.
    // Don't use this policy on production.
    struct allow_origin_all {};

    using allow_origin_policy_t = std::variant<allow_origins, allow_origin_if, allow_origin_all>;

    struct options {
        allow_origin_policy_t allow_origin_policy;
        std::vector<http::verb> allow_methods;
        std::vector<http::field> allow_headers;
        std::vector<http::field> expose_headers;
        std::chrono::seconds max_age{0};
        bool allow_private_network{false};
        bool allow_credentials{false};
        http::status options_resp_status{http::status::no_content};
    };

    explicit cors(const options& opts)
        : preflight_hdrs(generate_preflight_headers(opts)),
          cors_hdrs(generate_cors_headers(opts)),
          allow_origin_policy_(opts.allow_origin_policy),
          options_resp_status_(opts.options_resp_status) {}

    explicit cors(options&& opts) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
        : preflight_hdrs(generate_preflight_headers(opts)),
          cors_hdrs(generate_cors_headers(opts)),
          allow_origin_policy_(std::move(opts.allow_origin_policy)),
          options_resp_status_(opts.options_resp_status) {}

    middleware_result pre_handle(request& req, response& resp) const;

private:
    [[nodiscard]] static bool is_origin_same_as_host(std::string_view origin, const request& req);

    [[nodiscard]] bool is_origin_allowed(std::string_view origin) const {
        return std::visit(
            esl::overloaded{
                [](allow_origin_all) -> bool {
                    return true;
                },
                [origin](const allow_origins& allowed) -> bool {
                    return allowed.origins.contains(origin);
                },
                [origin](const allow_origin_if& allowed) -> bool {
                    return allowed.predicate(origin);
                }},
            allow_origin_policy_);
    }

    void handle_preflight(response::impl_type::header_type& resp_hdr) const {
        apply_headers(preflight_hdrs, resp_hdr);
    }

    void handle_normal_cors(response::impl_type::header_type& resp_hdr) const {
        apply_headers(cors_hdrs, resp_hdr);
    }

    // Hardcoded because it is not exposed publicly.
    static constexpr size_t maximum_value_cnt = 3;
    using header_values = boost::container::static_vector<std::string, maximum_value_cnt>;
    using http_header = boost::container::flat_map<std::string_view, header_values>;

    [[nodiscard]] static http_header generate_preflight_headers(const options& opts);

    [[nodiscard]] static http_header generate_cors_headers(const options& opts);

    static void apply_headers(const http_header& src_hdrs,
                              response::impl_type::header_type& resp_hdr) {
        for (const auto& src_hdr : src_hdrs) {
            esl::ignore_unused(resp_hdr.erase(src_hdr.first));
        }

        for (const auto& [name, values] : src_hdrs) {
            assert(!values.empty());
            for (const auto& value : values) {
                resp_hdr.insert(name, value);
            }
        }
    }

    http_header preflight_hdrs;
    http_header cors_hdrs;
    allow_origin_policy_t allow_origin_policy_;
    http::status options_resp_status_;
};

} // namespace fawkes
