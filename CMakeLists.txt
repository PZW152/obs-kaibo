cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "Kaibo is a dock plugin: ENABLE_FRONTEND_API and ENABLE_QT must be ON.")
endif()

# OBS-independent core, also built by tests/CMakeLists.txt.
add_library(
  kaibo-core
  STATIC
  src/core/areas.cpp
  src/core/bili_client.cpp
  src/core/cookies.cpp
  src/core/md5.cpp
  src/core/settings_model.cpp
  src/core/text.cpp
  src/vendor/json11/json11.cpp
)
target_include_directories(kaibo-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(kaibo-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/app/controller.cpp
    src/app/controller.hpp
    src/app/curl_transport.cpp
    src/app/curl_transport.hpp
    src/app/i18n.hpp
    src/app/obs_bridge.cpp
    src/app/obs_bridge.hpp
    src/app/secret_store.cpp
    src/app/secret_store.hpp
    src/app/store.cpp
    src/app/store.hpp
    src/app/worker.cpp
    src/app/worker.hpp
    src/ui/area_picker.cpp
    src/ui/area_picker.hpp
    src/ui/dock.cpp
    src/ui/dock.hpp
    src/ui/theme.cpp
    src/ui/theme.hpp
    src/ui/widgets.cpp
    src/ui/widgets.hpp
    src/vendor/qrcodegen/qrcodegen.cpp
)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE kaibo-core OBS::libobs OBS::obs-frontend-api CURL::libcurl Qt6::Core Qt6::Widgets Qt6::Svg
)
if(OS_WINDOWS)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE crypt32)
endif()

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
