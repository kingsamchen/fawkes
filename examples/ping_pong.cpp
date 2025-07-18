
#include <boost/asio/io_context.hpp>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include "fawkes/server.hpp"

DEFINE_uint32(port, 9876, "Port number to listen");

int main() {
    boost::asio::io_context io_ctx{1};

    try {
        fawkes::server svc(io_ctx);
        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        io_ctx.run();
    } catch (const std::exception& ex) {
        spdlog::error("Unexpected error: {}", ex.what());
    }

    return 0;
}
