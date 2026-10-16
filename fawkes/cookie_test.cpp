// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <algorithm>
#include <chrono>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <absl/strings/escaping.h>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/url/encode.hpp>
#include <boost/url/pct_string_view.hpp>
#include <boost/url/rfc/unreserved_chars.hpp>
#include <doctest/doctest.h>
#include <fmt/format.h>

#include "fawkes/cookie.hpp"
#include "fawkes/response.hpp"

namespace {

namespace http = boost::beast::http;

TEST_SUITE_BEGIN("Parsing Cookie Header");

TEST_CASE("Empty cookie header") {
    const http::request_header<> req_header;
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    CHECK(cv.empty());
    CHECK(cv.get_all("key").empty());
}

TEST_CASE("Parse and reference cookie entries") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie, "key1=value1; key2=value2; special=a%2Bb");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 3);

    auto value = cv.get("key1");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), "value1");
    const auto values = cv.get_all("key1");
    REQUIRE_EQ(values.size(), 1);
    CHECK_EQ(values.front(), "value1");

    value = cv.get("key2");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), "value2");

    value = cv.get("key3");
    CHECK_FALSE(value.has_value());
    CHECK(cv.get_all("key3").empty());

    SUBCASE("preserve percent escapes in entry values") {
        value = cv.get("special");
        REQUIRE(value.has_value());
        CHECK_EQ(value.value(), "a%2Bb");
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
    CHECK_EQ(value.value(), "");

    // Allow non-strict empty value case.
    value = cv.get("key2");
    REQUIRE(value.has_value());
    CHECK_EQ(value.value(), "");
}

TEST_CASE("Retain all duplicate values and return the first match") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie,
                   "key=foobar; key2=test; key=; key=foobar; key=second; key=third; "
                   "key=fourth; key=fifth; key=sixth; key=seventh");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 2);

    CHECK_EQ(cv.get("key").value(), "foobar");

    constexpr std::string_view expected_values[] = {
        "foobar",
        "",
        "foobar",
        "second",
        "third",
        "fourth",
        "fifth",
        "sixth",
        "seventh",
    };
    CHECK(std::ranges::equal(cv.get_all("key"), expected_values));
}

TEST_CASE("Get all distinguishes absent cookies from matching empty values") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie, "key=; key; key=\"\"; KEY=uppercase");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 2);

    CHECK(cv.get_all("missing").empty());
    constexpr std::string_view expected_values[] = {
        "",
        "",
        "",
    };
    CHECK(std::ranges::equal(cv.get_all("key"), expected_values));

    const auto uppercase_values = cv.get_all("KEY");
    REQUIRE_EQ(uppercase_values.size(), 1);
    CHECK_EQ(uppercase_values.front(), "uppercase");
}

TEST_CASE("Get all preserves raw values and removes surrounding quotes") {
    http::request_header<> req_header;
    req_header.set(http::field::cookie,
                   "key=\"a%2Fb\"; key=a+b/c:d; key=\"YWJjZA==\"; key=%00; "
                   "key=%252F; key=%2G");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);

    constexpr std::string_view expected_values[] = {
        "a%2Fb",
        "a+b/c:d",
        "YWJjZA==",
        "%00",
        "%252F",
        "%2G",
    };
    CHECK(std::ranges::equal(cv.get_all("key"), expected_values));
}

TEST_CASE("Parse cookie values with punctuation and optional quotes") {
    struct value_case {
        std::string_view input;
        std::string_view expected;
    };

    constexpr value_case valid_values[] = {
        {.input = "YWJjZA==", .expected = "YWJjZA=="},
        {.input = "foo=bar", .expected = "foo=bar"},
        {.input = "foo=bar=baz", .expected = "foo=bar=baz"},
        {.input = "a+b/c:d", .expected = "a+b/c:d"},
        {.input = "\"YWJjZA==\"", .expected = "YWJjZA=="},
        {.input = "\"a+b/c:d\"", .expected = "a+b/c:d"},
        {.input = "\"\"", .expected = ""},
        {.input = "%2b%2F%3A", .expected = "%2b%2F%3A"},
        {.input = "\"a%2Bb%25\"", .expected = "a%2Bb%25"},
        {.input = "%00", .expected = "%00"},
        {.input = "%2F", .expected = "%2F"},
        {.input = "%252F", .expected = "%252F"},
        {.input = "%2520", .expected = "%2520"},
        {.input = "100%", .expected = "100%"},
        {.input = "%", .expected = "%"},
        {.input = "%2", .expected = "%2"},
        {.input = "%2G", .expected = "%2G"},
        {.input = "%GG", .expected = "%GG"},
        {.input = "%41%", .expected = "%41%"},
        {.input = "%41%2G", .expected = "%41%2G"},
        {.input = "\"a%2Gb\"", .expected = "a%2Gb"},
    };

    for (const auto& test : valid_values) {
        CAPTURE(test.input);
        http::request_header<> req_header;
        req_header.set(http::field::cookie, std::string("key=") + std::string(test.input));
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        REQUIRE_EQ(cv.size(), 1);
        const auto value = cv.get("key");
        REQUIRE(value.has_value());
        CHECK_EQ(*value, test.expected);
    }
}

TEST_CASE("Retain duplicate values across multiple Cookie headers") {
    http::request_header<> req_header;
    req_header.insert(http::field::cookie, "key=first; empty=");
    req_header.insert(http::field::user_agent, "fawkes-cookie-test");
    req_header.insert(http::field::cookie, "other=YWJjZA==; key=second; bare");
    req_header.insert(http::field::cookie, "key=third; key=first");
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 4);
    CHECK_EQ(cv.get("key").value(), "first");
    CHECK_EQ(cv.get("other").value(), "YWJjZA==");
    CHECK_EQ(cv.get("empty").value(), "");
    CHECK_EQ(cv.get("bare").value(), "");

    constexpr std::string_view expected_values[] = {
        "first",
        "second",
        "third",
        "first",
    };
    CHECK(std::ranges::equal(cv.get_all("key"), expected_values));
}

TEST_CASE("Skip malformed entries without discarding valid neighbors") {
    constexpr std::string_view cookie_value =
        "=empty; bad name=value; good=\"; good=first; malformed=\"abc; good=a b; "
        "other=second; good=\"last\"; good=a,b; good=final";
    http::request_header<> req_header;
    req_header.set(http::field::cookie, cookie_value);
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    REQUIRE_EQ(cv.size(), 2);
    CHECK_EQ(cv.get("good").value(), "first");
    CHECK_EQ(cv.get("other").value(), "second");

    constexpr std::string_view expected_values[] = {
        "first",
        "last",
        "final",
    };
    CHECK(std::ranges::equal(cv.get_all("good"), expected_values));
    CHECK(cv.get_all("malformed").empty());
}

TEST_CASE("Reject malformed quotes and forbidden cookie-value bytes") {
    constexpr std::string_view invalid_values[] = {
        "\"",
        "\"unterminated",
        "unterminated\"",
        R"("a"b")",
        "a\"b",
        "a\\b",
        R"("a\b")",
        "a,b",
        "a b",
        "\"a b\"",
        "a\tb",
        "a\rb",
        "a\nb",
        std::string_view{"a\0b", 3},
        "a\x7Fz",
        "a\x80z",
        "a\xFFz",
        "\"a;b\"",
    };

    for (const std::string_view input : invalid_values) {
        CAPTURE(input);
        http::request_header<> req_header;
        req_header.set(http::field::cookie, std::string("key=") + std::string(input));
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK_FALSE(cv.contains("key"));
        CHECK_FALSE(cv.get("key").has_value());
    }
}

TEST_CASE("Validate all byte values against the cookie-octet alphabet") {
    constexpr std::string_view cookie_octets =
        "!#$%&'()*+-./0123456789:<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "[]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    constexpr unsigned int byte_count = 256;
    for (unsigned int byte = 0; byte < byte_count; ++byte) {
        CAPTURE(byte);
        const auto ch = static_cast<char>(byte);
        const auto value = std::string("a") + ch + "b";
        http::request_header<> req_header;
        // Quotes keep a semicolon from leaving a valid value before the delimiter.
        req_header.set(http::field::cookie, std::string("key=\"") + value + "\"");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        if (!cookie_octets.contains(ch)) {
            CHECK_FALSE(cv.contains("key"));
        } else {
            const auto parsed = cv.get("key");
            REQUIRE(parsed.has_value());
            CHECK_EQ(*parsed, value);
        }
    }
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
        INFO(cv.get("key").has_value());
        CHECK(cv.empty());
    }

    SUBCASE("the entry contains only whitespaces") {
        req_header.set(http::field::cookie, "    ");
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        CHECK(cv.empty());
    }
}

TEST_SUITE_END(); // Parsing Cookie Header

TEST_SUITE_BEGIN("Stringify Cookie Struct");

TEST_CASE("Simple cookie with only name/value") {
    const fawkes::cookie cookie("msg", "hello%20world");
    CHECK(cookie.validate().has_value());
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_CASE("Serialized cookie values roundtrip through the request parser") {
    constexpr std::string_view values[] = {
        "",
        "hello%20world",
        "YWJjZA==",
        "a+b/c:d",
        "%00",
        "%2F",
        "%252F",
        "literal%2G",
        "100%",
    };

    for (std::string_view expected : values) {
        CAPTURE(expected);
        const fawkes::cookie cookie("key", std::string(expected));
        CHECK(cookie.validate().has_value());
        const auto serialized = cookie.to_string();
        CHECK_EQ(serialized, fmt::format("key={}", expected));
        http::request_header<> req_header;
        req_header.set(http::field::cookie, serialized);
        auto [begin, end] = req_header.equal_range(http::field::cookie);
        const fawkes::cookie_view cv(begin, end);
        const auto value = cv.get("key");
        REQUIRE(value.has_value());
        CHECK_EQ(*value, expected);
    }
}

TEST_CASE("Invalid cookie values are rejected without escaping or sanitizing") {
    constexpr std::string_view invalid_values[] = {
        "hello world",
        " value",
        "value ",
        "a\tb",
        "a\rb",
        "a\nb",
        "\"quoted\"",
        "a,b",
        "a;b",
        "a\\b",
        "a\x7Fz",
        "a\x80z",
        "\xC3\xBC",
        std::string_view{"a\0b", 3},
    };

    for (const std::string_view value : invalid_values) {
        CAPTURE(value);
        const fawkes::cookie cookie("key", std::string(value));
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_value});
        CHECK(cookie.to_string().empty());
    }
}

TEST_CASE("Applications can explicitly percent-encode cookie values") {
    constexpr char payload_bytes[] = "hello world;\0utf-8:\xC3\xBC";
    const std::string_view payload(payload_bytes, sizeof(payload_bytes) - 1);
    const auto encoded = boost::urls::encode(payload, boost::urls::unreserved_chars);
    const fawkes::cookie cookie("key", encoded);
    http::request_header<> req_header;
    req_header.set(http::field::cookie, cookie.to_string());
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    const auto value = cv.get("key");
    REQUIRE(value.has_value());
    CHECK_EQ(*value, encoded);
    const auto escaped = boost::urls::make_pct_string_view(*value);
    REQUIRE(escaped.has_value());
    CHECK_EQ(escaped->decode(), payload);
}

TEST_CASE("Applications can explicitly Base64-encode cookie values") {
    constexpr char payload_bytes[] = "hello world;\0utf-8:\xC3\xBC!";
    const std::string_view payload(payload_bytes, sizeof(payload_bytes) - 1);
    std::string encoded;
    absl::Base64Escape(payload, &encoded);
    CHECK(encoded.ends_with("=="));
    const fawkes::cookie cookie("key", encoded);
    http::request_header<> req_header;
    req_header.set(http::field::cookie, cookie.to_string());
    auto [begin, end] = req_header.equal_range(http::field::cookie);
    const fawkes::cookie_view cv(begin, end);
    const auto value = cv.get("key");
    REQUIRE(value.has_value());
    CHECK_EQ(*value, encoded);
    std::string decoded;
    REQUIRE(absl::Base64Unescape(*value, &decoded));
    CHECK_EQ(decoded, payload);
}

TEST_CASE("Response cookie helper preserves raw values in separate Set-Cookie fields") {
    fawkes::response response;
    response.add_set_cookie(fawkes::cookie("sid", "YWJjZA=="));
    response.add_set_cookie(fawkes::cookie("value", "a+b/%2F"));
    response.add_set_cookie(fawkes::cookie("invalid", "hello world"));

    CHECK_EQ(response.header().count(http::field::cookie), 0);
    auto [begin, end] = response.header().equal_range(http::field::set_cookie);
    REQUIRE_EQ(std::distance(begin, end), 2);
    CHECK_EQ(begin->value(), "sid=YWJjZA==");
    ++begin;
    CHECK_EQ(begin->value(), "value=a+b/%2F");
}

TEST_CASE("Complex with all member set") {
    using namespace std::chrono_literals;

    fawkes::cookie cookie("msg", "hello%20world");
    cookie.path = "/";
    cookie.domain = ".example.com"; // Leading dot will be stripped.
    cookie.max_age = std::chrono::days(1);
    cookie.expires = std::chrono::sys_days(2025y / 12 / 12);
    cookie.http_only = true;
    cookie.secure = true;
    cookie.same_site = fawkes::same_site_policy::lax;
    // TODO(KC): replacing with Set-Cookie parse when available.
    static constexpr std::string_view expect_value =
        "msg=hello%20world; Path=/; Domain=example.com; Max-Age=86400"
        "; Expires=Fri, 12 Dec 2025 00:00:00 GMT; Secure; HttpOnly; SameSite=Lax";
    CHECK(cookie.validate().has_value());
    CHECK_EQ(cookie.to_string(), expect_value);
}

TEST_CASE("Returns empty string for invalid name") {
    const fawkes::cookie cookie("a b", "foobar");
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_name});
    CHECK_EQ(cookie.to_string(), "");
}

TEST_CASE("Skip invalid path") {
    fawkes::cookie cookie("msg", "hello%20world");
    cookie.path = "/test/a;b;c";
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_path});
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_CASE("Skip invalid domain") {
    fawkes::cookie cookie("msg", "hello%20world");
    cookie.domain = "/test/";
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});
    CHECK_EQ(cookie.to_string(), "msg=hello%20world");
}

TEST_CASE("Valid cookie domains") {
    constexpr std::string_view valid_domains[] = {
        "example.com",
        "EXAMPLE.com",
        "a-b.example",
        "3foo.example",
        "localhost",
        "127.0.0.1",
        "a",
        "xn--bcher-kva.example",
    };

    for (const std::string_view domain : valid_domains) {
        CAPTURE(domain);
        fawkes::cookie cookie("msg", "value");
        cookie.domain = domain;
        CHECK(cookie.validate().has_value());
        CHECK_EQ(cookie.to_string(), std::string("msg=value; Domain=") + std::string(domain));
    }

    SUBCASE("Normalize one leading dot") {
        fawkes::cookie cookie("msg", "value");
        cookie.domain = ".example.com";
        CHECK(cookie.validate().has_value());
        CHECK_EQ(cookie.to_string(), "msg=value; Domain=example.com");
    }

    SUBCASE("Omit unspecified domain") {
        const fawkes::cookie cookie("msg", "value");
        CHECK_EQ(cookie.to_string(), "msg=value");
    }
}

TEST_CASE("Reject invalid cookie domains") {
    constexpr std::string_view invalid_domains[] = {
        ".",
        "..example.com",
        "example..com",
        "example.com.",
        "-foo.example",
        "foo-.example",
        "example.-foo",
        "example.foo-",
        "foo_bar.example",
        "user@example.com",
        "example.com:443",
        "[::1]",
        "example.com;Max-Age=0",
        "example.com;Secure",
        "example.com;Domain=example.org",
        "example.com;Path=admin",
        " example.com",
        "example.com ",
        "example.com\r\n",
        "ex%61mple.com",
        "\xC3\xBC.example",
    };

    for (const std::string_view domain : invalid_domains) {
        CAPTURE(domain);
        fawkes::cookie cookie("msg", "value");
        cookie.domain = domain;
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});
        CHECK_EQ(cookie.to_string(), "msg=value");
    }

    SUBCASE("Reject embedded null") {
        fawkes::cookie cookie("msg", "value");
        cookie.domain =
            std::string_view{"example.com\0.evil", 17};
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});
        CHECK_EQ(cookie.to_string(), "msg=value");
    }
}

TEST_CASE("Cookie domain length limits") {
    constexpr std::size_t max_label_length = 63;
    fawkes::cookie cookie("msg", "value");

    SUBCASE("Maximum label length") {
        cookie.domain = std::string(max_label_length, 'a') + ".example";
        CHECK(cookie.validate().has_value());
        CHECK_EQ(cookie.to_string(), std::string("msg=value; Domain=") + cookie.domain);

        cookie.domain.insert(0, "a");
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});
        CHECK_EQ(cookie.to_string(), "msg=value");

        cookie.domain = "example." + std::string(max_label_length + 1, 'a');
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});
        CHECK_EQ(cookie.to_string(), "msg=value");
    }

    SUBCASE("Maximum normalized domain length") {
        constexpr std::size_t last_label_length = 61;
        const auto domain = std::string(max_label_length, 'a') + "." +
                            std::string(max_label_length, 'b') + "." +
                            std::string(max_label_length, 'c') + "." +
                            std::string(last_label_length, 'd');
        cookie.domain = domain;
        CHECK(cookie.validate().has_value());
        CHECK_EQ(cookie.to_string(), std::string("msg=value; Domain=") + domain);

        cookie.domain.insert(0, ".");
        CHECK(cookie.validate().has_value());
        CHECK_EQ(cookie.to_string(), std::string("msg=value; Domain=") + domain);

        cookie.domain.push_back('d');
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});
        CHECK_EQ(cookie.to_string(), "msg=value");
    }
}

TEST_CASE("Serialize expires within the supported UTC date range") {
    using namespace std::chrono_literals;

    struct expires_case {
        std::chrono::sys_seconds expires;
        std::string_view expected;
    };

    constexpr expires_case valid_expires[] = {
        {.expires = std::chrono::sys_days{1601y / 1 / 1},
         .expected = "Mon, 01 Jan 1601 00:00:00 GMT"},
        {.expires = std::chrono::sys_days{1601y / 1 / 1} + 1s,
         .expected = "Mon, 01 Jan 1601 00:00:01 GMT"},
        {.expires = std::chrono::sys_seconds{} - 1s,
         .expected = "Wed, 31 Dec 1969 23:59:59 GMT"},
        {.expires = std::chrono::sys_days{2000y / 2 / 29} + 12h + 34min + 56s,
         .expected = "Tue, 29 Feb 2000 12:34:56 GMT"},
        {.expires = std::chrono::sys_days{3001y / 1 / 1},
         .expected = "Thu, 01 Jan 3001 00:00:00 GMT"},
        {.expires = std::chrono::sys_days{9999y / 12 / 31},
         .expected = "Fri, 31 Dec 9999 00:00:00 GMT"},
        {.expires =
             std::chrono::sys_seconds{std::chrono::sys_days{9999y / 12 / 31}} + 23h + 59min + 59s,
         .expected = "Fri, 31 Dec 9999 23:59:59 GMT"},
    };

    for (const auto& test : valid_expires) {
        CAPTURE(test.expected);
        fawkes::cookie cookie("msg", "hello%20world");
        cookie.expires = test.expires;
        CHECK(cookie.validate().has_value());
        CHECK_EQ(cookie.to_string(), std::string("msg=hello%20world; Expires=") +
                                         std::string(test.expected));
    }
}

TEST_CASE("Skip expires outside the supported UTC date range") {
    using namespace std::chrono_literals;

    constexpr std::chrono::sys_seconds invalid_expires[] = {
        std::chrono::sys_days{1600y / 1 / 1},
        std::chrono::sys_days{1601y / 1 / 1} - 1s,
        std::chrono::sys_days{10000y / 1 / 1},
        std::chrono::sys_seconds::min(),
        std::chrono::sys_seconds::max(),
    };

    fawkes::cookie cookie("msg", "hello%20world");
    cookie.max_age = 1h;
    cookie.secure = true;
    cookie.http_only = true;
    for (const auto expires : invalid_expires) {
        CAPTURE(expires.time_since_epoch().count());
        cookie.expires = expires;
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_expires});
        CHECK_EQ(cookie.to_string(), "msg=hello%20world; Max-Age=3600; Secure; HttpOnly");
    }
}

template<typename T>
concept supports_get_all = requires(T&& cv) {
    std::forward<T>(cv).get_all("key");
};

TEST_CASE("Only lvaue cookie_view supports get_all") {
    static_assert(supports_get_all<fawkes::cookie_view&>);
    static_assert(supports_get_all<const fawkes::cookie_view&>);
    static_assert(!supports_get_all<fawkes::cookie_view&&>);
    static_assert(!supports_get_all<const fawkes::cookie_view&&>);
}

TEST_SUITE_END(); // Cookie Struct

TEST_SUITE_BEGIN("Validate Cookie Struct");

TEST_CASE("Empty values are valid") {
    fawkes::cookie cookie("sid", "");
    CHECK(cookie.validate().has_value());
    CHECK_EQ(cookie.to_string(), "sid=");

    SUBCASE("zero or negative max-age values are valid") {
        using namespace std::chrono_literals;
        constexpr std::chrono::seconds max_ages[] = {
            -1s,
            0s,
            1s,
        };
        for (const auto max_age : max_ages) {
            CAPTURE(max_age.count());
            cookie.max_age = max_age;
            CHECK(cookie.validate().has_value());
        }
    }
}

TEST_CASE("HTTP token names and browser policy remain separate") {
    constexpr std::string_view names[] = {
        "sid",
        "__Secure-sid",
        "__Host-sid",
        "!#$%&'*+-.^_`|~0123456789AZaz",
    };
    for (const std::string_view name : names) {
        CAPTURE(name);
        fawkes::cookie cookie(std::string(name), "value");
        cookie.path = "/app";
        cookie.domain = "example.com";
        cookie.same_site = fawkes::same_site_policy::none;
        CHECK(cookie.validate().has_value());
    }
}

TEST_CASE("Reject invalid HTTP token names") {
    constexpr std::string_view names[] = {
        "",
        "bad name",
        "name=value",
        "name;attribute",
        "name\r\n",
        std::string_view{"na\0me", 5},
        "na\x80me",
    };
    for (const std::string_view name : names) {
        CAPTURE(name);
        const fawkes::cookie cookie(std::string(name), "value");
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_name});
    }
}

TEST_CASE("Path validation checks bytes without requiring an absolute path") {
    constexpr std::string_view paths[] = {
        "",
        "/",
        "/with space/",
        "relative/path",
        " ",
        "/quote\"and\\backslash,~",
    };
    for (const std::string_view path : paths) {
        CAPTURE(path);
        fawkes::cookie cookie("sid", "value");
        cookie.path = path;
        CHECK(cookie.validate().has_value());
    }
}

TEST_CASE("Reject forbidden Path bytes") {
    constexpr std::string_view paths[] = {
        "/semi;colon",
        "/tab\t",
        "/line\r\n",
        std::string_view{"/null\0x", 7},
        "/\x7F",
        "/\x80",
        "/\xFF",
    };
    for (const std::string_view path : paths) {
        CAPTURE(path);
        fawkes::cookie cookie("sid", "value");
        cookie.path = path;
        CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_path});
    }
}

TEST_CASE("Report the first invalid field without changing tolerant serialization") {
    using namespace std::chrono_literals;

    constexpr std::chrono::sys_seconds invalid_expires = std::chrono::sys_days{1600y / 1 / 1};
    fawkes::cookie cookie("", "invalid value");
    cookie.path = "/invalid;path";
    cookie.domain = "bad_domain";
    cookie.expires = invalid_expires;
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_name});

    cookie.name = "sid";
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_value});

    cookie.value = "value";
    const auto serialized = cookie.to_string();
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_path});
    CHECK_EQ(cookie.path, "/invalid;path");
    CHECK_EQ(cookie.domain, "bad_domain");
    CHECK_EQ(cookie.expires, invalid_expires);
    CHECK_EQ(cookie.to_string(), serialized);
    CHECK_EQ(serialized, "sid=value");

    cookie.path = "/app";
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_domain});

    cookie.domain = "example.com";
    CHECK_EQ(cookie.validate(), std::unexpected{fawkes::cookie_error::invalid_expires});

    cookie.expires = std::chrono::sys_days{2025y / 1 / 1};
    CHECK(cookie.validate().has_value());
}

TEST_SUITE_END(); // Validate Cookie Struct

} // namespace
