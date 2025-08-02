// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>

#include "fawkes/router.hpp"

namespace fawkes {

namespace asio = boost::asio;
namespace beast = boost::beast;

class server {
public:
    explicit server(asio::io_context& io_ctx)
        : io_ctx_(io_ctx),
          acceptor_(io_ctx_) {}

    ~server() = default;

    server(const server&) = delete;
    server(server&&) = delete;
    server& operator=(const server&) = delete;
    server& operator=(server&&) = delete;

    void listen_and_serve(const std::string& addr, std::uint16_t port);

    // Throws `std::invalid_argument` if there is path conflict.
    void do_get(std::string_view path, route_handler_t&& handler) {
        router_.add_route(beast::http::verb::get, path, std::move(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    void do_post(std::string_view path, route_handler_t&& handler) {
        router_.add_route(beast::http::verb::post, path, std::move(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    void do_patch(std::string_view path, route_handler_t&& handler) {
        router_.add_route(beast::http::verb::patch, path, std::move(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    void do_put(std::string_view path, route_handler_t&& handler) {
        router_.add_route(beast::http::verb::put, path, std::move(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    void do_delete(std::string_view path, route_handler_t&& handler) {
        router_.add_route(beast::http::verb::delete_, path, std::move(handler));
    }

    // Throws `std::invalid_argument` if there is path conflict.
    void do_head(std::string_view path, route_handler_t&& handler) {
        router_.add_route(beast::http::verb::head, path, std::move(handler));
    }

private:
    asio::awaitable<void> do_listen();

    [[nodiscard]] asio::awaitable<void> serve_session(beast::tcp_stream stream) const;

    static void handle_session_error(const asio::ip::tcp::endpoint& remote,
                                     std::exception_ptr eptr);

    asio::io_context& io_ctx_;
    asio::ip::tcp::endpoint endpoint_; // TODO(KC): do we really need to save it?
    asio::ip::tcp::acceptor acceptor_;
    router router_;
};

} // namespace fawkes
