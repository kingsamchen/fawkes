// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <boost/beast/http/verb.hpp>
#include <boost/url.hpp>
#include <doctest/doctest.h>

#include "fawkes/middleware.hpp"
#include "fawkes/middlewares/cors.hpp"
#include "test_utils/stringification.hpp" // IWYU pragma: keep

namespace {

namespace http = boost::beast::http;
namespace urls = boost::urls;

TEST_SUITE_BEGIN("Middlewares/cors");

TEST_CASE("Allow origins policy") {
    SUBCASE("from initializer list") {
        const fawkes::cors::allow_origins allows{"foo.com",
                                                 std::string{"bar.com"},
                                                 std::string_view{"example.com"}};
        REQUIRE_EQ(allows.origins.size(), 3);
        CHECK(allows.origins.contains("foo.com"));
        CHECK(allows.origins.contains("bar.com"));
        CHECK(allows.origins.contains("example.com"));
        CHECK_FALSE(allows.origins.contains("test.co"));
    }

    SUBCASE("from range iterator") {
        std::vector<std::string_view> sv{"foo.com", "bar.com", "example.com"};
        const fawkes::cors::allow_origins allows(sv.begin(), sv.end());
        REQUIRE_EQ(allows.origins.size(), 3);
        CHECK(allows.origins.contains("foo.com"));
        CHECK(allows.origins.contains("bar.com"));
        CHECK(allows.origins.contains("example.com"));
        CHECK_FALSE(allows.origins.contains("test.co"));
    }

    SUBCASE("heterogenous lookup") {
        const fawkes::cors::allow_origins allows{"foo.com",
                                                 std::string{"bar.com"},
                                                 std::string_view{"example.com"}};
        REQUIRE_EQ(allows.origins.size(), 3);
        CHECK(allows.origins.contains(std::string_view{"foo.com"}));
        CHECK_FALSE(allows.origins.contains(std::string_view{"test.co"}));
    }
}

TEST_CASE("Allow an origin that meets predicate") {
    const fawkes::cors::allow_origin_if allow_if{[](std::string_view origin) {
        return origin.starts_with("test.");
    }};

    CHECK(allow_if.predicate("test.example.com"));
    CHECK_FALSE(allow_if.predicate("example.com"));
}

TEST_CASE("Handle simple CORS request") {
    fawkes::request req;
    req.header().set("Origin", "http://deadbeef.me:8080");
    req.header().set("Host", "cors-test.com");
    req.header().method(http::verb::get);

    fawkes::response resp;
    const fawkes::cors::options opts{.allow_origin_policy = fawkes::cors::allow_origin_if(
                                         [](std::string_view origin) {
                                             const urls::url_view url(origin);
                                             return url.host() == "deadbeef.me";
                                         }),
                                     .allow_methods{},
                                     .allow_headers{},
                                     .expose_headers{http::field::accept}};
    const fawkes::cors cors(opts);
    auto result = cors.pre_handle(req, resp);
    REQUIRE_EQ(result, fawkes::middleware_result::proceed);
    CHECK_EQ(resp.header()["Access-Control-Allow-Origin"], "http://deadbeef.me:8080");
    CHECK_EQ(resp.header()["Access-Control-Expose-Headers"], "Accept");
    CHECK_EQ(resp.header()["Vary"], "Origin");
}

TEST_CASE("Handle preflight request") {
    fawkes::request req;
    req.header().set("Origin", "http://deadbeef.me:8080");
    req.header().set("Host", "cors-test.com");
    req.header().method(http::verb::options);

    fawkes::response resp;
    const fawkes::cors::options opts{.allow_origin_policy = fawkes::cors::allow_origin_if(
                                         [](std::string_view origin) {
                                             const urls::url_view url(origin);
                                             return url.host() == "deadbeef.me";
                                         }),
                                     .allow_methods{http::verb::get,
                                                    http::verb::post,
                                                    http::verb::put},
                                     .allow_headers{http::field::content_type},
                                     .expose_headers{}};

    const fawkes::cors cors(opts);
    auto result = cors.pre_handle(req, resp);
    REQUIRE_EQ(result, fawkes::middleware_result::abort);

    CHECK_EQ(resp.header()["Access-Control-Allow-Origin"], "http://deadbeef.me:8080");
    CHECK_EQ(resp.header()["Access-Control-Allow-Methods"], "GET, POST, PUT");
    CHECK_EQ(resp.header()["Access-Control-Allow-Headers"], "Content-Type");

    auto vary_range = resp.header().equal_range("Vary");
    std::set<std::string_view> varys;
    for (const auto& field : std::ranges::subrange(vary_range.first, vary_range.second)) {
        varys.insert(field.value());
    }
    CHECK_EQ(varys, std::set<std::string_view>{"Origin",
                                               "Access-Control-Request-Method",
                                               "Access-Control-Request-Headers"});
}

TEST_SUITE_END(); // Middlewares/cors

} // namespace
