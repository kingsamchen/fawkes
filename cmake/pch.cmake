
set(FAWKES_PCH_HEADERS_BASE
  "<algorithm>"
  "<any>"
  "<array>"
  "<atomic>"
  "<deque>"
  "<fstream>"
  "<functional>"
  "<iostream>"
  "<limits>"
  "<map>"
  "<memory>"
  "<mutex>"
  "<numeric>"
  "<optional>"
  "<queue>"
  "<set>"
  "<sstream>"
  "<stack>"
  "<string_view>"
  "<string>"
  "<thread>"
  "<tuple>"
  "<unordered_map>"
  "<unordered_set>"
  "<utility>"
  "<variant>"
  "<vector>"
)

add_library(fawkes.pch OBJECT)

target_sources(fawkes.pch
  PRIVATE
    ${FAWKES_DIR}/build/pch/pch.cpp
)

fawkes_common_compile_configs(fawkes.pch)

fawkes_use_sanitizers(fawkes.pch)

target_precompile_headers(fawkes.pch
  PRIVATE
    "${FAWKES_PCH_HEADERS_BASE}"
)
