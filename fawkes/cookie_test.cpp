// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <doctest/doctest.h>

#include "fawkes/cookie.hpp"

namespace {

namespace http = boost::beast::http;

TEST_SUITE_BEGIN("Parsing Cookie Header");

TEST_CASE("Empty cookie header") {
    const http::request_header<> req_header;
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    CHECK(cv.empty());
}

TEST_CASE("Parse and reference cookie entries") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie, "key1=value1; key2=value2; special=a%2Bb");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 3);

    auto value = cv.get("key1");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), "value1"); // NOLINT(bugprone-unchecked-optional-access)

    value = cv.get("key2");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), "value2"); // NOLINT(bugprone-unchecked-optional-access)

    value = cv.get("key3");
    CHECK_FALSE(value.has_value());

    SUBCASE("auto-unescape for entry values") {
        value = cv.get("special");
        REQUIRE(value.has_value());
        CHECK_EQ(value.value(), "a+b"); // NOLINT(bugprone-unchecked-optional-access)
    }
}

TEST_CASE("Entry value is empty") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie, "key1=; key2");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 2);

    auto value = cv.get("key1");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), ""); // NOLINT(bugprone-unchecked-optional-access)

    // Allow non-strict empty value case.
    value = cv.get("key2");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), ""); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("Use the first entry when multiple matches") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie, "key=foobar; key2=test; key=");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 2);

    CHECK_EQ(cv.get("key").value(), "foobar"); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("Contains empty entry") {
    http::request_header<> req_header;

    SUBCASE("empty string") {
        req_header.set(http::field::cookie, "");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK(cv.empty());
    }

    SUBCASE("multiple empty entries") {
        req_header.set(http::field::cookie, "; ;");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK(cv.empty());
    }
}

TEST_CASE("Malformed cookie entry") {
    http::request_header<> req_header;

    SUBCASE("multiple = in one entry") {
        req_header.set(http::field::cookie, "key=foo=bar");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK(cv.empty());
    }

    SUBCASE("name is empty") {
        req_header.set(http::field::cookie, "=foo");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK(cv.empty());
    }

    SUBCASE("name is not valid") {
        req_header.set(http::field::cookie, "k@y=foo");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK(cv.empty());
    }

    SUBCASE("value is not valid") {
        // value cannot contain spaces
        req_header.set(http::field::cookie, "key=a b");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        INFO(cv.get("key").value()); // NOLINT(bugprone-unchecked-optional-access)
        CHECK(cv.empty());
    }
}

TEST_SUITE_END(); // Parsing Cookie Header

TEST_SUITE_BEGIN("Stringify Cookie Struct");

TEST_CASE("Simple cookie with only name/value") {
    const fawkes::cookie cookie("msg", "hello world");
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_CASE("Complex with all member set") {
    using namespace std::chrono_literals;

    fawkes::cookie cookie("msg", "hello world");
    cookie.path = "/";
    cookie.domain = ".example.com"; // Leading dot will be stripped.
    cookie.max_age = std::chrono::days(1);
    cookie.expires = std::chrono::sys_days(2025y / 12 / 12); // NOLINT(readability-magic-numbers)
    cookie.http_only = true;
    cookie.secure = true;
    cookie.same_site = fawkes::same_site_policy::lax;
    // TODO(KC): replacing with Set-Cookie parse when available.
    static constexpr std::string_view expect_value =
        "msg=hello%20world; Path=/; Domain=example.com; Max-Age=86400"
        "; Expires=Fri, 12 Dec 2025 00:00:00 GMT; Secure; HttpOnly; SameSite=Lax";
    CHECK_EQ(cookie.to_string(), expect_value);
}

TEST_CASE("Returns empty string for invalid name") {
    const fawkes::cookie cookie("a b", "foobar");
    CHECK_EQ(cookie.to_string(), "");
}

TEST_CASE("Skip invalid path") {
    fawkes::cookie cookie("msg", "hello world");
    cookie.path = "/test/a;b;c";
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_CASE("Skip invalid domain") {
    fawkes::cookie cookie("msg", "hello world");
    cookie.domain = "/test/";
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_CASE("Skip invalid expires") {
    using namespace std::chrono_literals;
    fawkes::cookie cookie("msg", "hello world");
    cookie.expires = std::chrono::sys_days{1600y / 1 / 1}; // NOLINT(readability-magic-numbers)
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_SUITE_END(); // Cookie Struct

} // namespace
