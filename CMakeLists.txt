cmake_minimum_required(VERSION 3.20)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Warnings on; hot-path hygiene flags (-fno-exceptions -fno-rtti) deferred — see README decision log.
add_compile_options(-Wall -Wextra)

# --- liblob: all engine logic lives here, tested once, shared by both binaries ---
add_library(lob STATIC
  src/book.cpp
  src/engine.cpp
  src/parser.cpp
  src/snapshot.cpp
  src/generator.cpp
)
target_include_directories(lob PUBLIC ${CMAKE_SOURCE_DIR}/include)

# --- binary B: correctness (replay + snapshot-diff vs LOBSTER oracle) ---
add_executable(verify apps/verify.cpp)
target_link_libraries(verify PRIVATE lob)

# --- binary A: latency histograms + throughput ---
add_executable(bench apps/bench.cpp)
target_link_libraries(bench PRIVATE lob)

# --- tests ---
add_executable(matching_test tests/matching_test.cpp)
target_link_libraries(matching_test PRIVATE lob)

add_executable(index_test tests/index_test.cpp)
target_link_libraries(index_test PRIVATE lob)

add_executable(avl_test tests/avl_test.cpp)
target_link_libraries(avl_test PRIVATE lob)

add_executable(parser_test tests/parser_test.cpp)
target_link_libraries(parser_test PRIVATE lob)

add_executable(snapshot_test tests/snapshot_test.cpp)
target_link_libraries(snapshot_test PRIVATE lob)

add_executable(generator_test tests/generator_test.cpp)
target_link_libraries(generator_test PRIVATE lob)
