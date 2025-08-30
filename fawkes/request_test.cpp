// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <optional>
#include <utility>

#include <doctest/doctest.h>

#include "fawkes/errors.hpp"
#include "fawkes/request.hpp"

namespace {

TEST_SUITE_BEGIN("HTTP Request");

TEST_CASE("Percent-decode path automatically") {
    fawkes::request::impl_type raw_req;
    raw_req.target("/search%26query?foobar");
    const fawkes::request req(std::move(raw_req));
    CHECK_EQ(req.as_impl().target(), "/search%26query?foobar");
    CHECK_EQ(req.path(), "/search&query");

    SUBCASE("target is equals to impl target") {
        CHECK_EQ(req.target(), req.as_impl().target());
    }
}

TEST_CASE("Throws when path part is invalid") {
    fawkes::request::impl_type raw_req;
    raw_req.target("/search%GAery?foobar"); // %GA is illegal
    CHECK_THROWS_AS(const fawkes::request req(std::move(raw_req)), fawkes::http_error);
}

TEST_CASE("No throw if only query string part is invalid") {
    fawkes::request::impl_type raw_req;
    raw_req.target("/search%26query?foobar=%GA"); // %GA is illegal
    std::optional<fawkes::request> or_req;
    REQUIRE_NOTHROW(or_req.emplace(std::move(raw_req)));
    REQUIRE(or_req.has_value());
    CHECK_EQ(or_req->path(), "/search&query");

    SUBCASE("discard whole query string if it is invalid") {
        CHECK_EQ(or_req->target(), "/search%26query");
    }

    SUBCASE("not equal to impl target") {
        CHECK_NE(or_req->target(), or_req->as_impl().target());
    }
}

TEST_CASE("Query parameters operations") {
    fawkes::request::impl_type raw_req;
    raw_req.target("/search%26query?key%2B1=hello%20world&key%2B2=&key%2B3&");
    const fawkes::request req(std::move(raw_req));

    SUBCASE("key+1 has explicit value") {
        auto val1 = req.queries().get("key+1");
        REQUIRE(val1.has_value());
        CHECK_EQ(*val1, "hello world"); // NOLINT(bugprone-unchecked-optional-access)

        auto val2 = req.queries().get_or("key+1", "empty");
        CHECK_EQ(val2, "hello world");
    }

    SUBCASE("key+2 has empty value") {
        auto val1 = req.queries().get("key+2");
        REQUIRE(val1.has_value());
        CHECK_EQ(*val1, ""); // NOLINT(bugprone-unchecked-optional-access)

        auto val2 = req.queries().get_or("key+2", "empty");
        CHECK_EQ(val2, "");
    }

    SUBCASE("key+3 has implicit empty value") {
        auto val1 = req.queries().get("key+3");
        REQUIRE(val1.has_value());
        CHECK_EQ(*val1, ""); // NOLINT(bugprone-unchecked-optional-access)

        auto val2 = req.queries().get_or("key+3", "empty");
        CHECK_EQ(val2, "");
    }

    SUBCASE("key+4 doesn't exist") {
        auto val1 = req.queries().get("key+4");
        CHECK_FALSE(val1.has_value());

        auto val2 = req.queries().get_or("key+4", "empty");
        CHECK_EQ(val2, "empty");
    }
}

TEST_SUITE_END(); // HTTP Request

} // namespace
