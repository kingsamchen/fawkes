// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/http/status.hpp>
#include <esl/ignore_unused.h>
#include <gflags/gflags.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include "fawkes/io_thread_pool.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace asio = boost::asio;
namespace http = boost::beast::http;

DEFINE_uint32(port, 7890, "Port number to listen on");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    spdlog::cfg::load_env_levels();

    try {
        asio::io_context io_ctx{1};
        fawkes::io_thread_pool io_pool(4);
        asio::thread_pool worker_pool(4);

        fawkes::server svc(io_ctx, io_pool);

        // Enable serve timeout, in case some handler may get stuck.
        fawkes::server::options opts;
        opts.serve_timeout = std::chrono::seconds(15);
        svc.set_options(opts);

        asio::signal_set signals(io_ctx, SIGINT, SIGTERM);
        signals.async_wait([&svc, &io_pool, &worker_pool](const boost::system::error_code& ec,
                                                          int /*signal*/) {
            if (!ec) {
                SPDLOG_INFO("Received signal, shutting down the server");

                // svc.stop() would close the acceptor to ensure no more new connections would
                // be accepted. It also closes any idle connections, and any active connections
                // after they finish the current request and response.
                // If your server impl has other sources that may generate new events or activities,
                // you may need to stop them first as well.
                svc.stop();

                // Wait for active io events to finish first.
                io_pool.join();

                // Then wait for auxiliary worker tasks to finish.
                worker_pool.join();

                SPDLOG_INFO("Signal handler exits");
            }
        });

        svc.do_get("/ping",
                   [](const fawkes::request& req, fawkes::response& resp)
                       -> asio::awaitable<void> {
                       esl::ignore_unused(req);
                       resp.text(http::status::ok, std::string{"Pong!"});
                       co_return;
                   });

        svc.do_post("/echo",
                    [](const fawkes::request& req, fawkes::response& resp)
                        -> asio::awaitable<void> {
                        SPDLOG_INFO("Request Content-Type: {}", req.header()["Content-Type"]);
                        resp.text(http::status::ok, req.body());
                        co_return;
                    });
        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        SPDLOG_INFO("Server is listenning at {}", FLAGS_port);
        io_ctx.run();
        SPDLOG_INFO("Server exits");
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected error: {}", ex.what());
    }

    return 0;
}
