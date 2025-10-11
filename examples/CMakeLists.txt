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

get_target_property(ping_pong_svc_FILES ping_pong_svc SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ping_pong_svc_FILES})

target_precompile_headers(ping_pong_svc REUSE_FROM fawkes::fawkes)

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

get_target_property(use_middlewares_FILES use_middlewares SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${use_middlewares_FILES})

target_precompile_headers(use_middlewares REUSE_FROM fawkes::fawkes)
