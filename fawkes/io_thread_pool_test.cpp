// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <latch>
#include <semaphore>
#include <set>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include <boost/asio/post.hpp>

#include "fawkes/io_thread_pool.hpp"

namespace {

namespace asio = boost::asio;

TEST_SUITE_BEGIN("IO Thread Pool");

TEST_CASE("Round-robin scheduling") {
    fawkes::io_thread_pool pool(4);

    std::vector<std::thread::id> tids;

    std::binary_semaphore sync{1};
    for (std::size_t i = 0U; i < pool.size(); ++i) {
        sync.acquire();
        asio::post(pool.get_executor(), [&sync, &tids] {
            tids.push_back(std::this_thread::get_id());
            sync.release();
        });
    }

    // Wait for previous work to complete.
    sync.acquire();

    REQUIRE_EQ(tids.size(), 4);
    CHECK_EQ(std::set<std::thread::id>(tids.begin(), tids.end()).size(), tids.size());

    asio::post(pool.get_executor(), [&sync, &tids] {
        CHECK_EQ(std::this_thread::get_id(), tids[0]);
        sync.release();
    });

    sync.acquire();
}

TEST_SUITE_END(); // IO Thread Pool

} // namespace
