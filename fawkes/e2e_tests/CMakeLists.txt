set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags CONFIG REQUIRED)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

add_executable(fawkes_test_server)

target_sources(fawkes_test_server
  PRIVATE
    test_server.cpp
)

target_link_libraries(fawkes_test_server
  PRIVATE
    fawkes::fawkes

    fmt::fmt
    gflags::gflags
    spdlog::spdlog
)

fawkes_common_compile_configs(fawkes_test_server)

fawkes_use_sanitizers(fawkes_test_server)

fawkes_clang_tidy_on_build(fawkes_test_server)

fawkes_source_folder(fawkes_test_server TARGET_FOLDER "tests")

target_precompile_headers(fawkes_test_server
  REUSE_FROM
    fawkes::pch
)

set(FAWKES_E2E_TEST_SUITES
  server_context
  server_stop
  server_transport
  server_timeout
  server_write
  server_workers
)

foreach(suite IN LISTS FAWKES_E2E_TEST_SUITES)
  add_test(NAME fawkes_e2e.${suite}
    COMMAND
    ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_${suite}.py
    --server $<TARGET_FILE:fawkes_test_server>
  )

  set_tests_properties(fawkes_e2e.${suite}
    PROPERTIES
      LABELS "fawkes_e2e"
      TIMEOUT 120
  )
endforeach()
