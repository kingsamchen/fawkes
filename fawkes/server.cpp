// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/server.hpp"

#include <chrono>
#include <exception>
#include <expected>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
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
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

#include "fawkes/errors.hpp"
#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"

namespace fawkes {

namespace json = boost::json;

namespace {

constexpr std::string_view expect_value = "100-continue";
constexpr unsigned int http_version_1_1 = 11;

template<typename F>
auto make_no_fail(F&& fn, std::source_location loc = std::source_location::current()) {
    return [f = std::forward<F>(fn), loc]() noexcept {
        try {
            f();
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Unhandled error for no-fail; original={}:{} what={}",
                         loc.file_name(), loc.line(), ex.what());
        } catch (...) {
            SPDLOG_ERROR("Unhandled error for no-fail; original={}:{}",
                         loc.file_name(), loc.line());
        }
    };
}

[[nodiscard]] std::expected<conn_info, boost::system::error_code>
make_conn_info(const asio::ip::tcp::socket& sock) {
    boost::system::error_code ec;

    auto local = sock.local_endpoint(ec);
    if (ec) {
        return std::unexpected(ec);
    }

    auto remote = sock.remote_endpoint(ec);
    if (ec) {
        return std::unexpected(ec);
    }

    return std::expected<conn_info, boost::system::error_code>{
        std::in_place, std::move(local), std::move(remote)};
}

std::string_view safe_request_content_length(const http::request<http::string_body>& request) {
    // `content_length()` requires a completed header; body_limit can be reported before then.
    const std::string_view raw = request[http::field::content_length];
    if (raw.empty()) {
        return "unknown";
    }
    return raw;
}

void set_body_limit(http::request_parser<http::string_body>& parser, const server::options& opts) {
    if (opts.request_body_limit == server::options::default_body_limit) {
        return;
    }

    if (opts.request_body_limit != server::options::no_body_limit) {
        parser.body_limit(opts.request_body_limit);
    } else {
        parser.body_limit(boost::none);
    }
}

} // namespace

namespace detail {

http::message_generator prepare_response(http::verb method, response& resp) {
    auto& impl = resp.as_impl();

    // RFC says this response cannot even contain the `Content-Length` header.
    if (impl.result() == http::status::no_content) {
        impl.erase(http::field::content_length);
        impl.erase(http::field::transfer_encoding);
        impl.erase(http::field::trailer);
        return http::response<http::empty_body>(std::move(impl.base()));
    }

    // Remove the `Content-Length` header to prevent the handler setting a wrong value.
    if (impl.result() == http::status::not_modified) {
        impl.erase(http::field::content_length);
        impl.erase(http::field::content_type);
        impl.erase(http::field::transfer_encoding);
        impl.erase(http::field::trailer);
        return http::response<http::empty_body>(std::move(impl.base()));
    }

    // If the body has been generated anyway, we set the `Content-Length` header,
    // but discard the body.
    if (method == http::verb::head) {
        impl.erase(http::field::transfer_encoding);
        impl.erase(http::field::trailer);

        if (!impl.body().empty()) {
            impl.content_length(impl.body().size());
        }

        return http::response<http::empty_body>(std::move(impl.base()));
    }

    impl.prepare_payload();
    return std::move(impl);
}

} // namespace detail

http::message_generator server::make_content_too_large_response(unsigned int version,
                                                                http::verb method) const {
    const json::object body{{"error", json::object{{"message", "Request body too large"}}}};

    response resp(version, false);
    resp.json(http::status::payload_too_large, json::serialize(body));
    if (rejection_response_hook_) {
        rejection_response_hook_(resp);
    }
    return detail::prepare_response(method, resp);
}

void server::listen_and_serve(const std::string& addr, std::uint16_t port) {
    const auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address(addr), port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);
    asio::co_spawn(io_ctx_.get(), do_listen(), [](std::exception_ptr eptr) {
        if (eptr) {
            std::rethrow_exception(eptr);
        }
    });
}

asio::awaitable<void> server::do_listen() {
    auto main_executor = co_await asio::this_coro::executor;

    for (;;) {
        // Pick executor.
        auto executor = [main_executor, this] {
            return io_pool_ ? io_pool_->get_executor() : main_executor;
        }();

        boost::system::error_code ec;
        auto sock = co_await acceptor_.async_accept(executor, asio::redirect_error(ec));
        if (ec) {
            if (ec == asio::error::operation_aborted || !acceptor_.is_open()) {
                SPDLOG_DEBUG("Acceptor is closed");
                co_return;
            }
            SPDLOG_ERROR("Failed to accept new connection; ec={}", fmt::streamed(ec));
            continue;
        }

        sock.set_option(asio::ip::tcp::no_delay{true}, ec);
        if (ec) {
            SPDLOG_WARN("Failed to set TCP_NODELAY; ec={}", fmt::streamed(ec));
            continue;
        }

        // The peer may have reset the connection while it was queued for accept.
        auto info_or = make_conn_info(sock);
        if (!info_or) {
            SPDLOG_WARN("Failed to construct the connection info; ec={}",
                        fmt::streamed(info_or.error()));
            continue;
        }

        beast::tcp_stream stream(std::move(sock));
        auto remote = info_or->remote;
        asio::co_spawn(executor,
                       serve_session(
                           std::move(stream), std::move(*info_or), stop_source_.get_token()),
                       [endpoint = std::move(remote)](std::exception_ptr eptr) {
                           handle_session_error(endpoint, eptr);
                       });
    }
}

asio::awaitable<void> server::serve_session(beast::tcp_stream stream,
                                            conn_info info,
                                            std::stop_token stop_token) const {
    using namespace std::chrono_literals;

    auto executor = co_await asio::this_coro::executor;

    asio::cancellation_signal stop_signal;
    const std::stop_callback stop_callback(stop_token,
                                           make_no_fail([&stop_signal, executor]() {
                                               asio::dispatch(executor, [&stop_signal]() {
                                                   stop_signal.emit(asio::cancellation_type::all);
                                               });
                                           }));

    beast::flat_buffer buf;
    const auto read_timeout = opts_.effective_read_timeout();

    // TODO(KC): handle http async_read/async_write exception ?
    // can stream still be usable in this case?
    bool need_rst_avoidance = false;
    for (; !stop_token.stop_requested();) {
        http::request_parser<http::string_body> parser;
        set_body_limit(parser, opts_);

        if (opts_.idle_timeout > 0ms) {
            stream.expires_after(opts_.idle_timeout);
        }

        constexpr std::size_t initial_buf_size = 512U;
        const auto bytes_read = co_await stream.async_read_some(
            buf.prepare(initial_buf_size),
            asio::bind_cancellation_slot(stop_signal.slot(), asio::deferred));
        buf.commit(bytes_read);

        if (read_timeout > 0ms) {
            stream.expires_after(read_timeout);
        }

        [[maybe_unused]] const auto before_read = std::chrono::steady_clock::now();

        boost::system::error_code ec;
        const auto bytes_consumed = parser.put(buf.data(), ec);
        if (ec && ec != http::error::need_more) {
            if (ec == http::error::body_limit) {
                const auto& req = parser.get();
                SPDLOG_ERROR("Failed to serve the request because body limit exceeded; "
                             "method={} target={} content_length={} remote={}",
                             req.method_string(),
                             req.target(),
                             safe_request_content_length(req),
                             fmt::streamed(info.remote));
                auto resp = make_content_too_large_response(req.version(), req.method());
                co_await beast::async_write(stream, std::move(resp));
                need_rst_avoidance = true;
                break;
            }
            throw boost::system::system_error(ec);
        }
        buf.consume(bytes_consumed);

        if (!parser.is_header_done()) {
            if (stop_token.stop_requested()) {
                break;
            }
            co_await http::async_read_header(
                stream,
                buf,
                parser,
                asio::bind_cancellation_slot(stop_signal.slot(), asio::redirect_error(ec)));
            if (ec) {
                if (ec == http::error::body_limit) {
                    const auto& req = parser.get();
                    SPDLOG_ERROR("Failed to serve the request because body limit exceeded; "
                                 "method={} target={} content_length={} remote={}",
                                 req.method_string(),
                                 req.target(),
                                 safe_request_content_length(req),
                                 fmt::streamed(info.remote));
                    auto resp = make_content_too_large_response(req.version(), req.method());
                    co_await beast::async_write(stream, std::move(resp));
                    need_rst_avoidance = true;
                    break;
                }
                throw boost::system::system_error(ec);
            }
        }

        if (parser.get().version() == http_version_1_1 &&
            beast::iequals(parser.get()[http::field::expect], expect_value)) {
            const http::response<http::empty_body> continue_resp{http::status::continue_,
                                                                 parser.get().version()};
            co_await http::async_write(stream, continue_resp);
        }

        // Read the body.
        if (!parser.is_done()) {
            if (stop_token.stop_requested()) {
                break;
            }
            co_await http::async_read(
                stream,
                buf,
                parser,
                asio::bind_cancellation_slot(stop_signal.slot(), asio::redirect_error(ec)));
            if (ec) {
                if (ec == http::error::body_limit) {
                    const auto& req = parser.get();
                    SPDLOG_ERROR("Failed to serve the request because body limit exceeded; "
                                 "method={} target={} content_length={} remote={}",
                                 req.method_string(),
                                 req.target(),
                                 safe_request_content_length(req),
                                 fmt::streamed(info.remote));
                    auto resp = make_content_too_large_response(req.version(), req.method());
                    co_await beast::async_write(stream, std::move(resp));
                    need_rst_avoidance = true;
                    break;
                }
                throw boost::system::system_error(ec);
            }
        }

        if (opts_.serve_timeout > 0ms) {
            const auto read_elapsed = std::chrono::steady_clock::now() - before_read;
            stream.expires_after(opts_.serve_timeout - read_elapsed);
        }

        auto resp = co_await handle_request(parser.release(), info);
        const bool keep_alive = resp.keep_alive();

        co_await beast::async_write(stream, std::move(resp));

        if (!keep_alive) {
            break;
        }
    }

    boost::system::error_code ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, ec);

    if (!ec && need_rst_avoidance && opts_.rst_avoidance_delay > 0ms) {
        asio::steady_timer waiter(executor, opts_.rst_avoidance_delay);
        co_await waiter.async_wait(asio::redirect_error(ec));
    }
}

asio::awaitable<http::message_generator> server::handle_request(
    http::request<http::string_body> req, conn_info info) const {
    const auto method = req.method();
    const auto http_ver = req.version();
    const auto keep_alive = req.keep_alive();

    response fwk_resp(http_ver, keep_alive);
    try {
        request fwk_req(std::move(req), std::move(info));
        co_await router_.dispatch(fwk_req, fwk_resp);
    } catch (const invalid_request_target& ex) {
        SPDLOG_ERROR("Invalid request target; what={} target={}", ex.what(), ex.target());
        const json::object body{{"error", json::object{{"message", "Invalid request"}}}};
        fwk_resp.json(http::status::bad_request, json::serialize(body));
        if (rejection_response_hook_) {
            rejection_response_hook_(fwk_resp);
        }
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected exception for the request; what={}", ex.what());
        const json::object body{
            {"error", json::object{{"message", "Unexpected server error"}}}};
        fwk_resp.json(http::status::internal_server_error, json::serialize(body));
    } catch (...) {
        SPDLOG_ERROR("Unexpected unknown exception for the request");
        const json::object body{
            {"error", json::object{{"message", "Unexpected server error"}}}};
        fwk_resp.json(http::status::internal_server_error, json::serialize(body));
    }

    co_return detail::prepare_response(method, fwk_resp);
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
                             fmt::streamed(remote), ex.code().message());
            } else if (ex.code() == beast::error::timeout ||
                       ex.code() == asio::error::operation_aborted) {
                SPDLOG_ERROR("Remote session timed out or cancelled; remote={} cause={}",
                             fmt::streamed(remote), ex.code().message());
            } else {
                SPDLOG_ERROR("Unhandled error for session; remote={} what={}",
                             fmt::streamed(remote), ex.what());
            }
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Unhandled error for session; remote={} what={}",
                         fmt::streamed(remote), ex.what());
        }
    }
}

} // namespace fawkes
