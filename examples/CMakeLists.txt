set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags CONFIG REQUIRED)

#
# A ping-pong server
#

add_executable(ping_pong_svc)

target_sources(ping_pong_svc
  PRIVATE
    ping_pong.cpp
)

target_link_libraries(ping_pong_svc
  PRIVATE
    fawkes::fawkes
    gflags::gflags
    spdlog::spdlog
)

fawkes_common_compile_configs(ping_pong_svc)

fawkes_use_sanitizers(ping_pong_svc)

fawkes_clang_tidy_on_build(ping_pong_svc)

fawkes_source_folder(ping_pong_svc TARGET_FOLDER "examples")

target_precompile_headers(ping_pong_svc REUSE_FROM fawkes::pch)

#
# Using middlewares
#

add_executable(use_middlewares)

target_sources(use_middlewares
  PRIVATE
    use_middlewares.cpp
)

target_link_libraries(use_middlewares
  PRIVATE
    esl::esl
    fawkes::fawkes
    gflags::gflags
    spdlog::spdlog
)

fawkes_common_compile_configs(use_middlewares)

fawkes_use_sanitizers(use_middlewares)

fawkes_clang_tidy_on_build(use_middlewares)

fawkes_source_folder(use_middlewares TARGET_FOLDER "examples")

target_precompile_headers(use_middlewares REUSE_FROM fawkes::pch)

#
# Using io_thread_pool
#

add_executable(use_io_thread_pool)

target_sources(use_io_thread_pool
  PRIVATE
    use_io_thread_pool.cpp
)

target_link_libraries(use_io_thread_pool
  PRIVATE
    fawkes::fawkes
    fmt::fmt
    gflags::gflags
    spdlog::spdlog
)

fawkes_common_compile_configs(use_io_thread_pool)

fawkes_use_sanitizers(use_io_thread_pool)

fawkes_clang_tidy_on_build(use_io_thread_pool)

fawkes_source_folder(use_io_thread_pool TARGET_FOLDER "examples")

target_precompile_headers(use_io_thread_pool REUSE_FROM fawkes::pch)
