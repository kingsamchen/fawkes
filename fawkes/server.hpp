// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <stop_token>
#include <string>
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
#include <boost/beast/http/string_body.hpp>

#include "fawkes/io_thread_pool.hpp"
#include "fawkes/router.hpp"

namespace fawkes {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;

class server {
public:
    struct options {
        // The maximum duration allowed for an established connection being idle.
        // If zero or negative, there is no timeout.
        std::chrono::milliseconds idle_timeout{0};

        // The maximum duration allowed to read the entire request, including the body.
        // If zero or negative, there is no timeout.
        std::chrono::milliseconds read_timeout{0};

        // The maximum duration allowed to read the entire request, handle it, and send back the
        // response.
        // Should be larger than `read_timeout`.
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
    };

    explicit server(asio::io_context& io_ctx)
        : io_ctx_(io_ctx),
          acceptor_(io_ctx_) {}

    server(asio::io_context& io_ctx, io_thread_pool& io_pool)
        : io_ctx_(io_ctx),
          io_pool_(&io_pool),
          acceptor_(io_ctx_) {}

    ~server() = default;

    server(const server&) = delete;
    server(server&&) = delete;
    server& operator=(const server&) = delete;
    server& operator=(server&&) = delete;

    void set_options(const options& opts) {
        opts_ = opts;
    }

    void listen_and_serve(const std::string& addr, std::uint16_t port);

    void stop() {
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
                                                      std::stop_token stop_token) const;

    [[nodiscard]] asio::awaitable<http::message_generator> handle_request(
        http::request<http::string_body> req) const;

    static void handle_session_error(const asio::ip::tcp::endpoint& remote,
                                     std::exception_ptr eptr);

    asio::io_context& io_ctx_;
    io_thread_pool* io_pool_{nullptr};
    options opts_;
    std::stop_source stop_source_;
    asio::ip::tcp::acceptor acceptor_;
    router router_;
};

} // namespace fawkes
