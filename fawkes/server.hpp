// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>

#include "fawkes/io_thread_pool.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/router.hpp"

namespace fawkes {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;

namespace detail {

// Set `close_connection` to true overrides any keep-alive set by handlers or hooks,
// keep as-is if false.
http::message_generator prepare_response(bool close_connection, http::verb method, response& resp);

} // namespace detail

class server {
public:
    struct options {
        // The maximum duration allowed for an established connection being idle.
        // If zero or negative, there is no timeout.
        std::chrono::milliseconds idle_timeout{0};

        // The maximum duration allowed to read the entire request, including the body.
        // If zero or negative, there is no timeout.
        std::chrono::milliseconds read_timeout{0};

        // The maximum duration allowed to read a request and write its response,
        // measured after the first received data. It does not interrupt handlers
        // or worker jobs. Should be larger than `read_timeout`.
        // If zero or negative, there is no timeout.
        std::chrono::milliseconds serve_timeout{0};

        // `read_timeout` may be larger than `serve_timeout`, making the serve timeout effectively
        // a read timeout.
        [[nodiscard]] constexpr auto effective_read_timeout() const noexcept {
            using namespace std::chrono_literals;

            const auto [min, max] = std::minmax(read_timeout, serve_timeout);
            if (max <= 0ms) {
                return 0ms;
            }
            return min > 0ms ? min : max;
        }

        static constexpr std::size_t no_body_limit = std::numeric_limits<std::size_t>::max();
        static constexpr std::uint32_t no_header_limit = std::numeric_limits<std::uint32_t>::max();

        // Size in bytes. If unset, use Beast's default (1 MiB). Use `no_body_limit` to disable
        // the body limit; zero rejects nonempty bodies.
        std::optional<std::size_t> request_body_limit;

        // Size in bytes, including the request line and header delimiters. If unset, use Beast's
        // default (8 KiB). Use `no_header_limit` for the largest header limit Beast supports.
        std::optional<std::uint32_t> request_header_limit;

        // Delay before closing a connection rejected during request parsing.
        // Closing with unread request data makes the kernel send a RST, which can discard the
        // error response; the delay gives the peer a best-effort chance to read it first.
        // If zero or negative, close immediately.
        std::chrono::milliseconds rst_avoidance_delay{500};
    };

    explicit server(asio::io_context& io_ctx)
        : io_ctx_(io_ctx),
          acceptor_(io_ctx_.get()) {}

    server(asio::io_context& io_ctx, io_thread_pool& io_pool)
        : io_ctx_(io_ctx),
          io_pool_(&io_pool),
          acceptor_(io_ctx_.get()) {}

    server(server&& other) noexcept = default;
    ~server() = default;

    server(const server&) = delete;
    server& operator=(const server&) = delete;
    server& operator=(server&&) = delete;

    void set_options(const options& opts) {
        opts_ = opts;
    }

    // Configure before listening; the callback must support concurrent calls.
    void set_rejection_response_hook(std::move_only_function<void(response&) const> hook) {
        rejection_response_hook_ = std::move(hook);
    }

    void listen_and_serve(const std::string& addr, std::uint16_t port);

    // Not thread-safe, must be called on the thread running the acceptor's executor.
    // This function initiates the shutdown of the server, and returns immediately.
    // Idle connections and incomplete request reads are cancelled. Fully read
    // requests finish their handlers and responses before their connections close.
    void stop() {
#ifndef NDEBUG
        {
            const auto& ex = acceptor_.get_executor();
            if (const auto* ex_ptr = ex.target<asio::io_context::executor_type>();
                ex_ptr != nullptr) {
                assert(ex_ptr->running_in_this_thread());
            }
        }
#endif
        [[maybe_unused]] boost::system::error_code ec;
        acceptor_.close(ec);
        stop_source_.request_stop();
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void do_get(std::string_view path, H&& handler) {
        router_.add_route(http::verb::get, path, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void do_get(std::string_view path, std::tuple<Mws...>&& middlewares, H&& handler) {
        router_.add_route(http::verb::get, path, std::move(middlewares), std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void do_post(std::string_view path, H&& handler) {
        router_.add_route(http::verb::post, path, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void do_post(std::string_view path, std::tuple<Mws...>&& middlewares, H&& handler) {
        router_.add_route(http::verb::post, path, std::move(middlewares), std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void do_patch(std::string_view path, H&& handler) {
        router_.add_route(http::verb::patch, path, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void do_patch(std::string_view path, std::tuple<Mws...>&& middlewares, H&& handler) {
        router_.add_route(http::verb::patch, path, std::move(middlewares),
                          std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void do_put(std::string_view path, H&& handler) {
        router_.add_route(http::verb::put, path, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void do_put(std::string_view path, std::tuple<Mws...>&& middlewares, H&& handler) {
        router_.add_route(http::verb::put, path, std::move(middlewares), std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void do_delete(std::string_view path, H&& handler) {
        router_.add_route(http::verb::delete_, path, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void do_delete(std::string_view path, std::tuple<Mws...>&& middlewares, H&& handler) {
        router_.add_route(http::verb::delete_, path, std::move(middlewares),
                          std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H>
    void do_head(std::string_view path, H&& handler) {
        router_.add_route(http::verb::head, path, std::forward<H>(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    template<is_user_handler H, is_middleware... Mws>
    void do_head(std::string_view path, std::tuple<Mws...>&& middlewares, H&& handler) {
        router_.add_route(http::verb::head, path, std::move(middlewares), std::forward<H>(handler));
    }

    router& get_router() {
        return router_;
    }

private:
    asio::awaitable<void> do_listen();

    [[nodiscard]] asio::awaitable<void> serve_session(beast::tcp_stream stream,
                                                      conn_info info,
                                                      std::stop_token stop_token) const;

    [[nodiscard]] asio::awaitable<http::message_generator> handle_request(
        http::request<http::string_body> req, conn_info info) const;

    static void handle_session_error(const asio::ip::tcp::endpoint& remote,
                                     std::exception_ptr eptr);

    // Returns `std::nullopt` if the read error `ec` does not warrant a response before the
    // connection closes.
    [[nodiscard]] std::optional<http::message_generator> make_rejection_response(
        const boost::system::error_code& ec,
        const http::request<http::string_body>& req,
        const conn_info& info) const;

    std::reference_wrapper<asio::io_context> io_ctx_;
    io_thread_pool* io_pool_{nullptr};
    options opts_;
    std::stop_source stop_source_;
    asio::ip::tcp::acceptor acceptor_;
    router router_;
    std::move_only_function<void(response&) const> rejection_response_hook_;
};

} // namespace fawkes
