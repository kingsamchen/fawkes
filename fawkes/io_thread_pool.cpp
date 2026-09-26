// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include "fawkes/io_thread_pool.hpp"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <thread>

#include <spdlog/spdlog.h>

namespace fawkes {

io_thread_pool::io_thread_pool(std::size_t num_threads) {
    if (num_threads == 0) {
        throw std::invalid_argument("number of threads cannot be 0");
    }

    pool_.reserve(num_threads);
    for (auto i = 0U; i < num_threads; ++i) {
        auto& ctx = pool_.emplace_back();
        ctx.thd = std::jthread([&ioc = *ctx.io_ctx_ptr] {
            for (;;) {
                try {
                    ioc.run();
                    break;
                } catch (const std::exception& ex) {
                    SPDLOG_ERROR("Unhandled exception from io_thread; ex={}", ex.what());
                }
            }
        });
    }
}

void io_thread_pool::stop() noexcept {
    for (std::size_t i = 0; i < pool_.size(); ++i) {
        try {
            pool_[i].io_ctx_ptr->stop();
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("Unhandled exception while stopping io_thread_pool context {}; what={}",
                         i, ex.what());
        } catch (...) {
            SPDLOG_ERROR("Unknown exception while stopping io_thread_pool context {}", i);
        }
    }
}

} // namespace fawkes
