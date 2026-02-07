// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <exception>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http/status.hpp>
#include <fmt/format.h>
#include <fmt/std.h>
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
        asio::io_context ioc{1};
        fawkes::io_thread_pool io_pool(4);

        fawkes::server server(ioc, io_pool);
        server.do_get("/tid",
                      [](const fawkes::request&, fawkes::response& resp) -> asio::awaitable<void> {
                          resp.text(http::status::ok, fmt::format("running on thread={}",
                                                                  std::this_thread::get_id()));
                          co_return;
                      });

        server.do_get("/status",
                      [](const fawkes::request&, fawkes::response& resp) -> asio::awaitable<void> {
                          resp.text(http::status::ok, std::string{"hello world"});
                          co_return;
                      });

        server.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        SPDLOG_INFO("Server is listening at {}", FLAGS_port);

        ioc.run();
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected error: {}", ex.what());
    }

    return 0;
}
