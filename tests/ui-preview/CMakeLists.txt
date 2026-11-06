# Renders the real dock in every state to PNG, with a fake OBS and network layer.
#   cmake -S tests/ui-preview -B build-preview && cmake --build build-preview
#   KB_OUT=shots QT_QPA_PLATFORM=offscreen build-preview/kaibo-ui-preview
cmake_minimum_required(VERSION 3.20)
project(kaibo-ui-preview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(Threads REQUIRED)

set(KAIBO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(KAIBO_SRC "${KAIBO_ROOT}/src")
add_executable(
  kaibo-ui-preview
  main.cpp
  stubs.cpp
  ${KAIBO_SRC}/app/controller.cpp
  ${KAIBO_SRC}/app/controller.hpp
  ${KAIBO_SRC}/app/secret_store.cpp
  ${KAIBO_SRC}/app/store.cpp
  ${KAIBO_SRC}/app/worker.cpp
  ${KAIBO_SRC}/core/areas.cpp
  ${KAIBO_SRC}/core/bili_client.cpp
  ${KAIBO_SRC}/core/cookies.cpp
  ${KAIBO_SRC}/core/md5.cpp
  ${KAIBO_SRC}/core/settings_model.cpp
  ${KAIBO_SRC}/core/text.cpp
  ${KAIBO_SRC}/ui/area_picker.cpp
  ${KAIBO_SRC}/ui/area_picker.hpp
  ${KAIBO_SRC}/ui/dock.cpp
  ${KAIBO_SRC}/ui/dock.hpp
  ${KAIBO_SRC}/ui/theme.cpp
  ${KAIBO_SRC}/ui/theme.hpp
  ${KAIBO_SRC}/ui/widgets.cpp
  ${KAIBO_SRC}/ui/widgets.hpp
  ${KAIBO_SRC}/vendor/json11/json11.cpp
  ${KAIBO_SRC}/vendor/qrcodegen/qrcodegen.cpp
)
target_include_directories(kaibo-ui-preview PRIVATE stubs "${KAIBO_SRC}")
target_compile_definitions(kaibo-ui-preview PRIVATE KB_DATA_DIR="${KAIBO_ROOT}/data/")
# main.cpp pokes the controller's state directly to reach every screen.
target_compile_options(kaibo-ui-preview PRIVATE -fno-access-control -w)
target_link_libraries(
  kaibo-ui-preview
  PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Threads::Threads
)
