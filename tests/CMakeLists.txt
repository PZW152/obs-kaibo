# Standalone build of the OBS-independent core and its unit tests.
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.20)
project(kaibo-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(KAIBO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_executable(
  kaibo-tests
  test_core.cpp
  ${KAIBO_SRC}/core/areas.cpp
  ${KAIBO_SRC}/core/bili_client.cpp
  ${KAIBO_SRC}/core/cookies.cpp
  ${KAIBO_SRC}/core/md5.cpp
  ${KAIBO_SRC}/core/settings_model.cpp
  ${KAIBO_SRC}/core/text.cpp
  ${KAIBO_SRC}/vendor/json11/json11.cpp
)
target_include_directories(kaibo-tests PRIVATE "${KAIBO_SRC}")
if(MSVC)
  target_compile_options(kaibo-tests PRIVATE /W3 /WX /utf-8)
else()
  target_compile_options(kaibo-tests PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

enable_testing()
add_test(NAME kaibo-core COMMAND kaibo-tests WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
