set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags CONFIG REQUIRED)

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

target_include_directories(ping_pong_svc
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

fawkes_common_compile_configs(ping_pong_svc)

fawkes_clang_tidy_on_build(ping_pong_svc)

fawkes_use_sanitizers(ping_pong_svc)

get_target_property(ping_pong_svc_FILES ping_pong_svc SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ping_pong_svc_FILES})

target_precompile_headers(ping_pong_svc REUSE_FROM fawkes::fawkes)
