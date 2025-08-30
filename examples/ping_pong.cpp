// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <boost/asio/io_context.hpp>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include "fawkes/server.hpp"

DEFINE_uint32(port, 9876, "Port number to listen");

int main() {
    try {
        boost::asio::io_context io_ctx{1};
        fawkes::server svc(io_ctx);
        svc.do_get("/ping", [](const fawkes::request& req, fawkes::response& resp) {
            auto q = req.queries().get("q");
            if (q.has_value()) {
                spdlog::info("q={}", *q);
            }
            resp.mutable_body() = "Pong!";
        });
        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        spdlog::info("ping-pong server is listenning at {}", FLAGS_port);
        io_ctx.run();
    } catch (const std::exception& ex) {
        spdlog::error("Unexpected error: {}", ex.what());
    }

    return 0;
}
