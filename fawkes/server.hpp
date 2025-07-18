// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <cstdint>
#include <exception>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>

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

private:
    asio::awaitable<void> do_listen();

    static asio::awaitable<void> serve_session(beast::tcp_stream stream);

    static void handle_session_error(const asio::ip::tcp::endpoint& remote,
                                     std::exception_ptr eptr);

    asio::io_context& io_ctx_;
    asio::ip::tcp::endpoint endpoint_; // TODO(KC): do we really need to save it?
    asio::ip::tcp::acceptor acceptor_;
};

} // namespace fawkes
