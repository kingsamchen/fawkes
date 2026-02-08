// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/server.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <string>
#include <utility>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
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
    auto main_executor = co_await asio::this_coro::executor;

    for (;;) {
        // Pick exeuctor.
        auto executor = [&main_executor, this] {
            return io_pool_ ? io_pool_->get_executor() : main_executor;
        }();

        auto [ec, sock] = co_await acceptor_.async_accept(executor, asio::as_tuple);
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
    using namespace std::chrono_literals;

    beast::flat_buffer buf;
    const auto read_timeout = opts_.effective_read_timeout();

    // TODO(KC): handle http async_read/async_write exception ?
    // can stream still be usable in this case?
    for (;;) {
        http::request_parser<http::string_body> parser;

        if (opts_.idle_timeout > 0ms) {
            stream.expires_after(opts_.idle_timeout);
        }

        constexpr std::size_t initial_buf_size = 512U;
        const auto bytes_read = co_await stream.async_read_some(buf.prepare(initial_buf_size));
        buf.commit(bytes_read);

        if (read_timeout > 0ms) {
            stream.expires_after(read_timeout);
        }

        [[maybe_unused]] const auto before_read = std::chrono::steady_clock::now();
        co_await http::async_read_header(stream, buf, parser);

        if (beast::iequals(parser.get()[http::field::expect], "100-continue")) {
            http::response<http::empty_body> continue_resp{http::status::continue_,
                                                           parser.get().version()};
            continue_resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            co_await http::async_write(stream, continue_resp);
        }

        if (!parser.is_done()) {
            co_await http::async_read(stream, buf, parser);
        }

        if (opts_.serve_timeout > 0ms) {
            const auto read_elapsed = std::chrono::steady_clock::now() - before_read;
            stream.expires_after(opts_.serve_timeout - read_elapsed);
        }

        auto resp = co_await handle_request(parser.release());
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

        if (co_await router_.run_pre_handle(fwk_req, fwk_resp) == middleware_result::abort) {
            co_return prepare_response(fwk_resp);
        }

        // User handler not found is not an unexpected error and thus should not abort
        // router-level middlewares.
        if (!handler) {
            const json::object body{
                {"error", json::object{{"message", "Unknown resource"}}}};
            fwk_resp.json(http::status::not_found, json::serialize(body));
            esl::ignore_unused(co_await router_.run_post_handle(fwk_req, fwk_resp));
            co_return prepare_response(fwk_resp);
        }

        const auto result = co_await (*handler)(fwk_req, fwk_resp);

        // Aborted by a per-route middleware.
        if (result == middleware_result::abort) {
            co_return prepare_response(fwk_resp);
        }

        esl::ignore_unused(co_await router_.run_post_handle(fwk_req, fwk_resp));

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
            if (ex.code() == http::error::end_of_stream ||
                ex.code() == asio::error::eof ||
                ex.code() == asio::error::connection_reset) {
                SPDLOG_DEBUG("Remote session closed; remote={} cause={}",
                             fmt::streamed(remote), ex.what());
            } else if (ex.code() == beast::error::timeout) {
                SPDLOG_ERROR("Remote session timed out; remote={}", fmt::streamed(remote));
            } else {
                SPDLOG_ERROR("Unhandled error for session; remote={} code={} what={}",
                             fmt::streamed(remote), ex.code().value(), ex.what());
            }
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Unhandled error for session; remote={} what={}",
                         fmt::streamed(remote), ex.what());
        }
    }
}

} // namespace fawkes
