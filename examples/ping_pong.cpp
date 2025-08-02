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
        svc.do_get("/ping", [](const fawkes::request&, fawkes::response& resp) {
            resp.body = "Pong!";
        });
        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        io_ctx.run();
    } catch (const std::exception& ex) {
        spdlog::error("Unexpected error: {}", ex.what());
    }

    return 0;
}
