// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/server.hpp"

#include <exception>
#include <functional>
#include <string>
#include <utility>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/version.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/system_error.hpp>
#include <esl/ignore_unused.h>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

#include "fawkes/middleware.hpp"
#include "fawkes/mime.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"

namespace fawkes {

namespace json = boost::json;

namespace {

http::response<http::string_body> make_unexpected_error_response(unsigned int http_version,
                                                                 bool keep_alive,
                                                                 std::string&& body) {
    http::response<http::string_body> resp{http::status::internal_server_error, http_version};
    resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    resp.set(http::field::content_type, mime::json);
    resp.keep_alive(keep_alive);
    resp.body() = std::move(body);
    resp.prepare_payload();
    return resp;
}

response::impl_type&& prepare_response(response& resp) {
    auto& impl = resp.as_impl();
    impl.prepare_payload();
    return std::move(impl);
}

} // namespace

void server::listen_and_serve(const std::string& addr, std::uint16_t port) {
    endpoint_ = asio::ip::tcp::endpoint(asio::ip::make_address(addr), port);

    acceptor_.open(endpoint_.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    acceptor_.bind(endpoint_);
    acceptor_.listen(asio::socket_base::max_listen_connections);
    asio::co_spawn(io_ctx_, do_listen(), asio::detached);
}

asio::awaitable<void> server::do_listen() {
    auto executor = co_await asio::this_coro::executor;

    for (;;) {
        auto [ec, sock] = co_await acceptor_.async_accept(asio::as_tuple);
        if (ec) {
            if (ec == asio::error::operation_aborted) {
                SPDLOG_DEBUG("Acceptor is closed");
                co_return;
            }
            SPDLOG_ERROR("Failed to accept new connection; ec={}", fmt::streamed(ec));
            continue;
        }

        auto remote_endpoint = sock.remote_endpoint();
        beast::tcp_stream stream(std::move(sock));
        asio::co_spawn(executor, serve_session(std::move(stream)),
                       std::bind_front(handle_session_error, std::move(remote_endpoint)));
    }
}

asio::awaitable<void> server::serve_session(beast::tcp_stream stream) const {
    beast::flat_buffer buf;

    // TODO(KC): handle http async_read/async_write exception ?
    // can stream still be usable in this case?
    for (;;) {
        http::request<http::string_body> req;
        co_await http::async_read(stream, buf, req);

        auto resp = co_await handle_request(std::move(req));
        const bool keep_alive = resp.keep_alive();

        co_await beast::async_write(stream, std::move(resp));

        if (!keep_alive) {
            break;
        }
    }

    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send);
}

asio::awaitable<http::message_generator> server::handle_request(
    http::request<http::string_body> req) const {
    const auto http_ver = req.version();
    const auto keep_alive = req.keep_alive();

    try {
        request fwk_req(std::move(req));
        response fwk_resp(http_ver, keep_alive);

        // Locating route completes path params for a request, and may be used in
        // a middleware.
        const auto* handler = router_.locate_route(std::as_const(fwk_req).header().method(),
                                                   std::as_const(fwk_req).path(),
                                                   fwk_req.params());

        if (router_.run_pre_handle(fwk_req, fwk_resp) == middleware_result::abort) {
            co_return prepare_response(fwk_resp);
        }

        // User handler not found is not an unexpected error and thus should not abort
        // router-level middlewares.
        if (!handler) {
            const json::object body{
                {"error", json::object{{"message", "Unknown resource"}}}};
            fwk_resp.json(http::status::not_found, json::serialize(body));
            esl::ignore_unused(router_.run_post_handle(fwk_req, fwk_resp));
            co_return prepare_response(fwk_resp);
        }

        const auto result = co_await (*handler)(fwk_req, fwk_resp);

        // Aborted by a per-route middleware.
        if (result == middleware_result::abort) {
            co_return prepare_response(fwk_resp);
        }

        esl::ignore_unused(router_.run_post_handle(fwk_req, fwk_resp));

        co_return prepare_response(fwk_resp);
    } catch (const std::exception& ex) {
        const json::object body{{"error", json::object{{"message", ex.what()}}}};
        co_return make_unexpected_error_response(http_ver,
                                                 keep_alive,
                                                 json::serialize(body));
    }
}

// static
void server::handle_session_error(const asio::ip::tcp::endpoint& remote, std::exception_ptr eptr) {
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const boost::system::system_error& ex) {
            if (ex.code() == http::error::end_of_stream) {
                SPDLOG_DEBUG("Remote session closed; remote={}", fmt::streamed(remote));
            } else {
                SPDLOG_ERROR("Unhandled system error for session; remote={} what={}",
                             fmt::streamed(remote), ex.what());
            }
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Unhandled error for session; remote={} what={}",
                         fmt::streamed(remote), ex.what());
        }
    }
}

} // namespace fawkes
