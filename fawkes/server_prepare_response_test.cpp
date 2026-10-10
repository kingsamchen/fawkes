// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/beast/core/buffer_traits.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <doctest/doctest.h>

#include "fawkes/mime.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;

constexpr unsigned int http_version_1_1 = 11;

struct wire_response {
    bool keep_alive{false};
    http::response_header<> header;
    // Bytes following the header, i.e. what the peer would read as the body.
    std::string payload;
};

// Serialize the message to a raw string, then parse its headers back.
wire_response write_out(http::message_generator gen) {
    wire_response out{.keep_alive = gen.keep_alive()};

    std::string raw;
    beast::error_code ec;
    while (!gen.is_done()) {
        const auto bufs = gen.prepare(ec);
        REQUIRE_MESSAGE(!ec, ec.message());
        raw += beast::buffers_to_string(bufs);
        gen.consume(beast::buffer_bytes(bufs));
    }

    http::response_parser<http::string_body> parser;
    parser.skip(true);
    const auto header_size = parser.put(asio::buffer(raw), ec);
    REQUIRE_MESSAGE(!ec, ec.message());
    REQUIRE(parser.is_done());

    out.header = std::move(parser.get().base());
    out.payload = raw.substr(header_size);
    return out;
}

fawkes::response make_text_response(http::status status, std::string body) {
    fawkes::response resp(http_version_1_1, true);
    resp.text(status, std::move(body));
    return resp;
}

void set_framing_headers(fawkes::response& resp) {
    resp.header().set(http::field::content_length, "100");
    resp.header().set(http::field::transfer_encoding, "chunked");
    resp.header().set(http::field::trailer, "Expires");
}

TEST_SUITE_BEGIN("Server/PrepareResponse");

TEST_CASE("GET response sends body with Content-Length") {
    auto resp = make_text_response(http::status::ok, "hello");

    const auto out = write_out(fawkes::detail::prepare_response(http::verb::get, resp));

    CHECK_EQ(out.header.result(), http::status::ok);
    CHECK_EQ(out.header[http::field::content_length], "5");
    CHECK_EQ(out.header[http::field::content_type], fawkes::mime::text);
    CHECK_EQ(out.header[http::field::x_content_type_options], "nosniff");
    CHECK_EQ(out.payload, "hello");
    CHECK(out.keep_alive);
}

TEST_CASE("HEAD response drops body and sets Content-Length from body") {
    auto resp = make_text_response(http::status::ok, "hello");

    SUBCASE("no Content-Length from handler") {
        // No-op.
    }

    SUBCASE("wrong Content-Length from handler is overridden") {
        resp.header().set(http::field::content_length, "100");
    }

    SUBCASE("Transfer-Encoding and Trailer from handler are removed") {
        set_framing_headers(resp);
    }

    const auto out = write_out(fawkes::detail::prepare_response(http::verb::head, resp));

    CHECK_EQ(out.header.result(), http::status::ok);
    CHECK_EQ(out.header[http::field::content_length], "5");
    CHECK_FALSE(out.header.contains(http::field::transfer_encoding));
    CHECK_FALSE(out.header.contains(http::field::trailer));
    CHECK_EQ(out.header[http::field::content_type], fawkes::mime::text);
    CHECK_EQ(out.header[http::field::x_content_type_options], "nosniff");

    CHECK(out.payload.empty());
    CHECK(out.keep_alive);
}

TEST_CASE("HEAD response with empty body leaves Content-Length to handler") {
    auto resp = make_text_response(http::status::ok, "");

    SUBCASE("no Content-Length from handler") {
        const auto out = write_out(fawkes::detail::prepare_response(http::verb::head, resp));

        CHECK_FALSE(out.header.contains(http::field::content_length));
        CHECK(out.payload.empty());
        CHECK(out.keep_alive);
    }

    SUBCASE("Content-Length from handler is kept") {
        resp.header().set(http::field::content_length, "42");

        const auto out = write_out(fawkes::detail::prepare_response(http::verb::head, resp));

        CHECK_EQ(out.header[http::field::content_length], "42");
        CHECK(out.payload.empty());
        CHECK(out.keep_alive);
    }
}

TEST_CASE("HEAD response with empty body preserves protocol version and connection policy") {
    const std::vector versions{10U, http_version_1_1};
    const std::vector keep_alive_values{false, true};
    for (const auto version : versions) {
        for (const auto keep_alive : keep_alive_values) {
            CAPTURE(version);
            CAPTURE(keep_alive);
            fawkes::response resp(version, keep_alive);
            resp.set_status(http::status::ok);

            const auto out = write_out(fawkes::detail::prepare_response(http::verb::head, resp));

            CHECK_EQ(out.header.version(), version);
            CHECK_EQ(out.keep_alive, keep_alive);
            CHECK_EQ(out.header[http::field::x_content_type_options], "nosniff");
            CHECK_FALSE(out.header.contains(http::field::content_length));
            CHECK(out.payload.empty());
        }
    }
}

TEST_CASE("No Content response carries neither framing headers nor body") {
    const std::vector methods{http::verb::get, http::verb::head, http::verb::post};
    for (const auto method : methods) {
        CAPTURE(method);
        auto resp = make_text_response(http::status::no_content, "unexpected");
        set_framing_headers(resp);

        const auto out = write_out(fawkes::detail::prepare_response(method, resp));

        CHECK_EQ(out.header.result(), http::status::no_content);
        CHECK_FALSE(out.header.contains(http::field::content_length));
        CHECK_FALSE(out.header.contains(http::field::transfer_encoding));
        CHECK_FALSE(out.header.contains(http::field::trailer));
        CHECK_EQ(out.header[http::field::content_type], fawkes::mime::text);
        CHECK_EQ(out.header[http::field::x_content_type_options], "nosniff");

        CHECK(out.payload.empty());
        CHECK(out.keep_alive);
    }
}

TEST_CASE("Not Modified response drops body, Content-Type and framing headers") {
    const std::vector methods{http::verb::get, http::verb::head};
    for (const auto method : methods) {
        CAPTURE(method);
        auto resp = make_text_response(http::status::not_modified, "unexpected");
        set_framing_headers(resp);
        resp.header().set(http::field::etag, "\"v1\"");
        resp.header().set(http::field::cache_control, "max-age=60");

        const auto out = write_out(fawkes::detail::prepare_response(method, resp));

        CHECK_EQ(out.header.result(), http::status::not_modified);
        CHECK_FALSE(out.header.contains(http::field::content_length));
        CHECK_FALSE(out.header.contains(http::field::content_type));
        CHECK_FALSE(out.header.contains(http::field::transfer_encoding));
        CHECK_FALSE(out.header.contains(http::field::trailer));
        CHECK_EQ(out.header[http::field::etag], "\"v1\"");
        CHECK_EQ(out.header[http::field::cache_control], "max-age=60");
        CHECK_EQ(out.header[http::field::x_content_type_options], "nosniff");

        CHECK(out.payload.empty());
        CHECK(out.keep_alive);
    }
}

TEST_SUITE_END(); // Server/PrepareResponse

} // namespace
