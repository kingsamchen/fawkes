
add_library(fawkes)

add_library(fawkes::fawkes ALIAS fawkes)

target_sources(fawkes
  PRIVATE
    server.cpp
    server.hpp

  $<$<BOOL:${WIN32}>:
  >

  $<$<NOT:$<BOOL:${WIN32}>>:
  >
)

target_include_directories(fawkes
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(fawkes
  PRIVATE
    fmt::fmt
    spdlog::spdlog

  PUBLIC
    Boost::asio
    Boost::beast
    Threads::Threads
)

fawkes_common_compile_configs(fawkes)

fawkes_clang_tidy_on_build(fawkes)

fawkes_use_sanitizers(fawkes)

get_target_property(fawkes_FILES fawkes SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${fawkes_FILES})

target_precompile_headers(fawkes
  PRIVATE
    "${FAWKES_PCH_HEADERS_BASE}"
)

#
# fawkes_test
#

if(NOT BUILD_TESTING)
  return()
endif()

add_executable(fawkes_test)

target_sources(fawkes_test
  PRIVATE
    test_main.cpp
)

target_link_libraries(fawkes_test
  PRIVATE
    doctest::doctest
    fawkes::fawkes
    spdlog::spdlog # TODO: Resolve pch issue
)

target_include_directories(fawkes_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

fawkes_common_compile_configs(fawkes_test)

fawkes_clang_tidy_on_build(fawkes_test)

fawkes_use_sanitizers(fawkes_test)

get_target_property(fawkes_test_FILES fawkes_test SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${fawkes_test_FILES})

target_precompile_headers(fawkes_test REUSE_FROM fawkes)

add_test(NAME FawkesTest COMMAND fawkes_test)
