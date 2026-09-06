find_package(absl CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS asio)
find_package(Boost CONFIG REQUIRED COMPONENTS beast)
find_package(Boost CONFIG REQUIRED COMPONENTS core)
find_package(Boost CONFIG REQUIRED COMPONENTS json)
find_package(Boost CONFIG REQUIRED COMPONENTS url)
find_package(doctest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED)

target_compile_definitions(Boost::asio
  INTERFACE
    $<$<BOOL:${WIN32}>:_WIN32_WINNT=0x0601>

    BOOST_ASIO_NO_DEPRECATED=1
)

# Work around MSVC false-positive unreachable-code warnings in fmt 12.2.0.
# https://github.com/fmtlib/fmt/pull/4822
target_compile_options(fmt::fmt
  INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/wd4702>
)

target_compile_definitions(spdlog::spdlog
  INTERFACE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
)
