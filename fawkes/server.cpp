// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/server.hpp"

#include <functional>
#include <string>
#include <utility>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/version.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/system_error.hpp>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

#include "fawkes/errors.hpp"

namespace fawkes {
namespace {

namespace http = beast::http;
namespace json = boost::json;

http::response<http::string_body> make_error_response(unsigned int http_version,
                                                      bool keep_alive,
                                                      http::status status_code,
                                                      std::string&& body) {
    http::response<http::string_body> resp{status_code, http_version};
    resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    resp.set(http::field::content_type, "application/json");
    resp.keep_alive(keep_alive);
    resp.body() = std::move(body);
    resp.prepare_payload();
    return resp;
}

http::message_generator handle_request(http::request<http::string_body>&& req,
                                       const router& router) {
    const auto http_ver = req.version();
    const auto keep_alive = req.keep_alive();
    try {
        request fwk_req(std::move(req));

        const auto* handler = router.locate_route(fwk_req.header().method(),
                                                  fwk_req.path(),
                                                  fwk_req.mutable_params());
        if (!handler) {
            throw http_error(http::status::not_found, "Unknown resource");
        }

        response faw_resp;
        (*handler)(fwk_req, faw_resp);

        auto& resp_impl = faw_resp.as_mutable_impl();
        resp_impl.version(http_ver);
        resp_impl.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        resp_impl.keep_alive(keep_alive);
        resp_impl.prepare_payload();

        return std::move(resp_impl);
    } catch (const http_error& ex) {
        json::object err{{"message", ex.what()}};
        if (const auto& ec = ex.error_code(); ec.has_value()) {
            err["code"] = *ec;
        }
        const json::object body{{"error", std::move(err)}};
        return make_error_response(http_ver, keep_alive, ex.status_code(), json::serialize(body));
    } catch (const std::exception& ex) {
        const json::object body{{"error", json::object{{"message", ex.what()}}}};
        return make_error_response(http_ver, keep_alive, http::status::internal_server_error,
                                   json::serialize(body));
    }
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

    // TODO(KC): handle http async_read/async_write exception ?
    // can stream still be usable in this case?
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
