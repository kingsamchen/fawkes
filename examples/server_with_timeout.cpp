// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono> // IWYU pragma: keep
#include <cstdint>
#include <exception>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http/status.hpp>
#include <gflags/gflags.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace asio = boost::asio;
namespace http = boost::beast::http;

DEFINE_uint32(port, 7890, "Port number to listen on");

struct log_access {
    static fawkes::middleware_result pre_handle(fawkes::request& req, fawkes::response& /*resp*/) {
        SPDLOG_INFO("Entering {} {}", req.header().method_string(), req.target());
        return fawkes::middleware_result::proceed;
    }

    static fawkes::middleware_result post_handle(fawkes::request& req, fawkes::response& resp) {
        SPDLOG_INFO("Leave {} -> {}", req.target(), resp.status_code());
        return fawkes::middleware_result::proceed;
    }
};

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    spdlog::cfg::load_env_levels();

    try {
        asio::io_context io_ctx{1};

        fawkes::server server(io_ctx);
        using namespace std::chrono_literals;
        const fawkes::server::options opts{
            .idle_timeout = 30s,
            .read_timeout = 5s,
            .serve_timeout = 15s,
        };
        server.set_options(opts);

        server.do_get("/query",
                      fawkes::middlewares::use(log_access{}),
                      [](const fawkes::request& /*req*/, fawkes::response& resp)
                          -> asio::awaitable<void> {
                          resp.text(http::status::ok, std::string{"hello world"});
                          co_return;
                      });

        server.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        SPDLOG_INFO("Server is listenning at {}", FLAGS_port);

        io_ctx.run();
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected error: {}", ex.what());
    }

    return 0;
}
