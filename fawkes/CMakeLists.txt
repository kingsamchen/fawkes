
add_library(fawkes)

add_library(fawkes::fawkes ALIAS fawkes)

target_sources(fawkes
  PRIVATE
    errors.hpp
    middleware.hpp
    path_params.hpp
    query_params.hpp
    request.cpp
    request.hpp
    router.cpp
    router.hpp
    server.cpp
    server.hpp
    tree.cpp
    tree.hpp

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
    esl::esl
    fmt::fmt
    spdlog::spdlog

  PUBLIC
    Boost::asio
    Boost::beast
    Boost::json
    Boost::url
    Threads::Threads
)

fawkes_common_compile_configs(fawkes)

fawkes_clang_tidy_on_build(fawkes)

fawkes_use_sanitizers(fawkes)

get_target_property(fawkes_FILES fawkes SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${fawkes_FILES})

target_precompile_headers(fawkes REUSE_FROM fawkes.pch)

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

    request_test.cpp
    router_test.cpp
    tree_test.cpp
)

target_link_libraries(fawkes_test
  PRIVATE
    doctest::doctest
    fawkes::fawkes
    fawkes::test_utils
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

target_precompile_headers(fawkes_test REUSE_FROM fawkes.pch)

add_test(NAME FawkesTest COMMAND fawkes_test)
