// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/http/status.hpp>
#include <esl/ignore_unused.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace asio = boost::asio;
namespace http = boost::beast::http;

DEFINE_uint32(port, 7890, "Port number to listen on");

int main(int argc, char* argv[]) {
    try {
        gflags::ParseCommandLineFlags(&argc, &argv, true);

        asio::io_context io_ctx{1};
        fawkes::server svc(io_ctx);
        svc.do_get("/ping",
                   [](const fawkes::request& req, fawkes::response& resp)
                       -> asio::awaitable<void> {
                       auto q = req.queries().get("q");
                       if (q.has_value()) {
                           SPDLOG_INFO("q={}", *q);
                       }
                       resp.text(http::status::ok, std::string{"Pong!"});
                       co_return;
                   });
        svc.do_get("/delayed",
                   [](const fawkes::request& req, fawkes::response& resp)
                       -> asio::awaitable<void> {
                       esl::ignore_unused(req);
                       SPDLOG_INFO("wait for a moment...");

                       asio::steady_timer timer(co_await asio::this_coro::executor);
                       timer.expires_after(std::chrono::seconds(3));
                       co_await timer.async_wait();

                       resp.text(http::status::ok, std::string{"Pong!"});

                       co_return;
                   });
        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        SPDLOG_INFO("ping-pong server is listenning at {}", FLAGS_port);
        io_ctx.run();
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected error: {}", ex.what());
    }

    return 0;
}
