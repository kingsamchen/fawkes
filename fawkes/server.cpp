// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/server.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/version.hpp>
#include <boost/system/system_error.hpp>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

namespace fawkes {
namespace {

namespace http = beast::http;

template<typename Body, typename Allocator>
http::message_generator handle_request( // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved)
        http::request<Body, http::basic_fields<Allocator>>&& req, const router& router) {
    const auto not_found =
            [&req](std::string_view why) {
                http::response<http::string_body> resp{http::status::not_found, req.version()};
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, "text/html");
                resp.keep_alive(req.keep_alive());
                resp.body() = std::string(why);
                resp.prepare_payload();
                return resp;
            };

    std::vector<param> params;
    params.resize(4);
    const auto* handler = router.locate_route(req.method(), req.target(), params);
    if (!handler) {
        return not_found("Unknown resource");
    }

    response http_resp;
    (*handler)(request{}, http_resp);

    http::response<http::string_body> resp{http::status::ok, req.version()};
    resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    resp.set(http::field::content_type, "text/plain");
    resp.keep_alive(req.keep_alive());
    resp.body() = http_resp.body;
    resp.prepare_payload();
    return resp;
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
                spdlog::debug("Acceptor is closed");
                co_return;
            }
            spdlog::error("Failed to accept new connection; ec={}", fmt::streamed(ec));
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

    for (;;) {
        http::request<http::string_body> req;
        co_await http::async_read(stream, buf, req);

        auto resp = handle_request(std::move(req), router_);
        const bool keep_alive = resp.keep_alive();

        co_await beast::async_write(stream, std::move(resp));

        if (!keep_alive) {
            break;
        }
    }

    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send);
}

// static
void server::handle_session_error(const asio::ip::tcp::endpoint& remote, std::exception_ptr eptr) {
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const boost::system::system_error& ex) {
            if (ex.code() == http::error::end_of_stream) {
                spdlog::debug("Remote session closed; remote={}", fmt::streamed(remote));
            } else {
                spdlog::error("Unhandled system error for session; remote={} what={}",
                              fmt::streamed(remote), ex.what());
            }
        } catch (const std::exception& ex) {
            spdlog::error("Unhandled error for session; remote={} what={}",
                          fmt::streamed(remote), ex.what());
        }
    }
}

} // namespace fawkes
